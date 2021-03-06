#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/rcc.h>
#include <string.h>

#include "esp8266.h"

void itoa(int n, char s[]);

volatile uint8_t pending_command = 0;   //1-waiting for OK; 2-waiting for ready; 3-waiting for >; 4-LINKED;
//5-SEND OK  (upon receiving will then increment to 6)
//6-Unlink(waiting for response)


static uint8_t strncmp_circ(char *in1, const char *in2, uint16_t start1, uint16_t wrap1, uint16_t len2);
static void uart_send_blocking_string(char *buff);
static uint8_t check_esp_buffer(void);
static void process_esp_buffer(void);
static void clear_buffer(void);
static uint8_t look_for_200(void);
static void esp_connect_ap_step2(char* ap_name, char* ap_password);

static char upload_string_s[] = "POST /api/upload HTTP/1.0\r\nHost: ukhas.net\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: ";
static char upload_string_e[] = "\r\nConnection: close\r\n\r\n";
char upload_firmware_bodge_factor = 0;//10;

//serial buffer
#define ESP_BUFF_LEN 60
uint8_t esp_buff_ptr_w = 0;
uint8_t esp_buff_ptr_r = 0;
uint16_t esp_buff_ptr_last = 0;
char esp_buff[ESP_BUFF_LEN];
uint8_t in_new_string = 0;

//response buffer
char *response_buffer = 0;
uint16_t response_maxlen = 0;
uint16_t response_ptr = 0;

//timeout
volatile uint8_t timeout = 0;

//echo flag
void (*echoptr)(char);
uint8_t enable_echo = 0;

//stuff for the non blocking upload
static uint8_t upload_string_state = 0;
static char* upload_dest_ip_nb;
static char* upload_string_nb;
static char* upload_respbuff_nb;
static uint16_t upload_resp_maxlen_nb;
static uint16_t* upload_resp_len_nb;

static volatile uint8_t cmd_state = 0;
static char* ap_conn_ssid_name;
static char* ap_conn_password;



void esp_init(void)
{
	//startup esp
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOF);
	//CH_PD, GPIO0 high; GPIO15 low
	//RST low->high
	gpio_mode_setup(GPIOF,GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO0|GPIO1);
	gpio_clear(GPIOF,GPIO0|GPIO1);  //RST low
	gpio_clear(GPIOF,GPIO0); //CH_PD low
	gpio_mode_setup(GPIOA,GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO1); //GPIO15
	gpio_mode_setup(GPIOA,GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO0); //GPIO15

	gpio_set(GPIOA,GPIO0); //gpio_clear(GPIOA,GPIO0);  //GPIO0 low   gpio_set(GPIOA,GPIO0);  //GPIO0 high
	gpio_clear(GPIOA,GPIO1);//GPIO15 low
	gpio_set(GPIOF,GPIO0); //CH_PD high
	gpio_set(GPIOF,GPIO1); //RST high
	timeout = 2;
	while(timeout);

}

uint8_t esp_reset(void)
{
	timeout = 20;
	gpio_set(GPIOA,GPIO0);  //gpio_clear(GPIOA,GPIO0);  //GPIO0 low    gpio_set(GPIOA,GPIO0);  //GPIO0 high
	gpio_clear(GPIOF,GPIO1); //RST low
	gpio_set(GPIOF,GPIO0); //CH_PD low
	while(timeout);
	gpio_set(GPIOF,GPIO1); //RST high
	gpio_set(GPIOF,GPIO0); //CH_PD high
	timeout = 10;
	while(timeout);

	clear_buffer();
	pending_command = 2;  //waiting for ready
	timeout = 30;
	uart_send_blocking_string("AT+RST\r\n");
	while((pending_command&0x7F) && (timeout)) process_esp_buffer();
	if (pending_command > 1) //failure
		return pending_command;
	return 0;
}

void esp_bootload(void)
{
	timeout = 20;
	gpio_clear(GPIOF,GPIO0); //CH_PD low
	gpio_clear(GPIOA,GPIO0);  //GPIO0 low
//	gpio_clear(GPIOF,GPIO1); //RST low

	while(timeout);
//	gpio_set(GPIOF,GPIO1); //RST high
	gpio_set(GPIOF,GPIO0); //CH_PD high
	timeout = 10;
	while(timeout);

	clear_buffer();
	/*
	timeout = 10;
	while(timeout);
	gpio_clear(GPIOF,GPIO0); //CH_PD low
	timeout = 1;
	while(timeout);
	gpio_set(GPIOF,GPIO0); //CH_PD low
	*/
}

uint8_t esp_disconnect_ap(void)
{
	clear_buffer();
	pending_command = 1;  //waiting for OK
	timeout = 10;
	uart_send_blocking_string("AT+CWQAP\n");
	while((pending_command&0x7F)  && (timeout)) process_esp_buffer();
	return pending_command;
}

//returns 0 if still uploading, ESP_UPLOAD_DONE_OK if done ok, otherwise error messages are >0xF0
uint8_t esp_service_cmd_task(void)
{
	uint8_t res = 0;

	process_esp_buffer();
	if ((pending_command>0) && (pending_command < 0xF0) && (timeout) )  //CHANGE THIS
		return 0;

	//check for errors
	if ((pending_command > 1) || (timeout == 0)){ //failure
		response_buffer = 0;
		res = pending_command;    //set error flag
	}
	if ((res > 0) && (res < 0xF0))
		res =  FAIL_TIMEOUT;



	switch (cmd_state){

		case 1:  //just finished step 1
			if (res>0){
				cmd_state = 0;
				return res;
			}
			esp_connect_ap_step2(ap_conn_ssid_name, ap_conn_password);
			//upload_step2_nb(upload_dest_ip_nb, upload_string_nb, upload_respbuff_nb, upload_resp_maxlen_nb);
			cmd_state = 2;

			break;
		case 2:
			if (res>0){
				cmd_state = 0;
				return res;
			}
			cmd_state = 0;
			return ESP_CONNECT_DONE_OK;

		default:
			cmd_state = 0;
			return FAIL_GEN;
			break;

	}

	return 0;
}

void esp_connect_ap_non_blocking(char* ap_name, char* ap_password)
{
	ap_conn_ssid_name = ap_name;
	ap_conn_password = ap_password;

	clear_buffer();
	pending_command = 1;  //waiting for OK
	timeout = 10;
	uart_send_blocking_string("AT+CWMODE=1\n");

	cmd_state = 1;
}

static void esp_connect_ap_step2(char* ap_name, char* ap_password)
{

	pending_command = 1;  //waiting for OK
	uart_send_blocking_string("AT+CWJAP=\"");
	uart_send_blocking_string(ap_name);
	uart_send_blocking_string("\",\"");
	uart_send_blocking_string(ap_password);
	uart_send_blocking_string("\"\n");
	timeout = 200;
	/*
	while((pending_command&0x7F) && (timeout) ){
		process_esp_buffer();
	}
	if (pending_command > 0) //failure
		return 1;
	if (timeout == 0)  //failure
		return 1;
	return 0; */
}

uint8_t esp_connect_ap(char* ap_name, char* ap_password)
{
	clear_buffer();
	pending_command = 1;  //waiting for OK
	timeout = 10;
	uart_send_blocking_string("AT+CWMODE=1\n");
	while((pending_command&0x7F)  && (timeout)) process_esp_buffer();
	if (pending_command > 1) //failure
		return pending_command;

	pending_command = 1;  //waiting for OK
	uart_send_blocking_string("AT+CWJAP=\"");
	uart_send_blocking_string(ap_name);
	uart_send_blocking_string("\",\"");
	uart_send_blocking_string(ap_password);
	uart_send_blocking_string("\"\n");
	timeout = 120;
	while((pending_command&0x7F) && (timeout) ){
		process_esp_buffer();
	}
	if (pending_command > 0) //failure
		return 1;
	if (timeout == 0)  //failure
		return 1;
	return 0;

}

uint8_t esp_conn_close(void)
{
	pending_command = 1;
	uart_send_blocking_string("AT+CIPCLOSE\n");
	return 0;
}

void esp_run_echo(char* command, void (*print_char)(char))
{
	echoptr = print_char;
	enable_echo = 1;
	pending_command = 1;
	uart_send_blocking_string(command);
	while((pending_command&0x7F) )
		process_esp_buffer();
	enable_echo = 0;

}

static void upload_step1_nb(char* dest_ip, char* string, char* respbuff, uint16_t resp_maxlen)
{
	//Initialise stuff for server response
	response_maxlen = resp_maxlen;
	response_ptr = 0;
	timeout = 20;

	clear_buffer();
	pending_command = 4;  //waiting for LINKED

	uart_send_blocking_string("AT+CIPSTART=\"TCP\",\"");
	uart_send_blocking_string(dest_ip);
	uart_send_blocking_string("\",80\n");

}

static void upload_step2_nb(char* dest_ip, char* string, char* respbuff, uint16_t resp_maxlen)
{
	char buff1[6];
	char buff2[6];
	uint16_t len = strlen(string);
	itoa(len,buff1);
	itoa(len + sizeof(upload_string_s)/sizeof(char)-1 + strlen(buff1)
			 + sizeof(upload_string_e)/sizeof(char)-1 + 4-upload_firmware_bodge_factor,buff2);
//	upload_firmware_bodge_factor++;

	pending_command = 3;  //waiting for >
	uart_send_blocking_string("AT+CIPSEND=");
	uart_send_blocking_string(buff2);
	uart_send_blocking_string("\r\n");

}

static void upload_step3_nb(char* dest_ip, char* string, char* respbuff, uint16_t resp_maxlen)
{
	char buff1[6];
	uint16_t len = strlen(string);
	itoa(len,buff1);

	pending_command = 5;  //waiting for unlink
	response_buffer = respbuff;
	uart_send_blocking_string(upload_string_s);
	uart_send_blocking_string(buff1);
	uart_send_blocking_string(upload_string_e);
	uart_send_blocking_string(string);
	uart_send_blocking_string("\r\n\r\n");
//	uart_send_blocking_string(".............\r\n"); //bodging...
	response_ptr = 0;

	timeout = 40;

}

static uint8_t upload_step4(uint16_t* resp_len)//char* dest_ip, char* string, char* respbuff, uint16_t resp_maxlen, uint16_t* resp_len)
{
	esp_conn_close();
	if (look_for_200() == 0){
		*resp_len = response_ptr;
		response_buffer = 0;
		return FAIL_NOT_200;
	}
	*resp_len = response_ptr;
	response_buffer = 0;

	return 0;
}

//returns 0 if still uploading, ESP_UPLOAD_DONE_OK if done ok, otherwise error messages are >0xF0
uint8_t esp_service_upload_task(void)
{
	uint8_t res = 0;

	process_esp_buffer();
	if ((pending_command>0) && (pending_command < 0xF0) && (timeout) )  //CHANGE THIS
		return 0;

	//check for errors
	if ((pending_command > 1) || (timeout == 0)){ //failure
		response_buffer = 0;
		esp_conn_close();
		res = pending_command;    //set error flag
	}
	if ((res > 0) && (res < 0xF0))
		res =  FAIL_TIMEOUT;



	switch (upload_string_state){

		case 1:  //just finished step 1
			if (res>0){
				upload_string_state = 0;
				return res;
			}
			upload_step2_nb(upload_dest_ip_nb, upload_string_nb, upload_respbuff_nb, upload_resp_maxlen_nb);
			upload_string_state = 2;

			break;

		case 2:
			if (res>0){
				upload_string_state = 0;
				return res;
			}
			upload_step3_nb(upload_dest_ip_nb, upload_string_nb, upload_respbuff_nb, upload_resp_maxlen_nb);
			upload_string_state = 3;
			break;

		case 3:
			if (res>0){
				upload_string_state = 0;
				return res;
			}
			upload_string_state = 0;

			res = upload_step4(upload_resp_len_nb);
			if (res>0)
				return res;

			return ESP_UPLOAD_DONE_OK;

		default:
			upload_string_state = 0;
			return FAIL_GEN;
			break;

	}

	return 0;
}

uint8_t esp_busy_upload(void)
{
	if (upload_string_state > 0)
		return 1;
	else
		return 0;

}

uint8_t esp_busy_cmd(void)
{
	if (cmd_state > 0)
		return 1;
	else
		return 0;

}

uint8_t esp_busy(void)
{
	if ((upload_string_state > 0) || (cmd_state > 0))
		return 1;
	else
		return 0;
}

void esp_upload_node_non_blocking_start(char* dest_ip, char* string, char* respbuff, uint16_t resp_maxlen, uint16_t* resp_len)
{
	//copy stuff for use later
	upload_dest_ip_nb = dest_ip;
	upload_string_nb = string;
	upload_respbuff_nb = respbuff;
	upload_resp_maxlen_nb = resp_maxlen;
	upload_resp_len_nb = resp_len;

	*resp_len = 0;
	upload_step1_nb(dest_ip, string, respbuff, resp_maxlen);
	upload_string_state = 1;

}


static uint8_t esp_upload_wait_response(void)
{
	while((pending_command&0x7F) && (timeout) ) process_esp_buffer();
	if ((pending_command > 1) || (timeout == 0)){ //failure
		response_buffer = 0;
		esp_conn_close();
		return pending_command;
	}
	return 0;
}

uint8_t esp_upload_node(char* dest_ip, char* string, char* respbuff, uint16_t resp_maxlen, uint16_t* resp_len)
{
	*resp_len = 0;
	uint8_t res = 0;
	upload_step1_nb(dest_ip, string, respbuff, resp_maxlen);
	res = esp_upload_wait_response();
	if (res>0){
		if (res == FAIL_STILL_CONN)
			esp_conn_close();
		return res;
	}

	upload_step2_nb(dest_ip, string, respbuff, resp_maxlen);
	res = esp_upload_wait_response();
	if (res>0)
		return res;

	upload_step3_nb(dest_ip, string, respbuff, resp_maxlen);
	res = esp_upload_wait_response();
	if (res>0)
		return res;

	res = upload_step4(resp_len);
	if (res>0)
		return res;

	return 0;
}

//0- not found; 1-found
static uint8_t look_for_200(void)
{
	if (response_buffer == 0)
		return 0;
	uint16_t i = 0;
	while((i+11) < response_ptr)
	{
		if (response_buffer[i] == '\"'){
			if (strncmp_circ(response_buffer, "\"error\":0,", i, response_maxlen, 10))
				return 1;
		}
		i++;
	}
	return 0;
}

void esp_process_line_rx(char *buffin, uint16_t line_start, uint16_t line_end, uint16_t buff_len)
{
	uint16_t ptr = line_start;
	uint16_t count = 0;			//number of non whitespace characters received
	uint16_t text_start = 0;	//first offset address of non whitespace
	char in;
	line_end++;
	if (line_end == buff_len)
		line_end = 0;
	while(ptr != line_end)
	{
		in = buffin[ptr];
		if ((in != ' ') || count > 0)
			count++;
		else
			text_start++;

		//look for things like OK, ready
		if (count == 2){
			if (strncmp_circ(buffin, "OK", line_start+text_start, buff_len, 2)){
				if (pending_command == 1)
					pending_command = 0;
			}
		}
		//if (count == 7){
		//	if (strncmp_circ(buffin, "CONNECT", line_start+text_start, buff_len, 7)){
		//		if (pending_command == 3)
		//			pending_command = 1;
		//	}
		//}
		if (count == 5){
			if (strncmp_circ(buffin, "ready", line_start+text_start, buff_len, 5)){
				if (pending_command == 2)
					pending_command = 0;
			}
		}
		if (count == 9){
			if (strncmp_circ(buffin, "no change", line_start+text_start, buff_len, 9)){
				if (pending_command == 1)
					pending_command = 0;
			}
		}
		if (count == 8){
			if (strncmp_circ(buffin, "DNS Fail", line_start+text_start, buff_len, 8)){
				if (pending_command == 4)
					pending_command = FAIL_DNS;
			}
		}
		if (count == 7){
			if (strncmp_circ(buffin, "SEND OK", line_start+text_start, buff_len, 7)){
				if (pending_command == 5)
					pending_command = 6;
			}
		}
		if (count == 6){
			if (strncmp_circ(buffin, "Unlink", line_start+text_start, buff_len, 6)){
				if (pending_command == 6)
					pending_command = 0;
			}
		}
		if (count == 4){
			if (strncmp_circ(buffin, "FAIL", line_start+text_start, buff_len, 4))
				pending_command = FAIL_GEN;
		}
		if (count == 12){
			if (strncmp_circ(buffin, "busy inet...", line_start+text_start, buff_len, 12))
				pending_command = FAIL_FATAL;
		}
		if (count == 5){
			if (strncmp_circ(buffin, "Error", line_start+text_start, buff_len, 5))
				pending_command = FAIL_GEN;

		}
		if (count == 6){
			if (strncmp_circ(buffin, "Linked", line_start+text_start, buff_len, 6)){
				if (pending_command == 4)
					pending_command = 0;
			}
		}
		if (count == 7){
			if (strncmp_circ(buffin, "CONNECT", line_start+text_start, buff_len, 7)){
				if (pending_command == 4)
					pending_command = 1;
			}
		}
		if (count == 14){
			if (strncmp_circ(buffin, "ALREAY CONNECT", line_start+text_start, buff_len, 14)){
				if (pending_command == 4)
					pending_command = FAIL_FATAL; //0;   //made this an error condition as it should be disconnected before trying to connect again
			}
		}
		if (count == 9){
			if (strncmp_circ(buffin, "busy p...", line_start+text_start, buff_len, 9)){
				pending_command = FAIL_FATAL;
			}
		}

		ptr++;
		if (ptr == buff_len)
			ptr = 0;
	}
}

void timeout_tick(void)
{
	if (timeout)
		timeout--;
}

static void uart_send_blocking_string(char *buff)
{
	while(*buff)
		usart_send_blocking(ESP_USART,*buff++);

}



//in1 is a circular buffer
//in2 is a \0 terminated char[]
//start1 is the offset of in1 for which the string to compare starts
//end1 is the length of in1 when the ptr needs to wrap to 0
//len2 is the maximum length of in2
//returns 1 if the same, else 0
static uint8_t strncmp_circ(char *in1, const char *in2, uint16_t start1, uint16_t wrap1, uint16_t len2)
{
	uint16_t ptr1 = start1;
	uint16_t ptr2 = 0;


	while(ptr2 < len2 && in2[ptr2] && in1[start1])
	{
		if (in1[ptr1] != in2[ptr2])
			return 0;

		ptr2++;
		ptr1++;
		if (ptr1 == wrap1)
			ptr1 = 0;
	}

	return 1;
}


void esp_rx_byte(uint8_t d)
{
	esp_buff_ptr_w++;
	if (esp_buff_ptr_w >= ESP_BUFF_LEN)
		esp_buff_ptr_w = 0;
	esp_buff[esp_buff_ptr_w] = d;

	if (pending_command == 3){
		if (d == '>')
			pending_command = 0;
	}

	if ((pending_command == 6) || (pending_command == 5)){
		if (response_buffer > 0){
			if (response_ptr < response_maxlen){
				response_buffer[response_ptr++] = d;
			}
		}
	}

	if (enable_echo){
		echoptr((char)d);
	}

}

static void process_esp_buffer(void)
{
	if (check_esp_buffer())
	{
		esp_process_line_rx(esp_buff, esp_buff_ptr_last, esp_buff_ptr_r, ESP_BUFF_LEN);
		esp_buff_ptr_last = esp_buff_ptr_r;
	}
}

static void clear_buffer(void)
{
	esp_buff_ptr_r = esp_buff_ptr_w;
	esp_buff_ptr_last = esp_buff_ptr_r;
}

//looks for strings in the bt buffer
//returns 1 if it has found a new string
static uint8_t check_esp_buffer(void)
{
	if (esp_buff_ptr_r != esp_buff_ptr_w)
	{
		if (esp_buff_ptr_r == esp_buff_ptr_last && in_new_string == 0) //increment both ptrs until a non \r\n char is found
		{
			esp_buff_ptr_r++;
			if (esp_buff_ptr_r >= ESP_BUFF_LEN)
				esp_buff_ptr_r = 0;

			if (!((esp_buff[esp_buff_ptr_r] == (char)10) || (esp_buff[esp_buff_ptr_r] == (char)13)))
				in_new_string = 1;

			esp_buff_ptr_last = esp_buff_ptr_r;
		}
		else if (in_new_string == 1)
		{
			uint16_t temp = esp_buff_ptr_r + 1;

			if (temp >= ESP_BUFF_LEN)
				temp = 0;

			if (((esp_buff[temp] == (char)10) || (esp_buff[temp] == (char)13))){
				in_new_string = 0;
				return 1;
			}
			else
				esp_buff_ptr_r = temp;
		}
	}
	return 0;
}

void reverse(s) char *s; {
char *j;
int c;

  j = s + strlen(s) - 1;
  while(s < j) {
    c = *s;
    *s++ = *j;
    *j-- = c;
  }
}

/* itoa:  convert n to characters in s */
 void itoa(int n, char s[])
 {
     int i, sign;

     if ((sign = n) < 0)  /* record sign */
         n = -n;          /* make n positive */
     i = 0;
     do {       /* generate digits in reverse order */
         s[i++] = n % 10 + '0';   /* get next digit */
     } while ((n /= 10) > 0);     /* delete it */
     if (sign < 0)
         s[i++] = '-';
     s[i] = '\0';
     reverse(s);
 }
