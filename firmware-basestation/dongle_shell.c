/**
 * This file is part of the ukhasnet dongle project. 
 *
 * @file usbserial.c
 * @author Jon Sowman <jon+github@jonsowman.com>
 * @copyright Jon Sowman 2015
 *
 * @addtogroup usbserial
 * @{
 */

#include <stdio.h>
#include <string.h>

#include "ch.h"
#include "hal.h"
#include "hal_channels.h"

#include "shell.h"
#include "chprintf.h"

#include "dongle_shell.h"
#include "esp.h"

/*===========================================================================*/
/* USB related stuff.                                                        */
/*===========================================================================*/

/*
 * Endpoints to be used for USBD1.
 */
#define USBD1_DATA_REQUEST_EP           1
#define USBD1_DATA_AVAILABLE_EP         1
#define USBD1_INTERRUPT_REQUEST_EP      2

/*
 * Serial over USB Driver structure.
 */
static SerialUSBDriver SDU1;

/**
 * Print level
 */
static uint8_t _level;

/*
 * USB Device Descriptor.
 */
static const uint8_t vcom_device_descriptor_data[18] = {
    USB_DESC_DEVICE       (0x0110,        /* bcdUSB (1.1).                    */
            0x02,          /* bDeviceClass (CDC).              */
            0x00,          /* bDeviceSubClass.                 */
            0x00,          /* bDeviceProtocol.                 */
            0x40,          /* bMaxPacketSize.                  */
            0x0483,        /* idVendor (ST).                   */
            0x5740,        /* idProduct.                       */
            0x0200,        /* bcdDevice.                       */
            1,             /* iManufacturer.                   */
            2,             /* iProduct.                        */
            3,             /* iSerialNumber.                   */
            1)             /* bNumConfigurations.              */
};

/*
 * Device Descriptor wrapper.
 */
static const USBDescriptor vcom_device_descriptor = {
    sizeof vcom_device_descriptor_data,
    vcom_device_descriptor_data
};

/* Configuration Descriptor tree for a CDC.*/
static const uint8_t vcom_configuration_descriptor_data[67] = {
    /* Configuration Descriptor.*/
    USB_DESC_CONFIGURATION(67,            /* wTotalLength.                    */
            0x02,          /* bNumInterfaces.                  */
            0x01,          /* bConfigurationValue.             */
            0,             /* iConfiguration.                  */
            0xC0,          /* bmAttributes (self powered).     */
            50),           /* bMaxPower (100mA).               */
    /* Interface Descriptor.*/
    USB_DESC_INTERFACE    (0x00,          /* bInterfaceNumber.                */
            0x00,          /* bAlternateSetting.               */
            0x01,          /* bNumEndpoints.                   */
            0x02,          /* bInterfaceClass (Communications
                              Interface Class, CDC section
                              4.2).                            */
            0x02,          /* bInterfaceSubClass (Abstract
                              Control Model, CDC section 4.3).   */
            0x01,          /* bInterfaceProtocol (AT commands,
                              CDC section 4.4).                */
            0),            /* iInterface.                      */
    /* Header Functional Descriptor (CDC section 5.2.3).*/
    USB_DESC_BYTE         (5),            /* bLength.                         */
    USB_DESC_BYTE         (0x24),         /* bDescriptorType (CS_INTERFACE).  */
    USB_DESC_BYTE         (0x00),         /* bDescriptorSubtype (Header
                                             Functional Descriptor.           */
    USB_DESC_BCD          (0x0110),       /* bcdCDC.                          */
    /* Call Management Functional Descriptor. */
    USB_DESC_BYTE         (5),            /* bFunctionLength.                 */
    USB_DESC_BYTE         (0x24),         /* bDescriptorType (CS_INTERFACE).  */
    USB_DESC_BYTE         (0x01),         /* bDescriptorSubtype (Call Management
                                             Functional Descriptor).          */
    USB_DESC_BYTE         (0x00),         /* bmCapabilities (D0+D1).          */
    USB_DESC_BYTE         (0x01),         /* bDataInterface.                  */
    /* ACM Functional Descriptor.*/
    USB_DESC_BYTE         (4),            /* bFunctionLength.                 */
    USB_DESC_BYTE         (0x24),         /* bDescriptorType (CS_INTERFACE).  */
    USB_DESC_BYTE         (0x02),         /* bDescriptorSubtype (Abstract
                                             Control Management Descriptor).  */
    USB_DESC_BYTE         (0x02),         /* bmCapabilities.                  */
    /* Union Functional Descriptor.*/
    USB_DESC_BYTE         (5),            /* bFunctionLength.                 */
    USB_DESC_BYTE         (0x24),         /* bDescriptorType (CS_INTERFACE).  */
    USB_DESC_BYTE         (0x06),         /* bDescriptorSubtype (Union
                                             Functional Descriptor).          */
    USB_DESC_BYTE         (0x00),         /* bMasterInterface (Communication
                                             Class Interface).                */
    USB_DESC_BYTE         (0x01),         /* bSlaveInterface0 (Data Class
                                             Interface).                      */
    /* Endpoint 2 Descriptor.*/
    USB_DESC_ENDPOINT     (USBD1_INTERRUPT_REQUEST_EP|0x80,
            0x03,          /* bmAttributes (Interrupt).        */
            0x0008,        /* wMaxPacketSize.                  */
            0xFF),         /* bInterval.                       */
    /* Interface Descriptor.*/
    USB_DESC_INTERFACE    (0x01,          /* bInterfaceNumber.                */
            0x00,          /* bAlternateSetting.               */
            0x02,          /* bNumEndpoints.                   */
            0x0A,          /* bInterfaceClass (Data Class
                              Interface, CDC section 4.5).     */
            0x00,          /* bInterfaceSubClass (CDC section
                              4.6).                            */
            0x00,          /* bInterfaceProtocol (CDC section
                              4.7).                            */
            0x00),         /* iInterface.                      */
    /* Endpoint 3 Descriptor.*/
    USB_DESC_ENDPOINT     (USBD1_DATA_AVAILABLE_EP,       /* bEndpointAddress.*/
            0x02,          /* bmAttributes (Bulk).             */
            0x0040,        /* wMaxPacketSize.                  */
            0x00),         /* bInterval.                       */
    /* Endpoint 1 Descriptor.*/
    USB_DESC_ENDPOINT     (USBD1_DATA_REQUEST_EP|0x80,    /* bEndpointAddress.*/
            0x02,          /* bmAttributes (Bulk).             */
            0x0040,        /* wMaxPacketSize.                  */
            0x00)          /* bInterval.                       */
};

/*
 * Configuration Descriptor wrapper.
 */
static const USBDescriptor vcom_configuration_descriptor = {
    sizeof vcom_configuration_descriptor_data,
    vcom_configuration_descriptor_data
};

/*
 * U.S. English language identifier.
 */
static const uint8_t vcom_string0[] = {
    USB_DESC_BYTE(4),                     /* bLength.                         */
    USB_DESC_BYTE(USB_DESCRIPTOR_STRING), /* bDescriptorType.                 */
    USB_DESC_WORD(0x0409)                 /* wLANGID (U.S. English).          */
};

/*
 * Vendor string.
 */
static const uint8_t vcom_string1[] = {
    USB_DESC_BYTE(38),                    /* bLength.                         */
    USB_DESC_BYTE(USB_DESCRIPTOR_STRING), /* bDescriptorType.                 */
    'S', 0, 'T', 0, 'M', 0, 'i', 0, 'c', 0, 'r', 0, 'o', 0, 'e', 0,
    'l', 0, 'e', 0, 'c', 0, 't', 0, 'r', 0, 'o', 0, 'n', 0, 'i', 0,
    'c', 0, 's', 0
};

/*
 * Device Description string.
 */
static const uint8_t vcom_string2[] = {
    USB_DESC_BYTE(56),                    /* bLength.                         */
    USB_DESC_BYTE(USB_DESCRIPTOR_STRING), /* bDescriptorType.                 */
    'C', 0, 'h', 0, 'i', 0, 'b', 0, 'i', 0, 'O', 0, 'S', 0, '/', 0,
    'R', 0, 'T', 0, ' ', 0, 'V', 0, 'i', 0, 'r', 0, 't', 0, 'u', 0,
    'a', 0, 'l', 0, ' ', 0, 'C', 0, 'O', 0, 'M', 0, ' ', 0, 'P', 0,
    'o', 0, 'r', 0, 't', 0
};

/*
 * Serial Number string.
 */
static const uint8_t vcom_string3[] = {
    USB_DESC_BYTE(8),                     /* bLength.                         */
    USB_DESC_BYTE(USB_DESCRIPTOR_STRING), /* bDescriptorType.                 */
    '0' + CH_KERNEL_MAJOR, 0,
    '0' + CH_KERNEL_MINOR, 0,
    '0' + CH_KERNEL_PATCH, 0
};

/*
 * Strings wrappers array.
 */
static const USBDescriptor vcom_strings[] = {
    {sizeof vcom_string0, vcom_string0},
    {sizeof vcom_string1, vcom_string1},
    {sizeof vcom_string2, vcom_string2},
    {sizeof vcom_string3, vcom_string3}
};

/*
 * Handles the GET_DESCRIPTOR callback. All required descriptors must be
 * handled here.
 */
static const USBDescriptor *get_descriptor(USBDriver *usbp,
        uint8_t dtype,
        uint8_t dindex,
        uint16_t lang) {

    (void)usbp;
    (void)lang;
    switch (dtype) {
        case USB_DESCRIPTOR_DEVICE:
            return &vcom_device_descriptor;
        case USB_DESCRIPTOR_CONFIGURATION:
            return &vcom_configuration_descriptor;
        case USB_DESCRIPTOR_STRING:
            if (dindex < 4)
                return &vcom_strings[dindex];
    }
    return NULL;
}

/**
 * @brief   IN EP1 state.
 */
static USBInEndpointState ep1instate;

/**
 * @brief   OUT EP1 state.
 */
static USBOutEndpointState ep1outstate;

/**
 * @brief   EP1 initialization structure (both IN and OUT).
 */
static const USBEndpointConfig ep1config = {
    USB_EP_MODE_TYPE_BULK,
    NULL,
    sduDataTransmitted,
    sduDataReceived,
    0x0040,
    0x0040,
    &ep1instate,
    &ep1outstate,
    1,
    NULL
};

/**
 * @brief   IN EP2 state.
 */
static USBInEndpointState ep2instate;

/**
 * @brief   EP2 initialization structure (IN only).
 */
static const USBEndpointConfig ep2config = {
    USB_EP_MODE_TYPE_INTR,
    NULL,
    sduInterruptTransmitted,
    NULL,
    0x0010,
    0x0000,
    &ep2instate,
    NULL,
    1,
    NULL
};

/*
 * Handles the USB driver global events.
 */
static void usb_event(USBDriver *usbp, usbevent_t event) {

    switch (event) {
        case USB_EVENT_RESET:
            return;
        case USB_EVENT_ADDRESS:
            return;
        case USB_EVENT_CONFIGURED:
            chSysLockFromISR();

            /* Enables the endpoints specified into the configuration.
               Note, this callback is invoked from an ISR so I-Class functions
               must be used.*/
            usbInitEndpointI(usbp, USBD1_DATA_REQUEST_EP, &ep1config);
            usbInitEndpointI(usbp, USBD1_INTERRUPT_REQUEST_EP, &ep2config);

            /* Resetting the state of the CDC subsystem.*/
            sduConfigureHookI(&SDU1);

            chSysUnlockFromISR();
            return;
        case USB_EVENT_SUSPEND:
            return;
        case USB_EVENT_WAKEUP:
            return;
        case USB_EVENT_STALLED:
            return;
    }
    return;
}

/*
 * USB driver configuration.
 */
static const USBConfig usbcfg = {
    usb_event,
    get_descriptor,
    sduRequestsHook,
    NULL
};

/*
 * Serial over USB driver configuration.
 */
static SerialUSBConfig serusbcfg = {
    &USBD1,
    USBD1_DATA_REQUEST_EP,
    USBD1_DATA_AVAILABLE_EP,
    USBD1_INTERRUPT_REQUEST_EP
};

/**
 * Set print level
 */
static void shell_set_level(uint8_t newlevel)
{
    _level = newlevel;
}

/**
 * Get print level
 */
uint8_t shell_get_level(void)
{
    return _level;
}

/*===========================================================================*/
/* Command line related.                                                     */
/*===========================================================================*/

#define SHELL_WA_SIZE   THD_WORKING_AREA_SIZE(1024)

static void cmd_mem(BaseSequentialStream *chp, int argc, char *argv[]) {
    size_t n, size;

    (void)argv;
    if (argc > 0) {
        chprintf(chp, "Usage: mem\r\n");
        return;
    }
    n = chHeapStatus(NULL, &size);
    chprintf(chp, "core free memory : %u bytes\r\n", chCoreGetStatusX());
    chprintf(chp, "heap fragments   : %u\r\n", n);
    chprintf(chp, "heap free total  : %u bytes\r\n", size);
}

static void cmd_threads(BaseSequentialStream *chp, int argc, char *argv[]) {
  static const char *states[] = {CH_STATE_NAMES};
  uint64_t busy = 0, total = 0;
  thread_t *tp;

  (void)argv;
  if (argc > 0) {
    chprintf(chp, "Usage: threads\r\n");
    return;
  }

  chprintf(chp,
    "name        |addr    |stack   |free|prio|refs|state    |time\r\n");
  chprintf(chp,
    "------------|--------|--------|----|----|----|---------|--------\r\n");
  tp = chRegFirstThread();
  do {
    chprintf(chp, "%12s|%.8lx|%.8lx|%4lu|%4lu|%4lu|%9s|%lu\r\n",
            chRegGetThreadNameX(tp),
            (uint32_t)tp, (uint32_t)tp->p_ctx.r13,
            (uint32_t)tp->p_ctx.r13 - (uint32_t)tp->p_stklimit,
            (uint32_t)tp->p_prio, (uint32_t)(tp->p_refs - 1),
            states[tp->p_state], (uint32_t)tp->p_time);
    if(tp->p_prio != 1) {
        busy += tp->p_time;
    }
    total += tp->p_time;
    tp = chRegNextThread(tp);
  } while (tp != NULL);
  chprintf(chp, "CPU Usage: %ld%%\r\n", busy*100/total);
}

/**
 * Passthrough interface for the ESP device
 */
static void cmd_esp(BaseSequentialStream *chp, int argc, char *argv[]) {
    if(argc < 1)
    {
        chprintf(chp, "Usage: esp [pt norm|boot] [ver] [reset] [status] [ip] [join <ssid> <pass>] [setname <name>]\r\n");
        return;
    }

    if(strcmp(argv[0], "pt") == 0)
    {
        // Kill the driver so the ESP thread is no longer using it
        sdStop(&SD1);

        static SerialConfig sc = {
                115200, 0, USART_CR2_STOP1_BITS | USART_CR2_LINEN, 0};

        if(strcmp(argv[1], "boot") == 0)
        {
            // Use bootloader mode
            // Put the ESP in RESET
            palClearPad(GPIOF, GPIOF_ESP_RST);
            palClearPad(GPIOF, GPIOF_ESP_CHPD);
            sc.speed = 115200;
            // Hold GPIO0 low to enable bootloader mode
            palClearPad(GPIOA, GPIOA_ESP_GPIO0);
            chprintf(chp, "ESP entering bootloader...");
        }
        else if(strcmp(argv[1], "norm") == 0)
        {
            // Normal mode, no need to reset the ESP
            palClearPad(GPIOF, GPIOF_ESP_RST);
            palClearPad(GPIOF, GPIOF_ESP_CHPD);
            sc.speed = 9600;
            palSetPad(GPIOA, GPIOA_ESP_GPIO0);
            chprintf(chp, "ESP entering normal mode...");
        }
        else
        {
            chprintf(chp, "Command not recognised\r\n");
            return;
        }
        
        // Wait for configuration to hold at ESP
        chThdSleepMilliseconds(100);

        // Configure the UART to talk at whichever baud we chose
        sdStart(&SD1, &sc);
        event_listener_t elSerialData;
        eventflags_t flags;
        chEvtRegisterMask(chnGetEventSource(&SD1), &elSerialData, EVENT_MASK(1));

        /*
         * Bring up ESP by pulling RST high
         */
        palSetPad(GPIOF, GPIOF_ESP_CHPD);
        palSetPad(GPIOF, GPIOF_ESP_RST);
        
        // Let ESP come up
        chThdSleepMilliseconds(500);

        chprintf(chp, "done!\r\n");

        while(TRUE)
        {
            // Wait for data ESP -> PC
            chEvtWaitOneTimeout(EVENT_MASK(1), MS2ST(1));
            flags = chEvtGetAndClearFlags(&elSerialData);
            if( flags & CHN_INPUT_AVAILABLE )
            {
                msg_t charbuf;
                do
                {
                    charbuf = chnGetTimeout(&SD1, TIME_IMMEDIATE);
                    if( charbuf != STM_TIMEOUT )
                        chSequentialStreamPut(chp, charbuf);
                }
                while( charbuf != STM_TIMEOUT );
            }
            // Wait for data PC -> ESP
            msg_t usbbuf;
            do {
                usbbuf = chnGetTimeout(&SDU1, TIME_IMMEDIATE);
                if( usbbuf != STM_TIMEOUT )
                {
                    // There is some data, forward it to the ESP
                    chSequentialStreamPut(&SD1, usbbuf);
                }
            }
            while( usbbuf != STM_TIMEOUT );
        }
    } /* argv[0] is pt */
    else if(strcmp(argv[0], "ver") == 0)
    {
        /* Send request for ESP to print its version */
        esp_request(ESP_MSG_VERSION, NULL);
    } /* argv[0] is ver */
    else if(strcmp(argv[0], "reset") == 0)
    {
        /* Ask to reset ESP */
        esp_request(ESP_MSG_RST, NULL);
    } /* argv[0] is reset */
    else if(strcmp(argv[0], "ip") == 0)
    {
        esp_request(ESP_MSG_IP, NULL);
    } /* argv[0] is ip */
    else if(strcmp(argv[0], "join") == 0)
    {
        if(argc != 3)
        {
            chprintf(chp, "Usage: esp join <ssid> <pass>\r\n");
            return;
        }
        // TODO: Check length of args !>64 bytes
        char tbuf[64];
        char *tbuf_ptr = tbuf;
        *tbuf_ptr++ = '"';
        strcpy(tbuf_ptr, argv[1]);
        tbuf_ptr += strlen(tbuf_ptr);
        *tbuf_ptr++ = '"';
        *tbuf_ptr++ = ',';
        *tbuf_ptr++ = '"';
        strcpy(tbuf_ptr, argv[2]);
        tbuf_ptr += strlen(tbuf_ptr);
        *tbuf_ptr++ = '"';
        *tbuf_ptr++ = '\0';
        esp_request(ESP_MSG_JOIN, tbuf);
    } /* argv[0] is join */
    else if(strcmp(argv[0], "status") == 0)
    {
        chprintf(chp, "Status: %d\r\n", esp_get_status());
    } /* argv[0] is status */
    else if(strcmp(argv[0], "testsend") == 0)
    {
        esp_request(ESP_MSG_START, "2aT19.0[JJJ]");
    }
    else if(strcmp(argv[0], "eoff") == 0)
    {
        esp_request(ESP_MSG_ECHOOFF, NULL);
    }
    else if(strcmp(argv[0], "setname") == 0)
    {
        esp_set_origin(argv[1]);
        chprintf(chp, "New origin: %s\r\n", argv[1]);
    } /* argv[0] is setname */
    else
    {
        chprintf(chp, "Command not recognised\r\n");
    }
}

static void cmd_show(BaseSequentialStream *chp, int argc, char *argv[]) {
    if(argc < 1)
    {
        chprintf(chp, "Usage: show [none|packet|debug]\r\n");
        return;
    }
    if(strcmp(argv[0], "none") == 0)
        shell_set_level(LEVEL_NONE);
    else if(strcmp(argv[0], "packet") == 0)
        shell_set_level(LEVEL_PACKET);
    else if(strcmp(argv[0], "debug") == 0)
        shell_set_level(LEVEL_DEBUG);
}

static const ShellCommand commands[] = {
    {"mem", cmd_mem},
    {"threads", cmd_threads},
    {"esp", cmd_esp},
    {"show", cmd_show},
    {NULL, NULL}
};

static const ShellConfig shell_cfg1 = {
    (BaseSequentialStream *)&SDU1,
    commands
};

/**
 * Get a pointer to the USB configuration held by this module. One can query
 * this struct to find out whether the USB is active or not, etc.
 * @returns A pointer to the relevant SerialUSBConfig.
 */
SerialUSBConfig* usb_get_config(void)
{
    return &serusbcfg;
}

/**
 * Get a pointer to the SerialUSBDriver in use for the USB serial connection.
 * This allows other threads to use chprintf etc, such as
 * chprintf((BaseSequentialStream *)SDU1, "hello world\r\n");
 * @returns A pointer to a SerialUSBDriver.
 */
SerialUSBDriver* usb_get_sdu(void)
{
    return &SDU1;
}

/**
 * Main thread for the USB Serial module. Start the USB serial driver and spawn
 * a shell using it. The shell is killed if the USB connection becomes
 * inactive, and is spawned again when it becomes active.
 */
THD_FUNCTION(UsbSerThread, arg)
{
    (void)arg;
    thread_t *shelltp = NULL;

    /*
     * Level initially none
     */
    _level = LEVEL_NONE;

    /*
     * Initializes a serial-over-USB CDC driver.
     */
    sduObjectInit(&SDU1);
    sduStart(&SDU1, &serusbcfg);

    /*
     * Activates the USB driver and then the USB bus pull-up on D+.
     * Note, a delay is inserted in order to not have to disconnect the cable
     * after a reset.
     */
    usbDisconnectBus(serusbcfg.usbp);
    chThdSleepMilliseconds(1000);
    usbStart(serusbcfg.usbp, &usbcfg);
    usbConnectBus(serusbcfg.usbp);

    /*
     * Shell manager initialization.
     */
    shellInit();
    while (true) {
        if (!shelltp && (SDU1.config->usbp->state == USB_ACTIVE))
            shelltp = shellCreate(&shell_cfg1, SHELL_WA_SIZE, NORMALPRIO);
        else if (chThdTerminatedX(shelltp)) {
            chThdRelease(shelltp);    /* Recovers memory of the previous shell.   */
            shelltp = NULL;           /* Triggers spawning of a new shell.        */
        }
        chThdSleepMilliseconds(1000);
    }
}

/**
 * @}
 */
