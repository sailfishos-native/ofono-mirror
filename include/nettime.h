/*
 * oFono - Open Source Telephony
 * Copyright (C) 2010  Nokia Corporation and/or its subsidiary(-ies)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_NETTIME_H
#define __OFONO_NETTIME_H

#ifdef __cplusplus
extern "C" {
#endif

struct ofono_network_time;

struct ofono_nettime_context {
	struct ofono_nettime_driver *driver;
	struct ofono_modem *modem;
	void *data;
};

struct ofono_nettime_driver {
	const char *name;
	int (*probe)(struct ofono_nettime_context *context);
	void (*remove)(struct ofono_nettime_context *context);
	void (*info_received)(struct ofono_nettime_context *context,
				struct ofono_network_time *info);
};

int ofono_nettime_driver_register(const struct ofono_nettime_driver *driver);
void ofono_nettime_driver_unregister(const struct ofono_nettime_driver *driver);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_NETTIME_H */
