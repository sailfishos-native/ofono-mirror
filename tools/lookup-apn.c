/*
 *  oFono - Open Source Telephony
 *  Copyright (C) 2008-2011  Intel Corporation. All rights reserved.
 *  Copyright (C) 2023  Cruise, LLC
 *
 *  SPDX-License-Identifier: GPL-2.0-only
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>

#include <ell/ell.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/types.h>
#include <ofono/gprs-context.h>

#include "provisiondb.h"

static const char *option_file;

static int lookup_apn(const char *match_mcc, const char *match_mnc,
							const char *match_spn)
{
	struct provision_db *pdb;
	struct provision_db_entry *contexts;
	size_t n_contexts;
	int r;
	size_t i;

	if (option_file) {
		fprintf(stdout, "Opening database at: '%s'\n", option_file);
		pdb = provision_db_new(option_file);
	} else {
		fprintf(stdout, "Opening database in default location\n");
		pdb = provision_db_new_default();
	}

	if (!pdb) {
		fprintf(stdout, "Database opening failed\n");
		return -EIO;
	}

	fprintf(stdout, "Searching for info for network: %s%s, spn: %s\n",
			match_mcc, match_mnc, match_spn ? match_spn : "<None>");

	r = provision_db_lookup(pdb, match_mcc, match_mnc, match_spn,
					&contexts, &n_contexts);
	if (r < 0) {
		fprintf(stderr, "Unable to lookup: %s\n", strerror(-r));
		return r;
	}

	for (i = 0; i < n_contexts; i++) {
		struct provision_db_entry *ap = contexts + i;

		fprintf(stdout, "\nName: %s\n", ap->name);
		fprintf(stdout, "APN: %s\n", ap->apn);
		fprintf(stdout, "Type: %x\n", ap->type);
		fprintf(stdout, "Proto: %x\n", ap->proto);

		if (ap->username)
			fprintf(stdout, "Username: %s\n", ap->username);

		if (ap->password)
			fprintf(stdout, "Password: %s\n", ap->password);

		if (ap->type & OFONO_GPRS_CONTEXT_TYPE_MMS) {
			if (ap->message_proxy)
				fprintf(stdout, "Message Proxy: %s\n",
							ap->message_proxy);
			fprintf(stdout, "Message Center: %s\n",
							ap->message_center);
		}
	}

	l_free(contexts);

	provision_db_free(pdb);

	return 0;
}

static void usage(void)
{
	printf("lookup-apn\nUsage:\n");
	printf("lookup-apn [options] <mcc> <mnc> [spn]\n");
	printf("Options:\n"
			"\t-v, --version	Show version\n"
			"\t-f, --file		Provision DB file to use\n"
			"\t-h, --help		Show help options\n");
}

static const struct option options[] = {
	{ "version",	no_argument,		NULL, 'v' },
	{ "help",	no_argument,		NULL, 'h' },
	{ "file",	required_argument,	NULL, 'f' },
	{ },
};

int main(int argc, char **argv)
{
	for (;;) {
		int opt = getopt_long(argc, argv, "f:vh", options, NULL);

		if (opt < 0)
			break;

		switch (opt) {
		case 'f':
			option_file = optarg;
			break;
		case 'v':
			printf("%s\n", VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage();
			return EXIT_SUCCESS;
		default:
			return EXIT_FAILURE;
		}
	}

	if (argc - optind > 3) {
		fprintf(stderr, "Invalid command line parameters\n");
		return EXIT_FAILURE;
	}

	if (argc - optind < 2) {
		fprintf(stderr, "Missing MCC MNC parameters\n");
		return EXIT_FAILURE;
	}

	return lookup_apn(argv[optind], argv[optind + 1],
			argc - optind == 3 ? argv[optind + 2] : NULL);
}
