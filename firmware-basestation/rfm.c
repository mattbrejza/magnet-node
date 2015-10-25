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

THD_FUNCTION(RfmThread, arg)
{
    (void)arg;

    // Set up the SPI driver
    spiStart(&RFM_SPID, &rfm_spicfg);
}

/**
 * @}
 */
