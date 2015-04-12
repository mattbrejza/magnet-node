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


uint8_t buff;

void init(void);
void _delay_ms(const uint32_t delay);
void uart_send_blocking_len(uint8_t *buff, uint16_t len);


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

void init (void)
{
	rcc_clock_setup_in_hsi_out_8mhz();
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOC);


/*
	//UI stuff
	//rotary encoder A,B: B12,A15; middle: B14
	gpio_mode_setup(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO12 | GPIO14);
	gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO15);
	//enable interrupts (just on one input)

	RCC_APB2ENR |= (1<<0);
	exti_select_source(EXTI14, GPIOB);
	exti_set_trigger(EXTI14, EXTI_TRIGGER_FALLING);
	exti_enable_request(EXTI14);
	exti_select_source(EXTI12, GPIOB);
	exti_set_trigger(EXTI12, EXTI_TRIGGER_BOTH);
	exti_enable_request(EXTI12);
	nvic_enable_irq(NVIC_EXTI4_15_IRQ);
	exti_select_source(EXTI15, GPIOA);
	exti_set_trigger(EXTI15, EXTI_TRIGGER_BOTH);
	exti_enable_request(EXTI15);

	//menu button B6; preset button A5
	gpio_mode_setup(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO6);
	gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO5);
	exti_select_source(EXTI6, GPIOB);
	exti_set_trigger(EXTI6, EXTI_TRIGGER_FALLING);
	exti_enable_request(EXTI6);
	exti_select_source(EXTI5, GPIOA);
	exti_set_trigger(EXTI5, EXTI_TRIGGER_FALLING);
	exti_enable_request(EXTI5);
*/
	//leds
	gpio_mode_setup(LED_AUX_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, LED_AUX_PIN);
	gpio_mode_setup(LED_868_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, LED_868_PIN);
	gpio_mode_setup(LED_WIFI_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, LED_WIFI_PIN);
	gpio_clear(LED_AUX_PORT,LED_AUX_PIN);
	gpio_set(LED_868_PORT,LED_868_PIN);
	gpio_clear(LED_WIFI_PORT,LED_WIFI_PIN);

	//systick
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_set_reload(7999);
	systick_interrupt_enable();
	systick_counter_enable();

/*
	//uart
	//nvic_enable_irq(NVIC_USART1_IRQ);
	rcc_periph_clock_enable(RCC_USART1);
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO9|GPIO10);
	gpio_set_af(GPIOA, GPIO_AF1, GPIO9|GPIO10);
	usart_set_baudrate(USART1, 115200);//9600 );
	usart_set_databits(USART1, 8);
	usart_set_stopbits(USART1, USART_CR2_STOP_1_0BIT);
	usart_set_mode(USART1, USART_MODE_TX_RX);
	usart_set_parity(USART1, USART_PARITY_NONE);
	usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);
	usart_enable(USART1);
*/

	//adc_start_conversion_regular(ADC1);

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
//	nvic_enable_irq(NVIC_USART1_IRQ);

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
//	nvic_enable_irq(NVIC_USART2_IRQ);

	//startup esp
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOF);
	//CH_PD, GPIO0 high; GPIO15 low
	//RST low->high
	gpio_mode_setup(GPIOF,GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO0|GPIO1);
	gpio_clear(GPIOF,GPIO0|GPIO1);  //RST low
	gpio_mode_setup(GPIOA,GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO0|GPIO1); //GPIO0/15

	gpio_set(GPIOA,GPIO0);  //GPIO0 high
	gpio_clear(GPIOA,GPIO1);//GPIO15 low
	gpio_set(GPIOF,GPIO0); //CH_PD high
	_delay_ms(100);
	gpio_set(GPIOF,GPIO1); //RST high

}

/*
void exti4_15_isr(void)
{

	if ((EXTI_PR & (GPIO12)) != 0 || ((EXTI_PR & (GPIO15)) != 0))  //rotary encoder irq
	{

		if ((EXTI_PR & (GPIO15)) != 0)
			EXTI_PR |= (GPIO15);
		if ((EXTI_PR & (GPIO12)) != 0)
			EXTI_PR |= (GPIO12);
	}

}

void usart2_isr(void)
{
	if (((USART_ISR(USART2) & USART_ISR_RXNE) != 0))
	{
		uint8_t d = (uint8_t)USART2_RDR;
		bt_buff_ptr_w++;
		if (bt_buff_ptr_w >= BTBUFFLEN)
			bt_buff_ptr_w = 0;
		bt_buff[bt_buff_ptr_w] = d;
	}
	else if (((USART_ISR(USART2) & USART_ISR_ORE) != 0))  //overrun, clear flag
	{
		USART2_ICR = USART_ICR_ORECF;
	}
}
*/
void sys_tick_handler(void)
{


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
//	init_wdt();
	/*
	while(1)
	{
		if (USART1_ISR & (1<<5)){  //RXNE
			r = USART1_RDR;
			usart_send_blocking(USART2, r);
		}
		if (USART2_ISR & (1<<5)){  //RXNE
			r = USART2_RDR;
			usart_send_blocking(USART1, r);
		}
		USART1_ICR = (1<<3);
		USART2_ICR = (1<<3);
	}
*/
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
 	while(1)
 	{

 		_delay_ms(1000);
 		gpio_clear(LED_AUX_PORT,LED_AUX_PIN);
		gpio_set(LED_868_PORT,LED_868_PIN);
		gpio_clear(LED_WIFI_PORT,LED_WIFI_PIN);

		_delay_ms(1000);
		gpio_clear(LED_AUX_PORT,LED_AUX_PIN);
		gpio_clear(LED_868_PORT,LED_868_PIN);
		gpio_set(LED_WIFI_PORT,LED_WIFI_PIN);

		_delay_ms(1000);
		gpio_set(LED_AUX_PORT,LED_AUX_PIN);
		gpio_clear(LED_868_PORT,LED_868_PIN);
		gpio_clear(LED_WIFI_PORT,LED_WIFI_PIN);

 	}
}

void _delay_ms(const uint32_t delay)
{
    uint32_t i, j;

    for(i=0; i< delay; i++)
        for(j=0; j<1000; j++)
            __asm__("nop");
}




