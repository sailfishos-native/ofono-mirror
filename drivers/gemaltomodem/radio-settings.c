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

#include "models.h"

static const char *none_prefix[] = { NULL };
static const char *sxrat_prefix[] = { "^SXRAT:", NULL };

struct radio_settings_data {
	GAtChat *chat;
};

static void gemalto_query_available_rats(struct ofono_radio_settings *rs,
			ofono_radio_settings_available_rats_query_cb_t cb,
			void *data)
{
	unsigned int available_rats;
	struct ofono_modem *modem = ofono_radio_settings_get_modem(rs);
	const char *model = ofono_modem_get_string(modem, "Model");

	available_rats = OFONO_RADIO_ACCESS_MODE_GSM
				| OFONO_RADIO_ACCESS_MODE_UMTS;

	if (!g_strcmp0(model, GEMALTO_MODEL_ALS3_PLS8x) ||
				!g_strcmp0(model, GEMALTO_MODEL_ELS81x))
		available_rats |= OFONO_RADIO_ACCESS_MODE_LTE;

	CALLBACK_WITH_SUCCESS(cb, available_rats, data);
}

static void sxrat_query_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_radio_settings_rat_mode_query_cb_t cb = cbd->cb;
	struct ofono_radio_settings *rs = cbd->data;
	struct ofono_modem *modem = ofono_radio_settings_get_modem(rs);
	const char *model = ofono_modem_get_string(modem, "Model");
	unsigned int mode;
	struct ofono_error error;
	GAtResultIter iter;

	DBG("ok %d", ok);

	decode_at_error(&error, g_at_result_final_response(result));

	if (!ok) {
		cb(&error, -1, cbd->data);
		return;
	}

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "^SXRAT:"))
		goto error;

	if (!g_strcmp0(model, GEMALTO_MODEL_ALS3_PLS8x) ||
				!g_strcmp0(model, GEMALTO_MODEL_ELS81x)) {
		int value, pref1, pref2;

		if (!g_at_result_iter_next_number(&iter, &value))
			goto error;

		g_at_result_iter_next_number_default(&iter, -1, &pref1);
		g_at_result_iter_next_number_default(&iter, -1, &pref2);

		DBG("mode %d pref1 %d pref2 %d", value, pref1, pref2);

		switch (value) {
		case 0:
			mode = OFONO_RADIO_ACCESS_MODE_GSM;
			break;
		case 1:
			mode = OFONO_RADIO_ACCESS_MODE_GSM |
				OFONO_RADIO_ACCESS_MODE_UMTS;
			break;
		case 2:
			mode = OFONO_RADIO_ACCESS_MODE_UMTS;
			break;
		case 3:
			mode = OFONO_RADIO_ACCESS_MODE_LTE;
			break;
		case 4:
			mode = OFONO_RADIO_ACCESS_MODE_UMTS |
				OFONO_RADIO_ACCESS_MODE_LTE;
			break;
		case 5:
			mode = OFONO_RADIO_ACCESS_MODE_GSM |
				OFONO_RADIO_ACCESS_MODE_LTE;
			break;
		case 6:
			mode = OFONO_RADIO_ACCESS_MODE_ANY;
			break;
		default:
			CALLBACK_WITH_FAILURE(cb, -1, cbd->data);
			return;
		}
	} else if (!g_strcmp0(model, GEMALTO_MODEL_EHS5_E)) {
		int act, act_pref;

		if (!g_at_result_iter_next_number(&iter, &act))
			goto error;

		g_at_result_iter_next_number_default(&iter, -1, &act_pref);

		DBG("act %d act_pref %d", act, act_pref);

		switch (act) {
		case 0:
			mode = OFONO_RADIO_ACCESS_MODE_GSM;
			break;
		case 1:
			mode = OFONO_RADIO_ACCESS_MODE_GSM |
				OFONO_RADIO_ACCESS_MODE_UMTS;
			break;
		case 2:
			mode = OFONO_RADIO_ACCESS_MODE_UMTS;
			break;
		default:
			CALLBACK_WITH_FAILURE(cb, -1, cbd->data);
			return;
		}
	} else
		goto error;

	cb(&error, mode, cbd->data);

	return;

error:
	CALLBACK_WITH_FAILURE(cb, -1, cbd->data);
}

static void gemalto_query_rat_mode(struct ofono_radio_settings *rs,
				ofono_radio_settings_rat_mode_query_cb_t cb,
				void *data)
{
	struct radio_settings_data *rsd = ofono_radio_settings_get_data(rs);
	struct cb_data *cbd = cb_data_new(cb, data);

	DBG("");

	if (g_at_chat_send(rsd->chat, "AT^SXRAT?", sxrat_prefix,
				sxrat_query_cb, cbd, g_free) == 0) {
		CALLBACK_WITH_FAILURE(cb, -1, data);
		g_free(cbd);
	}
}

static void sxrat_modify_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_radio_settings_rat_mode_set_cb_t cb = cbd->cb;
	struct ofono_error error;

	DBG("ok %d", ok);

	decode_at_error(&error, g_at_result_final_response(result));
	cb(&error, cbd->data);
}

static void gemalto_set_rat_mode(struct ofono_radio_settings *rs,
				unsigned int m,
				ofono_radio_settings_rat_mode_set_cb_t cb,
				void *data)
{
	struct radio_settings_data *rsd = ofono_radio_settings_get_data(rs);
	struct cb_data *cbd = cb_data_new(cb, data);
	struct ofono_modem *modem = ofono_radio_settings_get_modem(rs);
	const char *model = ofono_modem_get_string(modem, "Model");
	char buf[20];

	DBG("mode %d", m);

	if (!g_strcmp0(model, GEMALTO_MODEL_ALS3_PLS8x) ||
				!g_strcmp0(model, GEMALTO_MODEL_ELS81x)) {
		int val = 6, p1 = 3, p2 = 2;

		switch (m) {
		case OFONO_RADIO_ACCESS_MODE_ANY:
			val = 6;
			p1 = 3;
			p2 = 2;
			break;
		case OFONO_RADIO_ACCESS_MODE_GSM:
			val = 0;
			break;
		case OFONO_RADIO_ACCESS_MODE_UMTS:
			val = 2;
			break;
		case OFONO_RADIO_ACCESS_MODE_LTE:
			val = 3;
			break;
		case OFONO_RADIO_ACCESS_MODE_UMTS|OFONO_RADIO_ACCESS_MODE_GSM:
			val = 1;
			p1 = 2;
			break;
		case OFONO_RADIO_ACCESS_MODE_LTE|OFONO_RADIO_ACCESS_MODE_UMTS:
			val = 4;
			p1 = 3;
			break;
		case OFONO_RADIO_ACCESS_MODE_LTE|OFONO_RADIO_ACCESS_MODE_GSM:
			val = 5;
			p1 = 3;
			break;
		}

		if (val == 6)
			snprintf(buf, sizeof(buf), "AT^SXRAT=%u,%u,%u",
								val, p1, p2);
		else if (val == 1 || val == 4 || val == 5)
			snprintf(buf, sizeof(buf), "AT^SXRAT=%u,%u", val, p1);
		else
			snprintf(buf, sizeof(buf), "AT^SXRAT=%u", val);
	} else if (!g_strcmp0(model, GEMALTO_MODEL_EHS5_E)) {
		int act = 1, act_pref = 2;

		switch (m) {
		case OFONO_RADIO_ACCESS_MODE_ANY:
		case OFONO_RADIO_ACCESS_MODE_UMTS|OFONO_RADIO_ACCESS_MODE_GSM:
			act = 1;
			act_pref = 2;
			break;
		case OFONO_RADIO_ACCESS_MODE_GSM:
			act = 0;
			break;
		case OFONO_RADIO_ACCESS_MODE_UMTS:
			act = 2;
			break;
		case OFONO_RADIO_ACCESS_MODE_LTE:
		case OFONO_RADIO_ACCESS_MODE_LTE|OFONO_RADIO_ACCESS_MODE_UMTS:
		case OFONO_RADIO_ACCESS_MODE_LTE|OFONO_RADIO_ACCESS_MODE_GSM:
			goto error;
		}

		if (act == 1)
			snprintf(buf, sizeof(buf), "AT^SXRAT=%u,%u",
								act, act_pref);
		else
			snprintf(buf, sizeof(buf), "AT^SXRAT=%u", act);
	} else
		goto error;

	if (g_at_chat_send(rsd->chat, buf, none_prefix,
				sxrat_modify_cb, cbd, g_free) > 0)
		return;

error:
	CALLBACK_WITH_FAILURE(cb, data);
	g_free(cbd);
}

static void sxrat_support_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_radio_settings *rs = user_data;

	DBG("ok %d", ok);

	if (!ok) {
		ofono_radio_settings_remove(rs);
		return;
	}

	ofono_radio_settings_register(rs);
}

static int gemalto_radio_settings_probe(struct ofono_radio_settings *rs,
					unsigned int vendor, void *data)
{
	GAtChat *chat = data;
	struct radio_settings_data *rsd;

	DBG("");

	rsd = g_new0(struct radio_settings_data, 1);

	rsd->chat = g_at_chat_clone(chat);

	ofono_radio_settings_set_data(rs, rsd);

	g_at_chat_send(rsd->chat, "AT^SXRAT=?", sxrat_prefix,
			sxrat_support_cb, rs, NULL);

	return 0;
}

static void gemalto_radio_settings_remove(struct ofono_radio_settings *rs)
{
	struct radio_settings_data *rsd = ofono_radio_settings_get_data(rs);

	DBG("");

	ofono_radio_settings_set_data(rs, NULL);
	g_at_chat_unref(rsd->chat);
	g_free(rsd);
}

static const struct ofono_radio_settings_driver driver = {
	.probe			= gemalto_radio_settings_probe,
	.remove			= gemalto_radio_settings_remove,
	.query_available_rats	= gemalto_query_available_rats,
	.query_rat_mode		= gemalto_query_rat_mode,
	.set_rat_mode		= gemalto_set_rat_mode
};

OFONO_ATOM_DRIVER_BUILTIN(radio_settings, gemaltomodem, &driver)
