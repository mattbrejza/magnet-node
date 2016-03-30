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

/* Readings */
static readings_t readings;

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
 * Report an error to be sent in the next packet
 * @param error The error to report (in sensor.h)
 */
void sensor_log_error(uint8_t error)
{
    _errors |= error;
}

/**
 * Read temperature
 */
static void sensor_read(void)
{
    uint8_t buf[3];
    uint8_t tx;
    float t;
    msg_t res;

    // Temperature first
    tx = HTU_READ_TEMP;
    // TIME_IMMEDIATE is not allowed here
    res = i2cMasterTransmitTimeout(&I2CD2, HTU_ADDR, &tx, 1, buf, 3,
            MS2ST(100));
    if(res == MSG_OK)
    {
        t = (float)((buf[0] << 8) | buf[1])  / 65536.0;
        t = t * 175.72 - 46.85;
        readings.temp = (uint8_t)t;
        readings.temp_dec = (uint8_t)(t*100 - (float)readings.temp*100);
        readings.temp_valid = 1;
    }
    else
    {
        if(res == MSG_TIMEOUT)
            sensor_log_error(ERROR_HTU_TIMEOUT);
        readings.temp_valid = 0;
    }
    // Now do humidity
    tx = HTU_READ_HUMID;
    res = i2cMasterTransmitTimeout(&I2CD2, HTU_ADDR, &tx, 1, buf, 3,
            MS2ST(100));
    if(res == MSG_OK)
    {
        t = (float)((buf[0] << 8) | buf[1])  / 65536.0;
        t = t * 125.0 - 6.0;
        readings.humid = (uint8_t)t;
        readings.humid_dec = (uint8_t)(t*10 - (float)readings.humid*10);
        readings.humid_valid = 1;
    }
    else
    {
        if(res == MSG_TIMEOUT)
            sensor_log_error(ERROR_HTU_TIMEOUT);
        readings.humid_valid = 0;
    }
}

/**
 * Construct and post a sensor message
 */
static void sensor_message(esp_config_t* config)
{
    // Packet
    rfm_packet_t packet;
    char *packetptr;
    size_t written, remaining;

    // Wait until config is valid
    while(!config->validity);

    // Construct packet
    packetptr = (char *)packet.payload;
    remaining = RFM69_MAX_MESSAGE_LEN;

    // Write hops and sequence ID
    written = chsnprintf(packetptr, remaining, "0%c", _seqid);
    packetptr += written;
    remaining -= written;
    
    // If temperature valid, append
    if(readings.temp_valid)
    {
        written = chsnprintf(packetptr, remaining, "T%u.%u",
                readings.temp,
                readings.temp_dec);
        packetptr += written;
        remaining -= written;
    }
    
    // If humidity valid, append
    if(readings.humid_valid)
    {
        written = chsnprintf(packetptr, remaining, "H%u.%u",
            readings.humid,
            readings.humid_dec);
        packetptr += written;
        remaining -= written;
    }
    
    // And the rest
    written = chsnprintf(packetptr, remaining, "X%02X:%s[%s]",
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

    esp_config_t* esp_config = esp_get_config();

    // Configure timer and send initial message
    _sensor_timer = 0;

    // Set errors all zero
    _errors = 0;

    // Readings invalid to begin with
    readings.temp_valid = 0;
    readings.humid_valid = 0;

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
            sensor_read();
            sensor_message(esp_config);
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
