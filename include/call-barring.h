/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_CALL_BARRING_H
#define __OFONO_CALL_BARRING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ofono/types.h>

struct ofono_call_barring;

typedef void (*ofono_call_barring_set_cb_t)(const struct ofono_error *error,
						void *data);
typedef void (*ofono_call_barring_query_cb_t)(const struct ofono_error *error,
					int status, void *data);

struct ofono_call_barring_driver {
	int (*probe)(struct ofono_call_barring *cb, unsigned int vendor,
			void *data);
	void (*remove)(struct ofono_call_barring *cb);
	void (*set)(struct ofono_call_barring *barr, const char *lock,
			int enable, const char *passwd, int cls,
			ofono_call_barring_set_cb_t cb, void *data);
	void (*query)(struct ofono_call_barring *barr, const char *lock,
			int cls, ofono_call_barring_query_cb_t cb, void *data);
	void (*set_passwd)(struct ofono_call_barring *barr, const char *lock,
			const char *old_passwd, const char *new_passwd,
			ofono_call_barring_set_cb_t cb, void *data);
};

struct ofono_call_barring *ofono_call_barring_create(struct ofono_modem *modem,
							unsigned int vendor,
							const char *driver,
							void *data);

void ofono_call_barring_register(struct ofono_call_barring *cb);
void ofono_call_barring_remove(struct ofono_call_barring *cb);

void ofono_call_barring_set_data(struct ofono_call_barring *cb, void *data);
void *ofono_call_barring_get_data(struct ofono_call_barring *cb);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_CALL_BARRING_H */
