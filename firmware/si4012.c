/*
 * si4012.c
 *
 *  Created on: 10 Apr 2015
 *      Author: Matt
 */
#include <msp430.h>
#include <inttypes.h>
#include "i2c.h"
#include "si4012.h"

static uint8_t _shift;
static uint16_t _bitrate;
static uint32_t _frequency;

static uint8_t init_fifo(void)
{
	uint8_t status;

	i2c_start();
	i2c_write8(SI4012_ADDR);
	i2c_write8(0x65);  //command
	i2c_stop();

	i2c_start();
	i2c_write8(SI4012_ADDR | 1);
	status = i2c_read8(0xFF);  //status
	i2c_stop();

	return status;
}

static uint8_t set_fifo(uint8_t *buff, uint8_t len)
{
	uint8_t status;
	uint16_t i;

	i2c_start();
	i2c_write8(SI4012_ADDR);
	i2c_write8(0x66);  //command
	for (i = 0; i < len; i++)
		i2c_write8(buff[i]);
	i2c_stop();

	i2c_start();
	i2c_write8(SI4012_ADDR | 1);
	status = i2c_read8(0xFF);  //status
	i2c_stop();

	return status;
}

static uint8_t set_property(uint8_t prop_id, uint8_t *buff, uint8_t len)
{
	uint8_t status,i;

	i2c_start();
	i2c_write8(SI4012_ADDR);
	i2c_write8(0x11);  //command
	i2c_write8(prop_id);
	for (i = 0; (i < len) && (i < 6); i++)
		i2c_write8(buff[i]);
	i2c_stop();

	i2c_start();
	i2c_write8(SI4012_ADDR | 1);
	status = i2c_read8(0xFF);  //status
	i2c_stop();

	return status;
}

static uint8_t get_property(uint8_t prop_id, uint8_t *buff_out, uint8_t len)
{
	uint8_t status,i;

	i2c_start();
	i2c_write8(SI4012_ADDR);
	i2c_write8(0x12);  //command
	i2c_write8(prop_id);

	i2c_stop();

	i2c_start();
	i2c_write8(SI4012_ADDR | 1);
	status = i2c_read8(0x00);  //status
	for (i = 0; (i < (len-1)) && (i < (6-1)); i++)
		buff_out[i] = i2c_read8(0x00);
	buff_out[i++] = i2c_read8(0xFF);
	i2c_stop();

	return status;
}

static uint8_t tx_start(uint8_t *buff, uint8_t len) //, uint8_t *ActualDataSize)
{
	uint8_t status,i;

	i2c_start();
	i2c_write8(SI4012_ADDR);
	i2c_write8(0x62);  //command
	for (i = 0; (i < len) && (i < 5); i++)
		i2c_write8(buff[i]);
	i2c_stop();

	i2c_start();
	i2c_write8(SI4012_ADDR | 1);
	status = i2c_read8(0x00);  //status
	i2c_read8(0xFF); //*ActualDataSize = i2c_read8(0xFF);
	i2c_stop();

	return status;
}

static uint8_t get_state(uint8_t *buff_out)
{
	uint8_t status,i;

	i2c_start();
	i2c_write8(SI4012_ADDR);
	i2c_write8(0x61);  //command

	i2c_stop();

	i2c_start();
	i2c_write8(SI4012_ADDR | 1);
	status = i2c_read8(0x00);  //status
	for (i = 0; i < (5-1); i++)
		buff_out[i] = i2c_read8(0x00);
	buff_out[i++] = i2c_read8(0xFF);
	i2c_stop();

	return status;
}



void si4012_init(uint8_t shift, uint16_t bitrate, uint32_t frequency)
{

	P1DIR |= 0x01;		//shutdown pin as output
	P1OUT |= 0x01;      //enable shutdown

	i2c_init();

	_shift = shift;
	_bitrate = bitrate;
	_frequency = frequency;



}

void si4012_sleep(void)
{
	P1OUT |= 0x01;      //enable shutdown
}

void si4012_set_fsk_params(uint8_t shift, uint16_t bitrate, uint32_t frequency)
{
	uint8_t buff[6];

	buff[0] = 0x08 | 0; // external XO, MSB first, positive dev.
	set_property(0x10, &buff[0], 1);

	buff[0] = 1; //set fsk
	buff[1] = shift;// & 0x7F;
	set_property(0x20, &buff[0], 2);

	buff[0] = (bitrate>>8) & 0x3;
	buff[1] = bitrate & 0xff;
	buff[2] = 2; //ramp rate
	set_property(0x31, &buff[0], 3);

	buff[0] = (frequency>>24) & 0xFF;
	buff[1] = (frequency>>16) & 0xFF;
	buff[2] = (frequency>> 8) & 0xFF;
	buff[3] = frequency & 0xFF;
	set_property(0x40, &buff[0], 4);


	const uint32_t fc = 10000000;  //xo freqency
	buff[0] = (fc>>24) & 0xFF;
	buff[1] = (fc>>16) & 0xFF;
	buff[2] = (fc>> 8) & 0xFF;
	buff[3] = fc & 0xFF;
	buff[4] = 0x00;
	set_property(0x50, &buff[0], 5);

	buff[0] = 1;  //max drive
	buff[1] = 74; //power output
	buff[2] = 0;  //PA cap MSB
	buff[3] = 13; //PA cap LSB
	buff[4] = 0;  //turn off temp comp
	buff[5] = 127;//default
	set_property(0x60, &buff[0], 6);


}

uint8_t get_int_status(uint8_t *int_status)
{
	uint8_t status;

	i2c_start();
	i2c_write8(SI4012_ADDR);
	i2c_write8(0x64);  //command
	i2c_stop();

	i2c_start();
	i2c_write8(SI4012_ADDR | 1);
	status = i2c_read8(0x00);  //status
	*int_status = i2c_read8(0xFF);  //status
	i2c_stop();

	return status;
}

void si4012_transmit_short(uint8_t *buff, uint8_t len)
{
//	uint8_t r;

	init_fifo();
//	if (r != 0x80)
//		return r;
//
	si4012_set_fsk_params(_shift, _bitrate, _frequency);  //needs to be repeated after every sleep?

	set_fifo(buff, len);
//	if (r != 0x80)
//		return r;

	uint8_t txbuff[5];

	get_int_status(&buff[0]); //clear interrupts

	txbuff[0] = 0;   //packet size MSB
	txbuff[1] = len;
	txbuff[2] = 0;  //go into idle when done, immediate start
	txbuff[3] = 0;
	txbuff[4] = 0;

	tx_start(txbuff,5);
//	if (r != 0x80)
//		return r;

	txbuff[0] = 0;
	volatile unsigned int i;
	while((txbuff[0] & 0x08) == 0){
		//for (i = 0; i < 5000; i++);
		__bis_SR_register(LPM0_bits);
		get_int_status(&txbuff[0]);
	}

//	P1OUT |= 0x01;      //enable shutdown

//	return r;


}

