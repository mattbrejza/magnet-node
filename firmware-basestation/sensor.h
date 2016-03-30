/**
 * This file is part of the ukhasnet dongle project. 
 *
 * @file sensor.h
 * @author Jon Sowman <jon+github@jonsowman.com>
 * @copyright Jon Sowman 2015
 *
 * @addtogroup sensor
 * @{
 */

#ifndef __SENSOR_H__
#define __SENSOR_H__

#define HTU_ADDR 0x40
#define HTU_READ_TEMP 0xE3
#define HTU_READ_HUMID 0xE5

/* Sensor transmit every x seconds */
#define SENSOR_INTERVAL 180

/* Error types */
#define ERROR_HTU_TIMEOUT (1<<0)

/* Sensor readings */
typedef struct readings_t {
    uint8_t temp;
    uint8_t temp_dec;
    uint8_t humid;
    uint8_t humid_dec;
    uint8_t temp_valid;
    uint8_t humid_valid;
} readings_t;

void sensor_log_error(uint8_t error);

/**
 * Sensor thread
 */
THD_FUNCTION(SensorThread, arg);

#endif /* __SENSOR_H__ */

/**
 * @}
 */
