/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_SIM_AUTH_H
#define __OFONO_SIM_AUTH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <ofono/types.h>

struct ofono_sim_auth;

struct ofono_sim_auth *ofono_sim_auth_create(struct ofono_modem *modem);
void ofono_sim_auth_remove(struct ofono_sim_auth *sa);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_SIM_AUTH_H */
