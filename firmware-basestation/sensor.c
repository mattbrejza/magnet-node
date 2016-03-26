/**
 * This file is part of the ukhasnet dongle project. 
 *
 * @file sensor.c
 * @author Jon Sowman <jon+github@jonsowman.com>
 * @copyright Jon Sowman 2015
 *
 * @addtogroup sensor
 * @{
 */

#include <string.h>

#include "ch.h"
#include "chprintf.h"
#include "hal.h"

#include "dongle_shell.h"
#include "esp.h"
#include "rfm.h"
#include "sensor.h"

/* Mutex allowing us to use the SDU or not */
extern mutex_t sdu_mutex;

/*
 * I2C2 config. See p643 of F0x2 refman.
 */
const I2CConfig i2c2_config = { 
    STM32_TIMINGR_PRESC(11U) |
        STM32_TIMINGR_SCLDEL(4U) | STM32_TIMINGR_SDADEL(2U) |
        STM32_TIMINGR_SCLH(15U)  | STM32_TIMINGR_SCLL(19U),
    0,  
    0
};

// Serial driver
static SerialUSBDriver *SDU1;

/**
 * Read temperature
 */
void sensor_read_temperature(void)
{
    uint8_t buf[3];
    uint8_t tx = HTU_READ_TEMP;
    msg_t res;
    // TIME_IMMEDIATE is not allowed here
    res = i2cMasterTransmitTimeout(&I2CD2, HTU_ADDR, &tx, 1, buf, 1,
            TIME_INFINITE);
    if(res == MSG_OK)
    {
        chprintf((BaseSequentialStream*)SDU1, "%02X %02X %02X\r\n", buf[0],
                buf[1], buf[2]);
    }
}

/**
 * Main thread for HTU21
 */
THD_FUNCTION(SensorThread, arg)
{
    (void)arg;
    chRegSetThreadName("sensor");
    
    // Get pointer to SDU so we cna print to shell
    SDU1 = usb_get_sdu();
    chThdSleepMilliseconds(100);

    // Set up the SPI driver
    i2cStart(&I2CD2, &i2c2_config);

    while(TRUE)
    {
        // Check we should not suspend for shell. Lock will put the thread into
        // a WTMTX state (suspended) if it's held by the shell. Otherwise we
        // release it immediately and continue
        chMtxLock(&sdu_mutex);
        chMtxUnlock(&sdu_mutex);

        // Read temperature
        sensor_read_temperature();
        chThdSleepMilliseconds(1000);
    }
}

/**
 * @}
 */
