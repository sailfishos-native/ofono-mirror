/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_CBS_H
#define __OFONO_CBS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ofono/types.h>

struct ofono_cbs;

typedef void (*ofono_cbs_set_cb_t)(const struct ofono_error *error,
					void *data);

struct ofono_cbs_driver {
	int (*probe)(struct ofono_cbs *cbs, unsigned int vendor, void *data);
	void (*remove)(struct ofono_cbs *cbs);
	void (*set_topics)(struct ofono_cbs *cbs, const char *topics,
				ofono_cbs_set_cb_t cb, void *data);
	void (*clear_topics)(struct ofono_cbs *cbs,
				ofono_cbs_set_cb_t cb, void *data);
};

void ofono_cbs_notify(struct ofono_cbs *cbs, const unsigned char *pdu, int len);

struct ofono_cbs *ofono_cbs_create(struct ofono_modem *modem,
					unsigned int vendor,
					const char *driver, void *data);

void ofono_cbs_register(struct ofono_cbs *cbs);
void ofono_cbs_remove(struct ofono_cbs *cbs);

void ofono_cbs_set_data(struct ofono_cbs *cbs, void *data);
void *ofono_cbs_get_data(struct ofono_cbs *cbs);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_CBS_H */
