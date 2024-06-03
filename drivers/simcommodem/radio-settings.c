/*
 * oFono - Open Source Telephony
 * Copyright (C) 2017 Intel Corporation
 * Copyright (C) 2021 Sergey Matyukevich
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <glib.h>

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/radio-settings.h>

#include <drivers/atmodem/atutil.h>

#include "gatchat.h"
#include "gatresult.h"

static const char *none_prefix[] = { NULL };
static const char *cnmp_prefix[] = { "+CNMP:", NULL };

struct radio_settings_data {
	GAtChat *chat;
};

static void cnmp_query_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_radio_settings_rat_mode_query_cb_t cb = cbd->cb;
	unsigned int mode;
	struct ofono_error error;
	int r_mode;
	GAtResultIter iter;

	DBG("ok %d", ok);

	decode_at_error(&error, g_at_result_final_response(result));

	if (!ok) {
		cb(&error, -1, cbd->data);
		return;
	}

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+CNMP:"))
		goto error;

	if (!g_at_result_iter_next_number(&iter, &r_mode))
		goto error;

	DBG("r_mode %d", r_mode);

	switch (r_mode) {
	case 2:
		mode = OFONO_RADIO_ACCESS_MODE_ANY;
		break;
	case 13:
		mode = OFONO_RADIO_ACCESS_MODE_GSM;
		break;
	case 14:
		mode = OFONO_RADIO_ACCESS_MODE_UMTS;
		break;
	case 38:
		mode = OFONO_RADIO_ACCESS_MODE_LTE;
		break;
	default:
		CALLBACK_WITH_FAILURE(cb, -1, cbd->data);
		return;
	}

	cb(&error, mode, cbd->data);

	return;

error:
	CALLBACK_WITH_FAILURE(cb, -1, cbd->data);
}

static void simcom_query_rat_mode(struct ofono_radio_settings *rs,
				ofono_radio_settings_rat_mode_query_cb_t cb,
				void *data)
{
	struct radio_settings_data *rsd = ofono_radio_settings_get_data(rs);
	struct cb_data *cbd = cb_data_new(cb, data);

	DBG("");

	if (g_at_chat_send(rsd->chat, "AT+CNMP?", cnmp_prefix,
				cnmp_query_cb, cbd, g_free) == 0) {
		CALLBACK_WITH_FAILURE(cb, -1, data);
		g_free(cbd);
	}
}

static void cnmp_modify_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_radio_settings_rat_mode_set_cb_t cb = cbd->cb;
	struct ofono_error error;

	DBG("ok %d", ok);

	decode_at_error(&error, g_at_result_final_response(result));
	cb(&error, cbd->data);
}

static void simcom_set_rat_mode(struct ofono_radio_settings *rs,
				unsigned int m,
				ofono_radio_settings_rat_mode_set_cb_t cb,
				void *data)
{
	struct radio_settings_data *rsd = ofono_radio_settings_get_data(rs);
	struct cb_data *cbd = cb_data_new(cb, data);
	int w_mode = 2;
	char buf[20];

	DBG("mode %d", m);

	switch (m) {
	case OFONO_RADIO_ACCESS_MODE_ANY:
		w_mode = 2;
		break;
	case OFONO_RADIO_ACCESS_MODE_GSM:
		w_mode = 13;
		break;
	case OFONO_RADIO_ACCESS_MODE_UMTS:
	case OFONO_RADIO_ACCESS_MODE_UMTS|OFONO_RADIO_ACCESS_MODE_GSM:
		w_mode = 14;
		break;
	case OFONO_RADIO_ACCESS_MODE_LTE:
	case OFONO_RADIO_ACCESS_MODE_LTE|OFONO_RADIO_ACCESS_MODE_GSM:
	case OFONO_RADIO_ACCESS_MODE_LTE|OFONO_RADIO_ACCESS_MODE_UMTS:
		w_mode = 38;
		break;
	}

	snprintf(buf, sizeof(buf), "AT+CNMP=%u", w_mode);

	if (g_at_chat_send(rsd->chat, buf, none_prefix,
				cnmp_modify_cb, cbd, g_free) > 0)
		return;

	CALLBACK_WITH_FAILURE(cb, data);
	g_free(cbd);
}

static void simcom_query_available_rats(struct ofono_radio_settings *rs,
			ofono_radio_settings_available_rats_query_cb_t cb,
			void *data)
{
	unsigned int available_rats = OFONO_RADIO_ACCESS_MODE_GSM
				| OFONO_RADIO_ACCESS_MODE_UMTS
				| OFONO_RADIO_ACCESS_MODE_LTE;

	CALLBACK_WITH_SUCCESS(cb, available_rats, data);
}

static void cnmp_support_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_radio_settings *rs = user_data;

	DBG("ok %d", ok);

	if (!ok) {
		ofono_radio_settings_remove(rs);
		return;
	}

	ofono_radio_settings_register(rs);
}

static int simcom_radio_settings_probe(struct ofono_radio_settings *rs,
					unsigned int vendor, void *data)
{
	GAtChat *chat = data;
	struct radio_settings_data *rsd;

	DBG("");

	rsd = g_new0(struct radio_settings_data, 1);

	rsd->chat = g_at_chat_clone(chat);

	ofono_radio_settings_set_data(rs, rsd);

	g_at_chat_send(rsd->chat, "AT+CNMP=?", cnmp_prefix,
			cnmp_support_cb, rs, NULL);

	return 0;
}

static void simcom_radio_settings_remove(struct ofono_radio_settings *rs)
{
	struct radio_settings_data *rsd = ofono_radio_settings_get_data(rs);

	DBG("");

	ofono_radio_settings_set_data(rs, NULL);
	g_at_chat_unref(rsd->chat);
	g_free(rsd);
}

static const struct ofono_radio_settings_driver driver = {
	.probe			= simcom_radio_settings_probe,
	.remove			= simcom_radio_settings_remove,
	.query_rat_mode		= simcom_query_rat_mode,
	.set_rat_mode		= simcom_set_rat_mode,
	.query_available_rats	= simcom_query_available_rats
};

OFONO_ATOM_DRIVER_BUILTIN(radio_settings, simcommodem, &driver)
