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
#include "esp.h"

/* Mutex allowing us to use the SDU or not */
extern mutex_t sdu_mutex;

/* Timer for sending messages */
systime_t _sensor_timer;

/* Errors stored here */
uint8_t _errors;

/* Sequence id */
static char _seqid = 'a';

/*
 * I2C2 config. See p643 of F0x2 refman.
 */
static const I2CConfig i2c2_config = { 
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
static void sensor_read_temperature(void)
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
 * Construct and post a sensor message
 */
static void sensor_message(void)
{
    // Packet
    rfm_packet_t packet;
    esp_config_t* config;

    // Get esp config
    config = esp_get_config();

    // Wait until config is valid
    while(!config->validity);

    // Construct packet
    chsnprintf((char *)packet.payload, 64, "0%cX%02X:%s[%s]",
            _seqid,
            _errors,
            FW_VERSION,
            config->origin);

    // Set rssi to 0
    packet.rssi = 0;

    // Submit packet
    esp_request(ESP_MSG_START, &packet, ESP_PRIO_NORMAL);

    // Advance seqid
    _seqid = (_seqid == 'z') ? 'b' : _seqid + 1;

    // Reset timer and errors
    _sensor_timer = chVTGetSystemTime();
    _errors = 0;
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

    // Configure timer and send initial message
    _sensor_timer = 0;

    // Set errors all zero
    _errors = 0;

    // Set up the SPI driver
    i2cStart(&I2CD2, &i2c2_config);

    while(TRUE)
    {
        // Check we should not suspend for shell. Lock will put the thread into
        // a WTMTX state (suspended) if it's held by the shell. Otherwise we
        // release it immediately and continue
        chMtxLock(&sdu_mutex);
        chMtxUnlock(&sdu_mutex);

        if(chVTGetSystemTime() > _sensor_timer + S2ST(SENSOR_INTERVAL))
        {
            sensor_message();
        }
        else
        {
            chThdSleepMilliseconds(1000);
        }
    }
}

/**
 * @}
 */
