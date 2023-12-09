/*
 *  oFono - Open Source Telephony
 *  Copyright (C) 2023  Cruise, LLC
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

struct ofono_gprs_provision_data;
struct provision_db;

struct provision_db *provision_db_new(const char *pathname);
struct provision_db *provision_db_new_default(void);
void provision_db_free(struct provision_db *pdb);

int provision_db_lookup(struct provision_db *pdb,
			const char *mcc, const char *mnc, const char *spn,
			struct ofono_gprs_provision_data **items,
			size_t *n_items);
