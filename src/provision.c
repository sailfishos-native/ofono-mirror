/*
 *  oFono - Open Source Telephony
 *  Copyright (C) 2023  Cruise, LLC
 *
 *  SPDX-License-Identifier: GPL-2.0-only
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stddef.h>

#include <ofono/gprs-context.h>

#include "provisiondb.h"
#include "ofono.h"

static struct provision_db *pdb;

bool __ofono_provision_get_settings(const char *mcc,
				const char *mnc, const char *spn,
				struct provision_db_entry **settings,
				size_t *count)
{
	size_t n_contexts;
	struct provision_db_entry *contexts;
	int r;
	size_t i;
	uint32_t type;
	_auto_(l_strv_free) char **tags_filter = NULL;

	if (mcc == NULL || strlen(mcc) == 0 || mnc == NULL || strlen(mnc) == 0)
		return false;

	tags_filter = l_settings_get_string_list(__ofono_get_config(),
							"Provision",
							"TagsFilter", ',');

	r = provision_db_lookup(pdb, mcc, mnc, spn, tags_filter,
						&contexts, &n_contexts);
	if (r < 0)
		return false;

	DBG("Obtained %zd contexts for %s%s, spn: %s",
			n_contexts, mcc, mnc, spn);

	for (i = 0; i < n_contexts; i++) {
		struct provision_db_entry *ap = contexts + i;

		DBG("APN: %s, Type: %x, Proto: %x",
				ap->apn, ap->type, ap->proto);

		if (ap->type & OFONO_GPRS_CONTEXT_TYPE_MMS)
			DBG("MMS Proxy: %s, MMSC: %s", ap->message_proxy,
					ap->message_center);
	}

	/* Make sure there are no duplicates */
	for (i = 0, type = 0; i < n_contexts; i++) {
		struct provision_db_entry *ap = contexts + i;

		if (type & ap->type) {
			ofono_warn("Duplicate detected for %s%s, spn: %s",
					mcc, mnc, spn);
			l_free(contexts);
			return false;
		}

		type |= ap->type;
	}

	*count = n_contexts;
	*settings = contexts;

	return true;
}

static int provision_init(void)
{
	DBG("");

	pdb = provision_db_new_default();

	if (!pdb)
		l_warn("Unable to open provisioning database!");

	return 0;
}

static void provision_exit(void)
{
	provision_db_free(pdb);
}

OFONO_MODULE(provision, provision_init, provision_exit)
