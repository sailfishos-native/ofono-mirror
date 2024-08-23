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
#include <linux/if_link.h>
#include <linux/if_arp.h>

#include <ell/ell.h>

#include "ofono.h"
#include "rmnet.h"

#define RMNET_TYPE "rmnet"
#define MAX_MUX_IDS 254U

static struct l_netlink *rtnl;
static uint32_t dump_id;
static uint32_t link_notify_id;

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

static int rmnet_parse_info_data(struct l_netlink_attr *linkinfo,
					uint16_t *out_mux_id)
{
	struct l_netlink_attr info_data;
	bool have_info_data = false;
	uint16_t rta_type;
	uint16_t rta_len;
	const void *rta_data;
	uint16_t mux_id;

	while (!l_netlink_attr_next(linkinfo, &rta_type, &rta_len, &rta_data)) {
		switch (rta_type) {
		case IFLA_INFO_KIND:
			if (strncmp(rta_data, RMNET_TYPE, rta_len))
				return -EPROTOTYPE;

			break;
		case IFLA_INFO_DATA:
			if (l_netlink_attr_recurse(linkinfo, &info_data) < 0)
				return -EBADMSG;

			have_info_data = true;
			break;
		}
	}

	if (!have_info_data)
		return -ENOENT;

	while (!l_netlink_attr_next(&info_data,
					&rta_type, &rta_len, &rta_data)) {
		if (rta_type != IFLA_RMNET_MUX_ID)
			continue;

		if (rta_len != sizeof(uint16_t))
			return -EBADMSG;

		mux_id = l_get_u16(rta_data);
		if (mux_id > MAX_MUX_IDS || !mux_id)
			return -ERANGE;

		if (out_mux_id)
			*out_mux_id = mux_id;

		return 0;
	}

	return -ENOENT;
}

static int rmnet_parse_link(const void *data, uint32_t len,
				char *out_ifname,
				uint32_t *out_ifindex,
				uint16_t *out_mux_id)
{
	const struct ifaddrmsg *ifa = data;
	struct l_netlink_attr attr;
	struct l_netlink_attr linkinfo;
	bool have_linkinfo = false;
	const char *ifname = NULL;
	uint16_t ifnamelen = 0;
	uint16_t rta_type;
	uint16_t rta_len;
	const void *rta_data;
	int r;

	if (l_netlink_attr_init(&attr, sizeof(struct ifinfomsg),
							data, len) < 0)
		return -EBADMSG;

	while (!l_netlink_attr_next(&attr, &rta_type, &rta_len, &rta_data)) {
		switch (rta_type) {
		case IFLA_IFNAME:
			ifname = rta_data;
			ifnamelen = rta_len;
			break;
		case IFLA_LINKINFO:
			if (l_netlink_attr_recurse(&attr, &linkinfo) < 0)
				return -EBADMSG;

			have_linkinfo = true;
			break;
		}
	}

	if (!have_linkinfo || !ifname || !ifnamelen)
		return -ENOENT;

	r = rmnet_parse_info_data(&linkinfo, out_mux_id);
	if (r < 0)
		return r;

	if (out_ifname)
		l_strlcpy(out_ifname, ifname, MIN(ifnamelen + 1, IF_NAMESIZE));

	if (out_ifindex)
		*out_ifindex = ifa->ifa_index;

	return 0;
}

static void rmnet_link_dump_destroy(void *user_data)
{
	dump_id = 0;
}

static void rmnet_link_dump_cb(int error,
				uint16_t type, const void *data,
				uint32_t len, void *user_data)
{
	struct rmnet_ifinfo info;

	/* Check conditions that can't happen on a dump */
	if (error || type != RTM_NEWLINK)
		return;

	if (rmnet_parse_link(data, len,
				info.ifname, &info.ifindex, &info.mux_id) < 0)
		return;

	DBG("Existing rmnet link: %s(%u) mux_id: %u",
			info.ifname, info.ifindex, info.mux_id);
}

static int rmnet_link_dump(void)
{
	struct ifinfomsg ifi;
	struct l_netlink_message *nlm =
		l_netlink_message_new_sized(RTM_GETLINK, NLM_F_DUMP,
								sizeof(ifi));

	memset(&ifi, 0, sizeof(ifi));
	l_netlink_message_add_header(nlm, &ifi, sizeof(ifi));

	l_netlink_message_enter_nested(nlm, IFLA_LINKINFO);
	l_netlink_message_append_string(nlm, IFLA_INFO_KIND, RMNET_TYPE);
	l_netlink_message_leave_nested(nlm);

	dump_id = l_netlink_send(rtnl, nlm, rmnet_link_dump_cb, NULL,
				rmnet_link_dump_destroy);
	if (dump_id > 0)
		return 0;

	l_netlink_message_unref(nlm);
	return -EIO;
}

static void rmnet_link_notification(uint16_t type, const void *data,
					uint32_t len, void *user_data)
{
	char ifname[IF_NAMESIZE];
	uint16_t mux_id;
	uint32_t ifindex;

	if (type != RTM_NEWLINK && type != RTM_DELLINK)
		return;

	if (rmnet_parse_link(data, len, ifname, &ifindex, &mux_id) < 0)
		return;

	DBG("link_notification: %s(%u) with mux_id: %u",
			ifname, ifindex, mux_id);
}

static int rmnet_init(void)
{
	int r;

	DBG("");

	rtnl = l_netlink_new(NETLINK_ROUTE);
	if (!rtnl)
		return -EIO;

	r = rmnet_link_dump();
	if (r < 0)
		goto dump_failed;

	link_notify_id = l_netlink_register(rtnl, RTNLGRP_LINK,
					rmnet_link_notification, NULL, NULL);

	return 0;
dump_failed:
	l_netlink_destroy(rtnl);
	return r;
}

static void rmnet_exit(void)
{
	l_netlink_unregister(rtnl, link_notify_id);
	l_netlink_destroy(rtnl);
}

OFONO_MODULE(rmnet, rmnet_init, rmnet_exit)
