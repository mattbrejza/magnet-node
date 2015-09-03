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
#include "hal.h"

#include "esp.h"

/**
 * Size of the ring buffer into which we put incoming data from the ESP
 * that is waiting to be processed
 */
#define ESP_RINGBUF_SIZE 64

/**
 * Number of items in the ESP thread processing mailbox
 */
#define MAILBOX_ITEMS 8

/**
 * Quick facility to get the used value of a ring buffer
 * @param b A pointer to the buffer which we wish to query
 */
#define rb_getused_m(b) ((b->tail==b->head) ? 0:(b->head - b->tail + b->len) % b->len)

/**
 * Quick facility to get the free value of a ring buffer
 * @param b A pointer to the buffer which we wish to query
 */
#define rb_getfree_m(b) ((b->tail==b->head) ? b->len : (b->tail - b->head + b->len) % b->len)

/**
 * Reset a ring buffer to its original empty state
 * @param b A pointer to the buffer which we wish to query
 */
#define rb_reset_m(b) do {b->tail = b->head = 0} while (0)

/**
 * A ring buffer
 */
typedef struct ringbuffer_t
{
        char* buffer;
        uint16_t head, tail, len, mask;
        uint8_t overflow;
} ringbuffer_t;

/**
 * These are the messages that are posted to the mailbox
 */
typedef struct esp_message_t {
    uint32_t opcode;
    char buf[64];
} esp_message_t;

/**
 * Memory for the ESP buffer
 */
static char esp_ringbuf[ESP_RINGBUF_SIZE];

/**
 * A ring buffer that is used to keep track of unprocessed data from
 * the ESP
 */
static ringbuffer_t esp_ringbuffer;

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
    switch(msg->opcode)
    {
        case ESP_MSG_VERSION:
            // Set the state
            esp_state = ESP_MSG_VERSION;
            // Request the firmware version of the ESP
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_VERSION, 
                    strlen(ESP_STRING_VERSION), MS2ST(100));
            break;
        case ESP_MSG_AT:
            esp_state = ESP_MSG_AT;
            sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_AT, 
                    strlen(ESP_STRING_AT), MS2ST(100));
        default:
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
}

/**
 * Write a single byte to a ringbuffer.
 * @param rb The buffer into which to write
 * @param c The byte to write into the buffer
 * @returns 0 for success, non-0 for failure
 */
static uint8_t ringbuf_write_byte(ringbuffer_t *rb, char *c)
{
    // Write to head
    *(rb->buffer + rb->head) = *c;

    // Increment and wrap head if necessary
    rb->head = (rb->head + 1) & rb->mask;

    return 0;
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

    // Ring buffer initialisation
    esp_ringbuffer.buffer = esp_ringbuf;
    esp_ringbuffer.head = esp_ringbuffer.tail = 0;
    esp_ringbuffer.len = ESP_RINGBUF_SIZE;
    esp_ringbuffer.mask = esp_ringbuffer.len - 1;

    // Loop forever for this thread
    while(TRUE)
    {
        // If we're in the middle of a transaction, continue processing it
        if(esp_state != 0)
        {
            // Get a new byte if there is one
            if( esp_receive_byte(&newbyte) > 0 )
            {
                // Move into the ring buffer
                ringbuf_write_byte(&esp_ringbuffer, &newbyte);
                // Deal with the state machine if this is an end-of-line
                if(newbyte == '\n')
                    esp_state_machine();
            }
            else
            {
                // Only sleep this thread if the buffer is empty
                chThdSleepMilliseconds(1);
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

            // Process this message
            esp_process_msg((esp_message_t *)msg);
        }

    }
}

/**
 * @}
 */
