/**
 * This file is part of the ukhasnet dongle project. 
 *
 * @file esp.h
 * @author Jon Sowman <jon+github@jonsowman.com>
 * @copyright Jon Sowman 2015
 *
 * @addtogroup esp
 * @{
 */

#ifndef __ESP_H__
#define __ESP_H__

/**
 * Operation codes
 */
#define ESP_MSG_VERSION             0x01
#define ESP_MSG_AT                  0x02
#define ESP_MSG_RST                 0x03
#define ESP_MSG_CWMODE              0x04
#define ESP_MSG_IP                  0x05
#define ESP_MSG_JOIN                0x06
#define ESP_MSG_STATUS              0x07
#define ESP_MSG_SEND                0x08
#define ESP_MSG_START               0x09

/**
 * Link status
 */
#define ESP_LINKED                  1
#define ESP_NOTLINKED               0

/**
 * IP status (AT+CIPSTATUS)
 */
#define ESP_NOSTATUS                0
#define ESP_GOTIP                   2
#define ESP_CONNECTED               5
#define ESP_DISCONNECTED            4

/**
 * ESP responses
 */
#define ESP_RESP_OK                 "OK\r\n"
#define ESP_RESP_READY              "Ready"
#define ESP_RESP_NOCHANGE           "no change\r\n"
#define ESP_RESP_FAIL               "FAIL\r\n"
#define ESP_RESP_LINKED             "Linked\r\n"

void esp_request(uint32_t opcode, char* buf);
THD_FUNCTION(EspThread, arg);

#endif /* __ESP_H__ */

/**
 * @}
 */
