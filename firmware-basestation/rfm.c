/**
 * This file is part of the ukhasnet dongle project. 
 *
 * @file rfm.c
 * @author Jon Sowman <jon+github@jonsowman.com>
 * @copyright Jon Sowman 2015
 *
 * @addtogroup rfm
 * @{
 */

#include <string.h>

#include "ch.h"
#include "chprintf.h"
#include "hal.h"

#include "dongle_shell.h"
#include "esp.h"
#include "rfm.h"

/* Mutex allowing us to use the SDU or not */
extern mutex_t sdu_mutex;

/* Buffer for received data */
static rfm_packet_t rfm_packet;

// Timeout timer for the RFM
static systime_t rfm_timeout_timer;

// Baud rate = periph clock/3 = 1.5MHz
// DS[0111] is 8 bit data transfers
static const SPIConfig rfm_spicfg = {
    NULL,
    GPIOB,
    GPIOB_RFM_SS,
    SPI_CR1_BR_2,
    SPI_CR2_DS_2 | SPI_CR2_DS_1 | SPI_CR2_DS_0
};

// Track the current radio mode
static rfm_reg_t _mode;

// Serial driver
static SerialUSBDriver *SDU1;

/**
 * Send a byte to the RFM
 * @param data The byte to send
 */
static void spi_send_byte(const rfm_reg_t data)
{
    spiSend(&RFM_SPID, 1, (void *)&data);
}

/**
 * Receive a byte from the RFM
 * @returns The byte received
 */
static void spi_receive_byte(rfm_reg_t *data)
{
    spiReceive(&RFM_SPID, 1, (void *)data);
}

/**
 * Send bulk data to the RFM
 * @param data A pointer to the data to send
 * @param len The number of bytes to send
 */
static void spi_send_bulk(rfm_reg_t *data, const uint8_t len)
{
    spiSend(&RFM_SPID, len, (void *)data);
}

/**
 * Receive bulk data from the RFM
 * @param data A pointer into which the data will be read
 * @param len The number of bytes to read
 */
static void spi_receive_bulk(rfm_reg_t *data, const uint8_t len)
{
    spiReceive(&RFM_SPID, len, (void *)data);
}

/**
 * Read a register from the RFM
 * @param reg The register to read
 * @param res The rfm_reg_t into which the register value is put
 */
static rfm_status_t _rfm_read_register(const rfm_reg_t reg, rfm_reg_t *res)
{
    // Send the register then read the result back
    spiSelect(&RFM_SPID);
    spi_send_byte(reg);
    spi_receive_byte(res);
    spiUnselect(&RFM_SPID);
    return RFM_OK;
}

/**
 * Write a register to the RFM
 * @param reg The register to be written on the RFM
 * @param val The value to set the register 'reg' to
 */
static rfm_status_t _rfm_write_register(const rfm_reg_t reg, const rfm_reg_t val)
{
    // Set the WRITE bit to tell the RFM that we're writing (not reading)
    spiSelect(&RFM_SPID);
    spi_send_byte(reg | RFM69_SPI_WRITE_MASK);
    spi_send_byte(val);
    spiUnselect(&RFM_SPID);
    return RFM_OK;
}

/**
 * Bulk read data from the RFM
 * @param reg The register to begin the read form
 * @param res A pointer to an rfm_reg_t buffer into which we will read
 * @param len The number of bytes to read
 */
static rfm_status_t _rfm_read_burst(const rfm_reg_t reg, rfm_reg_t *res,
        uint8_t len)
{
    spiSelect(&RFM_SPID);
    // Send beginning address
    spi_send_byte(reg);
    // Read in bulk
    spi_receive_bulk(res, len);
    spiUnselect(&RFM_SPID);
    return RFM_OK;
}

/**
 * Bulk write data to the RFM
 * @param reg The first address which we will write to
 * @param data A pointer to an rfm_reg_t buffer which contains the data
 * @param len The number of bytes to write
 */
static rfm_status_t _rfm_write_burst(const rfm_reg_t reg, rfm_reg_t *data,
        uint8_t len)
{
    spiSelect(&RFM_SPID);
    spi_send_byte(reg | RFM69_SPI_WRITE_MASK);
    spi_send_bulk(data, len);
    spiUnselect(&RFM_SPID);
    return RFM_OK;
}

/**
 * Set the mode of the RFM69 radio/
 * @param mode The new mode of the radio
 */
static rfm_status_t rfm_setmode(const rfm_reg_t mode)
{
    rfm_reg_t res;
    _rfm_read_register(RFM69_REG_01_OPMODE, &res);
    _rfm_write_register(RFM69_REG_01_OPMODE, (res & 0xE3) | mode);
    _mode = mode;
    return RFM_OK;
}

/**
 * Clear the FIFO in the RFM69. We do this by entering STBY mode and then
 * returing to RX mode.
 * @warning Must only be called in RX Mode
 * @note Apparently this works... found in HopeRF demo code
 * @returns RFM_OK for success, RFM_FAIL for failure.
 */
static rfm_status_t _rfm_clearfifo(void)
{
    rfm_setmode(RFM69_MODE_STDBY);
    rfm_setmode(RFM69_MODE_RX);
    return RFM_OK;
}

/**
 * Get data from the RFM69 receive buffer.
 * @param rfm_packet A pointer into the rfm packet in which we would like the data
 * @param len The length of the data will be placed into this memory address
 * @param rfm_packet_waiting A boolean pointer which is true if a packet was
 * received and has been put into the buffer buf, false if there was no packet
 * to get from the RFM69.
 * @returns RFM_OK for success, RFM_FAIL for failure.
 */
static rfm_status_t rfm_receive(rfm_packet_t* rfm_packet, rfm_reg_t* len, 
        bool* rfm_packet_waiting)
{
    rfm_reg_t res;
    *len = 0;

    /* Check IRQ register for payloadready flag
     * (indicates RXed packet waiting in FIFO) */
    _rfm_read_register(RFM69_REG_28_IRQ_FLAGS2, &res);
    if(res & RF_IRQFLAGS2_PAYLOADREADY)
    {
        /* Get packet length from first byte of FIFO */
        _rfm_read_register(RFM69_REG_00_FIFO, len);
        /* Read FIFO into our Buffer */
        _rfm_read_burst(RFM69_REG_00_FIFO, rfm_packet->payload, *len);
        rfm_packet->payload[*len] = '\0';
        /* Read RSSI register (should be of the packet? - TEST THIS) */
        _rfm_read_register(RFM69_REG_24_RSSI_VALUE, &res);
        rfm_packet->rssi = -(res/2);
        /* Clear the radio FIFO (found in HopeRF demo code) */
        _rfm_clearfifo();
        *rfm_packet_waiting = true;
        return RFM_OK;
    } else {
        *rfm_packet_waiting = false;
        return RFM_OK;
    }
    
    return RFM_FAIL;
}

/**
 * Initialise and configure the RFM
 */
static void rfm_init(void)
{
    uint8_t i;
    rfm_reg_t res;

    // Raise RST for 100us to reset the RFM
    palSetPad(GPIOA, GPIOA_RFM_RST);
    chThdSleepMilliseconds(1);
    palClearPad(GPIOA, GPIOA_RFM_RST);

    // Wait a bit for the RFM to start up
    chThdSleepMilliseconds(100);

    /* Set up device */
    for (i = 0; CONFIG[i][0] != 255; i++)
        _rfm_write_register(CONFIG[i][0], CONFIG[i][1]);

    /* Set initial mode */
    _mode = RFM69_MODE_RX;
    rfm_setmode(_mode);

    /* Zero version number, RFM probably not
     * connected/functioning */
    res = 0;
    _rfm_read_register(RFM69_REG_10_VERSION, &res);
    while(!res)
    {
        chThdSleepMilliseconds(100);
        if(shell_get_level() >= LEVEL_DEBUG)
            chprintf((BaseSequentialStream *)SDU1, "RFM init failure\r\n");
        _rfm_read_register(RFM69_REG_10_VERSION, &res);
    }

    // Start timeout timer
    rfm_timeout_timer = chVTGetSystemTime();
}

/**
 * Main thread for RFM69
 */
THD_FUNCTION(RfmThread, arg)
{
    (void)arg;
    chRegSetThreadName("rfm");
    uint8_t i;
    rfm_reg_t len;
    bool packetwaiting;
    systime_t led_timer;
    
    /* Set to true if packet waiting */
    packetwaiting = false;
    
    // Get pointer to SDU so we cna print to shell
    SDU1 = usb_get_sdu();
    chThdSleepMilliseconds(100);

    // Set up the SPI driver
    spiStart(&RFM_SPID, &rfm_spicfg);

    /* Track time between LED flashes */
    led_timer = chVTGetSystemTime();

    // Init the RFM
    rfm_init();

    // Regularly poll the RFM for new packets, and if we get them,
    // post them to the ESP mailbox for uploading
    while(TRUE)
    {
        // Check we should not suspend for shell. Lock will put the thread into
        // a WTMTX state (suspended) if it's held by the shell. Otherwise we
        // release it immediately and continue
        chMtxLock(&sdu_mutex);
        chMtxUnlock(&sdu_mutex);

        // Check for new packets
        rfm_receive(&rfm_packet, &len, &packetwaiting);
        if(packetwaiting)
        {
            palSetPad(GPIOC, GPIOC_LED_868);
            led_timer = chVTGetSystemTime();
            if(shell_get_level() >= LEVEL_PACKET)
                chprintf((BaseSequentialStream *)SDU1, 
                        "Packet: %s (%ddBm)\r\n", 
                        rfm_packet.payload,
                        rfm_packet.rssi);
            esp_request(ESP_MSG_START, &rfm_packet, ESP_RETRIES_MAX,
                    chVTGetSystemTime(), ESP_PRIO_NORMAL);
            // Reset the timeout timer
            rfm_timeout_timer = chVTGetSystemTime();
            packetwaiting = false;
        }
        else if( chVTGetSystemTime() - rfm_timeout_timer > S2ST(60))
        {
            if(shell_get_level() >= LEVEL_DEBUG)
                chprintf((BaseSequentialStream *)SDU1, "RFM timeout - rebooting...\r\n");
            rfm_init();
        }
        else
        {
            chThdSleepMilliseconds(10);
            if(chVTGetSystemTime() > led_timer + MS2ST(500))
                palClearPad(GPIOC, GPIOC_LED_868);
        }
    }
}

/**
 * @}
 */
