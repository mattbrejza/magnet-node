/**
 * This file is part of the ukhasnet dongle project. 
 *
 * @file usbserial.h
 * @author Jon Sowman <jon+github@jonsowman.com>
 * @copyright Jon Sowman 2015
 *
 * @addtogroup usbserial
 * @{
 */

#ifndef __USBSERIAL_H__
#define __USBSERIAL_H__

#include "ch.h"
#include "hal.h"

SerialUSBConfig* usb_get_config(void);
SerialUSBDriver* usb_get_sdu(void);
THD_FUNCTION(UsbSerThread, arg);

#endif /* __USBSERIAL_H__ */

/**
 * @}
 */
