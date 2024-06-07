/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_CALL_SETTINGS_H
#define __OFONO_CALL_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#include <ofono/types.h>

struct ofono_call_settings;

typedef void (*ofono_call_settings_status_cb_t)(const struct ofono_error *error,
						int status, void *data);

typedef void (*ofono_call_settings_set_cb_t)(const struct ofono_error *error,
						void *data);

typedef void (*ofono_call_settings_clir_cb_t)(const struct ofono_error *error,
					int override, int network, void *data);

struct ofono_call_settings_driver {
	unsigned int flags;
	int (*probe)(struct ofono_call_settings *cs, unsigned int vendor,
			void *data);
	int (*probev)(struct ofono_call_settings *cs, unsigned int vendor,
			va_list args);
	void (*remove)(struct ofono_call_settings *cs);
	void (*clip_query)(struct ofono_call_settings *cs,
				ofono_call_settings_status_cb_t cb, void *data);
	void (*cnap_query)(struct ofono_call_settings *cs,
				ofono_call_settings_status_cb_t cb, void *data);
	void (*cdip_query)(struct ofono_call_settings *cs,
				ofono_call_settings_status_cb_t cb, void *data);
	void (*colp_query)(struct ofono_call_settings *cs,
				ofono_call_settings_status_cb_t cb, void *data);
	void (*clir_query)(struct ofono_call_settings *cs,
				ofono_call_settings_clir_cb_t cb, void *data);
	void (*colr_query)(struct ofono_call_settings *cs,
				ofono_call_settings_status_cb_t cb, void *data);
	void (*clir_set)(struct ofono_call_settings *cs, int mode,
				ofono_call_settings_set_cb_t cb, void *data);
	void (*cw_query)(struct ofono_call_settings *cs, int cls,
			ofono_call_settings_status_cb_t cb, void *data);
	void (*cw_set)(struct ofono_call_settings *cs, int mode, int cls,
			ofono_call_settings_set_cb_t cb, void *data);
};

struct ofono_call_settings *ofono_call_settings_create(
						struct ofono_modem *modem,
						unsigned int vendor,
						const char *driver, ...);

void ofono_call_settings_register(struct ofono_call_settings *cs);
void ofono_call_settings_remove(struct ofono_call_settings *cs);

void ofono_call_settings_set_data(struct ofono_call_settings *cs, void *data);
void *ofono_call_settings_get_data(struct ofono_call_settings *cs);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_CALL_SETTINGS_H */
