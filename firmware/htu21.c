/*
 * htu21.c
 *
 *  Created on: 11 Apr 2015
 *      Author: Matt
 */

#include <msp430.h>
#include <inttypes.h>
#include "i2c.h"
#include "htu21.h"

void htu21_init(void)
{
	i2c_init();
}


uint16_t htu21_read_sensor(uint8_t sensor)
{
	uint16_t out = 0;
//	uint8_t chk = 0;

	i2c_start();
	i2c_write8(HTU21_ADDR);
	i2c_write8(sensor);
	i2c_stop();
	i2c_start();
	i2c_write8(HTU21_ADDR | 1);
	out  = i2c_read8(0x00) << 8;  //MSB
	out |= i2c_read8(0xff);  //LSB
//	chk = i2c_read8(0xFF);        //checksum
	i2c_stop();

	return out;

}

//output in Cx100
int16_t convert_temperature(uint16_t in)
{
	in = in & 0xFFFC;
	int32_t a;
	a = (int32_t)in * 17572;
	a = a - (4685*65536);
	return (a>>16)&0xFFFF;
}

//output in %x100
uint16_t convert_humidity(uint16_t in)
{
	in = in & 0xFFFC;
	int32_t a;
	a = (int32_t)in * 125;
	a = a - 6;
	return (a>>16)&0xFFFF;
}


void htu21_set_resolution(uint8_t res)
{

}
