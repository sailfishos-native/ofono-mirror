/*
 *  oFono - Open Source Telephony
 *  Copyright (C) 2024  Cruise, LLC
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

struct rmnet_ifinfo {
	uint32_t ifindex;
	uint16_t mux_id;
	char ifname[IF_NAMESIZE];
};

typedef void (*rmnet_new_interfaces_func_t)(int error,
					unsigned int n_interfaces,
					const struct rmnet_ifinfo *interfaces,
					void *user_data);
typedef void (*rmnet_destroy_func_t)(void *user_data);

int rmnet_get_interfaces(uint32_t parent_ifindex, unsigned int n_interfaces,
				rmnet_new_interfaces_func_t cb,
				void *user_data, rmnet_destroy_func_t destroy);
int rmnet_del_interfaces(unsigned int n_interfaces,
					const struct rmnet_ifinfo *interfaces);
int rmnet_cancel(int id);
