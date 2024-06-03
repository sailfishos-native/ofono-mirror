/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_PRIVATE_NETWORK_H
#define __OFONO_PRIVATE_NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

struct ofono_private_network_settings {
	int fd;
	char *server_ip;
	char *peer_ip;
	char *primary_dns;
	char *secondary_dns;
};

typedef void (*ofono_private_network_cb_t)(
			const struct ofono_private_network_settings *settings,
			void *data);

struct ofono_private_network_driver {
	char *name;
	int (*request)(ofono_private_network_cb_t cb, void *data);
	void (*release)(int uid);
};

int ofono_private_network_driver_register(
			const struct ofono_private_network_driver *d);
void ofono_private_network_driver_unregister(
			const struct ofono_private_network_driver *d);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_PRIVATE_NETWORK_H */
