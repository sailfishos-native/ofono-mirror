/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib.h>
#include "ofono.h"

static GSList *g_drivers = NULL;

void __ofono_private_network_release(int id)
{
	GSList *d;

	DBG("");

	for (d = g_drivers; d; d = d->next) {
		const struct ofono_private_network_driver *driver = d->data;

		if (!driver->release)
			continue;

		driver->release(id);

		break;
	}
}

ofono_bool_t __ofono_private_network_request(ofono_private_network_cb_t cb,
						int *id, void *data)
{
	GSList *d;
	int uid;

	DBG("");

	for (d = g_drivers; d; d = d->next) {
		const struct ofono_private_network_driver *driver = d->data;

		if (!driver->request)
			continue;

		uid = driver->request(cb, data);
		if (uid <= 0)
			continue;

		*id = uid;
		return TRUE;
	}

	return FALSE;
}

int ofono_private_network_driver_register(
			const struct ofono_private_network_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	g_drivers = g_slist_prepend(g_drivers, (void *) d);

	return 0;
}

void ofono_private_network_driver_unregister(
			const struct ofono_private_network_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	g_drivers = g_slist_remove(g_drivers, (void *) d);
}
