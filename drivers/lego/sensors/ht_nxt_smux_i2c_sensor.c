/*
 * HiTechnic NXT Sensor Multiplexer device driver
 *
 * Copyright (C) 2013-2014 David Lechner <david@lechnology.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.

 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Note: The comment block below is used to generate docs on the ev3dev website.
 * Use kramdown (markdown) format. Use a '.' as a placeholder when blank lines
 * or leading whitespace is important for the markdown syntax.
 */

/**
 * DOC: website
 *
 * HiTechnic NXT Sensor Multiplexer I2C sensor driver
 *
 * A `ht-smux-i2c-sensor` device is loaded by the [ht-nxt-smux] driver
 * when it is detected by the [sensor mux] (automatic detection only works with
 * the sensors listed in the linked page) or when the sensor is set via the
 * [ht-smux-i2c-host] device. You can use any one of the sensors that has the
 * `nxt-i2c-sensor` module from the [list of supported sensors]. Keep in mind
 * though that the [sensor mux] operates in a read-only mode with I2C sensors.
 * Some modes of I2C sensors require writing data to the sensor and as a result,
 * these modes will not be usable via the [sensor mux].
 * .
 * ### sysfs attributes
 * .
 * These sensors use the [lego-sensor class]. Follow the link for more information.
 * .
 * This device can be found at `/sys/bus/lego/devices/in<N>:mux<M>:<device-name>`
 * where `<N>` is the input port on the EV3 (1 to 4), `<M>` is the input port
 * on the sensor mux (1 to 4) and `<device-name`> is the name of the sensor
 * (e.g. `lego-nxt-ultrasonic`).
 * .
 * [ht-nxt-smux]: /docs/sensors/hitechnic-nxt-sensor-multiplexer
 * [list of supported sensors]: /docs/sensors#supported-sensors
 * [lego-sensor class]: ../lego-sensor-class
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <lego.h>
#include <lego_sensor_class.h>

#include "nxt_i2c_sensor.h"
#include "ht_nxt_smux.h"

struct ht_nxt_smux_i2c_sensor_data {
	struct lego_device *ldev;
	struct nxt_i2c_sensor_info info;
	struct lego_sensor_device sensor;
	enum nxt_i2c_sensor_type type;
};

static int ht_nxt_smux_i2c_sensor_set_mode(void *context, u8 mode)
{
	struct ht_nxt_smux_i2c_sensor_data *data = context;
	struct lego_port_device *port = data->ldev->port;
	struct lego_sensor_mode_info *mode_info = &data->info.mode_info[mode];
	struct nxt_i2c_sensor_mode_info *i2c_mode_info = data->info.i2c_mode_info;

	ht_nxt_smux_port_set_i2c_data_reg(port, i2c_mode_info[mode].read_data_reg,
					  data->info.mode_info[mode].data_sets);
	port->nxt_i2c_ops->set_pin1_gpio(port->context,
					 i2c_mode_info[mode].pin1_state);
	lego_port_set_raw_data_ptr_and_func(port, mode_info->raw_data,
		lego_sensor_get_raw_data_size(mode_info), NULL, NULL);

	return 0;
}

static int ht_nxt_smux_i2c_sensor_probe(struct lego_device *ldev)
{
	struct ht_nxt_smux_i2c_sensor_data *data;
	const struct nxt_i2c_sensor_info *sensor_info;
	struct ht_nxt_smux_i2c_sensor_platform_data *pdata =
		ldev->dev.platform_data;
	int err, i;

	if (WARN_ON(!ldev->entry_id))
		return -EINVAL;

	if (WARN_ON(!pdata))
		return -EINVAL;

	sensor_info = &nxt_i2c_sensor_defs[ldev->entry_id->driver_data];

	data = kzalloc(sizeof(struct ht_nxt_smux_i2c_sensor_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->ldev = ldev;
	data->type = ldev->entry_id->driver_data;
	memcpy(&data->info, sensor_info, sizeof(struct nxt_i2c_sensor_info));

	strncpy(data->sensor.name, ldev->entry_id->name, LEGO_SENSOR_NAME_SIZE);
	strncpy(data->sensor.port_name, data->ldev->port->port_name,
		LEGO_SENSOR_NAME_SIZE);
	if (data->info.num_read_only_modes)
		data->sensor.num_modes = data->info.num_read_only_modes;
	else
		data->sensor.num_modes = data->info.num_modes;
	data->sensor.num_view_modes = 1;
	data->sensor.mode_info = data->info.mode_info;
	data->sensor.set_mode = ht_nxt_smux_i2c_sensor_set_mode;
	data->sensor.context = data;
	data->sensor.address = pdata->address;

	for (i = 0; i < data->sensor.num_modes; i++) {
		struct lego_sensor_mode_info *minfo = &data->info.mode_info[i];

		if (!minfo->raw_min && !minfo->raw_max)
			minfo->raw_max = 255;
		if (!minfo->pct_min && !minfo->pct_max)
			minfo->pct_max = 100;
		if (!minfo->si_min && !minfo->si_max)
			minfo->si_max = 255;
		if (!minfo->data_sets)
			minfo->data_sets = 1;
		if (!minfo->figures)
			minfo->figures = 5;
	}

	dev_set_drvdata(&ldev->dev, data);

	err = register_lego_sensor(&data->sensor, &ldev->dev);
	if (err) {
		dev_err(&ldev->dev, "could not register sensor!\n");
		goto err_register_lego_sensor;
	}

	ht_nxt_smux_port_set_i2c_addr(data->ldev->port, pdata->address,
				      data->info.slow);
	ht_nxt_smux_i2c_sensor_set_mode(data, 0);

	return 0;

err_register_lego_sensor:
	kfree(data);

	return err;
}

static int ht_nxt_smux_i2c_sensor_remove(struct lego_device *ldev)
{
	struct ht_nxt_smux_i2c_sensor_data *data = dev_get_drvdata(&ldev->dev);

	lego_port_set_raw_data_ptr_and_func(ldev->port, NULL, 0, NULL, NULL);
	ldev->port->nxt_i2c_ops->set_pin1_gpio(ldev->port->context,
					       LEGO_PORT_GPIO_FLOAT);
	unregister_lego_sensor(&data->sensor);
	dev_set_drvdata(&ldev->dev, NULL);
	kfree(data);

	return 0;
}

static struct lego_device_id ht_nxt_smux_i2c_sensor_id_table[] = {
	NXT_I2C_SENSOR_ID_TABLE_DATA
};
MODULE_DEVICE_TABLE(legoev3, ht_nxt_smux_i2c_sensor_id_table);

static struct lego_device_driver ht_nxt_smux_i2c_sensor_driver = {
	.driver = {
		.name	= "ht-nxt-smux-i2c-sensor",
	},
	.id_table	= ht_nxt_smux_i2c_sensor_id_table,
	.probe		= ht_nxt_smux_i2c_sensor_probe,
	.remove		= ht_nxt_smux_i2c_sensor_remove,
};
lego_device_driver(ht_nxt_smux_i2c_sensor_driver);

MODULE_DESCRIPTION("HiTechnic NXT Sensor Multiplexer I2C sensor device driver");
MODULE_AUTHOR("David Lechner <david@lechnology.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("lego:ht-nxt-smux-i2c-sensor");
