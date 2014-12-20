/*
 * Port functions for LEGO WeDo
 *
 * Copyright (C) 2014 Ralph Hempel <rhemple@hempeldesigngroup.com>
 * Copyright (C) 2014 David Lechner <david@lechnology.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.

 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/leds.h>
#include <linux/device.h>
#include <linux/slab.h>

#include <lego.h>
#include <lego_port_class.h>
#include <lego_sensor_class.h>
#include <dc_motor_class.h>

#include "wedo.h"

/*
 * -----------------------------------------------------------------------------
 * This file provides components for interfacing with the ports on the
 * LEGO WeDo USB brick.
 *
 * Each port has its own device node.
 * -----------------------------------------------------------------------------
 */

struct wedo_id_info {
	unsigned char max;
	unsigned char *name;
};

/* The max fields in this table must be in ascending order for the
 * state calculation to work
 */

const struct wedo_id_info wedo_id_infos[] = {
	[WEDO_TYPE_SHORTLO]	= {   9 , "shortlo"	},
	[WEDO_TYPE_BEND]	= {  27 , "bend"	},
	[WEDO_TYPE_TILT]	= {  47 , "tilt"	},
	[WEDO_TYPE_FUTURE]	= {  67 , "future"	},
	[WEDO_TYPE_RAW]		= {  87 , "raw"		},
	[WEDO_TYPE_TOUCH]	= { 109 , "touch"	},
	[WEDO_TYPE_SOUND]	= { 131 , "sound"	},
	[WEDO_TYPE_TEMP]	= { 152 , "temp"	},
	[WEDO_TYPE_LIGHT]	= { 169 , "light"	},
	[WEDO_TYPE_MOTION]	= { 190 , "motion"	},
	[WEDO_TYPE_LIGHTBRICK]	= { 211 , "lightbrick"	},
	[WEDO_TYPE_22]		= { 224 , "22"		},
	[WEDO_TYPE_OPEN]	= { 233 , "open"	},
	[WEDO_TYPE_MOTOR]	= { 246 , "motor"	},
	[WEDO_TYPE_SHORTHI]	= { 255 , "shorthi"	},
};

void wedo_port_update_output(struct wedo_port_data *wpd, signed char value)
{
	wpd->output = value;
	wedo_hub_request_output_update(wpd->usb);
}

/*
 * These functions handle registering lego-sensor devices on WeDo ports
 * as well as the mode callbacks
 */

static int wedo_sensor_set_mode(void *context, u8 mode)
{
	struct wedo_sensor_data *wsd = context;

	if (mode >= wsd->info.num_modes)
		return -EINVAL;

	return 0;
}

static int register_wedo_sensor(struct wedo_port_data *wpd,
				enum wedo_sensor_types type)
{
	struct wedo_sensor_data *wsd;
	int err;

	if (wpd->sensor_data)
		return -EINVAL;

	wsd = kzalloc(sizeof(struct wedo_sensor_data), GFP_KERNEL);
	if (!wsd)
		return -ENOMEM;

	wsd->wpd = wpd;

	memcpy(&wsd->info, &wedo_sensor_defs[type],
		sizeof(struct wedo_sensor_info));

	strncpy(wsd->sensor.name, wsd->info.name, LEGO_SENSOR_NAME_SIZE);
	strncpy(wsd->sensor.port_name, wpd->port.port_name,
	LEGO_SENSOR_NAME_SIZE);

	dev_info(&wpd->port.dev, "name %s port_name %s\n", wsd->sensor.name,
		 wsd->sensor.port_name);

	wsd->sensor.num_modes = wsd->info.num_modes;
	wsd->sensor.mode_info = wsd->info.mode_info;
	wsd->sensor.set_mode = wedo_sensor_set_mode;
	wsd->sensor.context = wsd;

	err = register_lego_sensor(&wsd->sensor, &wpd->port.dev);
	if (err)
		goto err_register_lego_sensor;

	wpd->sensor_data = wsd;

	wedo_sensor_set_mode(wsd, 0);

	return 0;

	err_register_lego_sensor: kfree(wsd);

	return err;
}

static void unregister_wedo_sensor(struct wedo_port_data *wpd)
{
	struct wedo_sensor_data *wsd = wpd->sensor_data;

	if (!wsd)
		return;

	unregister_lego_sensor(&wsd->sensor);
	wpd->sensor_data = NULL;
	kfree(wsd);
}

/*
 * These functions handle registering dc_motor devices on WeDo ports
 */

static int register_wedo_motor(struct wedo_port_data *wpd)
{
	struct wedo_motor_data *wmd;
	int err;

	if (wpd->motor_data)
		return -EINVAL;

	wmd = kzalloc(sizeof(struct wedo_motor_data), GFP_KERNEL);
	if (!wmd)
		return -ENOMEM;

	wmd->wpd = wpd;

	strncpy(wmd->md.name, "wedo-motor", DC_MOTOR_NAME_SIZE);
	strncpy(wmd->md.port_name, wpd->port.port_name, DC_MOTOR_NAME_SIZE);

	wmd->md.ops = &wedo_motor_ops;
	wmd->md.context = wmd;

	err = register_dc_motor(&wmd->md, &wpd->port.dev);
	if (err)
		goto err_register_dc_motor;

	wpd->motor_data = wmd;

	return 0;

	err_register_dc_motor: kfree(wmd);

	return err;
}

static void unregister_wedo_motor(struct wedo_port_data *wpd)
{
	struct wedo_motor_data *wmd = wpd->motor_data;

	if (!wmd)
		return;

	unregister_dc_motor(&wmd->md);
	wpd->motor_data = NULL;
	kfree(wmd);
}

/*
 * These functions handle registering led devices on WeDo ports
 */

struct wedo_led_data {
	struct wedo_port_data *wpd;
	struct led_classdev cdev;
	char name[LEGO_NAME_SIZE + 1];
};

static void wedo_port_led_brightness_set(struct led_classdev *cdev,
					 enum led_brightness brightness)
{
	struct wedo_led_data *wld = container_of(cdev, struct wedo_led_data, cdev);

	wedo_port_update_output(wld->wpd, brightness);
}

static enum led_brightness wedo_led_brightness_get(struct led_classdev *cdev)
{
	struct wedo_led_data *wld = container_of(cdev, struct wedo_led_data, cdev);

	return wld->wpd->output;
}

static int register_wedo_led(struct wedo_port_data *wpd)
{
	struct wedo_led_data *wld;
	int err;

	if (wpd->led_data)
		return -EINVAL;

	wld = kzalloc(sizeof(struct wedo_led_data), GFP_KERNEL);
	if (!wld)
		return -ENOMEM;

	wld->wpd = wpd;

	snprintf(wld->name, LEGO_NAME_SIZE, "wedo::%s", wpd->port.port_name);
	wld->cdev.name = wld->name;
	wld->cdev.max_brightness = 127;
	wld->cdev.brightness_set = wedo_port_led_brightness_set;
	wld->cdev.brightness_get = wedo_led_brightness_get;

	err = led_classdev_register(&wpd->port.dev, &wld->cdev);
	if (err)
		goto err_led_classdev_register;

	dev_info(&wpd->port.dev, "Bound wedo lightbrick as led %s\n",
		 dev_name(wld->cdev.dev));

	wpd->led_data = wld;

	return 0;

	err_led_classdev_register: kfree(wld);

	return err;
}

static void unregister_wedo_led(struct wedo_port_data *wpd)
{
	struct wedo_led_data *wld = wpd->led_data;

	if (!wld)
		return;

	led_classdev_unregister(&wld->cdev);
	wpd->led_data = NULL;
	kfree(wld);
}

/*
 * These functions handle registering devices on WeDo ports.
 *
 * There are only two generic types if devices that we handle:
 *
 * Input device ids get registered as lego-sensor class devices
 * Output device ids get registered as dc-motor or leds class devices
 *
 * Currently we only have the tilt and motion sensors for testing
 */

static int register_wedo_device(struct wedo_port_data *wpd,
				enum wedo_type_id id)
{
	int err = 0;

	wpd->type_id = id;

	switch (wpd->type_id) {

	case WEDO_TYPE_TILT:
		err = register_wedo_sensor(wpd, WEDO_TILT_SENSOR);
		break;
	case WEDO_TYPE_MOTION:
		err = register_wedo_sensor(wpd, WEDO_MOTION_SENSOR);
		break;
	case WEDO_TYPE_MOTOR:
		err = register_wedo_motor(wpd);
		break;
	case WEDO_TYPE_LIGHTBRICK:
		err = register_wedo_led(wpd);
		break;
	default:
		break;
	}

	return err;
}

static void unregister_wedo_device(struct wedo_port_data *wpd)
{
	unregister_wedo_sensor(wpd);
	unregister_wedo_motor(wpd);
	unregister_wedo_led(wpd);

	wedo_port_update_output(wpd, 0);
}


static void wedo_port_register_device_work(struct work_struct *work)
{
	struct wedo_port_data *wpd =
		container_of(work, struct wedo_port_data, register_device_work);
	int err;

	unregister_wedo_device(wpd);

	err = register_wedo_device(wpd, wpd->new_type_id);
	if (err)
		dev_err(&wpd->port.dev,
			"Error %d registering device type_id %d to '%s'\n",
			err, wpd->new_type_id, dev_name(&wpd->port.dev));
}

struct device_type wedo_port_type = {
	.name	= "wedo-port",
};

struct lego_port_mode_info wedo_port_mode_info[] = {
	[0] = {
		/**
		 * @description: Automatic
		 */
		.name = "AUTO",
	},
};

static int wedo_port_set_mode(void *context, u8 mode)
{
	/* There is only one mode */
	return 0;
}

static const char *wedo_port_get_status(void *context)
{
	struct wedo_port_data *wpd = context;

	return wedo_id_infos[wpd->type_id].name;
}

/*
 * Finally, we're at the public driver functions that register the WeDo
 * port devices for each hub.
 */

struct wedo_port_data *register_wedo_port(struct usb_interface *interface,
					  enum wedo_ports port_num)
{
	struct wedo_port_data *wpd;
	int err;

	wpd = kzalloc(sizeof(*wpd), GFP_KERNEL);
	if (!wpd)
		return ERR_PTR(-ENOMEM);

	wpd->usb = interface;
	snprintf(wpd->port.port_name, LEGO_PORT_NAME_SIZE, "usb%s:wedo%d",
		 dev_name(&interface->dev), port_num);
	wpd->port.num_modes = 1;
	wpd->port.mode_info = wedo_port_mode_info;
	wpd->port.set_mode = wedo_port_set_mode;
	wpd->port.get_status = wedo_port_get_status;
	wpd->port.motor_ops = &wedo_motor_ops;
	wpd->port.context = wpd;
	err = lego_port_register(&wpd->port, &wedo_port_type, &interface->dev);
	if (err)
		return ERR_PTR(err);

	INIT_WORK(&wpd->register_device_work, wedo_port_register_device_work);

	return wpd;
}

void unregister_wedo_port(struct wedo_port_data *wpd)
{
	if (!wpd)
		return;

	cancel_work_sync(&wpd->register_device_work);
	unregister_wedo_device(wpd);
	lego_port_unregister(&wpd->port);
	kfree(wpd);
}

/* Here's where we update the status of the devices connected to the
 * LEGO WeDo hub - this function is called when the wedo driver has
 * received a complete packet
 *
 * NOTE: only process ID changes if the output value is 0x00 or 0x80
 */

#define WEDO_PORT_TYPE_DEBOUNCE	32

void wedo_port_update_status(struct wedo_port_data *wpd)
{
	enum wedo_type_id id;

	struct wedo_sensor_data *wsd = NULL;
	struct wedo_motor_data *wmd = NULL;

	switch (wpd->type_id) {

	case WEDO_TYPE_TILT:
	case WEDO_TYPE_MOTION:
		wsd = wpd->sensor_data;
		if (wsd) {
			if (wsd->info.wedo_mode_info[wsd->sensor.mode].analog_cb)
				wsd->info.wedo_mode_info[wsd->sensor.mode].analog_cb(wsd);
			else
				wsd->info.mode_info[wsd->sensor.mode].raw_data[0] = wpd->input;
		}
		break;
	case WEDO_TYPE_MOTOR:
		wmd = wpd->motor_data;
		/* TODO: may need to change duty_cycle after reset */
		break;
	default:
		break;
	}

	for (id = 0; id < WEDO_TYPE_MAX; ++id)
		if (wpd->id <= wedo_id_infos[id].max)
			break;

	if (id != wpd->new_type_id) {
		wpd->type_debounce = 0;
		wpd->new_type_id = id;
	} else if (WEDO_PORT_TYPE_DEBOUNCE > wpd->type_debounce) {
		wpd->type_debounce++;
	} else if (WEDO_PORT_TYPE_DEBOUNCE == wpd->type_debounce) {
		if (id != wpd->type_id)
			schedule_work(&wpd->register_device_work);
		wpd->type_debounce++;
	}
}
