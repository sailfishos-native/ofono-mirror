/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_CALL_FORWARDING_H
#define __OFONO_CALL_FORWARDING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ofono/types.h>

struct ofono_call_forwarding;

/* 27.007 Section 7.11 Call Forwarding */
struct ofono_call_forwarding_condition {
	int status;
	int cls;
	struct ofono_phone_number phone_number;
	int time;
};

typedef void (*ofono_call_forwarding_set_cb_t)(const struct ofono_error *error,
						void *data);
typedef void (*ofono_call_forwarding_query_cb_t)(
			const struct ofono_error *error, int total,
			const struct ofono_call_forwarding_condition *list,
			void *data);

struct ofono_call_forwarding_driver {
	int (*probe)(struct ofono_call_forwarding *cf, unsigned int vendor,
			void *data);
	void (*remove)(struct ofono_call_forwarding *cf);
	void (*activation)(struct ofono_call_forwarding *cf,
				int type, int cls,
				ofono_call_forwarding_set_cb_t cb, void *data);
	void (*registration)(struct ofono_call_forwarding *cf,
				int type, int cls,
				const struct ofono_phone_number *number,
				int time,
				ofono_call_forwarding_set_cb_t cb, void *data);
	void (*deactivation)(struct ofono_call_forwarding *cf,
				int type, int cls,
				ofono_call_forwarding_set_cb_t cb, void *data);
	void (*erasure)(struct ofono_call_forwarding *cf, int type, int cls,
				ofono_call_forwarding_set_cb_t cb, void *data);
	void (*query)(struct ofono_call_forwarding *cf, int type, int cls,
				ofono_call_forwarding_query_cb_t cb,
				void *data);
};

struct ofono_call_forwarding *ofono_call_forwarding_create(
						struct ofono_modem *modem,
						unsigned int vendor,
						const char *driver, void *data);

void ofono_call_forwarding_register(struct ofono_call_forwarding *cf);
void ofono_call_forwarding_remove(struct ofono_call_forwarding *cf);

void ofono_call_forwarding_set_data(struct ofono_call_forwarding *cf,
					void *data);
void *ofono_call_forwarding_get_data(struct ofono_call_forwarding *cf);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_CALL_FORWARDING_H */
