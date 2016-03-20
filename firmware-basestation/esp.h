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
 * Size of the ring buffer into which we put incoming data from the ESP
 * that is waiting to be processed
 */
#define ESP_BUFFER_SIZE 512

/**
 * Size of the request buffer for constructing data to be sent to the ESP
 */
#define ESP_OUT_BUF_SIZE 512

/**
 * Number of items in the ESP thread processing mailbox
 */
#define MAILBOX_ITEMS 8

/**
 * Flash storage
 */
#define FLASH_STORAGE_ADDR          ((uint32_t)0x0800fc00)
#define FLASH_STORAGE_LEN           0x400
#define FLASH_PAGE_SIZE             0x400

#define MMIO16(addr)                (*(volatile uint16_t *)(addr))
#define MMIO32(addr)                (*(volatile uint32_t *)(addr))

#define PERIPH_BASE_AHB1            (PERIPH_BASE + 0x00020000)
#define FLASH_MEM_INTERFACE_BASE    (PERIPH_BASE_AHB1 + 0x2000)

/* --- FLASH registers ----------------------------------------------------- */

#define FLASH_ACR           MMIO32(FLASH_MEM_INTERFACE_BASE + 0x00)
#define FLASH_KEYR          MMIO32(FLASH_MEM_INTERFACE_BASE + 0x04)
#define FLASH_OPTKEYR       MMIO32(FLASH_MEM_INTERFACE_BASE + 0x08)
#define FLASH_SR            MMIO32(FLASH_MEM_INTERFACE_BASE + 0x0C)
#define FLASH_CR            MMIO32(FLASH_MEM_INTERFACE_BASE + 0x10)
#define FLASH_AR            MMIO32(FLASH_MEM_INTERFACE_BASE + 0x14)
#define FLASH_OBR           MMIO32(FLASH_MEM_INTERFACE_BASE + 0x1C)
#define FLASH_WRPR          MMIO32(FLASH_MEM_INTERFACE_BASE + 0x20)
/* Only present in STM32F10x XL series */
#define FLASH_KEYR2         MMIO32(FLASH_MEM_INTERFACE_BASE + 0x44)
#define FLASH_SR2           MMIO32(FLASH_MEM_INTERFACE_BASE + 0x4C)
#define FLASH_CR2           MMIO32(FLASH_MEM_INTERFACE_BASE + 0x50)
#define FLASH_AR2           MMIO32(FLASH_MEM_INTERFACE_BASE + 0x54)

/* --- FLASH Keys -----------------------------------------------------------*/

#define FLASH_KEYR_KEY1         ((uint32_t)0x45670123)
#define FLASH_KEYR_KEY2         ((uint32_t)0xcdef89ab)

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
#define ESP_MSG_ECHOOFF             0x10
#define ESP_MSG_CLOSE               0x11

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
#define ESP_RESP_ERROR              "ERROR"
#define ESP_RESP_ERROR2             "Error"
#define ESP_RESP_LINKED             "Linked\r\n"
#define ESP_RESP_ALREADY_CONNECTED  "ALREAY CONNECT\r\n"
#define ESP_RESP_UNLINK             "Unlink\r\n"
#define ESP_RESP_NOLINK             "link is not\r\n"

/**
 * These are the messages that are posted to the mailbox
 * @note These must be word-aligned, i.e. must be multiples of 4 bytes
 * on 32 bit architectures.
 */
typedef struct esp_message_t {
    uint32_t opcode;
    char payload[64];
} esp_message_t;

/**
 * The current status of the ESP is stored here
 */
typedef struct esp_status_t {
    uint8_t ipstatus;
    uint8_t linkstatus;
} esp_status_t;

/**
 * The configuration of the wifi portion of the node, including:
 * - the node name
 * - the wifi ssid
 * - the wifi password
 * @note This struct should be stored in non-volatile memory and loaded on
 * system boot.
 */
typedef struct esp_config_t {
    char origin[16];
    char ssid[16];
    char pass[16];
} esp_config_t;

void esp_request(uint32_t opcode, char* buf);
void esp_set_origin(char *neworigin);
uint8_t esp_get_status(void);
THD_FUNCTION(EspThread, arg);

#endif /* __ESP_H__ */

/**
 * @}
 */
