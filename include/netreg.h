/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_NETREG_H
#define __OFONO_NETREG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ofono/types.h>

struct ofono_netreg;

/* Theoretical limit is 16, but each GSM char can be encoded into
 *  * 3 UTF8 characters resulting in 16*3=48 chars
 *   */
#define OFONO_MAX_OPERATOR_NAME_LENGTH 63

struct ofono_network_operator {
	char name[OFONO_MAX_OPERATOR_NAME_LENGTH + 1];
	char mcc[OFONO_MAX_MCC_LENGTH + 1];
	char mnc[OFONO_MAX_MNC_LENGTH + 1];
	int status;
	int tech;
};

typedef void (*ofono_netreg_operator_cb_t)(const struct ofono_error *error,
					const struct ofono_network_operator *op,
					void *data);

typedef void (*ofono_netreg_register_cb_t)(const struct ofono_error *error,
						void *data);

typedef void (*ofono_netreg_operator_list_cb_t)(const struct ofono_error *error,
				int total,
				const struct ofono_network_operator *list,
				void *data);

typedef void (*ofono_netreg_status_cb_t)(const struct ofono_error *error,
					int status, int lac, int ci, int tech,
					void *data);

typedef void (*ofono_netreg_strength_cb_t)(const struct ofono_error *error,
						int strength, void *data);

/* Network related functions, including registration status, operator selection
 * and signal strength indicators.
 *
 * It is up to the plugin to implement CSQ polling if the modem does not support
 * vendor extensions for signal strength notification.
 */
struct ofono_netreg_driver {
	int (*probe)(struct ofono_netreg *netreg, unsigned int vendor,
			void *data);
	void (*remove)(struct ofono_netreg *netreg);
	void (*registration_status)(struct ofono_netreg *netreg,
			ofono_netreg_status_cb_t cb, void *data);
	void (*current_operator)(struct ofono_netreg *netreg,
			ofono_netreg_operator_cb_t cb, void *data);
	void (*list_operators)(struct ofono_netreg *netreg,
			ofono_netreg_operator_list_cb_t cb, void *data);
	void (*register_auto)(struct ofono_netreg *netreg,
			ofono_netreg_register_cb_t cb, void *data);
	void (*register_manual)(struct ofono_netreg *netreg,
				const char *mcc, const char *mnc,
				ofono_netreg_register_cb_t cb, void *data);
	void (*strength)(struct ofono_netreg *netreg,
			ofono_netreg_strength_cb_t, void *data);
};

void ofono_netreg_strength_notify(struct ofono_netreg *netreg, int strength);
void ofono_netreg_status_notify(struct ofono_netreg *netreg, int status,
					int lac, int ci, int tech);
void ofono_netreg_time_notify(struct ofono_netreg *netreg,
				struct ofono_network_time *info);

struct ofono_netreg *ofono_netreg_create(struct ofono_modem *modem,
						unsigned int vendor,
						const char *driver,
						void *data);

void ofono_netreg_register(struct ofono_netreg *netreg);
void ofono_netreg_remove(struct ofono_netreg *netreg);

void ofono_netreg_set_data(struct ofono_netreg *netreg, void *data);
void *ofono_netreg_get_data(struct ofono_netreg *netreg);

int ofono_netreg_get_location(struct ofono_netreg *netreg);
int ofono_netreg_get_cellid(struct ofono_netreg *netreg);
int ofono_netreg_get_status(struct ofono_netreg *netreg);
int ofono_netreg_get_technology(struct ofono_netreg *netreg);
const char *ofono_netreg_get_mcc(struct ofono_netreg *netreg);
const char *ofono_netreg_get_mnc(struct ofono_netreg *netreg);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_NETREG_H */
