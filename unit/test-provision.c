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
#include <assert.h>
#include <errno.h>
#include <ell/ell.h>

#include <ofono/types.h>
#include <ofono/gprs-context.h>

#include "provisiondb.h"

static struct provision_db *pdb;

static void null_provision_db(const void *data)
{
	struct provision_db_entry *items;
	size_t n_items;
	int r;

	r = provision_db_lookup(NULL, "123", "345", NULL, NULL,
				&items, &n_items);
	assert(r == -EBADF);
}

static void invalid_mcc_mnc(const void *data)
{
	struct provision_db_entry *items;
	size_t n_items;
	int r;

	r = provision_db_lookup(pdb, "3444", "33", NULL, NULL,
				&items, &n_items);
	assert(r == -EINVAL);

	r = provision_db_lookup(pdb, "3ab", "33", NULL, NULL, &items, &n_items);
	assert(r == -EINVAL);

	r = provision_db_lookup(pdb, "333", "3", NULL, NULL, &items, &n_items);
	assert(r == -EINVAL);

	r = provision_db_lookup(pdb, "333", "3334", NULL, NULL,
			&items, &n_items);
	assert(r == -EINVAL);
}

struct provision_test {
	const char *mcc;
	const char *mnc;
	const char *spn;
	int result;
	size_t n_items;
	const struct provision_db_entry *items;
};

static const struct provision_db_entry alpha_contexts[] = {
	{
		.name = "Internet",
		.type = OFONO_GPRS_CONTEXT_TYPE_INTERNET,
		.proto = OFONO_GPRS_PROTO_IP,
		.apn = "internet",
		.auth_method = OFONO_GPRS_AUTH_METHOD_NONE,
	},
	{
		.name = "IMS+MMS",
		.type = OFONO_GPRS_CONTEXT_TYPE_IMS |
			OFONO_GPRS_CONTEXT_TYPE_MMS |
			OFONO_GPRS_CONTEXT_TYPE_IA,
		.apn = "imsmms",
		.proto = OFONO_GPRS_PROTO_IPV6,
		.auth_method = OFONO_GPRS_AUTH_METHOD_PAP,
		.message_center = "foobar.mmsc:80",
		.message_proxy = "mms.proxy.net",
	},
};

static const struct provision_db_entry zyx_contexts[] = {
	{
		.name = "ZYX",
		.apn = "zyx",
		.type = OFONO_GPRS_CONTEXT_TYPE_INTERNET |
			OFONO_GPRS_CONTEXT_TYPE_IA,
		.auth_method = OFONO_GPRS_AUTH_METHOD_NONE,
		.proto = OFONO_GPRS_PROTO_IP,
	},
};

static const struct provision_db_entry beta_contexts[] = {
	{
		.type = OFONO_GPRS_CONTEXT_TYPE_INTERNET |
			OFONO_GPRS_CONTEXT_TYPE_IA,
		.proto = OFONO_GPRS_PROTO_IPV4V6,
		.apn = "beta.internet",
		.auth_method = OFONO_GPRS_AUTH_METHOD_CHAP,
	},
};

static const struct provision_db_entry charlie_contexts[] = {
	{
		.type = OFONO_GPRS_CONTEXT_TYPE_INTERNET |
			OFONO_GPRS_CONTEXT_TYPE_IA,
		.proto = OFONO_GPRS_PROTO_IPV4V6,
		.apn = "charlie.internet",
		.auth_method = OFONO_GPRS_AUTH_METHOD_CHAP,
	},
};

static const struct provision_db_entry xyz_contexts[] = {
	{
		.type = OFONO_GPRS_CONTEXT_TYPE_INTERNET |
			OFONO_GPRS_CONTEXT_TYPE_IA,
		.proto = OFONO_GPRS_PROTO_IPV4V6,
		.apn = "xyz",
		.auth_method = OFONO_GPRS_AUTH_METHOD_CHAP,
	}
};

/* Make sure mccmnc not in the database isn't found */
static const struct provision_test unknown_mcc_mnc = {
	.mcc = "994",
	.mnc = "42",
	.result = -ENOENT,
};

/* Successful lookup of 'Operator Beta' settings */
static const struct provision_test lookup_beta = {
	.mcc = "999",
	.mnc = "006",
	.result = 0,
	.n_items = L_ARRAY_SIZE(beta_contexts),
	.items = beta_contexts,
};

/* Make sure two digit mnc is treated as != to 3 digit mnc */
static const struct provision_test two_digit_mnc = {
	.mcc = "999",
	.mnc = "06",
	.result = -ENOENT,
};

/*
 * Fallback to non-MVNO settings in case SPN doesn't match and an operator with
 * no SPN is found.  This follows legacy oFono behavior and allows provisioning
 * to work on modem drivers that do not support EFspn reading.
 */
static const struct provision_test fallback_no_spn = {
	.mcc = "999",
	.mnc = "005",
	.spn = "Bogus",
	.result = 0,
	.n_items = L_ARRAY_SIZE(beta_contexts),
	.items = beta_contexts,
};

/* Same as above, but with an MVNO entry for the same mcc/mnc */
static const struct provision_test fallback_no_spn_2 = {
	.mcc = "999",
	.mnc = "002",
	.spn = "Bogus",
	.result = 0,
	.n_items = L_ARRAY_SIZE(alpha_contexts),
	.items = alpha_contexts,
};

/* Successful lookup of Operator Alpha */
static const struct provision_test lookup_alpha = {
	.mcc = "999",
	.mnc = "001",
	.result = 0,
	.n_items = L_ARRAY_SIZE(alpha_contexts),
	.items = alpha_contexts,
};

/* Successful lookup of ZYX (MVNO on Alpha) */
static const struct provision_test lookup_zyx = {
	.mcc = "999",
	.mnc = "01",
	.spn = "ZYX",
	.result = 0,
	.n_items = L_ARRAY_SIZE(zyx_contexts),
	.items = zyx_contexts,
};

/*
 * Successful lookup of Charlie.  This has to be an exact SPN match since
 * no wildcard value is available
 */
static const struct provision_test lookup_charlie = {
	.mcc = "999",
	.mnc = "10",
	.spn = "Charlie",
	.result = 0,
	.n_items = L_ARRAY_SIZE(charlie_contexts),
	.items = charlie_contexts,
};

/* Successful lookup of XYZ (MVNO on Charlie) */
static const struct provision_test lookup_xyz = {
	.mcc = "999",
	.mnc = "11",
	.spn = "XYZ",
	.result = 0,
	.n_items = L_ARRAY_SIZE(xyz_contexts),
	.items = xyz_contexts,
};

/* No match with for an MCC/MNC present in the DB, but no wildcard entry */
static const struct provision_test lookup_no_match = {
	.mcc = "999",
	.mnc = "11",
	.result = -ENOENT,
};

static void provision_lookup(const void *data)
{
	const struct provision_test *test = data;
	struct provision_db_entry *items;
	size_t n_items;
	size_t i;
	int r;

	r = provision_db_lookup(pdb, test->mcc, test->mnc, test->spn, NULL,
					&items, &n_items);
	assert(r == test->result);

	if (r < 0)
		return;

	assert(n_items == test->n_items);
	for (i = 0; i < n_items; i++) {
		const struct provision_db_entry *a = items + i;
		const struct provision_db_entry *b = test->items + i;

		assert(b->type == a->type);
		assert(b->proto == a->proto);
		assert(l_streq0(b->apn, a->apn));
		assert(l_streq0(b->name, a->name));
		assert(l_streq0(b->username, a->username));
		assert(l_streq0(b->password, a->password));
		assert(b->auth_method == a->auth_method);
		assert(l_streq0(b->message_proxy, a->message_proxy));
		assert(l_streq0(b->message_center, a->message_center));
	}

	l_free(items);
}

int main(int argc, char **argv)
{
	int r;

	l_test_init(&argc, &argv);

	l_test_add("Lookup on NULL provision db", null_provision_db, NULL);
	l_test_add("MCC/MNC input validation", invalid_mcc_mnc, NULL);
	l_test_add("Unknown MCC/MNC", provision_lookup, &unknown_mcc_mnc);
	l_test_add("Successful Lookup (Beta)", provision_lookup, &lookup_beta);
	l_test_add("Two digit MNC", provision_lookup, &two_digit_mnc);
	l_test_add("Fallback no-SPN", provision_lookup, &fallback_no_spn);
	l_test_add("Fallback no-SPN#2", provision_lookup, &fallback_no_spn_2);
	l_test_add("Successful lookup (Alpha)", provision_lookup, &lookup_alpha);
	l_test_add("Successful lookup (ZYX)", provision_lookup, &lookup_zyx);
	l_test_add("Exact match (Charlie)", provision_lookup, &lookup_charlie);
	l_test_add("Exact match (XYZ)", provision_lookup, &lookup_xyz);
	l_test_add("Exact math (no match)", provision_lookup, &lookup_no_match);

	pdb = provision_db_new(UNITDIR "test-provision.db");
	assert(pdb);

	r = l_test_run();
	provision_db_free(pdb);

	return r;
}
