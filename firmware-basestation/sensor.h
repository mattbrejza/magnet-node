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

/**
 * Sensor thread
 */
THD_FUNCTION(SensorThread, arg);

#endif /* __SENSOR_H__ */

/**
 * @}
 */
