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

#include "usbserial.h"
#include "esp.h"
#include "rfm.h"

#define RFM_SPID        SPID1
#define RFM_SS_PORT     GPIOB
#define RFM_SS_PIN      GPIOB_RFM_SS

/* Buffer for received data */
static rfm_reg_t rfm_buf[64];

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
 * @param buf A pointer into the local buffer in which we would like the data.
 * @param len The length of the data will be placed into this memory address
 * @param lastrssi The RSSI of the packet we're getting
 * @param rfm_packet_waiting A boolean pointer which is true if a packet was
 * received and has been put into the buffer buf, false if there was no packet
 * to get from the RFM69.
 * @returns RFM_OK for success, RFM_FAIL for failure.
 */
static rfm_status_t rfm_receive(rfm_reg_t* buf, rfm_reg_t* len, int16_t* lastrssi,
        bool* rfm_packet_waiting)
{
    rfm_reg_t res;
    *len = 0;

    /* Check IRQ register for payloadready flag
     * (indicates RXed packet waiting in FIFO) */
    _rfm_read_register(RFM69_REG_28_IRQ_FLAGS2, &res);
    if (res & RF_IRQFLAGS2_PAYLOADREADY) {
        /* Get packet length from first byte of FIFO */
        _rfm_read_register(RFM69_REG_00_FIFO, len);
        /* Read FIFO into our Buffer */
        _rfm_read_burst(RFM69_REG_00_FIFO, buf, *len);
        buf[*len] = '\0';
        /* Read RSSI register (should be of the packet? - TEST THIS) */
        _rfm_read_register(RFM69_REG_24_RSSI_VALUE, &res);
        *lastrssi = -(res/2);
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
 * Main thread for RFM69
 */
THD_FUNCTION(RfmThread, arg)
{
    (void)arg;
    uint8_t i;
    rfm_reg_t res, len;
    int16_t lastrssi;
    bool packetwaiting;
    
    packetwaiting = false;
    
    // Get pointer to SDU so we cna print to shell
    SDU1 = usb_get_sdu();
    chThdSleepMilliseconds(100);

    // Set up the SPI driver
    spiStart(&RFM_SPID, &rfm_spicfg);

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
        chprintf((BaseSequentialStream *)SDU1, "RFM init failure\r\n");
        _rfm_read_register(RFM69_REG_10_VERSION, &res);
    }

    // Regularly poll the RFM for new packets, and if we get them,
    // post them to the ESP mailbox for uploading
    while(TRUE)
    {
        // Check for new packets
        rfm_receive(rfm_buf, &len, &lastrssi, &packetwaiting);
        if(packetwaiting)
        {
            palSetPad(GPIOC, GPIOC_LED_868);
            chprintf((BaseSequentialStream *)SDU1, "Packet: %s\r\n",
                    rfm_buf);
            esp_request(ESP_MSG_START, (char *)rfm_buf);
            packetwaiting = false;
        }
        chThdSleepMilliseconds(10);
        palClearPad(GPIOC, GPIOC_LED_868);
    }
}

/**
 * @}
 */
