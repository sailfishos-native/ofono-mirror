/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_STK_H
#define __OFONO_STK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#include <ofono/types.h>

struct ofono_stk;

typedef void (*ofono_stk_envelope_cb_t)(const struct ofono_error *error,
					const unsigned char *rdata,
					int length, void *data);

typedef void (*ofono_stk_generic_cb_t)(const struct ofono_error *error,
					void *data);

struct ofono_stk_driver {
	unsigned int flags;
	int (*probe)(struct ofono_stk *stk, unsigned int vendor, void *data);
	int (*probev)(struct ofono_stk *stk, unsigned int vendor, va_list args);
	void (*remove)(struct ofono_stk *stk);
	void (*envelope)(struct ofono_stk *stk,
				int length, const unsigned char *command,
				ofono_stk_envelope_cb_t cb, void *data);
	void (*terminal_response)(struct ofono_stk *stk,
					int length, const unsigned char *resp,
					ofono_stk_generic_cb_t cb, void *data);
	void (*user_confirmation)(struct ofono_stk *stk, ofono_bool_t confirm);
};

struct ofono_stk *ofono_stk_create(struct ofono_modem *modem,
					unsigned int vendor,
					const char *driver, ...);

void ofono_stk_register(struct ofono_stk *stk);
void ofono_stk_remove(struct ofono_stk *stk);

void ofono_stk_set_data(struct ofono_stk *stk, void *data);
void *ofono_stk_get_data(struct ofono_stk *stk);

void ofono_stk_proactive_command_notify(struct ofono_stk *stk,
					int length, const unsigned char *pdu);

void ofono_stk_proactive_session_end_notify(struct ofono_stk *stk);

void ofono_stk_proactive_command_handled_notify(struct ofono_stk *stk,
						int length,
						const unsigned char *pdu);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_STK_H */
