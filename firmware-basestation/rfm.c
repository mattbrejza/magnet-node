/**
 * This file is part of the ukhasnet dongle project. 
 *
 * @file rfm.c
 * @author Jon Sowman <jon+github@jonsowman.com>
 * @copyright Jon Sowman 2015
 *
 * @addtogroup rfm
 * @{
 */

#include <string.h>

#include "ch.h"
#include "hal.h"

#include "esp.h"
#include "rfm.h"

THD_FUNCTION(RfmThread, arg)
{
    (void)arg;
}

/**
 * @}
 */
