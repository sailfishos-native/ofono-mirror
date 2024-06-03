/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_MESSAGE_WAITING_H
#define __OFONO_MESSAGE_WAITING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ofono/types.h>

struct ofono_message_waiting;

struct ofono_message_waiting *ofono_message_waiting_create(
						struct ofono_modem *modem);
void ofono_message_waiting_register(struct ofono_message_waiting *mw);
void ofono_message_waiting_remove(struct ofono_message_waiting *mw);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_MESSAGE_WAITING_H */
