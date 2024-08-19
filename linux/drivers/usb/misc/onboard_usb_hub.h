/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2022, Google LLC
 */

#ifndef _USB_MISC_ONBOARD_USB_HUB_H
#define _USB_MISC_ONBOARD_USB_HUB_H

struct onboard_hub_pdata {
	unsigned long reset_us;		/* reset pulse width in us */
};

static const struct onboard_hub_pdata microchip_usb424_data = {
	.reset_us = 1,
};

static const struct onboard_hub_pdata realtek_rts5411_data = {
	.reset_us = 0,
};

static const struct onboard_hub_pdata ti_tusb8041_data = {
	.reset_us = 3000,
};

static const struct onboard_hub_pdata genesys_gl850g_data = {
	.reset_us = 3,
};

static const struct onboard_hub_pdata genesys_gl852g_data = {
	.reset_us = 50,
};

static const struct onboard_hub_pdata vialab_vl817_data = {
	.reset_us = 10,
};

static const struct of_device_id onboard_hub_match[] = {
	{ .compatible = "usb424,2514", .data = &microchip_usb424_data, },
	{ .compatible = "usb424,2517", .data = &microchip_usb424_data, },
	{ .compatible = "usb451,8140", .data = &ti_tusb8041_data, },
	{ .compatible = "usb451,8142", .data = &ti_tusb8041_data, },
	{ .compatible = "usb5e3,608", .data = &genesys_gl850g_data, },
	{ .compatible = "usb5e3,610", .data = &genesys_gl852g_data, },
	{ .compatible = "usbbda,411", .data = &realtek_rts5411_data, },
	{ .compatible = "usbbda,5411", .data = &realtek_rts5411_data, },
	{ .compatible = "usbbda,414", .data = &realtek_rts5411_data, },
	{ .compatible = "usbbda,5414", .data = &realtek_rts5411_data, },
	{ .compatible = "usb2109,817", .data = &vialab_vl817_data, },
	{ .compatible = "usb2109,2817", .data = &vialab_vl817_data, },
	{}
};

#endif /* _USB_MISC_ONBOARD_USB_HUB_H */
