/*
 * htu21.h
 *
 *  Created on: 11 Apr 2015
 *      Author: Matt
 */

#ifndef HTU21_H_
#define HTU21_H_

#define HTU21_ADDR (0x80>>0)
#define HTU21_READ_TEMP 0xE3
#define HTU21_READ_HUMID 0xE5

#define H_I2C_RCC RCC_I2C2
#define H_I2C I2C2
#define H_I2C_PORT GPIOB
#define H_I2C_SCL_PIN GPIO10
#define H_I2C_SDA_PIN GPIO11
#define H_I2C_AFn GPIO_AF1

void htu21_init(void);
uint16_t htu21_read_sensor(uint8_t sensor);
uint16_t convert_humidity(uint16_t in);
void htu21_set_resolution(uint8_t);
int16_t convert_temperature(uint16_t in);

#endif /* HTU21_H_ */
