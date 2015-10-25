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
#include "hal.h"

#include "esp.h"
#include "rfm.h"

#define RFM_SPID        SPID1
#define RFM_SS_PORT     GPIOB
#define RFM_SS_PIN      GPIOB_RFM_SS

// Baud rate = periph clock/128
// DS[0111] is 8 bit data transfers
static const SPIConfig rfm_spicfg = {
    NULL,
    GPIOB,
    GPIOB_RFM_SS,
    SPI_CR1_BR_2 | SPI_CR1_BR_1,
    SPI_CR2_DS_2 | SPI_CR2_DS_1 | SPI_CR2_DS_0
};

// Track the current radio mode
static rfm_reg_t _mode;

/**
 * Send a byte to the RFM
 * @param data The byte to send
 */
static void rfm_send_byte(const rfm_reg_t data)
{
    spiSelect(&RFM_SPID);
    spiSend(&RFM_SPID, 1, (void *)&data);
    spiUnselect(&RFM_SPID);
}

/**
 * Receive a byte from the RFM
 * @returns The byte received
 */
static void rfm_receive_byte(rfm_reg_t *data)
{
    spiSelect(&RFM_SPID);
    spiReceive(&RFM_SPID, 1, (void *)data);
    spiUnselect(&RFM_SPID);
}

/**
 * Send bulk data to the RFM
 * @param data A pointer to the data to send
 * @param len The number of bytes to send
 */
static void rfm_send_bulk(rfm_reg_t *data, const uint8_t len)
{
    spiSelect(&RFM_SPID);
    spiSend(&RFM_SPID, len, (void *)data);
    spiUnselect(&RFM_SPID);
}

/**
 * Receive bulk data from the RFM
 * @param data A pointer into which the data will be read
 * @param len The number of bytes to read
 */
static void rfm_receive_bulk(rfm_reg_t *data, const uint8_t len)
{
    spiSelect(&RFM_SPID);
    spiReceive(&RFM_SPID, len, (void *)data);
    spiUnselect(&RFM_SPID);
}

/**
 * Read a register from the RFM
 * @param reg The register to read
 * @param result The rfm_reg_t into which the register value is put
 */
static rfm_status_t rfm_read_register(const rfm_reg_t reg, rfm_reg_t *res)
{
    // Send the register then read the result back
    rfm_send_byte(reg);
    rfm_receive_byte(res);
    return RFM_OK;
}

/**
 * Write a register to the RFM
 * @param reg The register to be written on the RFM
 * @param val The value to set the register 'reg' to
 */
static rfm_status_t rfm_write_register(const rfm_reg_t reg, const rfm_reg_t val)
{
    // Set the WRITE bit to tell the RFM that we're writing (not reading)
    rfm_send_byte(reg | RFM69_SPI_WRITE_MASK);
    rfm_send_byte(val);
    return RFM_OK;
}

/**
 * Bulk read data from the RFM
 * @param @

THD_FUNCTION(RfmThread, arg)
{
    (void)arg;

    // Set up the SPI driver
    spiStart(&RFM_SPID, &rfm_spicfg);
}

/**
 * @}
 */
