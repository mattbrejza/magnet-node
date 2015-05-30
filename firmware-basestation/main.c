#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/stm32/iwdg.h>
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/cm3/systick.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "main.h"
#include "esp8266.h"
#include "htu21.h"
#include "RFM69.h"

#include "wifipass.h"



uint8_t buff;
uint8_t flag_rx = 0;

void init(void);
void _delay_ms(const uint32_t delay);
void uart_send_blocking_len(uint8_t *buff, uint16_t len);
uint8_t add_node_to_packet(char* inout, uint8_t len, uint8_t maxlen);
uint8_t construct_upload_string(char* outbuff, char* inbuff, int8_t rssi, uint8_t maxlen);

const char node_name[] = "MB30";


void init_wdt(void)
{
	RCC_CSR |= 1;   //LSI on
	while(RCC_CSR&(1<<1));  //wait for LSI ready

	while(IWDG_SR&1);
	IWDG_KR = 0x5555;
	IWDG_PR = 0b110; // 40kHz/256

	while(IWDG_SR&2);
	IWDG_KR = 0x5555;
	IWDG_RLR = 2600;

	IWDG_KR = 0xAAAA;
	IWDG_KR = 0xCCCC;

}

/*
void usart1_isr(void)
{
	if (((USART_ISR(USART1) & USART_ISR_RXNE) != 0))
	{
		buff = (uint8_t)USART1_RDR;
		usart_send_blocking(USART2, buff);
	}
	else if (((USART_ISR(USART1) & USART_ISR_ORE) != 0))  //overrun, clear flag
	{
		USART1_ICR = USART_ICR_ORECF;
	}
}

void usart2_isr(void)
{
	if (((USART_ISR(USART2) & USART_ISR_RXNE) != 0))
	{
		buff = (uint8_t)USART2_RDR;
		usart_send_blocking(USART1, buff);
	}
	else if (((USART_ISR(USART2) & USART_ISR_ORE) != 0))  //overrun, clear flag
	{
		USART1_ICR = USART_ICR_ORECF;
	}
}
*/

void init (void)
{

	rcc_clock_setup_in_hsi_out_8mhz();
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOC);


	//leds
	gpio_mode_setup(LED_AUX_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, LED_AUX_PIN);
	gpio_mode_setup(LED_868_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, LED_868_PIN);
	gpio_mode_setup(LED_WIFI_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, LED_WIFI_PIN);
	gpio_clear(LED_AUX_PORT,LED_AUX_PIN);
	gpio_clear(LED_868_PORT,LED_868_PIN);
	gpio_clear(LED_WIFI_PORT,LED_WIFI_PIN);


	//radio interrupt
	gpio_mode_setup(RADIO_INT_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, RADIO_INT_PIN);

	//enable interrupts (just on one input)
	RCC_APB2ENR |= (1<<0);
	exti_select_source(EXTI8, RADIO_INT_PORT);
	exti_set_trigger(EXTI8, EXTI_TRIGGER_RISING);
	exti_enable_request(EXTI8);
	nvic_enable_irq(NVIC_EXTI4_15_IRQ);


	//systick
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_set_reload(7999);
	systick_interrupt_enable();
	systick_counter_enable();


	rcc_periph_clock_enable(RCC_USART1);
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO6|GPIO7);
	gpio_set_af(GPIOB, GPIO_AF0, GPIO6|GPIO7);
	usart_set_baudrate(USART1, 9600 );
	usart_set_databits(USART1, 8);
	usart_set_stopbits(USART1, USART_CR2_STOP_1_0BIT);
	usart_set_mode(USART1, USART_MODE_TX_RX);
	usart_set_parity(USART1, USART_PARITY_NONE);
	usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);
	usart_enable_rx_interrupt(USART1);
	usart_enable(USART1);
	nvic_enable_irq(NVIC_USART1_IRQ);

	rcc_periph_clock_enable(RCC_USART2);
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO2|GPIO3);
	gpio_set_af(GPIOA, GPIO_AF1, GPIO2|GPIO3);
	usart_set_baudrate(USART2, 9600 );
	usart_set_databits(USART2, 8);
	usart_set_stopbits(USART2, USART_CR2_STOP_1_0BIT);
	usart_set_mode(USART2, USART_MODE_TX_RX);
	usart_set_parity(USART2, USART_PARITY_NONE);
	usart_set_flow_control(USART2, USART_FLOWCONTROL_NONE);
	usart_enable_rx_interrupt(USART2);
	usart_enable(USART2);

	esp_init();

	rf69_init();

}



void usart1_isr(void)
{
	if (((USART_ISR(USART1) & USART_ISR_RXNE) != 0))
	{
		uint8_t d = (uint8_t)USART1_RDR;
		esp_rx_byte(d);
	}
	else if (((USART_ISR(USART1) & USART_ISR_ORE) != 0))  //overrun, clear flag
	{
		USART1_ICR = USART_ICR_ORECF;
	}
}

void sys_tick_handler(void)
{
	timeout_tick();
}

void exti4_15_isr(void)
{
	if (EXTI_PR & (GPIO8))  //rotary encoder irq
	{
		flag_rx = 1;

		if ((EXTI_PR & (GPIO8)) != 0)
			EXTI_PR |= (GPIO8);
	}

}

void uart_send_blocking_len(uint8_t *buff, uint16_t len)
{
	uint16_t i = 0;
	for (i = 0; i < len; i++)
		usart_send_blocking(USART1,*buff++);
}

int main(void)
{


	init();
	htu21_init();
//	init_wdt();

	uint8_t buff[64];
	char txbuff[128];
	char respbuff[512];
	flag_rx = 1;

	gpio_set(LED_AUX_PORT,LED_AUX_PIN);
	uint8_t count = 5;
	while((count-- > 0) && (esp_connect_ap(WIFI_AP,WIFI_PASS) >0 ));
	gpio_clear(LED_AUX_PORT,LED_AUX_PIN);
	gpio_set(LED_WIFI_PORT,LED_WIFI_PIN);

	while(1){

		uint8_t len,res;
		int8_t rssi;
		while(flag_rx == 0);
		flag_rx = 0;
		bool res_69 =  checkRx(buff, &len, &rssi);
		uint16_t res_len = 0;

		if (res_69 == true){
			gpio_set(LED_868_PORT,LED_868_PIN);
			uint8_t* p = buff;
			uint8_t i = len;
			while(i--)
				usart_send_blocking(USART2, *p++);
			usart_send_blocking(USART2, '\r');
			usart_send_blocking(USART2, '\n');

			i=add_node_to_packet((char*)buff,len,sizeof(buff)/sizeof(char));



			construct_upload_string(txbuff, (char*)buff, rssi, sizeof(txbuff)/sizeof(char));
			res = esp_upload_node("ukhas.net",txbuff,respbuff,sizeof(respbuff),&res_len);
			if (res){
				gpio_set(LED_AUX_PORT,LED_AUX_PIN);
				gpio_clear(LED_WIFI_PORT,LED_WIFI_PIN);

				char* rb = respbuff;
				while(res_len--)
					usart_send_blocking(USART2, (uint8_t)(*rb++));

				if (res != FAIL_NOT_200)
					esp_connect_ap(WIFI_AP,WIFI_PASS); //try connecting again
			}
			else{
				gpio_clear(LED_AUX_PORT,LED_AUX_PIN);
				gpio_set(LED_WIFI_PORT,LED_WIFI_PIN);

				////print server response
				//p=(uint8_t *)respbuff;
				//while(res_len--)
				//	usart_send_blocking(USART2, (uint8_t)(*p++));
			}

		}
		gpio_clear(LED_868_PORT,LED_868_PIN);
	}


{
	_delay_ms(1000);
	uint16_t hum = convert_humidity(htu21_read_sensor(HTU21_READ_HUMID));
	hum++;

	//rf69_send((uint8_t *)packet,sizeof(packet)-1,10);
}
_delay_ms(1000);
	esp_reset();
	gpio_set(LED_868_PORT,LED_868_PIN);
	while(esp_connect_ap(WIFI_AP,WIFI_PASS)) ;
	gpio_set(LED_WIFI_PORT,LED_WIFI_PIN);




	uint8_t r;
	while(1)
	{
		if (USART1_ISR & (1<<5)){  //RXNE
			r = USART1_RDR;
			usart_send_blocking(USART2, r);
		}
		if (USART2_ISR & (1<<5)){  //RXNE
			r = USART2_RDR;
			if ((r==10) || (r==13)){
				usart_send_blocking(USART1, '\r');
				usart_send_blocking(USART1, '\n');
			}
			else
				usart_send_blocking(USART1, r);
		}
		USART1_ICR = (1<<3);
		USART2_ICR = (1<<3);
	}
}

void _delay_ms(const uint32_t delay)
{
    uint32_t i, j;

    for(i=0; i< delay; i++)
        for(j=0; j<1000; j++)
            __asm__("nop");
}

uint8_t construct_upload_string(char* outbuff, char* inbuff, int8_t rssi, uint8_t maxlen)
{
	return snprintf(outbuff,maxlen,"origin=%s&data=%s&rssi=%i",node_name,inbuff,rssi);
}

//retuns 0 for error, otherwise the new length
uint8_t add_node_to_packet(char* inout, uint8_t len, uint8_t maxlen)
{
	while(len--)
	{
		if (inout[len] == ']'){
			inout[len] = ',';
			len++;
			const char* p = &node_name[0];
			while(*p){
				if (len >= maxlen)
					return 0;
				inout[len] = *p;
				len++;
				p++;
			}
			if ((len+1) >= maxlen)
				return 0;
			inout[len++] = ']';
			inout[len++] = '\0';

			//decrement first number
			if ((inout[0] > '0') && (inout[0] <= '9'))
				inout[0]--;
			return len;

		}
	}
	return 0;
}




