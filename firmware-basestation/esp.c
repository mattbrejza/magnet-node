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

#include <stdio.h>
#include <string.h>

#include "ch.h"
#include "chprintf.h"
#include "hal.h"

#include "esp.h"
#include "usbserial.h"

/**
 * Size of the ring buffer into which we put incoming data from the ESP
 * that is waiting to be processed
 */
#define ESP_BUFFER_SIZE 256

/**
 * Number of items in the ESP thread processing mailbox
 */
#define MAILBOX_ITEMS 8

static SerialUSBDriver *SDU1;

/**
 * These are the messages that are posted to the mailbox
 */
typedef struct esp_message_t {
    uint32_t opcode;
    char buf[64];
} esp_message_t;

/**
 * The current configuration and status of the ESP are stored here
 */
typedef struct esp_status_t {
    uint8_t ipstatus;
    uint8_t linkstatus;
} esp_status_t;

static esp_status_t esp_status;

/** Temporary storage for packets */
char packet_temp[64];

/**
 * Memory for the ESP buffer
 */
static char esp_buffer[ESP_BUFFER_SIZE];
static char *esp_buf_ptr;

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

uint32_t esp_state;

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
const char ESP_UPLOAD_START[] = "POST /api/upload HTTP/1.0\r\nHost: ukhas.net\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: ";
const char ESP_UPLOAD_END[] = "\r\nConnection: close\r\n\r\n";
const char UKHASNET_IP[] = "\"TCP\",\"212.71.255.157\",80";

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

    // Configure UART
    static const SerialConfig sc = {
        9600, 0, USART_CR2_STOP1_BITS | USART_CR2_LINEN, 0};
    sdStart(&SD1, &sc);

    // Wait a little
    chThdSleepMilliseconds(1000);

    // Flush all data from the ESP after reset
    while(sdGetTimeout(&SD1, TIME_IMMEDIATE) != Q_TIMEOUT);
}

/**
 * We got a message from somewhere, do something with it
 */
static void esp_process_msg(esp_message_t* msg)
{
    uint8_t packetlen;

    esp_state = msg->opcode;
    switch(esp_state)
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
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_JOIN,
                    strlen(ESP_STRING_JOIN), MS2ST(100));
            sdWriteTimeout(&SD1, (const uint8_t *)msg->buf,
                    strlen(msg->buf), MS2ST(100));
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_CRLF,
                    strlen(ESP_STRING_CRLF), MS2ST(100));
            break;
        case ESP_MSG_STATUS:
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_STATUS,
                    strlen(ESP_STRING_STATUS), MS2ST(100));
            break;
        case ESP_MSG_SEND:
            // Send the (up to) 64 byte message in the payload to the server
            packetlen = strlen(packet_temp);
            char s[6];
            sprintf(s, "%d\r\n", packetlen);
            // Send CIPSEND=xx where xx is the number of bytes
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_SEND,
                    strlen(ESP_STRING_SEND), MS2ST(100));
            // Now the number of bytes
            sdWriteTimeout(&SD1, (const uint8_t *)s,
                    strlen(s), MS2ST(100));
            // And an \r\n to terminate this line
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_CRLF,
                    strlen(ESP_STRING_CRLF), MS2ST(100));
            // Now begins the HTTP req
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_UPLOAD_START,
                    strlen(ESP_UPLOAD_START), MS2ST(100));
            // Insert content length
            sdWriteTimeout(&SD1, (const uint8_t *)s,
                    strlen(s), MS2ST(100));
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_UPLOAD_END,
                    strlen(ESP_UPLOAD_END), MS2ST(100));
            // Now the content
            sdWriteTimeout(&SD1, (const uint8_t *)packet_temp,
                    strlen(msg->buf), MS2ST(100));
            break;
        case ESP_MSG_START:
            // Send AT+CIPSTART="TCP","<ip>",<port>\r\n
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_START,
                    strlen(ESP_STRING_START), MS2ST(100));
            sdWriteTimeout(&SD1, (const uint8_t *)UKHASNET_IP,
                    strlen(UKHASNET_IP), MS2ST(100));
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_CRLF,
                    strlen(ESP_STRING_CRLF), MS2ST(100));
            strncpy(packet_temp, msg->buf, 64);
            break;
        default:
            esp_state = 0;
            break;
   }

    // Free the memory in the pool
    chPoolFree(&mailbox_mempool, (void *)msg);

    return;
}

/**
 * Receive a message from another thread. We allocate memory for the
 * esp_message_t and add it to the MailBox so that it will be processed.
 */
void esp_request(uint32_t opcode, char* buf)
{
    void* msg_in_pool;
    msg_t retval;

    // Construct the message (allow NULL pointers here)
    esp_message_t msg;
    msg.opcode = opcode;
    if( buf != NULL)
        strncpy(msg.buf, buf, 64);

    // Allocate memory for it in the pool
    msg_in_pool = chPoolAlloc(&mailbox_mempool);
    if(msg_in_pool == NULL) return;

    // Put message into pool
    memcpy(msg_in_pool, (void *)&msg, sizeof(esp_message_t));

    // Add to mailbox
    retval = chMBPost(&esp_mailbox, (intptr_t)msg_in_pool, TIME_IMMEDIATE);
    if( retval != MSG_OK )
    {
        // Something went wrong, free the memory and return
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
    char *bufptr, *bufptr2;
    uint8_t len;
    char user_print_buf[32];

    /* What we do here depends on what we're waiting for */
    switch(esp_state)
    {
        case ESP_MSG_VERSION:
            // Wait for OK
            if(strstr(esp_buffer, ESP_RESP_OK))
            {
                // Find first \n
                bufptr = strstr(esp_buffer, "\n");
                bufptr2 = strstr(bufptr+1, "\n");
                len = bufptr2 - bufptr;
                strncpy(user_print_buf, bufptr+1, len);
                user_print_buf[len] = '\0';
                chprintf((BaseSequentialStream*)SDU1, user_print_buf);
                chprintf((BaseSequentialStream*)SDU1, "\r\n");
                esp_state = 0;
            }
            break;
        case ESP_MSG_RST:
            if(strstr(esp_buffer, ESP_RESP_READY))
            {
                chprintf((BaseSequentialStream*)SDU1, "ESP reset successful\r\n");
                esp_state = 0;
            }
            break;
        case ESP_MSG_CWMODE:
            if(strstr(esp_buffer, ESP_RESP_OK)
                    || strstr(esp_buffer, ESP_RESP_NOCHANGE))
            {
                esp_state = 0;
            }
            break;
        case ESP_MSG_IP:
            if(strstr(esp_buffer, ESP_RESP_OK))
            {
                // Find first \n
                bufptr = strstr(esp_buffer, "\n");
                bufptr2 = strstr(bufptr+1, "\n");
                len = bufptr2 - bufptr;
                strncpy(user_print_buf, bufptr+1, len);
                user_print_buf[len] = '\0';
                chprintf((BaseSequentialStream*)SDU1, user_print_buf);
                chprintf((BaseSequentialStream*)SDU1, "\r\n");
                esp_state = 0;
            }
            break;
        case ESP_MSG_JOIN:
            if(strstr(esp_buffer, ESP_RESP_OK))
            {
                chprintf((BaseSequentialStream*)SDU1, "AP join success\r\n");
                esp_state = 0;
            }
            else if(strstr(esp_buffer, ESP_RESP_FAIL))
            {
                chprintf((BaseSequentialStream*)SDU1, "AP join failure\r\n");
                esp_state = 0;
            }
            break;
        case ESP_MSG_STATUS:
            if(strstr(esp_buffer, ESP_RESP_OK))
            {
                // Print first line of response from ESP only
                bufptr = strstr(esp_buffer, "\n");
                bufptr2 = strstr(bufptr+1, "\n");
                len = bufptr2 - bufptr;
                strncpy(user_print_buf, bufptr+1, len);
                user_print_buf[len] = '\0';
                chprintf((BaseSequentialStream*)SDU1, user_print_buf);
                chprintf((BaseSequentialStream*)SDU1, "\r\n");
                // Update status struct with integer status value
                esp_status.ipstatus = (uint8_t)(user_print_buf[len-3] - 48);
                esp_state = 0;
            }
            break;
        case ESP_MSG_START:
            if(strstr(esp_buffer, ESP_RESP_LINKED))
            {
                esp_status.linkstatus = ESP_LINKED;
                esp_state = 0;
            }
            break;
        case ESP_MSG_SEND:
            if(strstr(esp_buffer, ESP_RESP_OK))
            {
                esp_status.linkstatus = ESP_NOTLINKED;
                esp_state = 0;
            }
            break;
        default:
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

    // Set initial esp status
    esp_status.linkstatus = ESP_NOTLINKED;
    esp_status.ipstatus = ESP_NOSTATUS;

    // Get pointer to SDU so we cna print to shell
    SDU1 = usb_get_sdu();
    
    // Loop forever for this thread
    while(TRUE)
    {
        // If we're in the middle of a transaction, continue processing it
        if(esp_state != 0)
        {
            // Get a new byte if there is one
            if( esp_receive_byte(&newbyte) > 0 )
            {
                // Move into the buffer
                // FIXME: Buffer overrun possible here
                *esp_buf_ptr++ = newbyte;
                // Deal with the state machine at the end of each line
                if(newbyte == '\n')
                    esp_state_machine();
            }
            else
            {
                // Only sleep this thread if the buffer is empty
                chThdSleepMilliseconds(10);
            }
        } 
        else
        {
            // We've finished with the last transaction, wait for a new 
            // message in our mailbox and begin to process it
            // If there is no message, this will stall and the RTOS will
            // suspend this thread and do something else.
            mailbox_res = chMBFetch(&esp_mailbox, (msg_t *)&msg, TIME_INFINITE);

            // Check that we got something, otherwise pass
            if(mailbox_res != MSG_OK || msg == 0) continue;

            // Discard everything in buffer
            memset(esp_buffer, 0, ESP_BUFFER_SIZE);
            esp_buf_ptr = esp_buffer;

            // Process this message
            esp_process_msg((esp_message_t *)msg);
        }

    }
}

/**
 * @}
 */
