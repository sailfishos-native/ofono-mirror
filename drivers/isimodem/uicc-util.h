/*
 * oFono - Open Source Telephony
 * Copyright (C) 2011  ST-Ericsson AB
 * Copyright (C) 2011  Nokia Corporation and/or its subsidiary(-ies)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __ISIMODEM_UICC_UTIL_H
#define __ISIMODEM_UICC_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <gisi/client.h>

struct uicc_sim_data;

struct uicc_sim_application {
	int id;
	uint8_t type;
	uint8_t status;
	uint8_t length;

	struct uicc_sim_data *sim;
};

struct uicc_sim_data {
	GIsiClient *client;
	unsigned flags;
	int app_id;
	int app_type;
	uint8_t client_id;

	GIsiVersion version;

	gboolean server_running;

	gboolean pin_state_received;
	gboolean passwd_required;

	/* Application state */
	gboolean uicc_app_started;
	uint8_t trying_app_id;
	uint8_t trying_app_type;
	GHashTable *app_table;

	uint8_t pin1_id;
	uint8_t pin2_id;
};

gboolean uicc_get_fileid_path(struct uicc_sim_data *sd,
				int *mf_path,
				int *df1_path,
				int *df2_path,
				unsigned char *df_len,
				int fileid);

uint8_t uicc_get_sfi(const int fileid);

#ifdef __cplusplus
};
#endif

#endif /* __ISIMODEM_UICC_UTIL_H */
