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


//flash storage section
#define FLASH_STORAGE_ADDR ((uint32_t)0x0800f800)
#define FLASH_STORAGE_LEN 0x800
#define FLASH_PAGE_SIZE 0x800



uint8_t buff;
uint8_t flag_rx = 0;

void init(void);
void _delay_ms(const uint32_t delay);
void uart_send_blocking_len(uint8_t *buff, uint16_t len);
uint8_t add_node_to_packet(char* inout, uint8_t len, uint8_t maxlen);
uint8_t construct_upload_string(char* outbuff, char* inbuff, int8_t rssi, uint8_t maxlen);
void get_telemetry(char* buff, uint8_t maxlen, char *seq);
uint8_t upload_string(char* string, char* response, uint16_t response_len, int8_t rssi);
static void process_user_buffer(void);
static void user_send_non_blocking_char(char c);
static void user_send_non_blocking_str(char* c);
static uint8_t break_string_2_arg(char *input, char** para1, uint8_t* para1_len, char** para2, uint8_t* para2_len);
static uint8_t erase_settings_page(void);
static uint8_t write_settings_flash(char* node_name, uint8_t len_name, char* ssid, uint8_t ssid_len, char* pwd, uint8_t pwd_len);
static uint8_t read_settings_flash(char* node_name, uint8_t* name_len, char* ssid, uint8_t* ssid_len, char* pwd, uint8_t* pwd_len);
static uint8_t add_to_telem_buffer(char* string, int8_t rssi, uint16_t max_len);
static uint16_t get_telem_buffer_peek(char* out, int8_t *rssi, uint16_t max_len);
static uint16_t get_telem_buffer_pop(void);
static uint8_t telem_avaliable(void);
static uint8_t upload_string_handle_response(uint8_t res, char* response);

static char ssid[33] = {};   //one longer than needed for '\0' terminator
static char pwd[65] = {};
uint8_t ssid_valid = 0;

int8_t last_error = 0; //used for encoding error messages into telemetry

static uint8_t debug_mode = 0;

static uint8_t error_count = 0;


static char telem_buff[512];
static uint16_t telem_ptr_w = 0;
static uint16_t telem_ptr_r = 0;

char txbuff[128];
static uint16_t server_response_length = 0;

static char node_name_default[] = "MB30";
char node_name[33] = {0};

volatile uint16_t telem_count = 50;

static char user_input_buff[200];
static volatile uint16_t user_input_ptr = 0;
static volatile uint8_t user_cr_flag = 0;

static char user_out_buff[200];
static volatile uint8_t user_out_buff_r = 0;
static volatile uint8_t user_out_buff_w = 0;

static uint8_t uart_passthrough = 0;

#define min(a,b) \
({ __typeof__ (a) _a = (a); \
   __typeof__ (b) _b = (b); \
 _a < _b ? _a : _b; })
#define max(a,b) \
({ __typeof__ (a) _a = (a); \
   __typeof__ (b) _b = (b); \
 _a > _b ? _a : _b; })

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
	systick_set_reload(799999);
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
	usart_enable_tx_interrupt(USART2);
	usart_enable(USART2);
	nvic_enable_irq(NVIC_USART2_IRQ);

	esp_init();

	rf69_init();

}



void usart1_isr(void)
{
	if (((USART_ISR(USART1) & USART_ISR_RXNE) != 0))
	{
		uint8_t d = (uint8_t)USART1_RDR;
		esp_rx_byte(d);
		if (debug_mode)
			user_send_non_blocking_char(d);
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
		uint8_t d = (uint8_t)USART2_RDR;
		if (((d == 127) && (user_input_ptr > 0)) || (d != 127))
			user_send_non_blocking_char(d);  //echo
		if ((d == 127) && (user_input_ptr > 0))
			user_input_ptr--;
		if ((d != 10) && (d != 13) && (d != 127))
			user_input_buff[user_input_ptr++] = d;
		if (user_input_ptr >= sizeof(user_input_buff)/sizeof(char))
			user_input_ptr = sizeof(user_input_buff)/sizeof(char)-1;
		if ((d == '\n') || (d == '\r'))
			user_cr_flag = 1;
	}
	if (((USART_ISR(USART2) & USART_ISR_TXE) != 0))
	{
		if (user_out_buff_w != user_out_buff_r){
			USART_TDR(USART2) = user_out_buff[user_out_buff_r];
			user_out_buff_r++;
			if (user_out_buff_r >= sizeof(user_out_buff)/sizeof(char))
				user_out_buff_r = 0;
		}
		else
			usart_disable_tx_interrupt(USART2);
			//USART_RQR(USART2) = USART_RQR_RXFRQ;
	}
	else if (((USART_ISR(USART2) & USART_ISR_ORE) != 0))  //overrun, clear flag
	{
		USART2_ICR = USART_ICR_ORECF;
	}
}

static void process_user_buffer(void)
{
	user_cr_flag = 0;

	if (user_input_ptr == 0){
		user_send_non_blocking_str("\r\n>");
		return;
	}
	user_input_buff[user_input_ptr] = 0;

	char* arg1;
	uint8_t len1;
	char* arg2;
	uint8_t len2;
	uint8_t argn;

	if (strncmp("help",user_input_buff,user_input_ptr) == 0)
		user_send_non_blocking_str("\r\nAvailable commands: APSHOW, PASSTHROUGH, APCONNECT, SETNAME, IPCONFIG, DEBUGON, DEBUGOFF, ESPRST\r\n>");
	if (strncmp("PASSTHROUGH",user_input_buff,min(user_input_ptr,11)) == 0)
		uart_passthrough = 1;
	if (strncmp("APSHOW",user_input_buff,min(user_input_ptr,6)) == 0){
		esp_run_echo("AT+CWLAP\r\n",&user_send_non_blocking_char);
		user_send_non_blocking_str("\r\n>");
	}
	if (strncmp("APCONNECT",user_input_buff,min(user_input_ptr,9)) == 0){
		argn = break_string_2_arg(user_input_buff,&arg1,&len1,&arg2,&len2);
		if ((argn < 1) || (len1 > 32) || (len2 > 64))
			user_send_non_blocking_str("\r\nAPCONNECT ap_name {ap_password}\r\n>");
		else
		{
			esp_disconnect_ap();
			user_send_non_blocking_str("\r\nConnecting to ");
			arg1[len1] = 0;
			arg2[len2] = 0;
			strncpy(ssid,arg1,32);
			strncpy(pwd,arg2,64);
			user_send_non_blocking_str(arg1);
			user_send_non_blocking_str("\r\n");
			uint8_t res = esp_connect_ap(ssid, pwd);
			if (res)
				user_send_non_blocking_str("Failed\r\n>");
			else
				user_send_non_blocking_str("Success\r\n>");
			flash_unlock();
			erase_settings_page();
			write_settings_flash(node_name, strlen(node_name), ssid, len1, pwd, len2);
			ssid_valid = 1;
		}
	}
	if (strncmp("SETNAME",user_input_buff,min(user_input_ptr,7)) == 0){
		argn = break_string_2_arg(user_input_buff,&arg1,&len1,&arg2,&len2);
		if ((argn != 1) || (len1 > 32))
			user_send_non_blocking_str("\r\nSETNAME node_name\r\n>");
		else
		{
			user_send_non_blocking_str("Setting name to ");
			arg1[len1] = 0;
			user_send_non_blocking_str(arg1);
			user_send_non_blocking_str("\r\n>");
			strncpy(node_name, arg1,32);
			flash_unlock();
			erase_settings_page();
			if (ssid_valid)
				write_settings_flash(node_name, len1, ssid, strlen(ssid), pwd, strlen(pwd));
			else
				write_settings_flash(node_name, len1, ssid, 0, pwd, 0);

		}
	}
	if (strncmp("DEBUGON",user_input_buff,min(user_input_ptr,7)) == 0){
		user_send_non_blocking_str("\r\n>");
		debug_mode = 1;
	}
	if (strncmp("ESPRST",user_input_buff,min(user_input_ptr,6)) == 0){
		user_send_non_blocking_str("\r\n>");
		esp_reset();
	}
	if (strncmp("DEBUGOFF",user_input_buff,min(user_input_ptr,7)) == 0){
		user_send_non_blocking_str("\r\n>");
		debug_mode = 0;
	}
	if (strncmp("IPCONFIG",user_input_buff,min(user_input_ptr,8)) == 0){
		user_send_non_blocking_str("\r\n>");
		esp_run_echo("AT+CIFSR\r\n",&user_send_non_blocking_char);
		user_send_non_blocking_str("\r\n>");
	}
	user_input_ptr = 0;
	flash_lock();

}

//returns how many parameters where found (not including the initial command), up to a max of 2 parameters
static uint8_t break_string_2_arg(char *input, char** para1, uint8_t* para1_len, char** para2, uint8_t* para2_len)
{
	uint8_t arg_start_count = 0;
	uint8_t arg_end_count = 0;
	uint16_t len = 0;
	uint8_t run = 1;
	char prev = 0;

	if (*input == 0)
		return 0;

	//find the initial command
	while((run > 0) && (arg_end_count != 3))
	{
		if (((*input == ' ') || (*input == 0)) && (arg_start_count > 0) && (prev != ' ')){
			arg_end_count++;
			if (arg_end_count == 2){
				*para1_len = len;
			}
			if (arg_end_count == 3){
				*para2_len = len;
			}
		}
		if ((*input != ' ') && (arg_start_count == 0))
			arg_start_count++;
		if ((*input != ' ') && (arg_start_count == arg_end_count)){
			arg_start_count++;
			if (arg_start_count == 2){
				*para1 = input;
				len = 0;
			}
			if (arg_start_count == 3){
				*para2 = input;
				len = 0;
			}
		}

		//if this was the last character in the string
		if (*input == 0)
			run = 0;  //then exit the loop

		len++;
		prev = *input;
		input++;
	}
	return max(1,arg_end_count)-1;
}

void sys_tick_handler(void)
{
	timeout_tick();
	if (telem_count)
		telem_count--;
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
	init_wdt();

	uint8_t name_len, ssid_len, pwd_len;
	ssid_valid = read_settings_flash(node_name, &name_len, ssid, &ssid_len, pwd, &pwd_len);

	//set node name to default if yet to be configured
	uint8_t ptr = 0;
	if ((ssid_valid & (1<<0))== 0){
		while(node_name_default[ptr]){
			node_name[ptr] = node_name_default[ptr];
			ptr++;
		}
	}
	else
		node_name[name_len] = 0;  //just to be sure

	//set wifi valid flag
	if (ssid_valid & (1<<1)){
		ssid_valid = 1;
		ssid[ssid_len] = 0;
		pwd[pwd_len] = 0;
	}
	else{
		ssid_valid = 0;
		ssid[0] = 0;
		pwd[0] = 0;
	}


	uint8_t buff[64];
	char respbuff[512];
	uint16_t respbuff_len = sizeof(respbuff)/sizeof(uint8_t);
	flag_rx = 1;

	uint8_t upload_tries = 0;

	char seq = 'a';

	gpio_set(LED_AUX_PORT,LED_AUX_PIN);
	uint8_t res = esp_connect_ap(ssid,pwd);//(WIFI_AP,WIFI_PASS);
	if (res){
		user_send_non_blocking_str("Failed to connect to ");
		user_send_non_blocking_str(ssid);
		user_send_non_blocking_str("\r\n>");
	}
	else
		user_send_non_blocking_str("Wifi connected\r\n>");
	gpio_clear(LED_AUX_PORT,LED_AUX_PIN);
	gpio_set(LED_WIFI_PORT,LED_WIFI_PIN);

	while(1){

		uint8_t len;
		int8_t rssi;

		IWDG_KR = 0xAAAA;

		gpio_clear(LED_868_PORT,LED_868_PIN);

		if (user_cr_flag)
			process_user_buffer();

		if (flag_rx){
			flag_rx = 0;
			bool res_69 =  checkRx(buff, &len, &rssi);


			if (res_69 == true){
				gpio_set(LED_868_PORT,LED_868_PIN);
				if (add_node_to_packet((char*)buff,len,sizeof(buff)/sizeof(char))>0){
					add_to_telem_buffer((char*)buff,rssi,sizeof(buff)/sizeof(char));
				}
			}
		}
		if (telem_count == 0)
		{
			telem_count = 450;
			get_telemetry((char*)buff,sizeof(buff)/sizeof(char), &seq);
			add_to_telem_buffer((char*)buff,0,sizeof(buff)/sizeof(char));
			//upload_string((char*)buff,respbuff,respbuff_len,0);
		}

		//if stuff waiting to be uploaded
		if ((telem_avaliable()>0) & (esp_busy() == 0)){
			int8_t b_rssi;
			get_telem_buffer_peek((char*)buff,&b_rssi,sizeof(buff)/sizeof(char));
			res = upload_string((char*)buff,respbuff,respbuff_len,b_rssi);
			/*if (res){  //if upload failure
				upload_tries++;
				if (upload_tries > 5){
					get_telem_buffer_pop(); //give up, remove item
					upload_tries = 0;
					user_send_non_blocking_str("Giving up sending after 5 retries\r\n");
				}
			}
			else {
				get_telem_buffer_pop(); //remove successfully uploaded string
				upload_tries = 0;
			}*/

		}

		if (esp_busy()){
			res = esp_service_upload_task();
			if (res){
				if(res == ESP_UPLOAD_DONE_OK){
					res = 0;
					get_telem_buffer_pop();
					upload_tries = 0;
				}
				else
				{
					upload_tries++;
					if (upload_tries > 5){
						get_telem_buffer_pop();
						upload_tries = 0;
						user_send_non_blocking_str("Giving up sending after 5 retries\r\n");
					}
				}
				upload_string_handle_response(res,respbuff);
			}
		}



		if (uart_passthrough){
			usart_disable_tx_interrupt(USART2);
			usart_disable_rx_interrupt(USART2);
			usart_disable_rx_interrupt(USART1);
			uint8_t r;
			while(1)
			{
				IWDG_KR = 0xAAAA;
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
	while(esp_connect_ap(ssid,pwd)) ;
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

//returns 1 for success, 0 for buffer full
static uint8_t add_to_telem_buffer(char* string, int8_t rssi, uint16_t max_len)
{
	char suffix[] = {3,rssi,0};
	uint16_t count = 0;
	uint16_t write_ptr_old = telem_ptr_w;
	while((*string) && (count < (max_len))){
		telem_buff[telem_ptr_w++] = *string++;

		if (telem_ptr_w >= (sizeof(telem_buff)/sizeof(char)))
			telem_ptr_w = 0;
		if (telem_ptr_w == telem_ptr_r){    //if run out of buffer space
			telem_ptr_w = write_ptr_old;    //restore buffer to how it was before
			return 0;
		}
		count++;
	}

	//write the rssi and null terminator
	uint8_t i=0;
	for(i = 0; i < 3; i++){
		telem_buff[telem_ptr_w++] = suffix[i];

		if (telem_ptr_w >= (sizeof(telem_buff)/sizeof(char)))
			telem_ptr_w = 0;
		if (telem_ptr_w == telem_ptr_r){    //if run out of buffer space
			telem_ptr_w = write_ptr_old;    //restore buffer to how it was before
			return 0;
		}
		count++;
	}

	return 1;
}

//returns length of string
static uint16_t get_telem_buffer_peek(char* out, int8_t *rssi, uint16_t max_len)
{
	if (telem_avaliable() == 0)
		return 0;

	uint16_t ptr_r = telem_ptr_r;
	uint16_t count = 0;
	while ((count < (max_len-1)) && (telem_buff[ptr_r] != 0) && (telem_buff[ptr_r] != 3))
	{
		count++;
		*out++ = telem_buff[ptr_r++];
		if (ptr_r >= (sizeof(telem_buff)/sizeof(char)))
			ptr_r = 0;
		if (telem_ptr_w == ptr_r){ //should never occur
			*out = '\0';
			return count;
		}
	}

	//read rssi
	if (telem_buff[ptr_r] == 3){
		ptr_r++;
		if (ptr_r >= (sizeof(telem_buff)/sizeof(char)))
			ptr_r = 0;
		*rssi = telem_buff[ptr_r];
	}

	*out = '\0';
	return count;
}
//returns length of string
static uint16_t get_telem_buffer_pop(void)
{
	if (telem_avaliable() == 0)
		return 0;

	uint16_t count = 0;
	while ((telem_buff[telem_ptr_r] != 0) && (telem_buff[telem_ptr_r] != 3))
	{
		telem_ptr_r++;
		count++;
		if (telem_ptr_r >= (sizeof(telem_buff)/sizeof(char)))
			telem_ptr_r = 0;
		if (telem_ptr_w == telem_ptr_r) //should never occur
			return count;
	}

	//read rssi
	if (telem_buff[telem_ptr_r] == 3){
		telem_ptr_r++;
		if (telem_ptr_r >= (sizeof(telem_buff)/sizeof(char)))
			telem_ptr_r = 0;
		telem_ptr_r++;
		if (telem_ptr_r >= (sizeof(telem_buff)/sizeof(char)))
			telem_ptr_r = 0;
	}

	telem_ptr_r++;   //progress so the read pointer is sitting just past the '\0' at the start of the next string
	if (telem_ptr_r >= (sizeof(telem_buff)/sizeof(char)))
		telem_ptr_r = 0;
	return count;
}

//returns 1 for telem available, otherwise 0
static uint8_t telem_avaliable(void)
{
	if (telem_ptr_w == telem_ptr_r)
		return 0;
	else
		return 1;
}

//0 - success, >=1 - error
uint8_t upload_string(char* string, char* response, uint16_t response_len, int8_t rssi)
{

	gpio_clear(LED_WIFI_PORT,LED_WIFI_PIN);
	char* p = string;
	uint8_t i = strlen(string);


	//handle uart ui stuff
	user_send_non_blocking_char(27);
	user_send_non_blocking_str("[2K\r");
	while(i--)
		user_send_non_blocking_char(*p++);
	user_send_non_blocking_str("\r\n>");
	i = user_input_ptr;
	p = user_input_buff;
	while(i--)
			user_send_non_blocking_char(*p++);

	if (rssi < 0)
		i = snprintf(txbuff,128,"origin=%s&data=%s&rssi=%i",node_name,string,rssi);
	else
		i = snprintf(txbuff,128,"origin=%s&data=%s",node_name,string);

	if (i >= 127)
		return 0;

	esp_upload_node_non_blocking_start("ukhas.net",txbuff,response,response_len,&server_response_length);
	return 0;
	//uint8_t res = esp_upload_node("ukhas.net",txbuff,response,response_len,&res_len);
	//return upload_string_handle_response(res, response);
}

static uint8_t upload_string_handle_response(uint8_t res, char* response)
{
	if (res){
		char errcode[3];
		snprintf(errcode,3,"%02x",res);
		user_send_non_blocking_str("\r\n\r\nUpload failed with error code 0x");
		user_send_non_blocking_str(errcode);
		user_send_non_blocking_str("\r\n\r\n>");

		error_count++;
		gpio_set(LED_AUX_PORT,LED_AUX_PIN);
		gpio_clear(LED_WIFI_PORT,LED_WIFI_PIN);

		char* rb = response;
		while(server_response_length--)
			usart_send_blocking(USART2, (uint8_t)(*rb++));
		if (res == FAIL_FATAL){
		//	esp_reset();  //uncomment this when finished debugging why it occurs in the first place
			user_send_non_blocking_str("\r\n\r\nFATAL ERROR!!! RESETTING ESP... \r\n\r\n>");
			last_error = -10;
		}
		if (error_count > 10){
			esp_reset();
			user_send_non_blocking_str("\r\n\r\nToo many errors!!! RESETTING ESP... \r\n\r\n>");
			error_count = 0;
			last_error = -15;
		}
		if (res != FAIL_NOT_200)
			esp_connect_ap(ssid,pwd);//WIFI_AP,WIFI_PASS); //try connecting again
	}
	else{
		error_count = 0;
		gpio_clear(LED_AUX_PORT,LED_AUX_PIN);
		gpio_set(LED_WIFI_PORT,LED_WIFI_PIN);

	}

	gpio_set(LED_WIFI_PORT,LED_WIFI_PIN);
	return res;
}

void get_telemetry(char* buff, uint8_t maxlen, char *seq)
{
	uint16_t hr = htu21_read_sensor(HTU21_READ_HUMID);
	uint16_t tr = htu21_read_sensor(HTU21_READ_TEMP);
	uint16_t h = convert_humidity(hr);//&0x7FFF);
	int16_t t = convert_temperature(tr);//&0x7FFF);


	t=t/10;

	if (last_error){    //hides error messages in telemetry
		t = last_error;
		last_error = 0;
	}

	int8_t t_i = t/10;
	int8_t t_d = t%10;

	snprintf(buff,maxlen,"3aT%i.%iH%i[%s]",t_i,t_d,h,node_name);
	buff[1] = *seq;

	(*seq)++;
	if (*seq > 'z')
		*seq = 'b';

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

static void user_send_non_blocking_char(char c)
{

	user_out_buff[user_out_buff_w++] = c;
	uint8_t send;
	if (user_out_buff_w == user_out_buff_r)
		send = 1;

	if (user_out_buff_w >= sizeof(user_out_buff)/sizeof(char))
		user_out_buff_w = 0;

	if (((USART_ISR(USART2) & USART_ISR_RXNE) != 0) && (send > 0))
		 USART_TDR(USART2) = c;

	usart_enable_tx_interrupt(USART2);

}

static void user_send_non_blocking_str(char* c)
{
	while(*c)
		user_send_non_blocking_char(*c++);
}


//bit0 - name valid; bit1 - wifi valid
static uint8_t read_settings_flash(char* node_name, uint8_t* name_len, char* ssid, uint8_t* ssid_len, char* pwd, uint8_t* pwd_len)
{
	uint32_t flash_ptr = (uint32_t)(FLASH_STORAGE_ADDR);
	uint8_t return_val = 0;

	//read lengths first
	uint32_t lengths1 = *(uint32_t*)flash_ptr;
	flash_ptr+=4;
	uint32_t lengths2 = *(uint32_t*)flash_ptr;
	*name_len = lengths2 & 0xFF;
	*ssid_len = (lengths1 >> 0) & 0xFF;
	*pwd_len = (lengths1 >> 8) & 0xFF;
	if (((lengths1 >> 16) & 0xFF) == 0x6A )
		return_val |= (1<<1);
	if (((lengths2 >> 8) & 0xFF) == 0x74 )
		return_val |= (1<<0);

	uint32_t out_ptr = (uint32_t)(node_name);
	flash_ptr = (uint32_t)(FLASH_STORAGE_ADDR + 16);
	uint8_t bytes_read = 0;

	//now read each string
	if (return_val & (1<<0)){
		while(bytes_read < *name_len)
		{
			*(uint32_t*)out_ptr = *(uint32_t*)flash_ptr;
			flash_ptr += 4;
			out_ptr += 4;
			bytes_read += 4;
		}
	}

	out_ptr = (uint32_t)(ssid);
	flash_ptr = (uint32_t)(FLASH_STORAGE_ADDR + 16 + 64);
	bytes_read=0;
	//now ssid
	if (return_val & (1<<1)){
		while(bytes_read < *ssid_len)
		{
			*(uint32_t*)out_ptr = *(uint32_t*)flash_ptr;
			flash_ptr += 4;
			out_ptr += 4;
			bytes_read += 4;
		}

		out_ptr = (uint32_t)(pwd);
		flash_ptr = (uint32_t)(FLASH_STORAGE_ADDR + 16 + 64 + 64);
		bytes_read=0;
		//now pwd
		while(bytes_read < *pwd_len)
		{
			*(uint32_t*)out_ptr = *(uint32_t*)flash_ptr;
			flash_ptr += 4;
			out_ptr += 4;
			bytes_read += 4;
		}
	}

	return return_val;
}


//returns 1 for error
static uint8_t write_settings_flash(char* node_name, uint8_t len_name, char* ssid, uint8_t ssid_len, char* pwd, uint8_t pwd_len)
{
	ssid_len = min(ssid_len,32);
	pwd_len = min(pwd_len,64);
	len_name = min(len_name,32);
	uint32_t lens = (ssid_len<<0) | (pwd_len<<8) | (0x6A << 16);

	uint32_t flash_ptr = (FLASH_STORAGE_ADDR);
	uint32_t in_ptr = (uint32_t)(&lens);

	//write string lengths
	if (ssid_len > 0){
		flash_program_word(flash_ptr, *(uint32_t*)in_ptr);
		if(flash_get_status_flags() != FLASH_SR_EOP)
			return 1;
	}
	lens = len_name | (0x74 << 8);
	flash_ptr +=4;
	flash_program_word(flash_ptr, *(uint32_t*)in_ptr);
	if(flash_get_status_flags() != FLASH_SR_EOP)
		return 1;


	//write node name
	in_ptr = (uint32_t)(node_name);
	flash_ptr = FLASH_STORAGE_ADDR + 16;

	uint8_t bytes_written=0;
	while(bytes_written < len_name)
	{
		/*programming word data*/
		flash_program_word(flash_ptr, *(uint32_t*)in_ptr);
		if(flash_get_status_flags() != FLASH_SR_EOP)
			return 1;

		flash_ptr += 4;
		in_ptr += 4;
		bytes_written += 4;
	}

	//write ssid
	in_ptr = (uint32_t)(ssid);
	flash_ptr = FLASH_STORAGE_ADDR + 16 + 64;

	bytes_written=0;
	while(bytes_written < ssid_len)
	{
		/*programming word data*/
		flash_program_word(flash_ptr, *(uint32_t*)in_ptr);
		if(flash_get_status_flags() != FLASH_SR_EOP)
			return 1;

		flash_ptr += 4;
		in_ptr += 4;
		bytes_written += 4;
	}

	//write pwd
	in_ptr = (uint32_t)(pwd);
	flash_ptr = FLASH_STORAGE_ADDR + 16 + 64 +64;

	bytes_written=0;
	while(bytes_written < pwd_len)
	{
		/*programming word data*/
		flash_program_word(flash_ptr, *(uint32_t*)in_ptr);
		if(flash_get_status_flags() != FLASH_SR_EOP)
			return 1;

		flash_ptr += 4;
		in_ptr += 4;
		bytes_written += 4;
	}

	return 0;

}

//returns 0 for error
static uint8_t erase_settings_page(void)
{
	flash_erase_page(FLASH_STORAGE_ADDR);
	if(flash_get_status_flags() != FLASH_SR_EOP)
		return 0;
	else
		return 1;
}
