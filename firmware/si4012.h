/*
 * si4012.h
 *
 *  Created on: 10 Apr 2015
 *      Author: Matt
 */

#ifndef SI4012_H_
#define SI4012_H_

#define SI4012_ADDR 0xE0

void si4012_init(uint8_t shift, uint16_t bitrate, uint32_t frequency);
void si4012_sleep(void);
void si4012_transmit_short(uint8_t *buff, uint8_t len);



#endif /* SI4012_H_ */
