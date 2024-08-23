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
#include <stdio.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <linux/if_arp.h>

#include <ell/ell.h>

#include "ofono.h"
#include "rmnet.h"

#define RMNET_TYPE "rmnet"
#define MAX_MUX_IDS 254U

struct rmnet_request {
	uint32_t parent_ifindex;
	rmnet_new_interfaces_func_t new_cb;
	void *user_data;
	rmnet_destroy_func_t destroy;
	int id;
	bool canceled;
	uint32_t netlink_id;
	uint16_t request_type;
	uint8_t current;
	uint8_t n_interfaces;
	struct rmnet_ifinfo infos[];
};

static struct l_netlink *rtnl;
static uint32_t dump_id;
static uint32_t link_notify_id;
static struct l_uintset *mux_ids;
struct l_queue *request_q;
static int next_request_id = 1;

static void rmnet_request_free(struct rmnet_request *req)
{
	if (req->destroy)
		req->destroy(req->user_data);

	l_free(req);
}

static bool rmnet_request_id_matches(const void *a, const void *b)
{
	const struct rmnet_request *req = a;
	int id = L_PTR_TO_INT(b);

	return req->id == id;
}

static struct rmnet_request *__rmnet_del_request_new(unsigned int n_interfaces,
					const struct rmnet_ifinfo *interfaces)
{
	struct rmnet_request *req;

	req = l_malloc(sizeof(struct rmnet_request) +
				sizeof(struct rmnet_ifinfo) * n_interfaces);
	memset(req, 0, sizeof(struct rmnet_request));
	req->request_type = RTM_DELLINK;
	req->n_interfaces = n_interfaces;
	memcpy(req->infos, interfaces,
			sizeof(struct rmnet_ifinfo) * n_interfaces);

	return req;
}

static struct rmnet_request *__rmnet_cancel_request(void)
{
	struct rmnet_request *req = l_queue_pop_head(request_q);

	if (req->current) {
		struct rmnet_request *del_req =
			__rmnet_del_request_new(req->current, req->infos);
		l_queue_push_head(request_q, del_req);
	}

	return req;
}

static int rmnet_link_del(uint32_t ifindex, l_netlink_command_func_t cb,
				void *userdata,
				l_netlink_destroy_func_t destroy,
				uint32_t *out_command_id)
{
	struct l_netlink_message *nlm =
		l_netlink_message_new(RTM_DELLINK, 0);
	struct ifinfomsg ifi;
	uint32_t id;

	memset(&ifi, 0, sizeof(ifi));
	ifi.ifi_family = AF_UNSPEC;
	ifi.ifi_index = ifindex;

	l_netlink_message_add_header(nlm, &ifi, sizeof(ifi));

	id = l_netlink_send(rtnl, nlm, cb, userdata, destroy);
	if (!id) {
		l_netlink_message_unref(nlm);
		return -EIO;
	}

	if (out_command_id)
		*out_command_id = id;

	return 0;
}

static int rmnet_link_new(uint32_t parent_ifindex, uint8_t mux_id,
				const char ifname[static IF_NAMESIZE],
				l_netlink_command_func_t cb,
				void *userdata,
				l_netlink_destroy_func_t destroy,
				uint32_t *out_command_id)
{
	struct ifinfomsg ifi;
	struct l_netlink_message *nlm =
		l_netlink_message_new(RTM_NEWLINK, NLM_F_EXCL | NLM_F_CREATE);
	struct ifla_rmnet_flags flags;
	uint32_t id;

	memset(&ifi, 0, sizeof(ifi));
	ifi.ifi_family = AF_UNSPEC;
	ifi.ifi_type = ARPHRD_RAWIP;
	ifi.ifi_flags = 0;
	ifi.ifi_change = 0xFFFFFFFF;

	l_netlink_message_add_header(nlm, &ifi, sizeof(ifi));
	l_netlink_message_append_u32(nlm, IFLA_LINK, parent_ifindex);
	l_netlink_message_append_string(nlm, IFLA_IFNAME, ifname);

	l_netlink_message_enter_nested(nlm, IFLA_LINKINFO);
	l_netlink_message_append_string(nlm, IFLA_INFO_KIND, RMNET_TYPE);
	l_netlink_message_enter_nested(nlm, IFLA_INFO_DATA);
	l_netlink_message_append_u16(nlm, IFLA_RMNET_MUX_ID, mux_id);
	flags.flags = RMNET_FLAGS_INGRESS_DEAGGREGATION |
			RMNET_FLAGS_INGRESS_MAP_CKSUMV5 |
			RMNET_FLAGS_EGRESS_MAP_CKSUMV5;
	flags.mask = RMNET_FLAGS_EGRESS_MAP_CKSUMV4 |
			RMNET_FLAGS_INGRESS_MAP_CKSUMV4 |
			RMNET_FLAGS_EGRESS_MAP_CKSUMV5 |
			RMNET_FLAGS_INGRESS_MAP_CKSUMV5 |
			RMNET_FLAGS_INGRESS_DEAGGREGATION;
	l_netlink_message_append(nlm, IFLA_RMNET_FLAGS, &flags, sizeof(flags));
	l_netlink_message_leave_nested(nlm);
	l_netlink_message_leave_nested(nlm);

	id = l_netlink_send(rtnl, nlm, cb, userdata, destroy);
	if (!id) {
		l_netlink_message_unref(nlm);
		return -EIO;
	}

	if (out_command_id)
		*out_command_id = id;

	return 0;
}

static void rmnet_start_next_request(void);

static void rmnet_del_link_cb(int error, uint16_t type, const void *data,
					uint32_t len, void *user_data)
{
	struct rmnet_request *req = l_queue_peek_head(request_q);

	DBG("DELLINK %u (%u/%u) complete, error: %d",
		req->netlink_id, req->current, req->n_interfaces, error);

	req->netlink_id = 0;
	req->current += 1;

	if (req->current < req->n_interfaces)
		goto next_request;

	l_queue_pop_head(request_q);
	rmnet_request_free(req);

next_request:
	if (l_queue_length(request_q) > 0)
		rmnet_start_next_request();
}

static void rmnet_new_link_cb(int error, uint16_t type, const void *data,
					uint32_t len, void *user_data)
{
	struct rmnet_request *req = l_queue_peek_head(request_q);

	DBG("NEWLINK %u (%u/%u) complete, error: %d",
		req->netlink_id, req->current + 1, req->n_interfaces, error);

	req->netlink_id = 0;

	if (!error)
		req->current += 1;

	if (error || req->canceled) {
		__rmnet_cancel_request();
		req->n_interfaces = 0;
	} else {
		if (req->current < req->n_interfaces)
			goto next_request;

		l_queue_pop_head(request_q);
	}

	if (req->new_cb)
		req->new_cb(error, req->n_interfaces,
				req->n_interfaces ? req->infos : NULL,
				req->user_data);

	rmnet_request_free(req);
next_request:
	if (l_queue_length(request_q) > 0)
		rmnet_start_next_request();
}

static void rmnet_start_next_request(void)
{
	struct rmnet_request *req = l_queue_peek_head(request_q);
	uint32_t mux_id;
	struct rmnet_ifinfo *info;

	if (!req)
		return;

	if (req->request_type == RTM_DELLINK) {
		uint32_t ifindex = req->infos[req->current].ifindex;

		L_WARN_ON(rmnet_link_del(ifindex, rmnet_del_link_cb, NULL, NULL,
					&req->netlink_id) < 0);
		DBG("Start DELLINK: ifindex: %u, interface: %u/%u, request: %u",
				ifindex, req->current,
				req->n_interfaces, req->netlink_id);
		return;
	}

	info = req->infos + req->current;
	mux_id = l_uintset_find_unused_min(mux_ids);
	info->mux_id = mux_id;
	sprintf(info->ifname, RMNET_TYPE"%u", mux_id - 1);

	L_WARN_ON(rmnet_link_new(req->parent_ifindex, mux_id, info->ifname,
					rmnet_new_link_cb, NULL, NULL,
					&req->netlink_id) < 0);

	DBG("Start NEWLINK: parent: %u, interface: %u/%u, request: %u",
			req->parent_ifindex, req->current + 1,
			req->n_interfaces, req->netlink_id);
}

int rmnet_get_interfaces(uint32_t parent_ifindex, unsigned int n_interfaces,
				rmnet_new_interfaces_func_t cb,
				void *user_data, rmnet_destroy_func_t destroy)
{
	struct rmnet_request *req;

	if (!n_interfaces || n_interfaces > MAX_MUX_IDS)
		return -EINVAL;

	if (l_uintset_size(mux_ids) > MAX_MUX_IDS - n_interfaces)
		return -ENOSPC;

	req = l_malloc(sizeof(struct rmnet_request) +
				sizeof(struct rmnet_ifinfo) * n_interfaces);
	req->parent_ifindex = parent_ifindex;
	req->new_cb = cb;
	req->user_data = user_data;
	req->destroy = destroy;
	req->id = next_request_id++;
	req->canceled = false;
	req->request_type = RTM_NEWLINK;
	req->netlink_id = 0;
	req->current = 0;
	req->n_interfaces = n_interfaces;
	memset(req->infos, 0, sizeof(struct rmnet_ifinfo) * n_interfaces);

	if (next_request_id < 0)
		next_request_id = 1;

	l_queue_push_tail(request_q, req);

	if (l_queue_length(request_q) == 1 && !dump_id)
		rmnet_start_next_request();

	return req->id;
}

int rmnet_del_interfaces(unsigned int n_interfaces,
					const struct rmnet_ifinfo *interfaces)
{
	struct rmnet_request *req;

	if (!n_interfaces || n_interfaces > MAX_MUX_IDS)
		return -EINVAL;

	req = __rmnet_del_request_new(n_interfaces, interfaces);
	l_queue_push_tail(request_q, req);

	if (l_queue_length(request_q) == 1 && !dump_id)
		rmnet_start_next_request();

	return 0;
}

int rmnet_cancel(int id)
{
	struct rmnet_request *req;

	req = l_queue_peek_head(request_q);
	if (!req)
		return -ENOENT;

	/* Simple Case: Request not yet started (not queue head) */
	if (req->id != id) {
		req = l_queue_remove_if(request_q, rmnet_request_id_matches,
						L_INT_TO_PTR(id));
		if (!req)
			return -ENOENT;

		DBG("Removing non-head of queue request %d", id);
		rmnet_request_free(req);
		return 0;
	}

	/* Harder Case: In progress, but the next request not in flight */
	if (!l_netlink_request_sent(rtnl, req->netlink_id)) {
		DBG("Removing in-progress request (not in flight) %d", id);
		req = __rmnet_cancel_request();
		l_netlink_cancel(rtnl, req->netlink_id);
		rmnet_request_free(req);

		if (l_queue_length(request_q))
			rmnet_start_next_request();

		return 0;
	}

	/*
	 * Hardest Case: In progress, next request in flight
	 * We have to wait until the next callback since the ifindex won't be
	 * known until then.
	 */
	if (req->destroy)
		req->destroy(req->user_data);

	req->new_cb = NULL;
	req->destroy = NULL;
	req->user_data = NULL;

	DBG("Setting canceled on in-progress request %d", id);
	req->canceled = true;

	return 0;
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

	if (l_queue_length(request_q) > 0)
		rmnet_start_next_request();
}

static void rmnet_link_dump_cb(int error,
				uint16_t type, const void *data,
				uint32_t len, void *user_data)
{
	struct rmnet_ifinfo info;
	struct rmnet_request *req;

	/* Check conditions that can't happen on a dump */
	if (error || type != RTM_NEWLINK)
		return;

	if (rmnet_parse_link(data, len,
				info.ifname, &info.ifindex, &info.mux_id) < 0)
		return;

	DBG("Removing existing rmnet link: %s(%u) mux_id: %u",
			info.ifname, info.ifindex, info.mux_id);
	l_uintset_put(mux_ids, info.mux_id);

	req = __rmnet_del_request_new(1, &info);
	l_queue_push_tail(request_q, req);
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

/* For NEW_LINK requests, the ifindex comes in the multicast message */
static void update_new_link_ifindex(uint16_t mux_id,
					const char ifname[static IF_NAMESIZE],
					uint32_t ifindex)
{
	struct rmnet_request *req;
	struct rmnet_ifinfo *info;

	req = l_queue_peek_head(request_q);
	if (!req || req->request_type != RTM_NEWLINK)
		return;

	info = req->infos + req->current;
	if (info->mux_id == mux_id && !strcmp(info->ifname, ifname))
		info->ifindex = ifindex;
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

	if (type == RTM_NEWLINK) {
		l_uintset_put(mux_ids, mux_id);
		update_new_link_ifindex(mux_id, ifname, ifindex);
	} else
		l_uintset_take(mux_ids, mux_id);

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
	mux_ids = l_uintset_new_from_range(1, MAX_MUX_IDS);
	request_q = l_queue_new();

	return 0;
dump_failed:
	l_netlink_destroy(rtnl);
	return r;
}

static void rmnet_exit(void)
{
	l_queue_destroy(request_q, (l_queue_destroy_func_t) rmnet_request_free);
	l_uintset_free(mux_ids);
	l_netlink_unregister(rtnl, link_notify_id);
	l_netlink_destroy(rtnl);
}

OFONO_MODULE(rmnet, rmnet_init, rmnet_exit)
