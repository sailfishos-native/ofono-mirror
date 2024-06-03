/*
 * oFono - Open Source Telephony
 * Copyright (C) 2010  ST-Ericsson AB
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

typedef void (*caif_rtnl_create_cb_t) (int index, const char *ifname,
							void *user_data);

extern int caif_rtnl_create_interface(int type, int connid, int loop,
				caif_rtnl_create_cb_t cb, void *user_data);
extern int caif_rtnl_delete_interface(int index);

extern int caif_rtnl_init(void);
extern void caif_rtnl_exit(void);
