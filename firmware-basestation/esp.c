/**
 * This file is part of the ukhasnet dongle project. 
 *
 * @file esp.c
 * @author Jon Sowman <jon+github@jonsowman.com>
 * @copyright Jon Sowman 2015
 *
 * @addtogroup esp
 * @{
 */

#include <string.h>

#include "ch.h"
#include "chprintf.h"
#include "hal.h"

#include "esp.h"
#include "dongle_shell.h"

static SerialUSBDriver *SDU1;
static esp_status_t esp_status;
static esp_config_t esp_config;

/* The message we're currently processing, NULL if none */
static esp_message_t *curmsg;

/* Binary semaphore allowing us to use the SDU or not */
extern mutex_t sdu_mutex;

/**
 * Memory for the ESP buffers
 */
static char esp_buffer[ESP_BUFFER_SIZE];
static char *esp_buf_ptr;
static char esp_out_buf[ESP_OUT_BUF_SIZE];
static char *esp_out_buf_ptr;

/**
 * Memory for the mailbox (msg_ts are queued in here)
 */
static msg_t mailbox_buffer[MAILBOX_ITEMS];

/**
 * Memory for the mailbox_mempool where esp_message_ts are stored
 */
static char mempool_buffer[MAILBOX_ITEMS * sizeof(esp_message_t)];

/**
 * Messages are stored in this memory pool; pointers to them are posted in the
 * mailbox.
 */
MEMORYPOOL_DECL(mailbox_mempool, MAILBOX_ITEMS * sizeof(esp_message_t), NULL);

/**
 * Messages into this thread are posted to this mailbox, which holds pointers
 * to the full messages in the mempool.
 */
MAILBOX_DECL(esp_mailbox, &mailbox_buffer, MAILBOX_ITEMS);

/* Send commands to the ESP */
const char ESP_STRING_VERSION[] = "AT+GMR\r\n";
const char ESP_STRING_AT[] = "AT\r\n";
const char ESP_STRING_RST[] = "AT+RST\r\n";
const char ESP_STRING_CWMODE[] = "AT+CWMODE=1\r\n";
const char ESP_STRING_IP[] = "AT+CIFSR\r\n";
const char ESP_STRING_JOIN[] = "AT+CWJAP=";
const char ESP_STRING_CRLF[] = "\r\n";
const char ESP_STRING_STATUS[] = "AT+CIPSTATUS\r\n";
const char ESP_STRING_SEND[] = "AT+CIPSEND=";
const char ESP_STRING_START[] = "AT+CIPSTART=";
const char ESP_STRING_ECHOOFF[] = "ATE0\r\n";
const char ESP_UPLOAD_START[] = "POST /api/upload HTTP/1.0\r\nHost: ukhas.net\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: ";
const char ESP_UPLOAD_END[] = "\r\nConnection: close\r\n\r\n";
const char ESP_UPLOAD_CONTENT_ORIGIN[] = "origin=";
const char ESP_UPLOAD_CONTENT_DATA[] = "&data=";
const char ESP_UPLOAD_CONTENT_RSSI[] = "&rssi=";
const char UKHASNET_IP[] = "\"TCP\",\"212.71.255.157\",80";

static void flash_lock(void)
{
    FLASH_CR |= FLASH_CR_LOCK;
}

static void flash_unlock(void)
{
    /* Clear the unlock state. */
    FLASH_CR |= FLASH_CR_LOCK;

    /* Authorize the FPEC access. */
    FLASH_KEYR = FLASH_KEYR_KEY1;
    FLASH_KEYR = FLASH_KEYR_KEY2;
}

static uint32_t flash_get_status_flags(void)
{
    return FLASH_SR & (FLASH_SR_PGERR |
            FLASH_SR_EOP |
            FLASH_SR_WRPRTERR |
            FLASH_SR_BSY);
}

static void flash_wait_for_last_operation(void)
{
    while ((flash_get_status_flags() & FLASH_SR_BSY) == FLASH_SR_BSY);
}

static void flash_program_half_word(uintptr_t address, uint16_t data)
{
    flash_wait_for_last_operation();
    FLASH_CR |= FLASH_CR_PG;
    MMIO16(address) = data;
    flash_wait_for_last_operation();
    FLASH_CR &= ~FLASH_CR_PG;
}

static void flash_program_word(uint32_t address, uint32_t data)
{
    flash_program_half_word(address, (uint16_t)data);
    flash_program_half_word(address+2, (uint16_t)(data>>16));
}

static void flash_erase_page(uint32_t page_address)
{
    flash_wait_for_last_operation();
    FLASH_CR |= FLASH_CR_PER;
    FLASH_AR = page_address;
    FLASH_CR |= FLASH_CR_STRT;
    flash_wait_for_last_operation();
    FLASH_CR &= ~FLASH_CR_PER;
}

/**
 * The esp config is stored in an esp_config_t. Write this to flash.
 * @param config Pointer to an esp_config_t containing the configuration to
 * write.
 */
static void write_config(esp_config_t* config)
{
    uint8_t i;
    uintptr_t* ptr;

    // Erase the current settings
    flash_unlock();
    flash_erase_page(FLASH_STORAGE_ADDR);

    // Loop through the esp_config_t word-by-word and write to flash
    ptr = (uintptr_t *)config;
    for(i = 0; i < sizeof(esp_config_t)/sizeof(uintptr_t); i++)
    {
        flash_program_word(FLASH_STORAGE_ADDR + 4*i, *ptr++);
    }

    // Relock flash
    flash_lock();
}

/**
 * Read an esp_config_t stored in flash.
 * @param config Pointer to an esp_config_t which will be written with the
 * configuration from flash memory.
 */
static void read_config(esp_config_t* config)
{
    uintptr_t i;
    uintptr_t* ptr;

    // Read through the esp_config_t
    ptr = (uintptr_t *)config;
    for(i = 0; i < sizeof(esp_config_t)/sizeof(uintptr_t); i++)
    {
        *ptr++ = *(uintptr_t *)(FLASH_ORIGIN + 4*i);
    }
}

/**
 * Reset the ESP
 */
static void esp_reset(void)
{
    // Power off and go into reset
    palClearPad(GPIOF, GPIOF_ESP_RST);
    palClearPad(GPIOF, GPIOF_ESP_CHPD);

    // GPIO0 should be high for normal mode (not bootloader)
    palSetPad(GPIOA, GPIOA_ESP_GPIO0);

    // Wait a little
    chThdSleepMilliseconds(100);

    // Power up the ESP
    palSetPad(GPIOF, GPIOF_ESP_CHPD);
    palSetPad(GPIOF, GPIOF_ESP_RST);
}

/**
 * Initialise the ESP by booting it in normal mode and setting up the USART to
 * talk to it
 */
static void esp_init(void)
{
    // Set up memory pool for ESP messages
    chPoolObjectInit(&mailbox_mempool, sizeof(esp_message_t), NULL);
    chPoolLoadArray(&mailbox_mempool, (void *)mempool_buffer, MAILBOX_ITEMS);

    // Set up mailbox
    chMBObjectInit(&esp_mailbox, (msg_t *)mailbox_buffer, MAILBOX_ITEMS);

    // Reset ESP
    esp_reset();

    // Configure UART
    static const SerialConfig sc = {
        115200, 0, USART_CR2_STOP1_BITS | USART_CR2_LINEN, 0};
    sdStart(&SD1, &sc);

    // Wait a little
    chThdSleepMilliseconds(1000);

    // Flush all data from the ESP after reset
    while(sdGetTimeout(&SD1, TIME_IMMEDIATE) != Q_TIMEOUT);
}

/**
 * Set the name of this node
 */
void esp_set_origin(char *neworigin)
{
    chsnprintf(esp_config.origin, ORIGIN_LEN_MAX, "%s", neworigin);
    esp_config.validity |= ORIGIN_VALID;
    write_config(&esp_config);
}

/**
 * Set the SSID and password
 */
void esp_set_ssid_pass(char* ssid, char* pass)
{
    strncpy(esp_config.ssid, ssid, SSID_LEN_MAX);
    strncpy(esp_config.pass, pass, PASS_LEN_MAX);
    esp_config.validity |= SSID_PASS_VALID;
    write_config(&esp_config);
}

/**
 * Get the current ESP status
 */
uint8_t esp_get_status(void)
{
    return esp_status.ipstatus;
}

/**
 * Get current IP address
 */
char* esp_get_ip(void)
{
    return esp_status.ip;
}

/**
 * Get config
 */
esp_config_t * esp_get_config(void)
{
    return &esp_config;
}

/**
 * Remove the message content from the memory pool and set curmsg=NULL
 * once we have finished with it
 */
static void esp_curmsg_delete(void)
{
    chPoolFree(&mailbox_mempool, (void *)curmsg);
    curmsg = NULL;
}

/**
 * We got a message from somewhere, do something with it
 */
static void esp_process_msg(esp_message_t* msg)
{
    uint16_t contentlen;
    char contentlen_s[3];
    char rssilen_s[6];

    switch(msg->opcode)
    {
        case ESP_MSG_VERSION:
            // Request the firmware version of the ESP
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_VERSION, 
                    strlen(ESP_STRING_VERSION), MS2ST(100));
            break;
        case ESP_MSG_AT:
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_AT, 
                    strlen(ESP_STRING_AT), MS2ST(100));
            break;
        case ESP_MSG_ECHOOFF:
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_ECHOOFF, 
                    strlen(ESP_STRING_ECHOOFF), MS2ST(100));
            break;
        case ESP_MSG_RST:
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_RST, 
                    strlen(ESP_STRING_RST), MS2ST(100));
            break;
        case ESP_MSG_CWMODE:
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_CWMODE,
                    strlen(ESP_STRING_CWMODE), MS2ST(100));
            break;
        case ESP_MSG_IP:
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_IP,
                    strlen(ESP_STRING_IP), MS2ST(100));
            break;
        case ESP_MSG_JOIN:
            esp_out_buf_ptr = esp_out_buf;
            chsnprintf(esp_out_buf, ESP_OUT_BUF_SIZE, "%s%s\r\n",
                    ESP_STRING_JOIN, msg->rfm_packet.payload);
            sdWriteTimeout(&SD1, (const uint8_t *)esp_out_buf,
                    strlen(esp_out_buf), MS2ST(500));
            break;
        case ESP_MSG_STATUS:
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_STATUS,
                    strlen(ESP_STRING_STATUS), MS2ST(100));
            break;
        case ESP_MSG_SEND:
            // Find length of the rssi bit
            chsnprintf(rssilen_s, 6, "%d", curmsg->rfm_packet.rssi);
            // Send the (up to) 64 byte message in the payload to the server
            contentlen = strlen((char *)curmsg->rfm_packet.payload)
                + strlen(rssilen_s)
                + strlen(ESP_UPLOAD_CONTENT_ORIGIN)
                + strlen(ESP_UPLOAD_CONTENT_DATA)
                + strlen(ESP_UPLOAD_CONTENT_RSSI)
                + strlen(esp_config.origin);
            chsnprintf(contentlen_s, 3, "%u", contentlen);
            // Reset buf pointer
            esp_out_buf_ptr = esp_out_buf;
            // Send CIPSEND=xx where xx is the number of bytes
            chsnprintf(esp_out_buf_ptr, 512, "%s%u\r\n", ESP_STRING_SEND,
                    strlen(ESP_UPLOAD_START)
                    + strlen(contentlen_s)
                    + strlen(ESP_UPLOAD_END)
                    + strlen(ESP_UPLOAD_CONTENT_ORIGIN)
                    + strlen(esp_config.origin)
                    + strlen(ESP_UPLOAD_CONTENT_DATA)
                    + strlen((char *)curmsg->rfm_packet.payload)
                    + strlen(ESP_UPLOAD_CONTENT_RSSI)
                    + strlen(rssilen_s));
            esp_out_buf_ptr += strlen(esp_out_buf_ptr);
            sdWriteTimeout(&SD1, (const uint8_t *)esp_out_buf,
                    strlen(esp_out_buf), MS2ST(500));
            // Wait for the ESP to send ">"
            chThdSleepMilliseconds(10);
            // Now begins the HTTP req
            // Reset buf pointer
            esp_out_buf_ptr = esp_out_buf;
            chsnprintf(esp_out_buf_ptr, 512, "%s%u%s%s%s%s%s%s%d", ESP_UPLOAD_START,
                    contentlen,
                    ESP_UPLOAD_END,
                    ESP_UPLOAD_CONTENT_ORIGIN,
                    esp_config.origin,
                    ESP_UPLOAD_CONTENT_DATA,
                    curmsg->rfm_packet.payload,
                    ESP_UPLOAD_CONTENT_RSSI,
                    curmsg->rfm_packet.rssi);
            esp_out_buf_ptr += strlen(esp_out_buf_ptr);
            sdWriteTimeout(&SD1, (const uint8_t *)esp_out_buf,
                    strlen(esp_out_buf), MS2ST(500));
            break;
        case ESP_MSG_START:
            // If not connected or have no IP, abort this and try and connect
            if(esp_status.ipstatus > 1 && esp_status.ipstatus < 5 
                    && strcmp(esp_status.ip, "") != 0
                    && strncmp(esp_status.ip, "0.0.0.0", 8) != 0)
            {
                if(esp_config.validity & ORIGIN_VALID &&
                        esp_config.validity & SSID_PASS_VALID)
                {
                    // Send AT+CIPSTART="TCP","<ip>",<port>\r\n
                    esp_out_buf_ptr = esp_out_buf;
                    chsnprintf(esp_out_buf, ESP_OUT_BUF_SIZE, "%s%s\r\n",
                            ESP_STRING_START,
                            UKHASNET_IP);
                    sdWriteTimeout(&SD1, (const uint8_t *)esp_out_buf,
                            strlen(esp_out_buf), MS2ST(500));
                    chThdSleepMilliseconds(10);
                } else {
                    if(shell_get_level() >= LEVEL_DEBUG)
                        chprintf((BaseSequentialStream *)SDU1, "Set origin (> esp origin) and SSID/password (> esp ap)\r\n");
                }
            }
            else
            {
                /* Drop this since no connection/no ip */
                esp_curmsg_delete();
            }
            break;
        default:
            curmsg = NULL;
            break;
    }

    return;
}

/**
 * Receive a message from another thread. We allocate memory for the
 * esp_message_t and add it to the MailBox so that it will be processed.
 */
void esp_request(uint32_t opcode, rfm_packet_t* packet, uint8_t prio)
{
    void* msg_in_pool;
    msg_t retval;

    // Construct the message (allow NULL pointers here)
    esp_message_t msg;
    msg.opcode = opcode;
    if(packet != NULL)
    {
        strncpy((char *)msg.rfm_packet.payload, (char *)packet->payload, 64);
        msg.rfm_packet.rssi = packet->rssi;
    }

    // Allocate memory for it in the pool
    msg_in_pool = chPoolAlloc(&mailbox_mempool);
    if(msg_in_pool == NULL) return;

    // Put message into pool
    memcpy(msg_in_pool, (void *)&msg, sizeof(esp_message_t));

    // Add to mailbox, if high priority then post to front of queue
    if(prio == ESP_PRIO_HIGH)
        retval = chMBPostAhead(&esp_mailbox, (intptr_t)msg_in_pool, TIME_IMMEDIATE);
    else
        retval = chMBPost(&esp_mailbox, (intptr_t)msg_in_pool, TIME_IMMEDIATE);

    // Check MB response
    if( retval != MSG_OK )
    {
        // Something went wrong, free the memory and return
        if(shell_get_level() >= LEVEL_DEBUG)
            chprintf((BaseSequentialStream *)SDU1, "Error posting to mailbox\r\n");
        chPoolFree(&mailbox_mempool, msg_in_pool);
        return;
    }

    // Otherwise, the consumer (i.e. the main thread processing the queues)
    // will free the memory from the pool once it has finished with the message
    return;
}

/**
 * Receive a single byte from the buffered serial stream from the ESP. 
 * Put into the buffer
 * @param buf The buffer into which we should receive data
 * @returns The number of bytes received
 */
static size_t esp_receive_byte(char* buf)
{
    msg_t resp;
    resp = sdGetTimeout(&SD1, TIME_IMMEDIATE);
    if (resp == Q_TIMEOUT || resp == Q_RESET)
        return 0;
    else
        *buf = (char)resp;
        return 1;
}

/**
 * The state machine that handles incoming messages
 */
static void esp_state_machine(void)
{
    char *bufptr;
    char *bufptr2;
    uint8_t len;
    char user_print_buf[64];

    /* What we do here depends on what we're waiting for */
    switch(curmsg->opcode)
    {
        case ESP_MSG_VERSION:
            if(strstr(esp_buffer, ESP_RESP_OK))
            {
                // Find first \n and print the first line pf response
                bufptr = strstr(esp_buffer, "\n");
                if(bufptr)
                {
                    len = bufptr - esp_buffer;
                    strncpy(user_print_buf, esp_buffer, len);
                    user_print_buf[len-1] = '\0';
                    chprintf((BaseSequentialStream*)SDU1, "%s\r\n", user_print_buf);
                    esp_curmsg_delete();
                } else {
                    if(shell_get_level() >= LEVEL_DEBUG)
                        chprintf((BaseSequentialStream*)SDU1,
                                "ESP_MSG_VER: LF not found\r\n");
                }
            }
            break;
        case ESP_MSG_RST:
            if(strstr(esp_buffer, ESP_RESP_READY))
            {
                chprintf((BaseSequentialStream*)SDU1, "ESP reset OK\r\n");
                // Immediately re-disable echo
                esp_request(ESP_MSG_ECHOOFF, NULL, ESP_PRIO_HIGH);
                esp_curmsg_delete();
            }
            break;
        case ESP_MSG_ECHOOFF:
            if(strstr(esp_buffer, ESP_RESP_OK))
            {
                if(shell_get_level() >= LEVEL_DEBUG)
                    chprintf((BaseSequentialStream*)SDU1, "ESP echo off\r\n");
                esp_curmsg_delete();
            }
            break;
        case ESP_MSG_CWMODE:
            if(strstr(esp_buffer, ESP_RESP_OK)
                    || strstr(esp_buffer, ESP_RESP_NOCHANGE))
            {
                esp_curmsg_delete();
            }
            break;
        case ESP_MSG_IP:
            // Look for +CIFSR:\"10.0.0.100\" and extract IP
            if(strstr(esp_buffer, ESP_RESP_OK))
            {
                bufptr = strstr(esp_buffer, ",");
                bufptr2 = strstr(esp_buffer, "\r");
                if(bufptr && bufptr2)
                {
                    // Subtract 2 to remove the quotes around the IP
                    len = bufptr2 - bufptr - 3;
                    strncpy(user_print_buf, bufptr+2, len);
                    // Null terminate the string
                    user_print_buf[len] = '\0';
                    strncpy(esp_status.ip, user_print_buf, 16);
                    esp_curmsg_delete();
                } else {
                    if(shell_get_level() >= LEVEL_DEBUG)
                        chprintf((BaseSequentialStream*)SDU1,
                                "ESP_MSG_IP: CR or , not found\r\n");
                }
            }
            break;
        case ESP_MSG_JOIN:
            if(strstr(esp_buffer, ESP_RESP_OK))
            {
                chprintf((BaseSequentialStream*)SDU1, "AP join success\r\n");
                esp_curmsg_delete();
            }
            else if(strstr(esp_buffer, ESP_RESP_FAIL))
            {
                chprintf((BaseSequentialStream*)SDU1, "AP join failure\r\n");
                esp_curmsg_delete();
            }
            break;
        case ESP_MSG_STATUS:
            if(strstr(esp_buffer, ESP_RESP_OK))
            {
                // Look for STATUS:2 and set ipstatus to 2
                bufptr = strstr(esp_buffer, "STATUS:");
                if(bufptr)
                {
                    // Update status struct with integer status value
                    esp_status.ipstatus = (uint8_t)(*(bufptr+7) - 48);
                    esp_curmsg_delete();
                } else {
                    if(shell_get_level() >= LEVEL_DEBUG)
                        chprintf((BaseSequentialStream*)SDU1,
                                "ESP_MSG_STATUS: \"STATUS:\" not found\r\n");
                }
            }
            break;
        case ESP_MSG_START:
            // Will be "OK" or "ALREADY CONNECTED" depending on conn state
            if(strstr(esp_buffer, ESP_RESP_OK) || 
                    strstr(esp_buffer, ESP_RESP_ALREADY_CONNECTED))
            {
                esp_status.linkstatus = ESP_LINKED;
                palClearPad(GPIOC, GPIOC_LED_WIFI);
                // The payload of curmsg is the packet data from the RFM
                esp_request(ESP_MSG_SEND, &curmsg->rfm_packet, ESP_PRIO_HIGH);
                esp_curmsg_delete();
            }
            // If we get ERROR, or "link is not"; retry
            else if(strstr(esp_buffer, ESP_RESP_ERROR) ||
                    strstr(esp_buffer, ESP_RESP_NOLINK) ||
                    strstr(esp_buffer, ESP_RESP_ERROR2))
            {
                if(shell_get_level() >= LEVEL_DEBUG)
                    chprintf((BaseSequentialStream*)SDU1, "Error, dropping msg\r\n");
                esp_request(ESP_MSG_START, &curmsg->rfm_packet, ESP_PRIO_HIGH);
                esp_curmsg_delete();
            }
            break;
        case ESP_MSG_SEND:
            if(strstr(esp_buffer, ESP_RESP_UNLINK))
            {
                esp_status.linkstatus = ESP_NOTLINKED;
                palSetPad(GPIOC, GPIOC_LED_WIFI);
                esp_curmsg_delete();
            }
            // If we get ERROR, or "link is not"; retry
            else if(strstr(esp_buffer, ESP_RESP_ERROR) ||
                    strstr(esp_buffer, ESP_RESP_NOLINK) ||
                    strstr(esp_buffer, ESP_RESP_ERROR2))
            {
                if(shell_get_level() >= LEVEL_DEBUG)
                    chprintf((BaseSequentialStream*)SDU1, "POST error, retrying...\r\n");
                esp_request(ESP_MSG_START, &curmsg->rfm_packet, ESP_PRIO_HIGH);
                palSetPad(GPIOC, GPIOC_LED_WIFI);
                esp_curmsg_delete();
            }
            break;
        default:
            if(shell_get_level() >= LEVEL_DEBUG)
                chprintf((BaseSequentialStream*)SDU1, "Fatal, esp state not reset!\r\n");
            return;
    }
}

/**
 * Main processing thread. We wait for commands to do things, either uploading
 * packets, or processing a control command (e.g. connect to wifi access
 * point).
 */
THD_FUNCTION(EspThread, arg)
{
    (void)arg;
    chRegSetThreadName("esp");
    
    // Result of mailbox operations
    msg_t mailbox_res;

    // Mailbox item
    intptr_t msg;

    // Set up the ESP
    esp_init();

    // Byte that we get from ESP
    char newbyte;

    // Initialise start of buffer
    esp_buf_ptr = esp_buffer;

    // Timers
    systime_t timeout_timer;
    systime_t esp_status_timer;
    esp_status_timer = chVTGetSystemTime();

    // Set initial esp status
    esp_status.linkstatus = ESP_NOTLINKED;
    esp_status.ipstatus = ESP_NOSTATUS;

    // Get pointer to SDU so we can print to shell
    SDU1 = usb_get_sdu();

    // Process configuration
    read_config(&esp_config);
    // If config has never been written before, write an invalid config
    if(esp_config.validity == -1)
    {
        chsnprintf(esp_config.origin, ORIGIN_LEN_MAX, "%s", "CHANGEME");
        chsnprintf(esp_config.ssid, SSID_LEN_MAX, "%s", "testssid");
        chsnprintf(esp_config.pass, PASS_LEN_MAX, "%s", "testpass");
        esp_config.validity = 0;
        write_config(&esp_config);
    }

    // Turn off ESP ECHO and enable station mode
    esp_request(ESP_MSG_ECHOOFF, NULL, ESP_PRIO_NORMAL);
    esp_request(ESP_MSG_CWMODE, NULL, ESP_PRIO_NORMAL);
    esp_request(ESP_MSG_STATUS, NULL, ESP_PRIO_NORMAL);

    // Loop forever for this thread
    while(TRUE)
    {
        // Check we should not suspend for shell
        chMtxLock(&sdu_mutex);
        chMtxUnlock(&sdu_mutex);

        // If we're in the middle of a transaction, continue processing it
        if(curmsg != NULL)
        {
            // Get a new byte if there is one
            if( esp_receive_byte(&newbyte) > 0 )
            {
                // Move serial chars into buffer (but ignore NULL)
                // FIXME: Buffer overrun possible here
                if(newbyte)
                {
                    *esp_buf_ptr++ = newbyte;
                    if(newbyte == '\n')
                        esp_state_machine();
                }
            }
            else if(chVTGetSystemTime() > timeout_timer + MS2ST(5000))
            {
                if(shell_get_level() >= LEVEL_DEBUG)
                    chprintf((BaseSequentialStream *)SDU1,
                            "Message timed out, aborting\r\n");
                esp_curmsg_delete();
            }
            else
            {
                // Only sleep this thread if the buffer is empty
                chThdSleepMilliseconds(1);
            }
        } 
        else /* curmsg == NULL */
        {
            // We've finished with the last transaction, wait for a new 
            // message in our mailbox and begin to process it
            // If there is no message, this will stall and the RTOS will
            // suspend this thread and do something else.
            mailbox_res = chMBFetch(&esp_mailbox, (msg_t *)&msg, TIME_IMMEDIATE);

            // Check that we got something, otherwise pass
            if(mailbox_res != MSG_OK || msg == 0)
            {
                chThdSleepMilliseconds(1);
            }
            else
            {
                // Discard everything in buffer
                memset(esp_buffer, 0, ESP_BUFFER_SIZE);
                esp_buf_ptr = esp_buffer;

                // Process this message
                curmsg = (esp_message_t *)msg;
                timeout_timer = chVTGetSystemTime();
                esp_process_msg(curmsg);
            }
        }

        // If more than 1 second has passed, update the status
        if(chVTGetSystemTime() - esp_status_timer > MS2ST(2000))
        {
            // Change LED
            if(esp_status.ipstatus > 1 && esp_status.ipstatus < 5
                    && strcmp(esp_status.ip, "") != 0
                    && strncmp(esp_status.ip, "0.0.0.0", 8) != 0)
                palSetPad(GPIOC, GPIOC_LED_WIFI);
            else
                palClearPad(GPIOC, GPIOC_LED_WIFI);
            // Update esp_status.ipstatus
            esp_request(ESP_MSG_STATUS, NULL, ESP_PRIO_NORMAL);
            esp_request(ESP_MSG_IP, NULL, ESP_PRIO_NORMAL);
            esp_status_timer = chVTGetSystemTime();
        }
    }
}

/**
 * @}
 */
