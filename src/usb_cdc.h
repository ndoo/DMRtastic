// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#ifndef USB_CDC_H
#define USB_CDC_H

int usb_cdc_init(void);
void usb_cdc_wait_dtr(void);
void usb_cdc_flush_and_enable(void);

#endif /* USB_CDC_H */
