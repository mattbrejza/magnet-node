#include <msp430.h> 
#include <inttypes.h>

#include "si4012.h"
#include "htu21.h"

//                      PRE   PRE   PRE  SYNC  SYNC  LEN
uint8_t buf_out[28] = {0xAA, 0xAA, 0xAA, 0x2D, 0xAA,  20, '3', 'a', 'V', 'v', '.','v', 'T', '+', 't','t','.','t','S','h','h','[','M','B','b',']',0,0};

//#define USE_HTU21
#define USE_LIGHT

const int16_t temp_internal_cal = 690-8+15;//695;


uint8_t sequence = 'a';

volatile int16_t temp;
volatile uint16_t humid,adco,volts;

static const uint8_t shift = 6;
static const uint16_t bitrate = 0x14;
static const uint32_t frequency = 869537000-16000;  //37000 for MB1,  27000 for MB2

#define CRC_START 0x1D0F

uint16_t crc_xmodem_update (uint16_t crc, uint8_t data);
void format_hasnet_string(uint8_t *buff, uint8_t humidity, uint8_t volts10, int16_t temp10);



int main(void) {
//    WDTCTL = WDTPW | WDTHOLD;	// Stop watchdog timer
#ifdef USE_HTU21
	ADC10CTL0 = ADC10SHT_2 + ADC10ON + SREF_1 + REFON + REF2_5V;
	P1DIR |= 0x01;// | (1<<4);		//shutdown, sensor power pin as output
#else
    ADC10CTL0 = ADC10SHT_2 + ADC10ON + SREF_1 + REFON;// + REF2_5V;
    P1DIR |= 0x01 | (1<<4);		//shutdown, sensor power pin as output
#endif

//    ADC10AE0 |= 0x02;                         // PA.1 ADC option select
//    P1DIR |= 0x01;                            // Set P1.0 to output direction



    CCTL0 = CCIE;                             // CCR0 interrupt enabled
	__enable_interrupt();

	//setup clocks
	BCSCTL1 = (1<<7) | DIVA_3 | 3;  //RSEL = 3 (250kHz), ACLK = 12/8 = 1.5kHz
	BCSCTL3 = LFXT1S_2;


	si4012_init(shift, bitrate, frequency);

	uint16_t hum,tem;


    while(1)
     {

    	WDTCTL = WDT_ARST_1000;

    	//timer for delays
//    	CCR0 = 10000;
//    	TACTL = TASSEL_2 + MC_1;                  // SMCLK, up mode (to CCR0)

    	ADC10CTL1 = INCH_11;                       // input vcc/2
    	ADC10CTL0 |= (REFON | ADC10ON);			//adc buffer takes ~30us to settle, so enable in advance


    	P1OUT &= ~0x01;      //disable shutdown on radio (is disabled on tx finished)

#ifdef USE_HTU21
    	tem = htu21_read_sensor(HTU21_READ_TEMP);
#endif
    	ADC10CTL0 |= ENC + ADC10SC;             // Sampling and conversion start
#ifdef USE_HTU21
    	temp = convert_temperature(tem);
    	hum = htu21_read_sensor(HTU21_READ_HUMID);
		humid = convert_humidity(hum);
		//while(v & ADC10BUSY);     //should really check, but the humidity takes 14ms so the adc will be done
		adco = ADC10MEM;
		volts = adco * 25;
		temp = temp / 10;
#else
		//INSERT 30us setting time wait here

		P1OUT |= (1<<4);    //enable power to sensors
		ADC10AE0 = (1<<3) | (1<<5);

		while(ADC10CTL1 & ADC10BUSY);
		adco = ADC10MEM;
		volts = adco * 15;

		ADC10CTL0 &= ~ENC;
		ADC10CTL1 = INCH_10;                    //temperature sensor.
		ADC10CTL0 |= ENC + ADC10SC;             // Sampling and conversion start
		while(ADC10CTL1 & ADC10BUSY);
		temp = ADC10MEM;
		temp = temp - temp_internal_cal;
		temp = temp * 33;
		temp = temp >> 3;
#ifdef USE_LIGHT
		ADC10CTL0 &= ~ENC;
		ADC10CTL1 = INCH_5;                    //wetness sensor.
		ADC10CTL0 |= ENC + ADC10SC;             // Sampling and conversion start
		while(ADC10CTL1 & ADC10BUSY);
		humid = ADC10MEM;
		humid = humid / 2;
		if (humid > 100)
			humid = 100;
#else
		ADC10CTL0 &= ~ENC;
		ADC10CTL1 = INCH_3;                    //wetness sensor.
		ADC10CTL0 |= ENC + ADC10SC;             // Sampling and conversion start
		while(ADC10CTL1 & ADC10BUSY);
		humid = ADC10MEM;
		humid = humid / 15;
#endif
#endif



		//turn adc off
		ADC10CTL0 &= ~ENC;
		ADC10CTL0 &= ~(REFON | ADC10ON);


		volts >>= 1;
		volts >>= 8;


		format_hasnet_string(&buf_out[6],humid,volts,temp);

		uint16_t crc = CRC_START;
		uint8_t i;
		for (i = 5; i < (sizeof(buf_out)-2); i++)
			crc = crc_xmodem_update(crc,buf_out[i]);

		crc = 0xFFFF - crc;  //invert for some reason

		buf_out[(sizeof(buf_out)-2)] = (crc>>8)&0xFF;
		buf_out[(sizeof(buf_out)-1)] = crc&0xFF;


		si4012_transmit_short(buf_out, sizeof(buf_out));

		P1OUT |= 0x01;      //enable shutdown
#ifndef USE_HTU21
//		P1OUT &= ~(1<<4);
#endif

		//go to sleep for a while
		CCR0 = 29000;  //increase to 28000 or so
		TACTL = TASSEL_1 + MC_1;        // ACLK, up mode (to CCR0)
		TAR = 0; 						//reset the timer
		__bis_SR_register(LPM3_bits);
		WDTCTL = WDT_ARST_1000;
		__bis_SR_register(LPM3_bits);
		WDTCTL = WDT_ARST_1000;
		__bis_SR_register(LPM3_bits);

     }


}

// Timer A0 interrupt service routine
#pragma vector=TIMERA0_VECTOR
__interrupt void Timer_A (void)
{
	__bic_SR_register_on_exit(LPM3_bits);   // Clear LPM0 bits from 0(SR)
}

// see http://www.atmel.com/webdoc/AVRLibcReferenceManual/group__util__crc_1gaca726c22a1900f9bad52594c8846115f.html
uint16_t crc_xmodem_update (uint16_t crc, uint8_t data) {
    crc = crc ^ ((uint16_t) data << 8);
    uint8_t i;
    for (i=8; i>0; i--) {
        if (crc & 0x8000) {
            crc = (crc << 1) ^ 0x1021;
        } else {
            crc <<= 1;
        }
    }
    return crc;
}

void format_hasnet_string(uint8_t *buff, uint8_t humidity, uint8_t volts10, int16_t temp10)
{
	//const char blank_packet[] = "3aVv.vT+tt.tHhh[M1]";
	//uint8_t i;
	//for (i = 0; i< sizeof(blank_packet); i++)
	//	buff[i] = (uint8_t)blank_packet[i];
/*	buff[0] = 3;
	buff[2] = (uint8_t)'V';
	buff[4] = (uint8_t)'.';
	buff[6] = (uint8_t)'T';
	buff[7] = (uint8_t)'+';
	buff[12] = (uint8_t)'H';

	buff[15] = (uint8_t)'[';
	buff[16] = (uint8_t)'M';
	buff[17] = (uint8_t)'B';
	buff[18] = (uint8_t)'1';
	buff[19] = (uint8_t)']';
*/

	buff[1] = sequence;

	//volts
	uint8_t v = volts10/10;
	buff[3] = v+48;
	v = volts10-(10*v);
	buff[5] = v+48;

	//humidity
	uint8_t h = humidity/10;
	buff[13] = h+48;
	h = humidity-(10*h);
	buff[14] = h+48;

	//temperature

	if (temp10 < 0){
		buff[7] = '-';
		temp10 = temp10*-1;
	}
	else
		buff[7] = '0';
	int8_t t = temp10/10;
	if ((t<10))// && (t > -10))
		buff[8] = '0';
	else{
		uint8_t t10 = t/10;//abs(t)/10;
		buff[8] = t10+48;
	}
	t = abs(t)%10;
	buff[9] = t+48;
	t = temp10%10;
	buff[11] = t+48;

	sequence++;
	if (sequence == ((uint8_t)'z'+1))
		sequence = (uint8_t)'b';

}
