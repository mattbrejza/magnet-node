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

/**
 * ESP responses
 */
#define ESP_RESP_OK                 "OK\r\n"

void esp_request(uint32_t opcode, char* buf);
THD_FUNCTION(EspThread, arg);

#endif /* __ESP_H__ */

/**
 * @}
 */
