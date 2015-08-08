/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2010 Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2014 Kuldeep Singh Dhaka <kuldeepdhaka9@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "usb.h"

/* Buffer to be used for control requests. */
uint8_t usbd_control_buffer[128];

static int cdcacm_control_request(usbd_device *usbd_dev, 
        struct usb_setup_data *req, uint8_t **buf,
        uint16_t *len, 
        void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req))
{
    (void)complete;
    (void)buf;
    (void)usbd_dev;

    switch(req->bRequest) {
    case USB_CDC_REQ_SET_CONTROL_LINE_STATE: {
        /*
         * This Linux cdc_acm driver requires this to be implemented
         * even though it's optional in the CDC spec, and we don't
         * advertise it in the ACM functional descriptor.
         */
        char local_buf[10];
        struct usb_cdc_notification *notif = (void *)local_buf;

        /* We echo signals back to host as notification. */
        notif->bmRequestType = 0xA1;
        notif->bNotification = USB_CDC_NOTIFY_SERIAL_STATE;
        notif->wValue = 0;
        notif->wIndex = 0;
        notif->wLength = 2;
        local_buf[8] = req->wValue & 3;
        local_buf[9] = 0;
        // usbd_ep_write_packet(0x83, buf, 10);
        return 1;
        }
    case USB_CDC_REQ_SET_LINE_CODING: 
        if(*len < sizeof(struct usb_cdc_line_coding))
            return 0;

        return 1;
    }
    return 0;
}

static void cdcacm_data_rx_cb(usbd_device *usbd_dev, uint8_t ep)
{
    (void)ep;

    char buf[64];
    int len = usbd_ep_read_packet(usbd_dev, 0x01, buf, 64);

    /*
    if (len) {
        usbd_ep_write_packet(usbd_dev, 0x82, buf, len);
        buf[len] = 0;
    }
    */
    usbd_ep_write_packet(usbd_dev, 0x82, "hello\r\n", 8);
}

void cdcacm_set_config(usbd_device *usbd_dev, uint16_t wValue)
{
    (void)wValue;

    usbd_ep_setup(usbd_dev, 0x01, USB_ENDPOINT_ATTR_BULK, 64, cdcacm_data_rx_cb);
    usbd_ep_setup(usbd_dev, 0x82, USB_ENDPOINT_ATTR_BULK, 64, NULL);
    usbd_ep_setup(usbd_dev, 0x83, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);

    usbd_register_control_callback(
                usbd_dev,
                USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
                USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
                cdcacm_control_request);
}

void rcc_clock_setup_in_hsi48_out_48mhz_corrected(void)
{
    rcc_osc_on(HSI);
    rcc_wait_for_osc_ready(HSI);
    rcc_set_sysclk_source(HSI);

    // correction (f072 has PREDIV after clock multiplexer (near PLL)
    //Figure 12. Clock tree (STM32F07x devices)  P96    RM0091
    //applies to rcc_clock_setup_in_hsi_out_*mhz()
    rcc_set_prediv(RCC_CFGR2_PREDIV_DIV2);

    rcc_set_hpre(RCC_CFGR_HPRE_NODIV);
    rcc_set_ppre(RCC_CFGR_PPRE_NODIV);

    flash_set_ws(FLASH_ACR_LATENCY_024_048MHZ);

    // 8MHz * 12 / 2 = 48MHz
    rcc_set_pll_multiplication_factor(RCC_CFGR_PLLMUL_MUL12);

    RCC_CFGR &= ~RCC_CFGR_PLLSRC;

    rcc_osc_on(PLL);
    rcc_wait_for_osc_ready(PLL);
    rcc_set_sysclk_source(PLL);
}

/*
enum rcc_osc rcc_usb_clock_source(void)
{
    return (RCC_CFGR3 & RCC_CFGR3_USBSW) ? PLL : HSI48;
}

void rcc_set_usbclk_source(enum rcc_osc clk)
{
    switch (clk) {
    case PLL:
        RCC_CFGR3 |= RCC_CFGR3_USBSW;
    case HSI48:
        RCC_CFGR3 &= ~RCC_CFGR3_USBSW;
    case HSI:
    case HSE:
    case LSI:
    case LSE:
    case HSI14:
        break;
    }
}
*/

void crs_configure_usbsof_autotrim(void)
{
    rcc_periph_clock_enable(RCC_CRS);
    
    CRS_CFGR &= ~CRS_CFGR_SYNCSRC;
    CRS_CFGR |= CRS_CFGR_SYNCSRC_USB_SOF;
    
    CRS_CR |= CRS_CR_AUTOTRIMEN;
    CRS_CR |= CRS_CR_CEN;
}

/*void rcc_clock_setup_in_hsi48_out_48mhz(void)
{
    rcc_osc_on(HSI48);
    rcc_wait_for_osc_ready(HSI48);
    
    rcc_set_hpre(RCC_CFGR_HPRE_NODIV);
    rcc_set_ppre(RCC_CFGR_PPRE_NODIV);
    
    flash_set_ws(FLASH_ACR_LATENCY_024_048MHZ);
}

int main(void)
{
    usbd_device *usbd_dev;

    rcc_clock_setup_in_hsi48_out_48mhz();
    crs_configure_usbsof_autotrim();
    rcc_set_usbclk_source(HSI48);
    rcc_set_sysclk_source(HSI48);

    usbd_dev = usbd_init(&stm32f0x2_usb_driver, &dev, &config, usb_strings, 
            3, usbd_control_buffer, sizeof(usbd_control_buffer));
    usbd_register_set_config_callback(usbd_dev, cdcacm_set_config);

    while (1)
        usbd_poll(usbd_dev);
}*/
