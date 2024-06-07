/*
 * oFono - Open Source Telephony
 * Copyright (C) 2016  Endocode AG
 * Copyright (C) 2018 Gemalto M2M
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_LTE_H
#define __OFONO_LTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#include <ofono/types.h>

struct ofono_lte;

struct ofono_lte_default_attach_info {
	char apn[OFONO_GPRS_MAX_APN_LENGTH + 1];
	enum ofono_gprs_proto proto;
	enum ofono_gprs_auth_method auth_method;
	char username[OFONO_GPRS_MAX_USERNAME_LENGTH + 1];
	char password[OFONO_GPRS_MAX_PASSWORD_LENGTH + 1];
};

typedef void (*ofono_lte_cb_t)(const struct ofono_error *error, void *data);

struct ofono_lte_driver {
	int (*probe)(struct ofono_lte *lte, unsigned int vendor, void *data);
	int (*probev)(struct ofono_lte *lte, unsigned int vendor, va_list args);
	void (*remove)(struct ofono_lte *lte);
	void (*set_default_attach_info)(const struct ofono_lte *lte,
			const struct ofono_lte_default_attach_info *info,
			ofono_lte_cb_t cb, void *data);
};

struct ofono_lte *ofono_lte_create(struct ofono_modem *modem,
					unsigned int vendor,
					const char *driver, ...);

void ofono_lte_register(struct ofono_lte *lte);

void ofono_lte_remove(struct ofono_lte *lte);

void ofono_lte_set_data(struct ofono_lte *lte, void *data);

void *ofono_lte_get_data(const struct ofono_lte *lte);

struct ofono_modem *ofono_lte_get_modem(const struct ofono_lte *lte);

#ifdef __cplusplus
}
#endif

#endif
