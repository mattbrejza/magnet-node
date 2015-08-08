#define ESP_USART USART1

void esp_process_line_rx(char *buffin, uint16_t line_start, uint16_t line_end, uint16_t buff_len);
void esp_init(void);
uint8_t esp_reset(void);
uint8_t esp_connect_ap(char* ap_name, char* ap_password);
uint8_t esp_disconnect_ap(void);
void esp_rx_byte(uint8_t d);
uint8_t esp_upload_node(char* dest_ip, char* string, char* respbuff, uint16_t resp_maxlen, uint16_t* resp_len);
void timeout_tick(void);
void esp_run_echo(char* command, void (*print_char)(char));
uint8_t esp_service_upload_task(void);
void esp_upload_node_non_blocking_start(char* dest_ip, char* string, char* respbuff, uint16_t resp_maxlen, uint16_t* resp_len);
uint8_t esp_busy(void);
//uint8_t esp_conn_close(void);

#define FAIL_DNS 0xF0
#define FAIL_STILL_CONN 0xF1
#define FAIL_FATAL 0xFE
#define FAIL_GEN 0xFF
#define FAIL_NOT_200 0xF3
#define FAIL_TIMEOUT 0xF2

#define ESP_UPLOAD_DONE_OK 0xA0
