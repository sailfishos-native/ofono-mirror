/*
 *  oFono - Open Source Telephony
 *  Copyright (C) 2024  Cruise, LLC
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <linux/rtnetlink.h>
#include <net/if.h>

#include <ell/ell.h>

#include "ofono.h"
#include "rmnet.h"

static struct l_netlink *rtnl;

int rmnet_get_interfaces(uint32_t parent_ifindex, unsigned int n_interfaces,
				rmnet_new_interfaces_func_t cb,
				void *user_data, rmnet_destroy_func_t destroy)
{
	return -ENOTSUP;
}

int rmnet_del_interfaces(unsigned int n_interfaces,
					const struct rmnet_ifinfo *interfaces)
{
	return -ENOTSUP;
}

int rmnet_cancel(int id)
{
	return -ENOTSUP;
}

static int rmnet_init(void)
{
	DBG("");

	rtnl = l_netlink_new(NETLINK_ROUTE);
	if (!rtnl)
		return -EIO;

	return 0;
}

static void rmnet_exit(void)
{
	l_netlink_destroy(rtnl);
}

OFONO_MODULE(rmnet, rmnet_init, rmnet_exit)
