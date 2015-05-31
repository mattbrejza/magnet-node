/*
 * htu21.c
 *
 *  Created on: 11 Apr 2015
 *      Author: Matt
 */
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/stm32/rcc.h>
#include <inttypes.h>
#include "htu21.h"

uint8_t i2c_busy(uint32_t i2c);
void i2c_set_bytes_to_transfer(uint32_t i2c, uint32_t n_bytes);
void i2c_set_7bit_address(uint32_t i2c, uint8_t addr);
uint8_t i2c_transmit_int_status(uint32_t i2c);
uint8_t i2c_nack(uint32_t i2c);
uint8_t i2c_get_data(uint32_t i2c);
void htu_write_i2c(uint32_t i2c, uint8_t i2c_addr, uint8_t size, uint8_t *data);
void htu_read_i2c(uint32_t i2c, uint8_t i2c_addr, uint8_t size, uint8_t *data);
void htu_read_reg_i2c(uint32_t i2c, uint8_t i2c_addr, uint8_t reg, uint8_t size,uint8_t *data);
static void htu_write_i2c_start(uint32_t i2c, uint8_t i2c_addr, uint8_t size);
static void htu_write_i2c_next_byte(uint32_t i2c, uint8_t byte);
uint8_t i2c_received_data(uint32_t i2c);






void htu21_init(void)
{
	rcc_periph_clock_enable(H_I2C_RCC);
	gpio_mode_setup(H_I2C_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE,
				H_I2C_SCL_PIN|H_I2C_SDA_PIN);
	gpio_set_af(H_I2C_PORT, H_I2C_AFn, H_I2C_SCL_PIN|H_I2C_SDA_PIN);

	I2C_CR1(H_I2C) = 0;   //turns a bunch of stuff off, currently disabled
	//prescaler = 1, low period = 0x13, high period = 0xf, hold time = 2, setup = 4
	I2C_TIMINGR(H_I2C) = (1<<28) | (4<<20) | (2<<16) | (0xf<<8) | 0x13;
	I2C_CR1(H_I2C) |= I2C_CR1_PE;   //enable

}


uint16_t htu21_read_sensor(uint8_t sensor)
{


	uint8_t in_buff[3];
	uint16_t out = 0;
	htu_write_i2c(H_I2C, HTU21_ADDR, 1, &sensor);
	htu_read_i2c(H_I2C, HTU21_ADDR | 1, 3, in_buff);
	out = (in_buff[0] << 8) | in_buff[1];
	return out;

	/*
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
*/
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



uint8_t i2c_busy(uint32_t i2c)
{
	if ((I2C_ISR(i2c) & I2C_ISR_BUSY) != 0) {
		return 1;
	}
	return 0;
}
void i2c_set_bytes_to_transfer(uint32_t i2c, uint32_t n_bytes)
{
	I2C_CR2(i2c) = (I2C_CR2(i2c) & ~(0xFF<<16)) |
		       (n_bytes << I2C_CR2_NBYTES_SHIFT);
}
void i2c_set_7bit_address(uint32_t i2c, uint8_t addr)
{
	I2C_CR2(i2c) = (I2C_CR2(i2c) & ~(0x3FF)) |
		       ((addr));// << 1);
}
uint8_t i2c_transmit_int_status(uint32_t i2c)
{
	if ((I2C_ISR(i2c) & I2C_ISR_TXIS) != 0) {
		return 1;
	}

	return 0;
}
uint8_t i2c_nack(uint32_t i2c)
{
	if ((I2C_ISR(i2c) & I2C_ISR_NACKF) != 0) {
		return 1;
	}

	return 0;
}
uint8_t i2c_get_data(uint32_t i2c)
{
	return I2C_RXDR(i2c) & 0xff;
}
uint8_t i2c_received_data(uint32_t i2c)
{
	if ((I2C_ISR(i2c) & I2C_ISR_RXNE) != 0) {
		return 1;
	}
	return 0;
}

//from libopencm3
void htu_write_i2c(uint32_t i2c, uint8_t i2c_addr, uint8_t size, uint8_t *data)
{
	int wait;
	int i;
	while (i2c_busy(i2c) == 1);
	while ((I2C_CR2(i2c) & I2C_CR2_START)); //(i2c_is_start(i2c) == 1);
	/*Setting transfer properties*/
	i2c_set_bytes_to_transfer(i2c, size);
	i2c_set_7bit_address(i2c, (i2c_addr));// & 0x7F));
	I2C_CR2(i2c) &= ~I2C_CR2_RD_WRN; //i2c_set_write_transfer_dir(i2c);
	I2C_CR2(i2c) |= I2C_CR2_AUTOEND; //i2c_enable_autoend(i2c);
	/*start transfer*/
	I2C_CR2(i2c) |= I2C_CR2_START; //i2c_send_start(i2c);


	for (i = 0; i < size; i++) {
		wait = true;
		while (wait) {
			if (i2c_transmit_int_status(i2c)) {
				wait = false;
			}
			while (i2c_nack(i2c));
		}
		I2C_TXDR(i2c) = data[i]; //i2c_send_data(i2c, data[i]);
	}
}

void htu_read_i2c(uint32_t i2c, uint8_t i2c_addr, uint8_t size, uint8_t *data)
{
	int wait;
	int i;
	while (i2c_busy(i2c) == 1);
	while ((I2C_CR2(i2c) & I2C_CR2_START)); //(i2c_is_start(i2c) == 1);
	/*Setting transfer properties*/
	i2c_set_bytes_to_transfer(i2c, size);
	i2c_set_7bit_address(i2c, i2c_addr);
	I2C_CR2(i2c) |= I2C_CR2_RD_WRN; //i2c_set_read_transfer_dir(i2c);
	I2C_CR2(i2c) |= I2C_CR2_AUTOEND; //i2c_enable_autoend(i2c);
	/*start transfer*/
	I2C_CR2(i2c) |= I2C_CR2_START; //i2c_send_start(i2c);

	for (i = 0; i < size; i++) {
		while (i2c_received_data(i2c) == 0);
		data[i] = i2c_get_data(i2c);
	}
 }

void htu_read_reg_i2c(uint32_t i2c, uint8_t i2c_addr, uint8_t reg, uint8_t size,
              uint8_t *data)
{
	int wait;
	int i;
	while (i2c_busy(i2c) == 1);
	while ((I2C_CR2(i2c) & I2C_CR2_START)); //(i2c_is_start(i2c) == 1);
	/*Setting transfer properties*/
	i2c_set_bytes_to_transfer(i2c, 1);
	i2c_set_7bit_address(i2c, i2c_addr);
	I2C_CR2(i2c) |= I2C_CR2_RD_WRN; //i2c_set_write_transfer_dir(i2c);
	I2C_CR2(i2c) &= ~I2C_CR2_AUTOEND; //i2c_disable_autoend(i2c);
	/*start transfer*/
	I2C_CR2(i2c) |= I2C_CR2_START; //i2c_send_start(i2c);

	wait = true;
	while (wait) {
		if (i2c_transmit_int_status(i2c)) {
			wait = false;
		}
		while (i2c_nack(i2c)); /* Some error */
	}
	I2C_TXDR(i2c) = reg; //i2c_send_data(i2c, data[i]);

	while ((I2C_CR2(i2c) & I2C_CR2_START)); //(i2c_is_start(i2c) == 1);
	/*Setting transfer properties*/
	i2c_set_bytes_to_transfer(i2c, size);
	i2c_set_7bit_address(i2c, i2c_addr);
	I2C_CR2(i2c) |= I2C_CR2_RD_WRN; //i2c_set_write_transfer_dir(i2c);
	I2C_CR2(i2c) &= ~I2C_CR2_AUTOEND; //i2c_enable_autoend(i2c);
	/*start transfer*/
	I2C_CR2(i2c) |= I2C_CR2_START; //i2c_send_start(i2c);

	for (i = 0; i < size; i++) {
		while (i2c_received_data(i2c) == 0);
		data[i] = i2c_get_data(i2c);
	}
 }



