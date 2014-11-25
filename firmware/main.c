#include <msp430.h> 
#include <inttypes.h>

/*
 * main.c
 */
int main(void) {
    WDTCTL = WDTPW | WDTHOLD;	// Stop watchdog timer
	
    ADC10CTL0 = ADC10SHT_2 + ADC10ON;// + ADC10IE; // ADC10ON, interrupt enabled
    ADC10CTL1 = INCH_1;                       // input A1
    ADC10AE0 |= 0x02;                         // PA.1 ADC option select
    P1DIR |= 0x01;                            // Set P1.0 to output direction

    for (;;)
    {
      ADC10CTL0 |= ENC + ADC10SC;             // Sampling and conversion start
      //__bis_SR_register(CPUOFF + GIE);        // LPM0, ADC10_ISR will force exit
      while(ADC10CTL1 & ADC10BUSY);
      uint16_t r = ADC10MEM;
     // printf("%d\n",r);
      if (r < 0x1FF)
        P1OUT &= ~0x01;                       // Clear P1.0 LED off
      else
        P1OUT |= 0x01;                        // Set P1.0 LED on
    }

	return 0;
}


// ADC10 interrupt service routine
#pragma vector=ADC10_VECTOR
__interrupt void ADC10_ISR(void)
{
  __bic_SR_register_on_exit(CPUOFF);        // Clear CPUOFF bit from 0(SR)
}
