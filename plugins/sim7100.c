/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 * Copyright (C) 2009  Collabora Ltd
 * Copyright 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * This file was originally copied from g1.c and
 * modified by Bob Ham <bob.ham@puri.sm>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <errno.h>

#include <glib.h>
#include <gatchat.h>
#include <gattty.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono.h>
#include <ofono/plugin.h>
#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/call-barring.h>
#include <ofono/call-forwarding.h>
#include <ofono/call-meter.h>
#include <ofono/call-settings.h>
#include <ofono/devinfo.h>
#include <ofono/message-waiting.h>
#include <ofono/netreg.h>
#include <ofono/phonebook.h>
#include <ofono/radio-settings.h>
#include <ofono/sim.h>
#include <ofono/sms.h>
#include <ofono/ussd.h>
#include <ofono/voicecall.h>
#include <ofono/gprs.h>
#include <ofono/gprs-context.h>

#include <drivers/atmodem/vendor.h>
#include <drivers/atmodem/atutil.h>

static const char *cfun_prefix[] = { "+CFUN:", NULL };

enum sim7x00_model {
	SIMCOM_UNKNOWN = 0,
	SIMCOM_A76XX,
};

struct sim7100_data {
	GAtChat *at;
	GAtChat *ppp;
	enum sim7x00_model model;
};

static void sim7100_debug(const char *str, void *user_data)
{
	const char *prefix = user_data;

	ofono_info("%s: %s", prefix, str);
}

/* Detect hardware, and initialize if found */
static int sim7100_probe(struct ofono_modem *modem)
{
	struct sim7100_data *data;

	DBG("");

	data = g_try_new0(struct sim7100_data, 1);
	if (data == NULL)
		return -ENOMEM;

	ofono_modem_set_data(modem, data);

	return 0;
}

static void sim7100_remove(struct ofono_modem *modem)
{
	struct sim7100_data *data = ofono_modem_get_data(modem);

	DBG("");

	if (!data)
		return;

	if (data->at)
		g_at_chat_unref(data->at);

	if (data->ppp)
		g_at_chat_unref(data->ppp);

	ofono_modem_set_data(modem, NULL);
	g_free (data);
}

static void cfun_set_on_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct sim7100_data *data = ofono_modem_get_data(modem);

	DBG("");

	if (data->model == SIMCOM_A76XX)
		ofono_modem_set_capabilities(modem, OFONO_MODEM_CAPABILITY_LTE);

	if (ok)
		ofono_modem_set_powered(modem, TRUE);
}

static void cgmm_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct sim7100_data *data = ofono_modem_get_data(modem);
	GAtResultIter iter;
	const char *model;

	if (!ok) {
		ofono_error("%s: failed to query model",
						ofono_modem_get_path(modem));
		ofono_modem_set_powered(modem, FALSE);
		return;
	}

	g_at_result_iter_init(&iter, result);

	while (g_at_result_iter_next(&iter, NULL)) {
		if (!g_at_result_iter_next_unquoted_string(&iter, &model))
			continue;

		DBG("modem model: %s", model);

		if (g_str_has_prefix(model, "A7672"))
			data->model = SIMCOM_A76XX;

		break;
	}

	switch (data->model) {
	case SIMCOM_A76XX:
		/* ignore NO CARRIER on the AT channel when disconnecting PPP */
		g_at_chat_blacklist_terminator(data->at,
					G_AT_CHAT_TERMINATOR_NO_CARRIER);
		break;
	default:
		break;
	}

	/* power up modem */
	g_at_chat_send(data->at, "AT+CFUN=4", NULL, cfun_set_on_cb, modem,
									NULL);
}

static int open_device(struct ofono_modem *modem, char *devkey, GAtChat **chat)
{
	DBG("devkey=%s", devkey);

	*chat = at_util_open_device(modem, devkey, sim7100_debug, devkey, NULL);
	if (*chat == NULL)
		return -EIO;

	return 0;
}

static int sim7100_enable(struct ofono_modem *modem)
{
	struct sim7100_data *data = ofono_modem_get_data(modem);
	int err;

	DBG("");

	err = open_device(modem, "AT", &data->at);
	if (err < 0)
		return err;

	err = open_device(modem, "PPP", &data->ppp);
	if (err < 0)
		return err;

	/* ensure modem is in a known state; verbose on, echo/quiet off */
	g_at_chat_send(data->at, "ATE0Q0V1", NULL, NULL, NULL, NULL);

	/* query modem model string */
	g_at_chat_send(data->at, "AT+CGMM", NULL, cgmm_cb, modem, NULL);

	return -EINPROGRESS;
}

static void cfun_set_off_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct sim7100_data *data = ofono_modem_get_data(modem);

	DBG("");

	g_at_chat_unref(data->ppp);
	g_at_chat_unref(data->at);
	data->at = data->ppp = NULL;

	if (ok)
		ofono_modem_set_powered(modem, FALSE);
}

static int sim7100_disable(struct ofono_modem *modem)
{
	struct sim7100_data *data = ofono_modem_get_data(modem);

	DBG("");

	/* power down modem */
	g_at_chat_cancel_all(data->ppp);
	g_at_chat_cancel_all(data->at);
	g_at_chat_unregister_all(data->ppp);
	g_at_chat_unregister_all(data->at);
	g_at_chat_send(data->at, "AT+CFUN=0", NULL, cfun_set_off_cb,
								modem, NULL);

	return -EINPROGRESS;
}

static void sim7100_pre_sim(struct ofono_modem *modem)
{
	struct sim7100_data *data = ofono_modem_get_data(modem);
	struct ofono_sim *sim;

	DBG("");

	ofono_devinfo_create(modem, 0, "atmodem", data->at);

	switch (data->model) {
	case SIMCOM_A76XX:
		sim = ofono_sim_create(modem, OFONO_VENDOR_SIMCOM_A76XX,
							"atmodem", data->at);
		ofono_voicecall_create(modem, 0, "atmodem", data->at);
		break;
	default:
		sim = ofono_sim_create(modem, 0, "atmodem", data->at);
		ofono_voicecall_create(modem, OFONO_VENDOR_SIMCOM,
							"atmodem", data->at);
		break;
	}

	if (sim)
		ofono_sim_inserted_notify(sim, TRUE);
}

static void sim7100_post_sim(struct ofono_modem *modem)
{
	struct sim7100_data *data = ofono_modem_get_data(modem);
	struct ofono_message_waiting *mw;
	struct ofono_gprs *gprs = NULL;
	struct ofono_gprs_context *gc = NULL;

	DBG("");

	ofono_ussd_create(modem, 0, "atmodem", data->at);
	ofono_call_forwarding_create(modem, 0, "atmodem", data->at);
	ofono_call_settings_create(modem, 0, "atmodem", data->at);
	ofono_call_meter_create(modem, 0, "atmodem", data->at);
	ofono_call_barring_create(modem, 0, "atmodem", data->at);
	ofono_phonebook_create(modem, 0, "atmodem", data->at);

	switch (data->model) {
	case SIMCOM_A76XX:
		ofono_netreg_create(modem, OFONO_VENDOR_SIMCOM_A76XX,
							"atmodem", data->at);
		ofono_sms_create(modem, OFONO_VENDOR_SIMCOM_A76XX,
							"atmodem", data->at);
		ofono_radio_settings_create(modem, 0, "simcommodem", data->at);
		gprs = ofono_gprs_create(modem, OFONO_VENDOR_SIMCOM_A76XX,
							"atmodem", data->at);
		ofono_lte_create(modem, OFONO_VENDOR_SIMCOM_A76XX,
							"atmodem", data->at);
		break;
	default:
		ofono_netreg_create(modem, 0, "atmodem", data->at);
		ofono_sms_create(modem, OFONO_VENDOR_SIMCOM, "atmodem",
								data->at);
		gprs = ofono_gprs_create(modem, 0, "atmodem", data->at);
		break;
	}

	gc = ofono_gprs_context_create(modem, 0, "atmodem", data->ppp);

	if (gprs && gc)
		ofono_gprs_add_context(gprs, gc);

	mw = ofono_message_waiting_create(modem);
	if (mw)
		ofono_message_waiting_register(mw);
}

static void set_online_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_modem_online_cb_t cb = cbd->cb;
	struct ofono_error error;

	DBG("ok: %i", ok);

	decode_at_error(&error, g_at_result_final_response(result));
	cb(&error, cbd->data);
}

static void sim7100_set_online(struct ofono_modem *modem, ofono_bool_t online,
				ofono_modem_online_cb_t cb, void *user_data)
{
	struct sim7100_data *data = ofono_modem_get_data(modem);
	struct cb_data *cbd = cb_data_new(cb, user_data);
	char const *command = online ? "AT+CFUN=1" : "AT+CFUN=4";

	DBG("%s", online ? "online" : "offline");

	if (g_at_chat_send(data->at, command, cfun_prefix, set_online_cb, cbd,
				g_free) > 0)
		return;

	CALLBACK_WITH_FAILURE(cb, cbd->data);

	g_free(cbd);
}

static struct ofono_modem_driver sim7100_driver = {
	.probe		= sim7100_probe,
	.remove		= sim7100_remove,
	.enable		= sim7100_enable,
	.disable	= sim7100_disable,
	.set_online	= sim7100_set_online,
	.pre_sim	= sim7100_pre_sim,
	.post_sim	= sim7100_post_sim,
};

OFONO_MODEM_DRIVER_BUILTIN(sim7100, &sim7100_driver)
