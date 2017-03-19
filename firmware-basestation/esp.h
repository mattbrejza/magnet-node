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

#include "rfm.h"

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
 * Buffer size for extracting data from ESP responses
 */
#define USER_PRINT_BUF_SIZE 64

/**
 * Number of items in the ESP thread processing mailbox
 */
#define MAILBOX_ITEMS 16

/**
 * Priority levels for queueing messages for ESP
 */
#define ESP_PRIO_NORMAL 0
#define ESP_PRIO_HIGH   1

/**
 * Max number of retries allowed for transactions to/from the ESP.
 */
#define ESP_RETRIES_MAX 3

/**
 * Flash storage
 */
#define FLASH_STORAGE_ADDR          ((uint32_t)0x0800fc00)
#define FLASH_STORAGE_LEN           0x400
#define FLASH_PAGE_SIZE             0x400

/* Maximal length of the node name */
#define ORIGIN_LEN_MAX              32

/* Maximal length of the wifi SSID */
#define SSID_LEN_MAX                32

/* Maximal length of the password */
#define PASS_LEN_MAX                32

/* Validity bits */
#define ORIGIN_VALID                (1<<0)
#define SSID_PASS_VALID             (1<<1)

/* Where origin, ssid and pass are stored in flash */
#define FLASH_ORIGIN                FLASH_STORAGE_ADDR
#define FLASH_SSID                  FLASH_STORAGE_ADDR + ORIGIN_LEN_MAX
#define FLASH_PASS                  FLASH_SSID + SSID_LEN_MAX
#define FLASH_VALID                 FLASH_PASS + PASS_LEN_MAX

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
#define ESP_RESP_READY              "ready\r\n"
#define ESP_RESP_NOCHANGE           "no change\r\n"
#define ESP_RESP_FAIL               "FAIL\r\n"
#define ESP_RESP_ERROR              "ERROR"
#define ESP_RESP_ERROR2             "Error"
#define ESP_RESP_LINKED             "Linked\r\n"
#define ESP_RESP_ALREADY_CONNECTED  "ALREADY CONNECT"
#define ESP_RESP_UNLINK             "Unlink\r\n"
#define ESP_RESP_SENDOK             "SEND OK\r\n"
#define ESP_RESP_SENDFAIL           "SEND FAIL\r\n"
#define ESP_RESP_NOLINK             "link is not\r\n"

/**
 * These are the messages that are posted to the mailbox
 * @note These must be word-aligned, i.e. must be multiples of 4 bytes
 * on 32 bit architectures.
 */
typedef struct esp_message_t {
    uint32_t opcode;
    systime_t timestamp;
    int32_t retries;
    rfm_packet_t rfm_packet;
} esp_message_t;

/**
 * The current status of the ESP is stored here
 */
typedef struct esp_status_t {
    uint8_t ipstatus;
    uint8_t linkstatus;
    char ip[16];
} esp_status_t;

/**
 * The configuration of the wifi portion of the node, including:
 * - the node name
 * - the wifi ssid
 * - the wifi password
 * @note This struct should be stored in non-volatile memory and loaded on
 * system boot.
 * @warning This must be aligned to word boundaries (32 bit on ARM CMx)
 */
typedef struct esp_config_t {
    char origin[ORIGIN_LEN_MAX];
    char ssid[SSID_LEN_MAX];
    char pass[PASS_LEN_MAX];
    int32_t validity;
} esp_config_t;

void esp_request(uint32_t opcode, rfm_packet_t* packet, uint8_t retries,
        systime_t timestamp, uint8_t prio);
void esp_set_origin(char *neworigin);
void esp_set_ssid_pass(char* ssid, char* pass);
uint8_t esp_get_status(void);
char* esp_get_ip(void);
esp_config_t * esp_get_config(void);
THD_FUNCTION(EspThread, arg);

#endif /* __ESP_H__ */

/**
 * @}
 */
