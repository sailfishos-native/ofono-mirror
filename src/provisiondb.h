/*
 *  oFono - Open Source Telephony
 *  Copyright (C) 2023  Cruise, LLC
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdint.h>

struct provision_db;

struct provision_db_entry {
	uint32_t type; /* Multiple types can be set in a bitmap */
	enum ofono_gprs_proto proto;
	const char *name;
	const char *apn;
	const char *username;
	const char *password;
	enum ofono_gprs_auth_method auth_method;
	const char *message_proxy;
	const char *message_center;
	const char *tags;
};

struct provision_db *provision_db_new(const char *pathname);
struct provision_db *provision_db_new_default(void);
void provision_db_free(struct provision_db *pdb);

int provision_db_lookup(struct provision_db *pdb,
			const char *mcc, const char *mnc, const char *spn,
			char **tags_filter,
			struct provision_db_entry **items,
			size_t *n_items);
