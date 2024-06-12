/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2012  Intel Corporation
 * Copyright (C) 2024  Cruise, LLC
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <net/if.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/plugin.h>
#include <ofono/modem.h>
#include <ofono/devinfo.h>
#include <ofono/call-barring.h>
#include <ofono/call-forwarding.h>
#include <ofono/call-settings.h>
#include <ofono/ussd.h>
#include <ofono/voicecall.h>
#include <ofono/netreg.h>
#include <ofono/netmon.h>
#include <ofono/sim.h>
#include <ofono/sms.h>
#include <ofono/lte.h>
#include <ofono/gprs.h>
#include <ofono/gprs-context.h>
#include <ofono/radio-settings.h>
#include <ofono/location-reporting.h>
#include <ofono/log.h>
#include <ofono/message-waiting.h>

#include <ell/ell.h>

#include <drivers/qmimodem/qmi.h>
#include <drivers/qmimodem/dms.h>
#include <drivers/qmimodem/util.h>

struct qrtrqmi_data {
	struct qmi_qrtr_node *node;
	struct qmi_service *dms;
	int main_net_ifindex;
	char main_net_name[IFNAMSIZ];
	bool have_voice : 1;
};

static void qrtrqmi_debug(const char *str, void *user_data)
{
	const char *prefix = user_data;

	ofono_info("%s%s", prefix, str);
}

/*
 * Probe the modem.  The following modem properties are expected to be set
 * in order to initialize the driver properly:
 *
 * NetworkInterface
 *   The string that contains the 'main' network device.  This can be
 *   "rmnet_ipa" on SoC systems, or "wwan0" for upstream linux systems.
 *
 * NetworkInterfaceIndex
 *   The index of the main interface given by NetworkInterface
 *
 * NetworkInterfaceKernelDriver
 *   The kernel driver that is being used by the main network device.
 *
 * Bus
 *   The bus of the modem.  Values can be "embedded", or "pci"
 */
static int qrtrqmi_probe(struct ofono_modem *modem)
{
	struct qrtrqmi_data *data;
	const char *if_driver;
	const char *ifname;
	int ifindex;
	const char *bus;

	DBG("%p", modem);

	if_driver = ofono_modem_get_string(modem,
						"NetworkInterfaceKernelDriver");
	ifname = ofono_modem_get_string(modem, "NetworkInterface");
	ifindex = ofono_modem_get_integer(modem, "NetworkInterfaceIndex");
	bus = ofono_modem_get_string(modem, "Bus");

	DBG("net: %s[%s](%d) %s", ifname, if_driver, ifindex, bus);

	if (!if_driver || !ifname || !ifindex || !bus)
		return -EPROTO;

	data = l_new(struct qrtrqmi_data, 1);
	data->main_net_ifindex =
		ofono_modem_get_integer(modem, "NetworkInterfaceIndex");
	l_strlcpy(data->main_net_name,
			ofono_modem_get_string(modem, "NetworkInterface"),
			sizeof(data->main_net_name));
	ofono_modem_set_data(modem, data);
	ofono_modem_set_capabilities(modem, OFONO_MODEM_CAPABILITY_LTE);

	return 0;
}

static void qrtrqmi_deinit(struct qrtrqmi_data *data)
{
	qmi_service_free(data->dms);
	data->dms = NULL;
	qmi_qrtr_node_free(data->node);
	data->node = NULL;
}

static void qrtrqmi_remove(struct ofono_modem *modem)
{
	struct qrtrqmi_data *data = ofono_modem_get_data(modem);

	DBG("%p", modem);

	ofono_modem_set_data(modem, NULL);

	qrtrqmi_deinit(data);
	l_free(data);
}

static void power_reset_cb(struct qmi_result *result, void *user_data)
{
	struct ofono_modem *modem = user_data;

	DBG("");

	if (qmi_result_set_error(result, NULL)) {
		qrtrqmi_deinit(ofono_modem_get_data(modem));
		ofono_modem_set_powered(modem, FALSE);
		return;
	}

	ofono_modem_set_powered(modem, TRUE);
}

static void get_oper_mode_cb(struct qmi_result *result, void *user_data)
{
	struct ofono_modem *modem = user_data;
	struct qrtrqmi_data *data = ofono_modem_get_data(modem);
	struct qmi_param *param;
	uint8_t mode;

	DBG("");

	if (qmi_result_set_error(result, NULL))
		goto error;

	if (!qmi_result_get_uint8(result, QMI_DMS_RESULT_OPER_MODE, &mode))
		goto error;

	switch (mode) {
	case QMI_DMS_OPER_MODE_ONLINE:
		param = qmi_param_new_uint8(QMI_DMS_PARAM_OPER_MODE,
						QMI_DMS_OPER_MODE_LOW_POWER);

		if (qmi_service_send(data->dms, QMI_DMS_SET_OPER_MODE, param,
					power_reset_cb, modem, NULL) > 0)
			return;

		break;
	default:
		ofono_modem_set_powered(modem, TRUE);
		return;
	}

error:
	qrtrqmi_deinit(data);
}

static void lookup_done(void *user_data)
{
	struct ofono_modem *modem = user_data;
	struct qrtrqmi_data *data = ofono_modem_get_data(modem);
	struct qmi_qrtr_node *node = data->node;

	DBG("");

	if (!qmi_qrtr_node_has_service(node, QMI_SERVICE_DMS) ||
			!qmi_qrtr_node_has_service(node, QMI_SERVICE_UIM) ||
			!qmi_qrtr_node_has_service(node, QMI_SERVICE_WDS) ||
			!qmi_qrtr_node_has_service(node, QMI_SERVICE_NAS))
		goto error;

	data->dms = qmi_qrtr_node_get_service(node, QMI_SERVICE_DMS);
	if (qmi_service_send(data->dms, QMI_DMS_GET_OPER_MODE, NULL,
					get_oper_mode_cb, modem, NULL) > 0)
		return;
error:
	qrtrqmi_deinit(data);
	ofono_modem_set_powered(modem, FALSE);
}

static int qrtrqmi_enable(struct ofono_modem *modem)
{
	struct qrtrqmi_data *data = ofono_modem_get_data(modem);
	int r;

	DBG("%p", modem);

	data->node = qmi_qrtr_node_new(0);
	if (!data->node)
		return -EIO;

	if (getenv("OFONO_QMI_DEBUG"))
		qmi_qrtr_node_set_debug(data->node, qrtrqmi_debug, "QRTR: ");

	r = qmi_qrtr_node_lookup(data->node, lookup_done, modem, NULL);
	if (!r)
		return -EINPROGRESS;

	return r;
}

static void power_disable_cb(struct qmi_result *result, void *user_data)
{
	struct ofono_modem *modem = user_data;

	DBG("");

	qrtrqmi_deinit(ofono_modem_get_data(modem));
	ofono_modem_set_powered(modem, FALSE);
}

static int qrtrqmi_disable(struct ofono_modem *modem)
{
	struct qrtrqmi_data *data = ofono_modem_get_data(modem);
	struct qmi_param *param;

	DBG("%p", modem);

	param = qmi_param_new_uint8(QMI_DMS_PARAM_OPER_MODE,
					QMI_DMS_OPER_MODE_LOW_POWER);

	if (qmi_service_send(data->dms, QMI_DMS_SET_OPER_MODE, param,
					power_disable_cb, modem, NULL))
		return -EINPROGRESS;

	qmi_param_free(param);
	qrtrqmi_deinit(data);
	return -EIO;
}

static void set_online_cb(struct qmi_result *result, void *user_data)
{
	struct cb_data *cbd = user_data;
	ofono_modem_online_cb_t cb = cbd->cb;

	DBG("");

	if (qmi_result_set_error(result, NULL))
		CALLBACK_WITH_FAILURE(cb, cbd->data);
	else
		CALLBACK_WITH_SUCCESS(cb, cbd->data);
}

static void qrtrqmi_set_online(struct ofono_modem *modem, ofono_bool_t online,
				ofono_modem_online_cb_t cb, void *user_data)
{
	struct qrtrqmi_data *data = ofono_modem_get_data(modem);
	struct cb_data *cbd = cb_data_new(cb, user_data);
	struct qmi_param *param;

	DBG("%p %s", modem, online ? "online" : "offline");

	param = qmi_param_new_uint8(QMI_DMS_PARAM_OPER_MODE,
					online ? QMI_DMS_OPER_MODE_ONLINE :
						QMI_DMS_OPER_MODE_LOW_POWER);

	if (qmi_service_send(data->dms, QMI_DMS_SET_OPER_MODE, param,
				set_online_cb, cbd, l_free) > 0)
		return;

	qmi_param_free(param);
	l_free(cbd);
}

static void qrtrqmi_pre_sim(struct ofono_modem *modem)
{
	struct qrtrqmi_data *data = ofono_modem_get_data(modem);
	struct qmi_qrtr_node *node = data->node;
	struct qmi_service *voice;

	DBG("%p", modem);

	ofono_devinfo_create(modem, 0, "qmimodem",
			qmi_qrtr_node_get_service(node, QMI_SERVICE_DMS));

	ofono_sim_create(modem, 0, "qmimodem",
			qmi_qrtr_node_get_service(node, QMI_SERVICE_DMS),
			qmi_qrtr_node_get_service(node, QMI_SERVICE_UIM));

	voice = qmi_qrtr_node_get_service(node, QMI_SERVICE_VOICE);
	if (voice) {
		data->have_voice = true;
		ofono_voicecall_create(modem, 0, "qmimodem", voice);
	}
}

static int setup_gprs_context(uint8_t mux_id, const char *interface,
				struct ofono_gprs *gprs)
{
	struct ofono_modem *modem = ofono_gprs_get_modem(gprs);
	struct qrtrqmi_data *data = ofono_modem_get_data(modem);
	struct qmi_qrtr_node *node = data->node;
	struct ofono_gprs_context *gc;

	gc = ofono_gprs_context_create(modem, 0, "qmimodem",
			qmi_qrtr_node_get_service(node, QMI_SERVICE_WDS));
	if (!gc) {
		ofono_warn("Unable to create gprs-context for: %s, %s[%u]",
				ofono_modem_get_path(modem), interface, mux_id);
		return -ENOPKG;
	}

	ofono_gprs_add_context(gprs, gc);
	ofono_gprs_context_set_interface(gc, interface);
	return 0;
}

static void setup_gprs(struct ofono_modem *modem)
{
	struct qrtrqmi_data *data = ofono_modem_get_data(modem);
	struct qmi_qrtr_node *node = data->node;
	int n_premux = ofono_modem_get_integer(modem, "NumPremuxInterfaces");
	struct ofono_gprs *gprs;
	const char *interface;
	char buf[256];
	int i;

	gprs = ofono_gprs_create(modem, 0, "qmimodem",
			qmi_qrtr_node_get_service(node, QMI_SERVICE_WDS),
			qmi_qrtr_node_get_service(node, QMI_SERVICE_NAS));
	if (!gprs) {
		ofono_warn("Unable to create gprs for: %s",
					ofono_modem_get_path(modem));
		return;
	}

	for (i = 0; i < n_premux; i++) {
		int mux_id;

		sprintf(buf, "PremuxInterface%dMuxId", i + 1);
		mux_id = ofono_modem_get_integer(modem, buf);

		sprintf(buf, "PremuxInterface%d", i + 1);
		interface = ofono_modem_get_string(modem, buf);

		setup_gprs_context(mux_id, interface, gprs);
	}
}

static void qrtrqmi_post_sim(struct ofono_modem *modem)
{
	struct qrtrqmi_data *data = ofono_modem_get_data(modem);
	struct qmi_qrtr_node *node = data->node;
	struct qmi_service *wms;

	DBG("%p", modem);

	ofono_lte_create(modem, 0, "qmimodem",
			qmi_qrtr_node_get_service(node, QMI_SERVICE_WDS));

	ofono_radio_settings_create(modem, 0, "qmimodem",
			qmi_qrtr_node_get_service(node, QMI_SERVICE_DMS),
			qmi_qrtr_node_get_service(node, QMI_SERVICE_NAS));

	wms = qmi_qrtr_node_get_service(node, QMI_SERVICE_WMS);
	if (wms) {
		struct ofono_message_waiting *mw = NULL;

		ofono_sms_create(modem, 0, "qmimodem", wms);

		if (qmi_qrtr_node_has_service(node, QMI_SERVICE_UIM))
			mw = ofono_message_waiting_create(modem);

		if (mw)
			ofono_message_waiting_register(mw);
	}

	setup_gprs(modem);
}

static void qrtrqmi_post_online(struct ofono_modem *modem)
{
	struct qrtrqmi_data *data = ofono_modem_get_data(modem);
	struct qmi_qrtr_node *node = data->node;

	DBG("%p", modem);

	ofono_netreg_create(modem, 0, "qmimodem",
			qmi_qrtr_node_get_service(node, QMI_SERVICE_NAS));
	ofono_netmon_create(modem, 0, "qmimodem",
			qmi_qrtr_node_get_service(node, QMI_SERVICE_NAS));

	if (!data->have_voice)
		return;

	ofono_ussd_create(modem, 0, "qmimodem",
			qmi_qrtr_node_get_service(node, QMI_SERVICE_VOICE));
	ofono_call_settings_create(modem, 0, "qmimodem",
			qmi_qrtr_node_get_service(node, QMI_SERVICE_VOICE));
	ofono_call_barring_create(modem, 0, "qmimodem",
			qmi_qrtr_node_get_service(node, QMI_SERVICE_VOICE));
	ofono_call_forwarding_create(modem, 0, "qmimodem",
			qmi_qrtr_node_get_service(node, QMI_SERVICE_VOICE));
}

static struct ofono_modem_driver qrtrqmi_driver = {
	.probe		= qrtrqmi_probe,
	.remove		= qrtrqmi_remove,
	.enable		= qrtrqmi_enable,
	.disable	= qrtrqmi_disable,
	.set_online	= qrtrqmi_set_online,
	.pre_sim	= qrtrqmi_pre_sim,
	.post_sim	= qrtrqmi_post_sim,
	.post_online	= qrtrqmi_post_online,
};

OFONO_MODEM_DRIVER_BUILTIN(qrtrqmi, &qrtrqmi_driver)
