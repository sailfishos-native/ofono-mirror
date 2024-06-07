/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_CALL_METER_H
#define __OFONO_CALL_METER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#include <ofono/types.h>

struct ofono_call_meter;

typedef void (*ofono_call_meter_query_cb_t)(const struct ofono_error *error,
						int value, void *data);

typedef void (*ofono_call_meter_puct_query_cb_t)(
					const struct ofono_error *error,
					const char *currency, double ppu,
					void *data);

typedef void(*ofono_call_meter_set_cb_t)(const struct ofono_error *error,
						void *data);

struct ofono_call_meter_driver {
	int (*probe)(struct ofono_call_meter *cm, unsigned int vendor,
			void *data);
	int (*probev)(struct ofono_call_meter *cm, unsigned int vendor,
			va_list args);
	void (*remove)(struct ofono_call_meter *cm);
	void (*call_meter_query)(struct ofono_call_meter *cm,
				ofono_call_meter_query_cb_t cb, void *data);
	void (*acm_query)(struct ofono_call_meter *cm,
				ofono_call_meter_query_cb_t cb, void *data);
	void (*acm_reset)(struct ofono_call_meter *cm, const char *sim_pin2,
				ofono_call_meter_set_cb_t cb, void *data);
	void (*acm_max_query)(struct ofono_call_meter *cm,
				ofono_call_meter_query_cb_t cb, void *data);
	void (*acm_max_set)(struct ofono_call_meter *cm, int new_value,
				const char *sim_pin2,
				ofono_call_meter_set_cb_t cb, void *data);
	void (*puct_query)(struct ofono_call_meter *cm,
			ofono_call_meter_puct_query_cb_t cb, void *data);
	void (*puct_set)(struct ofono_call_meter *cm, const char *currency,
				double ppu, const char *sim_pin2,
				ofono_call_meter_set_cb_t cb, void *data);
};

struct ofono_call_meter *ofono_call_meter_create(struct ofono_modem *modem,
							unsigned int vendor,
							const char *driver,
							...);

void ofono_call_meter_register(struct ofono_call_meter *cm);
void ofono_call_meter_remove(struct ofono_call_meter *cm);

void ofono_call_meter_maximum_notify(struct ofono_call_meter *cm);
void ofono_call_meter_changed_notify(struct ofono_call_meter *cm,
					int new_value);

void ofono_call_meter_set_data(struct ofono_call_meter *cm, void *data);
void *ofono_call_meter_get_data(struct ofono_call_meter *cm);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_CALL_METER_H */
