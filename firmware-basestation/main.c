/**
 * This file is part of the ukhasnet dongle project. 
 *
 * @file main.c
 * @author Jon Sowman <jon+github@jonsowman.com>
 * @copyright Jon Sowman 2015
 *
 * @addtogroup main
 * @{
 */

#include <stdio.h>
#include <string.h>

#include "ch.h"
#include "hal.h"
#include "hal_channels.h"

#include "shell.h"
#include "chprintf.h"

#include "usbserial.h"

static THD_WORKING_AREA(waBlinker, 128);
static THD_WORKING_AREA(waUsbSer, 128);

/*===========================================================================*/
/* Generic code.                                                             */
/*===========================================================================*/

/*
 * Red LED blinker thread, times are in milliseconds.
 */
static THD_FUNCTION(BlinkerThread, arg) {

    (void)arg;
    chRegSetThreadName("blinker");

    // Get a pointer to the usb configuration
    SerialUSBConfig* serusbcfg = get_usb_config();

    while (true) {
        systime_t time = serusbcfg->usbp->state == USB_ACTIVE ? 250 : 500;
        /*systime_t time = 250;*/
        palClearPad(GPIOC, GPIOC_LED_AUX);
        chThdSleepMilliseconds(time);
        palSetPad(GPIOC, GPIOC_LED_AUX);
        chThdSleepMilliseconds(time);
    }
}

/*
 * Application entry point.
 */
int main(void) {
    /*
     * System initializations.
     * - HAL initialization, this also initializes the configured device drivers
     *   and performs the board-specific initializations.
     * - Kernel initialization, the main() function becomes a thread and the
     *   RTOS is active.
     */
    halInit();
    chSysInit();

    /*
     * Create USB Serial
     */
    chThdCreateStatic(waUsbSer, sizeof(waUsbSer), NORMALPRIO, UsbSerThread, NULL);

    /*
     * Creates the blinker thread.
     */
    chThdCreateStatic(waBlinker, sizeof(waBlinker), NORMALPRIO, BlinkerThread, NULL);

    /*
     * Normal main() thread activity, in this demo it does nothing except
     * sleeping in a loop and check the button state.
     */
    while (true) {
        chThdSleepMilliseconds(1000);
    }
}

/**
 * @}
 */
