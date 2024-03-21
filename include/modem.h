/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2008-2011  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __OFONO_MODEM_H
#define __OFONO_MODEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ofono/types.h>

struct ofono_modem;
struct ofono_gprs;
struct ofono_sim;
struct ofono_voicecall;

enum ofono_modem_type {
	OFONO_MODEM_TYPE_HARDWARE = 0,
	OFONO_MODEM_TYPE_HFP,
	OFONO_MODEM_TYPE_SAP,
	OFONO_MODEM_TYPE_TEST,
};

enum ofono_modem_capability {
	OFONO_MODEM_CAPABILITY_LTE = 0x1,
};

typedef void (*ofono_modem_online_cb_t)(const struct ofono_error *error,
					void *data);

typedef ofono_bool_t (*ofono_modem_compare_cb_t)(struct ofono_modem *modem,
							void *user_data);

struct ofono_driver_desc {
	const char *name;
	const void *driver;
} __attribute__((aligned(8)));

#define OFONO_ATOM_DRIVER_BUILTIN(type, name, driver)			\
	_Pragma("GCC diagnostic push")					\
	_Pragma("GCC diagnostic ignored \"-Wattributes\"")		\
	static struct ofono_driver_desc					\
		__ofono_builtin_ ## type ## _ ##name			\
		__attribute__((used, retain, section("__" #type),	\
				aligned(8))) = {			\
			#name, driver					\
		};							\
	_Pragma("GCC diagnostic pop")

struct ofono_modem_driver {
	enum ofono_modem_type modem_type;

	/* Detect existence of device and initialize any device-specific data
	 * structures */
	int (*probe)(struct ofono_modem *modem);

	/* Destroy data structures allocated during probe and cleanup */
	void (*remove)(struct ofono_modem *modem);

	/* Power up device */
	int (*enable)(struct ofono_modem *modem);

	/* Power down device */
	int (*disable)(struct ofono_modem *modem);

	/* Enable or disable cellular radio */
	void (*set_online)(struct ofono_modem *modem, ofono_bool_t online,
				ofono_modem_online_cb_t callback, void *data);

	/* Populate the atoms available without SIM / Locked SIM */
	void (*pre_sim)(struct ofono_modem *modem);

	/* Populate the atoms that are available with SIM / Unlocked SIM*/
	void (*post_sim)(struct ofono_modem *modem);

	/* Populate the atoms available online */
	void (*post_online)(struct ofono_modem *modem);
};

#define OFONO_MODEM_DRIVER_BUILTIN(name, driver)				\
	_Pragma("GCC diagnostic push")					\
	_Pragma("GCC diagnostic ignored \"-Wattributes\"")		\
	static struct ofono_driver_desc					\
		__ofono_builtin_modem_driver_ ##name			\
		__attribute__((used, retain, section("__modem"),	\
				aligned(8))) = {			\
			#name, driver					\
		};							\
	_Pragma("GCC diagnostic pop")

void ofono_modem_add_interface(struct ofono_modem *modem,
				const char *interface);
void ofono_modem_remove_interface(struct ofono_modem *modem,
					const char *interface);

const char *ofono_modem_get_path(struct ofono_modem *modem);
struct ofono_sim *ofono_modem_get_sim(struct ofono_modem *modem);
struct ofono_gprs *ofono_modem_get_gprs(struct ofono_modem *modem);
struct ofono_voicecall *ofono_modem_get_voicecall(struct ofono_modem *modem);

void ofono_modem_set_data(struct ofono_modem *modem, void *data);
void *ofono_modem_get_data(struct ofono_modem *modem);

struct ofono_modem *ofono_modem_create(const char *name, const char *type);
int ofono_modem_register(struct ofono_modem *modem);

ofono_bool_t ofono_modem_is_registered(struct ofono_modem *modem);
void ofono_modem_remove(struct ofono_modem *modem);

void ofono_modem_reset(struct ofono_modem *modem);

void ofono_modem_set_powered(struct ofono_modem *modem, ofono_bool_t powered);
ofono_bool_t ofono_modem_get_powered(struct ofono_modem *modem);

ofono_bool_t ofono_modem_get_online(struct ofono_modem *modem);

ofono_bool_t ofono_modem_get_emergency_mode(struct ofono_modem *modem);

void ofono_modem_set_name(struct ofono_modem *modem, const char *name);
void ofono_modem_set_driver(struct ofono_modem *modem, const char *type);

/*
 * Set the capabilities of the modem.  This method should be called in
 * the driver probe() method if the capability information can be obtained
 * early, for example using the model information, or vid/pid of the device.
 *
 * Otherwise, it should be called prior to setting the device powered.
 */
void ofono_modem_set_capabilities(struct ofono_modem *modem,
						unsigned int capabilities);

int ofono_modem_set_string(struct ofono_modem *modem,
				const char *key, const char *value);
const char *ofono_modem_get_string(struct ofono_modem *modem, const char *key);

int ofono_modem_set_integer(struct ofono_modem *modem,
				const char *key, int value);
int ofono_modem_get_integer(struct ofono_modem *modem, const char *key);

int ofono_modem_set_boolean(struct ofono_modem *modem,
				const char *key, ofono_bool_t value);
ofono_bool_t ofono_modem_get_boolean(struct ofono_modem *modem,
					const char *key);

struct ofono_modem *ofono_modem_find(ofono_modem_compare_cb_t func,
					void *user_data);

void ofono_modem_set_powered_timeout_hint(struct ofono_modem *modem,
							unsigned int seconds);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_MODEM_H */
