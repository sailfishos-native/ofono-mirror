/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_DEVINFO_H
#define __OFONO_DEVINFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ofono/types.h>

struct ofono_devinfo;

typedef void (*ofono_devinfo_query_cb_t)(const struct ofono_error *error,
					const char *attribute, void *data);

struct ofono_devinfo_driver {
	int (*probe)(struct ofono_devinfo *info, unsigned int vendor,
			void *data);
	void (*remove)(struct ofono_devinfo *info);
	void (*query_manufacturer)(struct ofono_devinfo *info,
			ofono_devinfo_query_cb_t cb, void *data);
	void (*query_serial)(struct ofono_devinfo *info,
			ofono_devinfo_query_cb_t cb, void *data);
	void (*query_model)(struct ofono_devinfo *info,
			ofono_devinfo_query_cb_t cb, void *data);
	void (*query_revision)(struct ofono_devinfo *info,
			ofono_devinfo_query_cb_t cb, void *data);
	void (*query_svn)(struct ofono_devinfo *info,
			ofono_devinfo_query_cb_t cb, void *data);
};

struct ofono_devinfo *ofono_devinfo_create(struct ofono_modem *modem,
							unsigned int vendor,
							const char *driver,
							void *data);
void ofono_devinfo_register(struct ofono_devinfo *info);
void ofono_devinfo_remove(struct ofono_devinfo *info);

void ofono_devinfo_set_data(struct ofono_devinfo *info, void *data);
void *ofono_devinfo_get_data(struct ofono_devinfo *info);

struct ofono_modem *ofono_devinfo_get_modem(struct ofono_devinfo *info);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_MODEM_INFO_H */
