#define ESP_USART USART1

void esp_process_line_rx(char *buffin, uint16_t line_start, uint16_t line_end, uint16_t buff_len);
void esp_init(void);
uint8_t esp_reset(void);
uint8_t esp_connect_ap(char* ap_name, char* ap_password);
void esp_rx_byte(uint8_t d);
uint8_t esp_upload_node(char* dest_ip, char* string, char* respbuff, uint16_t resp_maxlen, uint16_t* resp_len);
void timeout_tick(void);

#define FAIL_DNS 0xF0
#define FAIL_GEN 0xFF
#define FAIL_NOT_200 0xF1
