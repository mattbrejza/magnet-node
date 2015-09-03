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

/**
 * Initialise the ESP by booting it in normal mode and setting up the USART to
 * talk to it
 */
static void esp_init(void)
{
    // Power off and go into reset
    palClearPad(GPIOF, GPIOF_ESP_RST);
    palClearPad(GPIOF, GPIOF_ESP_CHPD);

    // Configure UART
    static const SerialConfig sc = {
        9600, 0, USART_CR2_STOP1_BITS | USART_CR2_LINEN, 0};
    sdStart(&SD1, &sc);

    // Wait a little
    chThdSleepMilliseconds(100);

    // Power up the ESP
    palSetPad(GPIOF, GPIOF_ESP_CHPD);
    palSetPad(GPIOF, GPIOF_ESP_RST);

    // Set up mailbox
    chMBObjectInit(&esp_mailbox, (msg_t *)mailbox_buffer, MAILBOX_ITEMS);

    // Set up memory pool for ESP messages
    chPoolObjectInit(&mailbox_mempool, sizeof(esp_message_t), NULL);
    chPoolLoadArray(&mailbox_mempool, (void *)mempool_buffer, MAILBOX_ITEMS);

    // Wait a little
    chThdSleepMilliseconds(100);
}

/**
 * We got a message from somewhere, do something with it
 */
static void esp_process_msg(esp_message_t* msg)
{
    size_t ntxed;

    switch(msg->opcode)
    {
        case ESP_MSG_VERSION:
            // Set the state
            esp_state = ESP_MSG_VERSION;
            // Request the firmware version of the ESP
            ntxed = sdWriteTimeout(&SD1, (const uint8_t *)ESP_STRING_VERSION, 
                    strlen(ESP_STRING_VERSION), MS2ST(100));
            break;
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
 * Write n bytes to a RingBuffer.
 *
 * This is done via a fast memcpy operation and as such, there is logic in this
 * function to transparently handle the copy even if we're wrapping over the
 * boundary of the ring buffer.
 *
 * @param buf A pointer to the ring buffer we want to write to
 * @param data A pointer to the data to be written
 * @param n The number of bytes to be written to the ring buffer
 * @returns 0 for success, non-0 for failure
 */
static uint8_t ringbuf_write(ringbuffer_t *buf, char* data, uint16_t n)
{
    uint16_t rem;

    // Check we're not writing more than the buffer can hold
    if(n >= buf->len)
        return 1;

    // Make sure there's enough free space in the buffer for our data
    if(rb_getfree_m(buf) < n)
    {
        buf->overflow = 1;
        return 1;
    }

    // We can do a single memcpy as long as we don't wrap around the buffer
    if(buf->head + n < buf->len)
    {
        // We won't wrap, we can quickly memcpy
        memcpy(buf->buffer + buf->head, data, n);
        buf->head = (buf->head + n) & buf->mask;
    } else {
        // We're going to wrap, copy in 2 blocks
        // Copy the first (SD_BUF_LEN - buf->head) bytes
        rem = buf->len - buf->head;
        memcpy(buf->buffer + buf->head, data, rem);
        buf->head = (buf->head + rem) & buf->mask;
        // Copy the remaining bytes
        memcpy(buf->buffer + buf->head, data + rem, n - rem);
        buf->head = (buf->head + (n-rem)) & buf->mask;
    }
    return 0;
}

/**
 * Read n bytes from a ring buffer.
 *
 * This is done via a fast memcpy operation and as such, there is logic in this
 * function to transparently handle the copy even if we're wrapping over the
 * boundary of the ring buffer.
 *
 * @param buf A pointer to the ring buffer we want to write to
 * @param read_buffer Copy data into this array
 * @param n The number of bytes to be read from the ring buffer
 * @returns 0 for success, non-0 for failure
 */
static uint8_t ringbuf_read(ringbuffer_t *buf, char* read_buffer, uint16_t n)
{
    uint16_t rem;

    // We can't read more data than the buffer holds!
    if(n >= buf->len)
        return 1;

    // We can't read more bytes than the buffer currently contains
    if(n > rb_getused_m(buf))
        n = rb_getused_m(buf);

    if(buf->tail + n < buf->len)
    {
        // We won't wrap, we can quickly memcpy
        memcpy(read_buffer, buf->buffer + buf->tail, n);
        buf->tail = (buf->tail + n) & buf->mask;
    } else {
        // We're going to wrap, copy in 2 blocks
        // Copy the first (SD_BUF_LEN - buf->head) bytes
        rem = buf->len - buf->tail;
        memcpy(read_buffer, buf->buffer + buf->tail, rem);
        buf->tail = (buf->tail + rem) & buf->mask;
        // Copy the remaining bytes
        memcpy(read_buffer + rem, buf->buffer + buf->tail, n - rem);
        buf->tail = (buf->tail + (n-rem)) & buf->mask;
    }
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
        } 
        else
        {
            // We've finished with the last transaction, wait for a new 
            // message in our mailbox and begin to process it
            mailbox_res = chMBFetch(&esp_mailbox, (msg_t *)&msg, TIME_INFINITE);

            // Check that we got something, otherwise pass
            if(mailbox_res != MSG_OK || msg == 0) continue;

            // Process this message
            esp_process_msg((esp_message_t *)msg);
        }

        // Sleep
        chThdSleepMilliseconds(100);
    }
}

/**
 * @}
 */
