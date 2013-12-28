/*
 * Support for the input and output ports on the LEGO Mindstorms EV3
 *
 * Copyright (C) 2013 David Lechner <david@lechnology.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.

 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_LEGOEV3_PORTS_H
#define __LINUX_LEGOEV3_PORTS_H

#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/platform_device.h>
#include <linux/i2c-gpio.h>
#include <mach/legoev3.h>

struct legoev3_input_port_platform_data {
	enum legoev3_input_port_id id;
	unsigned pin1_gpio;
	unsigned pin2_gpio;
	unsigned pin5_gpio;
	unsigned pin6_gpio;
	unsigned buf_ena_gpio;
	unsigned i2c_clk_gpio;
	unsigned i2c_dev_id;
	unsigned i2c_pin_mux;
	unsigned uart_pin_mux;
};

struct legoev3_output_port_platform_data {
	enum legoev3_output_port_id id;
	unsigned pin1_gpio;
	unsigned pin2_gpio;
	unsigned pin5_gpio;
	unsigned pin6_gpio;
	const char *pwm_dev_name;
};

struct legoev3_ports_platform_data {
	struct legoev3_input_port_platform_data input_port_data[LEGOEV3_NUM_PORT_IN];
	struct legoev3_output_port_platform_data output_port_data[LEGOEV3_NUM_PORT_OUT];
};

struct legoev3_port_driver {
	struct device_driver driver;
};

static inline struct legoev3_port_driver
*to_legoev3_port_driver(struct device_driver *drv)
{
	return drv ? container_of(drv, struct legoev3_port_driver, driver) : NULL;
}

extern int legoev3_register_port_driver(struct legoev3_port_driver *);
extern void legoev3_unregister_port_driver(struct legoev3_port_driver *);
#define legoev3_port_driver(driver) \
module_driver(driver, legoev3_register_port_driver, legoev3_unregister_port_driver);

extern struct attribute_group legoev3_device_type_attr_grp;
extern struct bus_type legoev3_bus_type;

#endif /* __LINUX_LEGOEV3_PORTS_H */
