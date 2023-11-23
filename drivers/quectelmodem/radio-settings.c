/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2017 Intel Corporation. All rights reserved.
 *  Copyright (C) 2021 Sergey Matyukevich. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
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
static const char *qcfg_prefix[] = { "+QCFG:", NULL };

struct radio_settings_data {
	GAtChat *chat;
};

static void qcfg_query_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_radio_settings_rat_mode_query_cb_t cb = cbd->cb;
	unsigned int mode;
	struct ofono_error error;
	int nwscanseq;
	GAtResultIter iter;

	DBG("ok %d", ok);

	decode_at_error(&error, g_at_result_final_response(result));

	if (!ok) {
		cb(&error, -1, cbd->data);
		return;
	}

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+QCFG: \"nwscanseq\","))
		goto error;

	if (!g_at_result_iter_next_number(&iter, &nwscanseq))
		goto error;

	DBG("nwscanseq %d", nwscanseq);

	switch (nwscanseq) {
	case 0:
	case 4:
	case 5:
	case 12:
		mode = OFONO_RADIO_ACCESS_MODE_ANY;
		break;
	case 1:
		mode = OFONO_RADIO_ACCESS_MODE_GSM;
		break;
	case 2:
		mode = OFONO_RADIO_ACCESS_MODE_UMTS;
		break;
	case 3:
		mode = OFONO_RADIO_ACCESS_MODE_LTE;
		break;
	case 6:
	case 8:
		mode = OFONO_RADIO_ACCESS_MODE_UMTS |
			OFONO_RADIO_ACCESS_MODE_LTE;
		break;
	case 7:
	case 10:
		mode = OFONO_RADIO_ACCESS_MODE_GSM |
			OFONO_RADIO_ACCESS_MODE_LTE;
		break;
	case 9:
	case 11:
		mode = OFONO_RADIO_ACCESS_MODE_GSM |
			OFONO_RADIO_ACCESS_MODE_UMTS;
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

static void quectel_query_rat_mode(struct ofono_radio_settings *rs,
				ofono_radio_settings_rat_mode_query_cb_t cb,
				void *data)
{
	struct radio_settings_data *rsd = ofono_radio_settings_get_data(rs);
	struct cb_data *cbd = cb_data_new(cb, data);

	DBG("");

	if (g_at_chat_send(rsd->chat, "AT+QCFG=\"nwscanseq\"", qcfg_prefix,
				qcfg_query_cb, cbd, g_free) == 0) {
		CALLBACK_WITH_FAILURE(cb, -1, data);
		g_free(cbd);
	}
}

static void qcfg_modify_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_radio_settings_rat_mode_set_cb_t cb = cbd->cb;
	struct ofono_error error;

	DBG("ok %d", ok);

	decode_at_error(&error, g_at_result_final_response(result));
	cb(&error, cbd->data);
}

static void quectel_set_rat_mode(struct ofono_radio_settings *rs,
				unsigned int m,
				ofono_radio_settings_rat_mode_set_cb_t cb,
				void *data)
{
	struct radio_settings_data *rsd = ofono_radio_settings_get_data(rs);
	struct cb_data *cbd = cb_data_new(cb, data);
	int nwscanseq;
	char buf[30];

	DBG("mode %d", m);

	switch (m) {
	case OFONO_RADIO_ACCESS_MODE_ANY:
		nwscanseq = 0;
		break;
	case OFONO_RADIO_ACCESS_MODE_GSM:
		nwscanseq = 1;
		break;
	case OFONO_RADIO_ACCESS_MODE_UMTS:
		nwscanseq = 2;
		break;
	case OFONO_RADIO_ACCESS_MODE_LTE:
		nwscanseq = 3;
		break;
	case OFONO_RADIO_ACCESS_MODE_UMTS|OFONO_RADIO_ACCESS_MODE_GSM:
		nwscanseq = 9;
		break;
	case OFONO_RADIO_ACCESS_MODE_LTE|OFONO_RADIO_ACCESS_MODE_UMTS:
		nwscanseq = 6;
		break;
	case OFONO_RADIO_ACCESS_MODE_LTE|OFONO_RADIO_ACCESS_MODE_GSM:
		nwscanseq = 7;
		break;
	default:
		ofono_warn("Unhandled radio mode: %d", m);
		goto error;
	}

	snprintf(buf, sizeof(buf), "AT+QCFG=\"nwscanseq\",%u", nwscanseq);

	if (g_at_chat_send(rsd->chat, buf, none_prefix,
				qcfg_modify_cb, cbd, g_free) > 0)
		return;

error:
	CALLBACK_WITH_FAILURE(cb, data);
	g_free(cbd);
}

static void quectel_query_available_rats(struct ofono_radio_settings *rs,
			ofono_radio_settings_available_rats_query_cb_t cb,
			void *data)
{
	unsigned int available_rats = OFONO_RADIO_ACCESS_MODE_GSM
				| OFONO_RADIO_ACCESS_MODE_UMTS
				| OFONO_RADIO_ACCESS_MODE_LTE;

	CALLBACK_WITH_SUCCESS(cb, available_rats, data);
}

static void qcfg_support_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_radio_settings *rs = user_data;

	DBG("ok %d", ok);

	if (!ok) {
		ofono_radio_settings_remove(rs);
		return;
	}

	ofono_radio_settings_register(rs);
}

static int quectel_radio_settings_probe(struct ofono_radio_settings *rs,
					unsigned int vendor, void *data)
{
	GAtChat *chat = data;
	struct radio_settings_data *rsd;

	DBG("");

	rsd = g_new0(struct radio_settings_data, 1);

	rsd->chat = g_at_chat_clone(chat);

	ofono_radio_settings_set_data(rs, rsd);

	g_at_chat_send(rsd->chat, "AT+QCFG=\"nwscanseq\"", qcfg_prefix,
			qcfg_support_cb, rs, NULL);

	return 0;
}

static void quectel_radio_settings_remove(struct ofono_radio_settings *rs)
{
	struct radio_settings_data *rsd = ofono_radio_settings_get_data(rs);

	DBG("");

	ofono_radio_settings_set_data(rs, NULL);
	g_at_chat_unref(rsd->chat);
	g_free(rsd);
}

static const struct ofono_radio_settings_driver driver = {
	.probe			= quectel_radio_settings_probe,
	.remove			= quectel_radio_settings_remove,
	.query_rat_mode		= quectel_query_rat_mode,
	.set_rat_mode		= quectel_set_rat_mode,
	.query_available_rats	= quectel_query_available_rats
};

OFONO_ATOM_DRIVER_BUILTIN(radio_settings, quectelmodem, &driver)
