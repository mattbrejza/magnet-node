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
#define ESP_MSG_VERSION 0x01

void esp_request(uint8_t opcode, char* buf);
THD_FUNCTION(EspThread, arg);

#endif /* __ESP_H__ */

/**
 * @}
 */
