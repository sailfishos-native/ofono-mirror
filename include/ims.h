/*
 * oFono - Open Source Telephony
 * Copyright (C) 2017  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_IMS_H
#define __OFONO_IMS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#include <ofono/types.h>

struct ofono_ims;

typedef void (*ofono_ims_register_cb_t)(const struct ofono_error *error,
						void *data);
typedef void (*ofono_ims_status_cb_t)(const struct ofono_error *error,
						int reg_info, int ext_info,
						void *data);

struct ofono_ims_driver {
	int (*probe)(struct ofono_ims *ims, unsigned int vendor, void *data);
	int (*probev)(struct ofono_ims *ims, unsigned int vendor, va_list args);
	void (*remove)(struct ofono_ims *ims);
	void (*ims_register)(struct ofono_ims *ims,
				ofono_ims_register_cb_t cb, void *data);
	void (*ims_unregister)(struct ofono_ims *ims,
				ofono_ims_register_cb_t cb, void *data);
	void (*registration_status)(struct ofono_ims *ims,
				ofono_ims_status_cb_t cb, void *data);
};

void ofono_ims_status_notify(struct ofono_ims *ims, int reg_info,
							int ext_info);

struct ofono_ims *ofono_ims_create(struct ofono_modem *modem,
					unsigned int vendor,
					const char *driver, ...);

void ofono_ims_register(struct ofono_ims *ims);
void ofono_ims_remove(struct ofono_ims *ims);

void ofono_ims_set_data(struct ofono_ims *ims, void *data);
void *ofono_ims_get_data(const struct ofono_ims *ims);

#ifdef __cplusplus
}
#endif

#endif
