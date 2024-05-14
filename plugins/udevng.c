/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2008-2011  Intel Corporation. All rights reserved.
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

#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <libudev.h>

#include <glib.h>
#include <ell/ell.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/plugin.h>
#include <ofono/modem.h>
#include <ofono/log.h>

enum modem_type {
	MODEM_TYPE_USB,
	MODEM_TYPE_SERIAL,
	MODEM_TYPE_PCIE,
	MODEM_TYPE_EMBEDDED,
	MODEM_TYPE_MHI,
};

struct modem_info {
	char *syspath;
	char *devname;
	char *driver;
	char *vendor;
	char *model;
	enum modem_type type;
	union {
		GSList *devices;
		struct serial_device_info *serial;
	};
	struct ofono_modem *modem;
	const char *sysattr;
};

struct device_info {
	char *devnode;
	char *interface;
	char *number;
	char *label;
	char *sysattr;
	char *kernel_driver;
	struct udev_device *udev_device;
};

struct serial_device_info {
	char *devpath;
	char *devnode;
	char *subsystem;
	struct udev_device *dev;
};

static const char *get_ifname(const struct device_info *info)
{
	struct udev_device *udev_device = info->udev_device;
	const char *net_name;

	net_name = udev_device_get_property_value(udev_device, "ID_NET_NAME");
	if (net_name)
		return net_name;

	net_name = udev_device_get_property_value(udev_device, "INTERFACE");
	if (net_name)
		return net_name;

	/* Fall back to using sysname (M: field in udevadm) */
	return udev_device_get_sysname(udev_device);
}

static gboolean setup_isi(struct modem_info *modem)
{
	const char *node = NULL;
	int addr = 0;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;

		DBG("%s %s %s %s %s", info->devnode, info->interface,
				info->number, info->label, info->sysattr);

		if (g_strcmp0(info->sysattr, "820") == 0) {
			if (g_strcmp0(info->interface, "2/254/0") == 0)
				addr = 16;

			node = get_ifname(info);
		}
	}

	if (node == NULL)
		return FALSE;

	DBG("interface=%s address=%d", node, addr);

	ofono_modem_set_string(modem->modem, "Interface", node);
	ofono_modem_set_integer(modem->modem, "Address", addr);

	return TRUE;
}

static gboolean setup_mbm(struct modem_info *modem)
{
	const char *mdm = NULL, *app = NULL, *network = NULL, *gps = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;

		DBG("%s %s %s %s %s", info->devnode, info->interface,
				info->number, info->label, info->sysattr);

		if (g_str_has_suffix(info->sysattr, "Modem") == TRUE ||
				g_str_has_suffix(info->sysattr,
							"Modem 2") == TRUE) {
			if (mdm == NULL)
				mdm = info->devnode;
			else
				app = info->devnode;
		} else if (g_str_has_suffix(info->sysattr,
						"GPS Port") == TRUE ||
				g_str_has_suffix(info->sysattr,
						"Module NMEA") == TRUE) {
			gps = info->devnode;
		} else if (g_str_has_suffix(info->sysattr,
						"Network Adapter") == TRUE ||
				g_str_has_suffix(info->sysattr,
						"gw") == TRUE ||
				g_str_has_suffix(info->sysattr,
						"NetworkAdapter") == TRUE) {
			network = get_ifname(info);
		}
	}

	if (mdm == NULL || app == NULL)
		return FALSE;

	DBG("modem=%s data=%s network=%s gps=%s", mdm, app, network, gps);

	ofono_modem_set_string(modem->modem, "ModemDevice", mdm);
	ofono_modem_set_string(modem->modem, "DataDevice", app);
	ofono_modem_set_string(modem->modem, "GPSDevice", gps);
	ofono_modem_set_string(modem->modem, "NetworkInterface", network);

	return TRUE;
}

static gboolean setup_hso(struct modem_info *modem)
{
	const char *ctl = NULL, *app = NULL, *mdm = NULL, *net = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;

		DBG("%s %s %s %s %s", info->devnode, info->interface,
				info->number, info->label, info->sysattr);

		if (g_strcmp0(info->sysattr, "Control") == 0)
			ctl = info->devnode;
		else if (g_strcmp0(info->sysattr, "Application") == 0)
			app = info->devnode;
		else if (g_strcmp0(info->sysattr, "Modem") == 0)
			mdm = info->devnode;
		else if (!info->sysattr) {
			const char *net_name = get_ifname(info);

			if (g_str_has_prefix(net_name, "hso"))
				net = get_ifname(info);
		}
	}

	if (ctl == NULL || app == NULL)
		return FALSE;

	DBG("control=%s application=%s modem=%s network=%s",
						ctl, app, mdm, net);

	ofono_modem_set_string(modem->modem, "Control", ctl);
	ofono_modem_set_string(modem->modem, "Application", app);
	ofono_modem_set_string(modem->modem, "Modem", mdm);
	ofono_modem_set_string(modem->modem, "NetworkInterface", net);

	return TRUE;
}

static int setup_qmi_netdev(struct modem_info *modem,
					const struct device_info *net)
{
	const char *attr_value;
	uint32_t ifindex;
	int r;

	if (!net->kernel_driver)
		return -EINVAL;

	attr_value = udev_device_get_sysattr_value(net->udev_device,
								"ifindex");
	if (!attr_value)
		return -EINVAL;

	r = l_safe_atou32(attr_value, &ifindex);
	if (r < 0)
		return r;

	ofono_modem_set_string(modem->modem, "NetworkInterfaceKernelDriver",
							net->kernel_driver);
	ofono_modem_set_string(modem->modem, "NetworkInterface",
							get_ifname(net));
	ofono_modem_set_integer(modem->modem, "NetworkInterfaceIndex",
							ifindex);

	return 0;
}

static int setup_qmi_qmux(struct modem_info *modem,
				const struct device_info *qmi,
				const struct device_info *net)
{
	DBG("qmi: %s net: %s kernel_driver: %s interface_number: %s",
			qmi->devnode, get_ifname(net),
			net->kernel_driver, net->number);

	if (modem->type != MODEM_TYPE_USB)
		return -ENOTSUP;

	if (!net->number)
		return -EINVAL;

	if (!qmi->kernel_driver)
		return -EINVAL;

	ofono_modem_set_driver(modem->modem, "gobi");
	ofono_modem_set_string(modem->modem, "Device", qmi->devnode);
	ofono_modem_set_string(modem->modem, "DeviceProtocol", "qmux");
	ofono_modem_set_string(modem->modem, "InterfaceNumber", net->number);

	ofono_modem_set_string(modem->modem, "Bus", "usb");

	return setup_qmi_netdev(modem, net);
}

static int setup_qmi_qrtr(struct modem_info *modem,
				const struct device_info *net)
{
	DBG("net: %s kernel_driver: %s", get_ifname(net), net->kernel_driver);

	switch (modem->type) {
	case MODEM_TYPE_EMBEDDED:
		ofono_modem_set_string(modem->modem, "Bus", "embedded");
		break;
	case MODEM_TYPE_MHI:
		ofono_modem_set_string(modem->modem, "Bus", "pcie");
		break;
	case MODEM_TYPE_USB:
	case MODEM_TYPE_SERIAL:
	case MODEM_TYPE_PCIE:
		return -ENOTSUP;
	}

	ofono_modem_set_driver(modem->modem, "gobi");
	ofono_modem_set_string(modem->modem, "DeviceProtocol", "qrtr");

	return setup_qmi_netdev(modem, net);
}

static gboolean setup_gobi_qrtr_premux(struct modem_info *modem,
					const char *name, int premux_index)
{
	const char *rmnet_data_prefix = "rmnet_data";
	int rmnet_data_prefix_length = strlen(rmnet_data_prefix);
	char buf[256];
	int r;
	uint32_t data_id;
	uint32_t mux_id;

	r = l_safe_atou32(name + rmnet_data_prefix_length, &data_id);
	if (r < 0)
		return FALSE;

	mux_id = data_id + 1;

	DBG("Adding premux interface %s, mux id: %d", name, mux_id);
	sprintf(buf, "PremuxInterface%d", premux_index);
	ofono_modem_set_string(modem->modem, buf, name);
	sprintf(buf, "PremuxInterface%dMuxId", premux_index);
	ofono_modem_set_integer(modem->modem, buf, mux_id);

	return TRUE;
}

static gboolean setup_gobi_qrtr(struct modem_info *modem)
{
	const struct device_info *ipa_info = NULL;
	int premux_count = 0;
	int r;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;
		const char *name;

		name = udev_device_get_sysname(info->udev_device);
		if (l_str_has_prefix(name, "rmnet_ipa"))
			ipa_info = info;
		else if (l_str_has_prefix(name, "rmnet_data")) {
			int premux_index = premux_count + 1;

			if (setup_gobi_qrtr_premux(modem, name, premux_index))
				premux_count++;
		}
	}

	if (premux_count < 3) {
		DBG("Not enough rmnet_data interfaces found");
		return FALSE;
	}

	ofono_modem_set_integer(modem->modem, "NumPremuxInterfaces",
							premux_count);

	if (!ipa_info) {
		DBG("No rmnet_ipa interface found");
		return FALSE;
	}

	r = setup_qmi_qrtr(modem, ipa_info);
	if (r < 0)
		return FALSE;

	return TRUE;
}

static gboolean setup_gobi(struct modem_info *modem)
{
	const struct device_info *qmi = NULL;
	const struct device_info *net = NULL;
	const char *mdm = NULL;
	const char *gps = NULL;
	const char *diag = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		const struct device_info *info = list->data;
		const char *subsystem =
			udev_device_get_subsystem(info->udev_device);

		DBG("%s %s %s %s %s %s", info->devnode, info->interface,
						info->number, info->label,
						info->sysattr, subsystem);

		if (g_strcmp0(subsystem, "usbmisc") == 0) /* cdc-wdm */
			qmi = info;
		else if (g_strcmp0(subsystem, "net") == 0) /* wwan */
			net = info;
		else if (g_strcmp0(subsystem, "tty") == 0) {
			if (g_strcmp0(info->interface, "255/255/255") == 0) {
				if (g_strcmp0(info->number, "00") == 0)
					diag = info->devnode; /* ec20 */
				else if (g_strcmp0(info->number, "01") == 0)
					diag = info->devnode; /* gobi */
				else if (g_strcmp0(info->number, "02") == 0)
					mdm = info->devnode; /* gobi */
				else if (g_strcmp0(info->number, "03") == 0)
					gps = info->devnode; /* gobi */
			} else if (g_strcmp0(info->interface, "255/0/0") == 0) {
				if (g_strcmp0(info->number, "01") == 0)
					gps = info->devnode; /* ec20 */
				if (g_strcmp0(info->number, "02") == 0)
					mdm = info->devnode; /* ec20 */
				/* ignore the 3rd device second AT/mdm iface */
			}
		}
	}

	if (qmi == NULL || mdm == NULL || net == NULL)
		return FALSE;

	DBG("qmi=%s net=%s mdm=%s gps=%s diag=%s",
			qmi->devnode, get_ifname(net), mdm, gps, diag);

	if (setup_qmi_qmux(modem, qmi, net) < 0)
		return FALSE;

	ofono_modem_set_string(modem->modem, "Modem", mdm);
	ofono_modem_set_string(modem->modem, "Diag", diag);

	return TRUE;
}

static gboolean setup_sierra(struct modem_info *modem)
{
	const struct device_info *net = NULL;
	const struct device_info *qmi = NULL;
	const char *mdm = NULL;
	const char *app = NULL;
	const char *diag = NULL;

	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		const struct device_info *info = list->data;
		const char *subsystem =
			udev_device_get_subsystem(info->udev_device);

		DBG("%s %s %s %s %s", info->devnode, info->interface,
				info->number, info->label, subsystem);

		if (g_strcmp0(info->interface, "255/255/255") == 0) {
			if (g_strcmp0(info->number, "01") == 0)
				diag = info->devnode;
			else if (g_strcmp0(info->number, "03") == 0)
				mdm = info->devnode;
			else if (g_strcmp0(info->number, "04") == 0)
				app = info->devnode;
			else if (g_strcmp0(info->number, "07") == 0)
				net = info;
			else if (g_strcmp0(subsystem, "net") == 0) {
				/*
				 * When using the voice firmware on a mc7304
				 * the second cdc-wdm interface doesn't handle
				 * qmi messages properly.
				 * Some modems still have a working second
				 * cdc-wdm interface, some are not. But always
				 * the first interface works.
				 */
				if (g_strcmp0(info->number, "08") == 0) {
					net = info;
				} else if (g_strcmp0(info->number, "0a") == 0) {
					if (net == NULL)
						net = info;
				}
			} else if (g_strcmp0(subsystem, "usbmisc") == 0) {
				if (g_strcmp0(info->number, "08") == 0) {
					qmi = info;
				} else if (g_strcmp0(info->number, "0a") == 0) {
					if (qmi == NULL)
						qmi = info;
				}
			}
		}
	}

	if (qmi != NULL && net != NULL) {
		if (setup_qmi_qmux(modem, qmi, net) < 0)
			return FALSE;

		goto done;
	}

	if (mdm == NULL || net == NULL)
		return FALSE;

	ofono_modem_set_string(modem->modem, "NetworkInterface",
					get_ifname(net));
done:
	DBG("modem=%s app=%s net=%s diag=%s qmi=%s",
			mdm, app, get_ifname(net), diag, qmi->devnode);

	ofono_modem_set_string(modem->modem, "Modem", mdm);
	ofono_modem_set_string(modem->modem, "App", app);
	ofono_modem_set_string(modem->modem, "Diag", diag);

	return TRUE;
}

static gboolean setup_huawei(struct modem_info *modem)
{
	const struct device_info *net = NULL;
	const struct device_info *qmi = NULL;
	const char *mdm = NULL;
	const char *pcui = NULL;
	const char *diag = NULL;

	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		const struct device_info *info = list->data;

		DBG("%s %s %s %s", info->devnode, info->interface,
						info->number, info->label);

		if (g_strcmp0(info->label, "modem") == 0 ||
				g_strcmp0(info->interface, "255/1/1") == 0 ||
				g_strcmp0(info->interface, "255/2/1") == 0 ||
				g_strcmp0(info->interface, "255/3/1") == 0 ||
				g_strcmp0(info->interface, "255/1/49") == 0) {
			mdm = info->devnode;
		} else if (g_strcmp0(info->label, "pcui") == 0 ||
				g_strcmp0(info->interface, "255/1/2") == 0 ||
				g_strcmp0(info->interface, "255/2/2") == 0 ||
				g_strcmp0(info->interface, "255/2/18") == 0 ||
				g_strcmp0(info->interface, "255/3/18") == 0 ||
				g_strcmp0(info->interface, "255/1/50") == 0) {
			pcui = info->devnode;
		} else if (g_strcmp0(info->label, "diag") == 0 ||
				g_strcmp0(info->interface, "255/1/3") == 0 ||
				g_strcmp0(info->interface, "255/2/3") == 0 ||
				g_strcmp0(info->interface, "255/1/51") == 0) {
			diag = info->devnode;
		} else if (g_strcmp0(info->interface, "255/1/8") == 0 ||
				g_strcmp0(info->interface, "255/1/56") == 0) {
			net = info;
		} else if (g_strcmp0(info->interface, "255/1/9") == 0 ||
				g_strcmp0(info->interface, "255/1/57") == 0) {
			qmi = info;
		} else if (g_strcmp0(info->interface, "255/255/255") == 0) {
			if (g_strcmp0(info->number, "00") == 0)
				mdm = info->devnode;
			else if (g_strcmp0(info->number, "01") == 0)
				pcui = info->devnode;
			else if (g_strcmp0(info->number, "02") == 0)
				pcui = info->devnode;
			else if (g_strcmp0(info->number, "03") == 0)
				pcui = info->devnode;
			else if (g_strcmp0(info->number, "04") == 0)
				pcui = info->devnode;
		}
	}

	if (qmi != NULL && net != NULL) {
		if (setup_qmi_qmux(modem, qmi, net) < 0)
			return FALSE;

		goto done;
	}

	if (mdm == NULL || pcui == NULL)
		return FALSE;

	ofono_modem_set_string(modem->modem, "NetworkInterface",
					get_ifname(net));
done:
	DBG("mdm=%s pcui=%s diag=%s qmi=%s net=%s",
		mdm, pcui, diag, qmi->devnode, get_ifname(net));

	ofono_modem_set_string(modem->modem, "Modem", mdm);
	ofono_modem_set_string(modem->modem, "Pcui", pcui);
	ofono_modem_set_string(modem->modem, "Diag", diag);

	return TRUE;
}

static gboolean setup_speedup(struct modem_info *modem)
{
	const char *aux = NULL, *mdm = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;

		DBG("%s %s %s %s", info->devnode, info->interface,
						info->number, info->label);

		if (g_strcmp0(info->label, "aux") == 0) {
			aux = info->devnode;
			if (mdm != NULL)
				break;
		} else if (g_strcmp0(info->label, "modem") == 0) {
			mdm = info->devnode;
			if (aux != NULL)
				break;
		}
	}

	if (aux == NULL || mdm == NULL)
		return FALSE;

	DBG("aux=%s modem=%s", aux, mdm);

	ofono_modem_set_string(modem->modem, "Aux", aux);
	ofono_modem_set_string(modem->modem, "Modem", mdm);

	return TRUE;
}

static gboolean setup_linktop(struct modem_info *modem)
{
	const char *aux = NULL, *mdm = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;

		DBG("%s %s %s %s", info->devnode, info->interface,
						info->number, info->label);

		if (g_strcmp0(info->interface, "2/2/1") == 0) {
			if (g_strcmp0(info->number, "01") == 0)
				aux = info->devnode;
			else if (g_strcmp0(info->number, "03") == 0)
				mdm = info->devnode;
		}
	}

	if (aux == NULL || mdm == NULL)
		return FALSE;

	DBG("aux=%s modem=%s", aux, mdm);

	ofono_modem_set_string(modem->modem, "Aux", aux);
	ofono_modem_set_string(modem->modem, "Modem", mdm);

	return TRUE;
}

static gboolean setup_icera(struct modem_info *modem)
{
	const char *aux = NULL, *mdm = NULL, *net = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;

		DBG("%s %s %s %s", info->devnode, info->interface,
						info->number, info->label);

		if (g_strcmp0(info->interface, "2/2/1") == 0) {
			if (g_strcmp0(info->number, "00") == 0)
				aux = info->devnode;
			else if (g_strcmp0(info->number, "01") == 0)
				aux = info->devnode;
			else if (g_strcmp0(info->number, "02") == 0)
				mdm = info->devnode;
			else if (g_strcmp0(info->number, "03") == 0)
				mdm = info->devnode;
		} else if (g_strcmp0(info->interface, "2/6/0") == 0) {
			if (g_strcmp0(info->number, "05") == 0)
				net = get_ifname(info);
			else if (g_strcmp0(info->number, "06") == 0)
				net = get_ifname(info);
			else if (g_strcmp0(info->number, "07") == 0)
				net = get_ifname(info);
		}
	}

	if (aux == NULL || mdm == NULL)
		return FALSE;

	DBG("aux=%s modem=%s net=%s", aux, mdm, net);

	ofono_modem_set_string(modem->modem, "Aux", aux);
	ofono_modem_set_string(modem->modem, "Modem", mdm);
	ofono_modem_set_string(modem->modem, "NetworkInterface", net);

	return TRUE;
}

static gboolean setup_alcatel(struct modem_info *modem)
{
	const char *aux = NULL, *mdm = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;

		DBG("%s %s %s %s", info->devnode, info->interface,
						info->number, info->label);

		if (g_strcmp0(info->label, "aux") == 0) {
			aux = info->devnode;
			if (mdm != NULL)
				break;
		} else if (g_strcmp0(info->label, "modem") == 0) {
			mdm = info->devnode;
			if (aux != NULL)
				break;
		} else if (g_strcmp0(info->interface, "255/255/255") == 0) {
			if (g_strcmp0(info->number, "03") == 0)
				aux = info->devnode;
			else if (g_strcmp0(info->number, "05") == 0)
				mdm = info->devnode;
		}
	}

	if (aux == NULL || mdm == NULL)
		return FALSE;

	DBG("aux=%s modem=%s", aux, mdm);

	ofono_modem_set_string(modem->modem, "Aux", aux);
	ofono_modem_set_string(modem->modem, "Modem", mdm);

	return TRUE;
}

static gboolean setup_novatel(struct modem_info *modem)
{
	const char *aux = NULL, *mdm = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;

		DBG("%s %s %s %s", info->devnode, info->interface,
						info->number, info->label);

		if (g_strcmp0(info->label, "aux") == 0) {
			aux = info->devnode;
			if (mdm != NULL)
				break;
		} else if (g_strcmp0(info->label, "modem") == 0) {
			mdm = info->devnode;
			if (aux != NULL)
				break;
		} else if (g_strcmp0(info->interface, "255/255/255") == 0) {
			if (g_strcmp0(info->number, "00") == 0)
				aux = info->devnode;
			else if (g_strcmp0(info->number, "01") == 0)
				mdm = info->devnode;
		}
	}

	if (aux == NULL || mdm == NULL)
		return FALSE;

	DBG("aux=%s modem=%s", aux, mdm);

	ofono_modem_set_string(modem->modem, "Aux", aux);
	ofono_modem_set_string(modem->modem, "Modem", mdm);

	return TRUE;
}

static gboolean setup_nokia(struct modem_info *modem)
{
	const char *aux = NULL, *mdm = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;

		DBG("%s %s %s %s", info->devnode, info->interface,
						info->number, info->label);

		if (g_strcmp0(info->label, "aux") == 0) {
			aux = info->devnode;
			if (mdm != NULL)
				break;
		} else if (g_strcmp0(info->label, "modem") == 0) {
			mdm = info->devnode;
			if (aux != NULL)
				break;
		} else if (g_strcmp0(info->interface, "10/0/0") == 0) {
			if (g_strcmp0(info->number, "02") == 0)
				mdm = info->devnode;
			else if (g_strcmp0(info->number, "04") == 0)
				aux = info->devnode;
		}
	}

	if (aux == NULL || mdm == NULL)
		return FALSE;

	DBG("aux=%s modem=%s", aux, mdm);

	ofono_modem_set_string(modem->modem, "Aux", aux);
	ofono_modem_set_string(modem->modem, "Modem", mdm);

	return TRUE;
}

static gboolean setup_telit(struct modem_info *modem)
{
	const char *mdm = NULL, *aux = NULL, *gps = NULL, *net = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;

		DBG("%s %s %s %s", info->devnode, info->interface,
						info->number, info->label);

		if (g_strcmp0(info->label, "aux") == 0) {
			aux = info->devnode;
			if (mdm != NULL)
				break;
		} else if (g_strcmp0(info->label, "modem") == 0) {
			mdm = info->devnode;
			if (aux != NULL)
				break;
		} else if (g_strcmp0(info->interface, "255/255/255") == 0) {
			if (g_strcmp0(info->number, "00") == 0)
				mdm = info->devnode;
			else if (g_strcmp0(info->number, "02") == 0)
				gps = info->devnode;
			else if (g_strcmp0(info->number, "03") == 0)
				aux = info->devnode;
		} else if (g_strcmp0(info->interface, "2/2/1") == 0) {
			if (g_strcmp0(info->number, "00") == 0)
				mdm = info->devnode;
			else if (g_strcmp0(info->number, "06") == 0)
				aux = info->devnode;
			else if (g_strcmp0(info->number, "0a") == 0)
				gps = info->devnode;
		} else if (info->sysattr && (g_str_has_suffix(info->sysattr,
						"CDC NCM") == TRUE)) {
			net = get_ifname(info);
		}
	}

	if (aux == NULL || mdm == NULL)
		return FALSE;

	DBG("modem=%s aux=%s gps=%s net=%s", mdm, aux, gps, net);

	ofono_modem_set_string(modem->modem, "Modem", mdm);
	ofono_modem_set_string(modem->modem, "Aux", aux);
	ofono_modem_set_string(modem->modem, "GPS", gps);

	if (net != NULL)
		ofono_modem_set_string(modem->modem, "NetworkInterface", net);

	return TRUE;
}

static gboolean setup_telitqmi(struct modem_info *modem)
{
	const struct device_info *net = NULL;
	const struct device_info *qmi = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		const struct device_info *info = list->data;
		const char *subsystem =
				udev_device_get_subsystem(info->udev_device);

		DBG("%s %s %s %s %s", info->devnode, info->interface,
				info->number, info->label, subsystem);

		if ((!g_strcmp0(info->interface, "255/255/255") ||
				!g_strcmp0(info->interface, "255/255/80")) &&
				g_strcmp0(info->number, "02") == 0) {
			if (g_strcmp0(subsystem, "net") == 0)
				net = info;
			else if (g_strcmp0(subsystem, "usbmisc") == 0)
				qmi = info;
		}
	}

	if (qmi == NULL || net == NULL)
		return FALSE;

	if (setup_qmi_qmux(modem, qmi, net) < 0)
		return FALSE;

	if (g_strcmp0(modem->model, "1070"))
		ofono_modem_set_boolean(modem->modem, "ForceSimLegacy", TRUE);

	return TRUE;
}

static gboolean setup_droid(struct modem_info *modem)
{
	const char *at = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;
		const char *subsystem =
			udev_device_get_subsystem(info->udev_device);

		DBG("%s %s %s %s %s", info->devnode, info->interface,
				info->number, info->label, subsystem);

		if (g_strcmp0(info->interface, "255/255/255") == 0 &&
				g_strcmp0(info->number, "04") == 0) {
			at = info->devnode;
		}
	}

	if (at == NULL)
		return FALSE;

	ofono_modem_set_string(modem->modem, "Device", at);
	ofono_modem_set_driver(modem->modem, "droid");

	return TRUE;
}

/* TODO: Not used as we have no simcom driver */
static gboolean setup_simcom(struct modem_info *modem)
{
	const char *mdm = NULL, *aux = NULL, *gps = NULL, *diag = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;

		DBG("%s %s %s %s", info->devnode, info->interface,
						info->number, info->label);

		if (g_strcmp0(info->label, "aux") == 0) {
			aux = info->devnode;
			if (mdm != NULL)
				break;
		} else if (g_strcmp0(info->label, "modem") == 0) {
			mdm = info->devnode;
			if (aux != NULL)
				break;
		} else if (g_strcmp0(info->interface, "255/255/255") == 0) {
			if (g_strcmp0(info->number, "00") == 0)
				diag = info->devnode;
			else if (g_strcmp0(info->number, "01") == 0)
				gps = info->devnode;
			else if (g_strcmp0(info->number, "02") == 0)
				aux = info->devnode;
			else if (g_strcmp0(info->number, "03") == 0)
				mdm = info->devnode;
		}
	}

	if (aux == NULL || mdm == NULL)
		return FALSE;

	DBG("modem=%s aux=%s gps=%s diag=%s", mdm, aux, gps, diag);

	ofono_modem_set_string(modem->modem, "Modem", mdm);
	ofono_modem_set_string(modem->modem, "Data", aux);
	ofono_modem_set_string(modem->modem, "GPS", gps);

	return TRUE;
}

static gboolean setup_zte(struct modem_info *modem)
{
	const char *aux = NULL, *mdm = NULL, *qcdm = NULL;
	const char *modem_intf;
	GSList *list;

	DBG("%s", modem->syspath);

	if (g_strcmp0(modem->model, "0016") == 0 ||
				g_strcmp0(modem->model, "0017") == 0 ||
				g_strcmp0(modem->model, "0117") == 0)
		modem_intf = "02";
	else
		modem_intf = "03";

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;

		DBG("%s %s %s %s", info->devnode, info->interface,
						info->number, info->label);

		if (g_strcmp0(info->label, "aux") == 0) {
			aux = info->devnode;
			if (mdm != NULL)
				break;
		} else if (g_strcmp0(info->label, "modem") == 0) {
			mdm = info->devnode;
			if (aux != NULL)
				break;
		} else if (g_strcmp0(info->interface, "255/255/255") == 0) {
			if (g_strcmp0(info->number, "00") == 0)
				qcdm = info->devnode;
			else if (g_strcmp0(info->number, "01") == 0)
				aux = info->devnode;
			else if (g_strcmp0(info->number, modem_intf) == 0)
				mdm = info->devnode;
		}
	}

	if (aux == NULL || mdm == NULL)
		return FALSE;

	DBG("aux=%s modem=%s qcdm=%s", aux, mdm, qcdm);

	ofono_modem_set_string(modem->modem, "Aux", aux);
	ofono_modem_set_string(modem->modem, "Modem", mdm);

	return TRUE;
}

static gboolean setup_samsung(struct modem_info *modem)
{
	const char *control = NULL, *network = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;

		DBG("%s %s %s %s", info->devnode, info->interface,
						info->number, info->label);

		if (g_strcmp0(info->interface, "10/0/0") == 0)
			control = info->devnode;
		else if (g_strcmp0(info->interface, "255/0/0") == 0)
			network = get_ifname(info);
	}

	if (control == NULL && network == NULL)
		return FALSE;

	DBG("control=%s network=%s", control, network);

	ofono_modem_set_string(modem->modem, "ControlPort", control);
	ofono_modem_set_string(modem->modem, "NetworkInterface", network);

	return TRUE;
}

static gboolean setup_quectel_usb(struct modem_info *modem)
{
	const char *aux = NULL, *mdm = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;

		DBG("%s %s %s %s", info->devnode, info->interface,
						info->number, info->label);

		if (g_strcmp0(info->label, "aux") == 0) {
			aux = info->devnode;
			if (mdm != NULL)
				break;
		} else if (g_strcmp0(info->label, "modem") == 0) {
			mdm = info->devnode;
			if (aux != NULL)
				break;
		} else if (g_strcmp0(info->interface, "255/255/255") == 0) {
			if (g_strcmp0(info->number, "02") == 0)
				aux = info->devnode;
			else if (g_strcmp0(info->number, "03") == 0)
				mdm = info->devnode;
		}
	}

	if (aux == NULL || mdm == NULL)
		return FALSE;

	DBG("aux=%s modem=%s", aux, mdm);

	ofono_modem_set_string(modem->modem, "Aux", aux);
	ofono_modem_set_string(modem->modem, "Modem", mdm);

	return TRUE;
}

static gboolean setup_quectel_serial(struct modem_info *modem)
{
	struct serial_device_info *info = modem->serial;
	const char *value;

	value = udev_device_get_property_value(info->dev,
						"OFONO_QUECTEL_GPIO_CHIP");
	if (value)
		ofono_modem_set_string(modem->modem, "GpioChip", value);

	value = udev_device_get_property_value(info->dev,
						"OFONO_QUECTEL_GPIO_OFFSET");
	if (value)
		ofono_modem_set_string(modem->modem, "GpioOffset", value);

	value = udev_device_get_property_value(info->dev,
						"OFONO_QUECTEL_GPIO_LEVEL");
	if (value)
		ofono_modem_set_boolean(modem->modem, "GpioLevel", TRUE);

	value = udev_device_get_property_value(info->dev,
						"OFONO_QUECTEL_MUX");
	if (value)
		ofono_modem_set_string(modem->modem, "Mux", value);

	value = udev_device_get_property_value(info->dev,
						"OFONO_QUECTEL_RTSCTS");
	ofono_modem_set_string(modem->modem, "RtsCts", value ? value : "off");
	ofono_modem_set_string(modem->modem, "Device", info->devnode);

	return TRUE;
}

static gboolean setup_quectel(struct modem_info *modem)
{
	if (modem->type == MODEM_TYPE_SERIAL)
		return setup_quectel_serial(modem);
	else if (modem->type == MODEM_TYPE_USB)
		return setup_quectel_usb(modem);
	else
		return FALSE;
}

static gboolean is_premultiplexed(const struct device_info *net)
{
	struct udev_device *parent = udev_device_get_parent(net->udev_device);

	if (!parent)
		return FALSE;

	if (g_strcmp0(udev_device_get_subsystem(parent), "net") == 0)
		return TRUE;

	return FALSE;
}

static gboolean setup_quectelqmi(struct modem_info *modem)
{
	const struct device_info *net = NULL;
	const struct device_info *qmi = NULL;
	const char *gps = NULL;
	const char *aux = NULL;
	GSList *list;
	const char *premux_interfaces[8];
	int n_premux = 0;
	const char *qmap_size;

	memset(premux_interfaces, 0, sizeof(premux_interfaces));

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = g_slist_next(list)) {
		const struct device_info *info = list->data;
		const char *subsystem =
			udev_device_get_subsystem(info->udev_device);

		DBG("%s %s %s %s %s", info->devnode, info->interface,
				info->number, info->label, subsystem);

		if (g_strcmp0(info->interface, "255/255/255") == 0) {
			if (g_strcmp0(subsystem, "usbmisc") == 0) {
				qmi = info;
				continue;
			}

			if (g_strcmp0(subsystem, "net"))
				continue;

			if (is_premultiplexed(info)) {
				premux_interfaces[n_premux] = info->devnode;
				n_premux += 1;
				continue;
			}

			net = info;
		} else if (g_strcmp0(info->interface, "255/0/0") == 0 &&
				g_strcmp0(info->number, "01") == 0) {
			gps = info->devnode;
		} else if (g_strcmp0(info->interface, "255/0/0") == 0 &&
				g_strcmp0(info->number, "02") == 0) {
			aux = info->devnode;
		}
	}

	if (qmi == NULL || net == NULL)
		return FALSE;

	DBG("gps=%s aux=%s", gps, aux);

	if (setup_qmi_qmux(modem, qmi, net) < 0)
		return FALSE;

	qmap_size = udev_device_get_sysattr_value(net->udev_device,
							"qmap_size");
	if (qmap_size) {
		uint32_t max_aggregation_size;

		if (l_safe_atou32(qmap_size, &max_aggregation_size) == 0)
			ofono_modem_set_integer(modem->modem,
						"MaxAggregationSize",
						max_aggregation_size);
	}

	if (gps)
		ofono_modem_set_string(modem->modem, "GPS", gps);

	if (aux)
		ofono_modem_set_string(modem->modem, "Aux", aux);

	if (n_premux) {
		char buf[256];
		int i;

		ofono_modem_set_integer(modem->modem,
					"NumPremuxInterfaces", n_premux);
		for (i = 0; i < n_premux; i++) {
			const char *device = premux_interfaces[i];
			int len = strlen(device);

			if (!len)
				continue;

			sprintf(buf, "PremuxInterface%d", i + 1);
			ofono_modem_set_string(modem->modem, buf, device);
			sprintf(buf, "PremuxInterface%dMuxId", i + 1);
			ofono_modem_set_integer(modem->modem, buf,
						0x80 + device[len - 1] - '0');
		}
	}

	return TRUE;
}

static gboolean setup_mbim(struct modem_info *modem)
{
	const char *ctl = NULL, *net = NULL, *atcmd = NULL;
	GSList *list;
	char descriptors[PATH_MAX];

	DBG("%s [%s:%s]", modem->syspath, modem->vendor, modem->model);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;
		const char *subsystem =
			udev_device_get_subsystem(info->udev_device);

		DBG("%s %s %s %s %s %s", info->devnode, info->interface,
						info->number, info->label,
						info->sysattr, subsystem);

		if (g_strcmp0(subsystem, "usbmisc") == 0) /* cdc-wdm */
			ctl = info->devnode;
		else if (g_strcmp0(subsystem, "net") == 0) /* wwan */
			net = get_ifname(info);
		else if (g_strcmp0(subsystem, "tty") == 0) {
			if (g_strcmp0(info->number, "02") == 0)
				atcmd = info->devnode;
		}
	}

	if (ctl == NULL || net == NULL)
		return FALSE;

	DBG("ctl=%s net=%s atcmd=%s", ctl, net, atcmd);

	sprintf(descriptors, "%s/descriptors", modem->syspath);

	ofono_modem_set_string(modem->modem, "Device", ctl);
	ofono_modem_set_string(modem->modem, "NetworkInterface", net);
	ofono_modem_set_string(modem->modem, "DescriptorFile", descriptors);

	return TRUE;
}

static gboolean setup_serial_modem(struct modem_info *modem)
{
	struct serial_device_info *info;

	info = modem->serial;

	ofono_modem_set_string(modem->modem, "Device", info->devnode);

	return TRUE;
}

static gboolean setup_tc65(struct modem_info *modem)
{
	ofono_modem_set_driver(modem->modem, "cinterion");

	return setup_serial_modem(modem);
}

static gboolean setup_ehs6(struct modem_info *modem)
{
	ofono_modem_set_driver(modem->modem, "cinterion");

	return setup_serial_modem(modem);
}

static gboolean setup_ifx(struct modem_info *modem)
{
	struct serial_device_info *info;
	const char *value;

	info = modem->serial;

	value = udev_device_get_property_value(info->dev, "OFONO_IFX_LDISC");
	if (value)
		ofono_modem_set_string(modem->modem, "LineDiscipline", value);

	value = udev_device_get_property_value(info->dev, "OFONO_IFX_AUDIO");
	if (value)
		ofono_modem_set_string(modem->modem, "AudioSetting", value);

	value = udev_device_get_property_value(info->dev, "OFONO_IFX_LOOPBACK");
	if (value)
		ofono_modem_set_string(modem->modem, "AudioLoopback", value);

	ofono_modem_set_string(modem->modem, "Device", info->devnode);

	return TRUE;
}

static gboolean setup_wavecom(struct modem_info *modem)
{
	struct serial_device_info *info;
	const char *value;

	info = modem->serial;

	value = udev_device_get_property_value(info->dev,
						"OFONO_WAVECOM_MODEL");
	if (value)
		ofono_modem_set_string(modem->modem, "Model", value);

	ofono_modem_set_string(modem->modem, "Device", info->devnode);

	return TRUE;
}

static gboolean setup_isi_serial(struct modem_info *modem)
{
	struct serial_device_info *info;
	const char *value;

	info = modem->serial;

	if (g_strcmp0(udev_device_get_subsystem(info->dev), "net") != 0)
		return FALSE;

	value = udev_device_get_sysattr_value(info->dev, "type");
	if (g_strcmp0(value, "820") != 0)
		return FALSE;

	/* OK, we want this device to be a modem */
	value = udev_device_get_sysname(info->dev);
	if (value)
		ofono_modem_set_string(modem->modem, "Interface", value);

	value = udev_device_get_property_value(info->dev, "OFONO_ISI_ADDRESS");
	if (value)
		ofono_modem_set_integer(modem->modem, "Address", atoi(value));

	return TRUE;
}

static gboolean setup_ublox(struct modem_info *modem)
{
	const char *aux = NULL, *mdm = NULL, *net = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;

		DBG("%s %s %s %s", info->devnode, info->interface,
					info->number, info->label);

		if (g_strcmp0(info->label, "aux") == 0) {
			aux = info->devnode;
			if (mdm != NULL)
				break;
		} else if (g_strcmp0(info->label, "modem") == 0) {
			mdm = info->devnode;
			if (aux != NULL)
				break;
		/*
		 * "2/2/1"
		 *  - a common modem interface both for older models like LISA,
		 *    and for newer models like TOBY.
		 * For TOBY-L2, NetworkInterface can be detected for each
		 * profile:
		 *  - low-medium throughput profile : 2/6/0
		 *  - fairly backward-compatible profile : 10/0/0
		 *  - high throughput profile : 224/1/3
		 */
		} else if (g_strcmp0(info->interface, "2/2/1") == 0) {
			if (!g_strcmp0(modem->model, "1010")) {
				if (g_strcmp0(info->number, "06") == 0)
					aux = info->devnode;
			} else {
				if (g_strcmp0(info->number, "02") == 0)
					aux = info->devnode;
			}
			if (g_strcmp0(info->number, "00") == 0)
				mdm = info->devnode;
		} else if (g_strcmp0(info->interface, "2/6/0") == 0 ||
				g_strcmp0(info->interface, "2/13/0") == 0 ||
				g_strcmp0(info->interface, "10/0/0") == 0 ||
				g_strcmp0(info->interface, "224/1/3") == 0) {
			net = get_ifname(info);
		}
	}

	/* Abort only if both interfaces are NULL, as it's highly possible that
	 * only one of 2 interfaces is available for U-blox modem.
	 */
	if (aux == NULL && mdm == NULL)
		return FALSE;

	DBG("aux=%s modem=%s net=%s", aux, mdm, net);

	ofono_modem_set_string(modem->modem, "Aux", aux);
	ofono_modem_set_string(modem->modem, "Modem", mdm);
	ofono_modem_set_string(modem->modem, "NetworkInterface", net);

	return TRUE;
}

static gboolean setup_gemalto(struct modem_info *modem)
{
	const char *app = NULL, *gps = NULL, *mdm = NULL,
		*net = NULL, *qmi = NULL, *net2 = NULL;

	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;
		const char *subsystem =
			udev_device_get_subsystem(info->udev_device);

		DBG("%s %s %s %s %s", info->devnode, info->interface,
				info->number, info->label, subsystem);

		/* PHS8-P */
		if (g_strcmp0(info->interface, "255/255/255") == 0) {
			if (g_strcmp0(info->number, "01") == 0)
				gps = info->devnode;
			else if (g_strcmp0(info->number, "02") == 0)
				app = info->devnode;
			else if (g_strcmp0(info->number, "03") == 0)
				mdm = info->devnode;
			else if (g_strcmp0(subsystem, "net") == 0)
				net = get_ifname(info);
			else if (g_strcmp0(subsystem, "usbmisc") == 0)
				qmi = info->devnode;
		}

		/* Cinterion ALS3, PLS8-E, PLS8-X, EHS5-E */
		if (g_strcmp0(info->interface, "2/2/1") == 0) {
			if (g_strcmp0(info->number, "00") == 0)
				mdm = info->devnode;
			else if (g_strcmp0(info->number, "02") == 0)
				app = info->devnode;
			else if (g_strcmp0(info->number, "04") == 0)
				gps = info->devnode;
		}

		if (g_strcmp0(info->interface, "2/6/0") == 0) {
			if (g_strcmp0(subsystem, "net") == 0) {
				if (g_strcmp0(info->number, "0a") == 0)
					net = get_ifname(info);
				if (g_strcmp0(info->number, "0c") == 0)
					net2 = get_ifname(info);
			}
		}
	}

	DBG("application=%s gps=%s modem=%s network=%s qmi=%s",
			app, gps, mdm, net, qmi);

	if (app == NULL || mdm == NULL)
		return FALSE;

	ofono_modem_set_string(modem->modem, "Application", app);
	ofono_modem_set_string(modem->modem, "GPS", gps);
	ofono_modem_set_string(modem->modem, "Modem", mdm);
	ofono_modem_set_string(modem->modem, "Device", qmi);
	ofono_modem_set_string(modem->modem, "Model", modem->model);
	ofono_modem_set_string(modem->modem, "NetworkInterface", net);

	if (net2)
		ofono_modem_set_string(modem->modem, "NetworkInterface2", net2);

	return TRUE;
}

static gboolean setup_xmm7xxx(struct modem_info *modem)
{
	const char *mdm = NULL, *net = NULL, *net2 = NULL, *net3 = NULL;
	GSList *list;

	DBG("%s %s %s %s %s %s\n", modem->syspath, modem->devname,
		modem->driver, modem->vendor, modem->model, modem->sysattr);

	for (list = modem->devices; list; list = list->next) {
		struct device_info *info = list->data;
		const char *syspath =
				udev_device_get_syspath(info->udev_device);
		const char *subsystem =
				udev_device_get_subsystem(info->udev_device);

		DBG("%s %s %s %s %s %s %s\n", syspath, info->devnode,
				info->interface, info->number, info->label,
				info->sysattr, subsystem);

		if (g_strcmp0(subsystem, "pci") == 0) {
			if ((g_strcmp0(modem->vendor, "0x8086") == 0) &&
				(g_strcmp0(modem->model, "0x7560") == 0)) {
				mdm = "/dev/iat";
				net = "inm0";
				net2 = "inm1";
				net3 = "inm2";
				ofono_modem_set_string(modem->modem,
					"CtrlPath", "/PCIE/IOSM/CTRL/1");
				ofono_modem_set_string(modem->modem, "DataPath",
					"/PCIE/IOSM/IPS/");
			}
		} else { /* For USB */
			if (g_strcmp0(modem->model, "095a") == 0) {
				if (g_strcmp0(subsystem, "tty") == 0) {
					if (g_strcmp0(info->number, "00") == 0)
						mdm = info->devnode;
				} else if (g_strcmp0(subsystem, "net")
									== 0) {
					if (g_strcmp0(info->number, "06") == 0)
						net = get_ifname(info);
					if (g_strcmp0(info->number, "08") == 0)
						net2 = get_ifname(info);
					if (g_strcmp0(info->number, "0a") == 0)
						net3 = get_ifname(info);
				}
			} else {
				if (g_strcmp0(subsystem, "tty") == 0) {
					if (g_strcmp0(info->number, "02") == 0)
						mdm = info->devnode;
				} else if (g_strcmp0(subsystem, "net")
									== 0) {
					if (g_strcmp0(info->number, "00") == 0)
						net = get_ifname(info);
				}
			}

			ofono_modem_set_string(modem->modem, "CtrlPath",
								"/USBCDC/0");
			ofono_modem_set_string(modem->modem, "DataPath",
								"/USBHS/NCM/");
		}
	}

	if (mdm == NULL || net == NULL)
		return FALSE;

	DBG("modem=%s net=%s\n", mdm, net);

	ofono_modem_set_string(modem->modem, "Modem", mdm);
	ofono_modem_set_string(modem->modem, "NetworkInterface", net);

	if (net2)
		ofono_modem_set_string(modem->modem, "NetworkInterface2", net2);

	if (net3)
		ofono_modem_set_string(modem->modem, "NetworkInterface3", net3);

	return TRUE;
}

static gboolean setup_sim7x00(struct modem_info *modem)
{
	const struct device_info *net = NULL;
	const struct device_info *qmi = NULL;
	const char *mdm = NULL;
	const char *ppp = NULL;
	const char *audio = NULL;
	const char *diag = NULL;
	const char *gps = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		const struct device_info *info = list->data;
		const char *subsystem =
				udev_device_get_subsystem(info->udev_device);

		DBG("%s %s %s %s %s %s", info->devnode, info->interface,
						info->number, info->label,
						info->sysattr, subsystem);

		/*
		 * SIM7100 serial port layout:
		 * 0: QCDM/DIAG
		 * 1: NMEA
		 * 2: AT
		 * 3: AT/PPP
		 * 4: audio
		 *
		 * -- https://www.spinics.net/lists/linux-usb/msg135728.html
		 */
		if (g_strcmp0(subsystem, "usbmisc") == 0) /* cdc-wdm */
			qmi = info; /* SIM7600 */
		else if (g_strcmp0(subsystem, "net") == 0) /* wwan */
			net = info; /* SIM7600 */
		else if (g_strcmp0(subsystem, "tty") == 0) {
			if (g_strcmp0(info->interface, "255/255/255") == 0) {
				if (g_strcmp0(info->number, "00") == 0)
					diag = info->devnode; /* SIM7x00 */
			} else if (g_strcmp0(info->interface, "255/0/0") == 0) {
				if (g_strcmp0(info->number, "01") == 0)
					gps = info->devnode; /* SIM7x00 */
				else if (g_strcmp0(info->number, "02") == 0)
					mdm = info->devnode; /* SIM7x00 */
				else if (g_strcmp0(info->number, "03") == 0)
					ppp = info->devnode; /* SIM7100 */
				else if (g_strcmp0(info->number, "04") == 0)
					audio = info->devnode; /* SIM7100 */
			}
		}
	}

	if (mdm == NULL)
		return FALSE;

	if (qmi != NULL && net != NULL) {
		DBG("mdm=%s gps=%s diag=%s", mdm, gps, diag);

		if (setup_qmi_qmux(modem, qmi, net) < 0)
			return FALSE;

		ofono_modem_set_string(modem->modem, "Modem", mdm);
	} else {
		DBG("at=%s ppp=%s gps=%s diag=%s, audio=%s",
						mdm, ppp, gps, diag, audio);

		ofono_modem_set_driver(modem->modem, "sim7100");

		ofono_modem_set_string(modem->modem, "AT", mdm);
		ofono_modem_set_string(modem->modem, "PPP", ppp);
		ofono_modem_set_string(modem->modem, "Audio", audio);
	}

	ofono_modem_set_string(modem->modem, "GPS", gps);
	ofono_modem_set_string(modem->modem, "Diag", diag);
	return TRUE;
}

static gboolean setup_sim76xx(struct modem_info *modem)
{
	const char *diag = NULL;
	const char *mdm = NULL;
	const char *ppp = NULL;
	const char *gps = NULL;
	GSList *list;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		const struct device_info *info = list->data;
		const char *subsystem;

		subsystem = udev_device_get_subsystem(info->udev_device);
		if (!g_str_equal(subsystem, "tty"))
			continue;

		DBG("%s %s %s", info->devnode, info->interface, info->number);

		/*
		 * SIM76xx USB numbering:
		 * 0: RNDIS (ep_87)
		 * 1: RNDIS (ep_0c and ep_83)
		 * 2: QCDM/DIAG (ttyUSB0)
		 * 3: NMEA (ttyUSB3)
		 * 4: AT (ttyUSB1)
		 * 5: AT/PPP (ttyUSB2)
		 */
		if (g_str_equal(info->number, "02"))
			diag = info->devnode;
		else if (g_str_equal(info->number, "03"))
			gps = info->devnode;
		else if (g_str_equal(info->number, "04"))
			mdm = info->devnode;
		else if (g_str_equal(info->number, "05"))
			ppp = info->devnode;
	}

	if (mdm == NULL)
		return FALSE;

	DBG("at=%s ppp=%s gps=%s diag=%s", mdm, ppp, gps, diag);

	ofono_modem_set_driver(modem->modem, "sim7100");
	ofono_modem_set_string(modem->modem, "AT", mdm);
	ofono_modem_set_string(modem->modem, "PPP", ppp);

	return TRUE;
}

static gboolean setup_mhi(struct modem_info *modem)
{
	const struct device_info *net = NULL;
	const struct device_info *qrtr = NULL;
	GSList *list;
	int r;

	DBG("%s", modem->syspath);

	for (list = modem->devices; list; list = list->next) {
		const struct device_info *info = list->data;
		const char *subsystem =
				udev_device_get_subsystem(info->udev_device);

		DBG("%s", udev_device_get_syspath(info->udev_device));

		if (l_streq0(udev_device_get_property_value(info->udev_device,
								"MODALIAS"),
					"mhi:IPCR"))
			qrtr = info;
		else if (l_streq0(subsystem, "net"))
			net = info;
	}

	DBG("net: %p, qrtr: %p", net, qrtr);

	if (!net || !qrtr)
		return FALSE;

	r = setup_qmi_qrtr(modem, net);
	if (r < 0)
		return FALSE;

	return TRUE;
}

static struct {
	const char *name;
	gboolean (*setup)(struct modem_info *modem);
	const char *sysattr;
} driver_list[] = {
	{ "isiusb",	setup_isi,	"type"			},
	{ "mbm",	setup_mbm,	"device/interface"	},
	{ "hso",	setup_hso,	"hsotype"		},
	{ "gobi",	setup_gobi	},
	{ "sierra",	setup_sierra	},
	{ "huawei",	setup_huawei	},
	{ "speedup",	setup_speedup	},
	{ "linktop",	setup_linktop	},
	{ "alcatel",	setup_alcatel	},
	{ "novatel",	setup_novatel	},
	{ "nokia",	setup_nokia	},
	{ "telit",	setup_telit,	"device/interface"	},
	{ "telitqmi",	setup_telitqmi	},
	{ "simcom",	setup_simcom	},
	{ "sim7x00",	setup_sim7x00	},
	{ "sim76xx",	setup_sim76xx	},
	{ "zte",	setup_zte	},
	{ "icera",	setup_icera	},
	{ "samsung",	setup_samsung	},
	{ "quectel",	setup_quectel	},
	{ "quectelqmi",	setup_quectelqmi},
	{ "ublox",	setup_ublox	},
	{ "gemalto",	setup_gemalto	},
	{ "xmm7xxx",	setup_xmm7xxx	},
	{ "mbim",	setup_mbim	},
	{ "droid",	setup_droid	},
	/* Following are non-USB modems */
	{ "ifx",	setup_ifx		},
	{ "u8500",	setup_isi_serial	},
	{ "n900",	setup_isi_serial	},
	{ "calypso",	setup_serial_modem	},
	{ "cinterion",	setup_serial_modem	},
	{ "sim900",	setup_serial_modem	},
	{ "wavecom",	setup_wavecom		},
	{ "tc65",	setup_tc65		},
	{ "ehs6",	setup_ehs6		},
	{ "gobiqrtr",	setup_gobi_qrtr		},
	{ "mhi",	setup_mhi		},
	{ }
};

static GHashTable *modem_list;

static const char *get_sysattr(const char *driver)
{
	unsigned int i;

	for (i = 0; driver_list[i].name; i++) {
		if (g_str_equal(driver_list[i].name, driver) == TRUE)
			return driver_list[i].sysattr;
	}

	return NULL;
}

static void device_info_free(struct device_info *info)
{
	g_free(info->devnode);
	g_free(info->interface);
	g_free(info->number);
	g_free(info->label);
	g_free(info->sysattr);
	g_free(info->kernel_driver);
	udev_device_unref(info->udev_device);
	g_free(info);
}

static void serial_device_info_free(struct serial_device_info *info)
{
	g_free(info->devpath);
	g_free(info->devnode);
	g_free(info->subsystem);
	udev_device_unref(info->dev);
	g_free(info);
}

static void destroy_modem(gpointer data)
{
	struct modem_info *modem = data;
	GSList *list;

	DBG("%s", modem->syspath);

	ofono_modem_remove(modem->modem);

	switch (modem->type) {
	case MODEM_TYPE_USB:
	case MODEM_TYPE_PCIE:
	case MODEM_TYPE_EMBEDDED:
	case MODEM_TYPE_MHI:
		for (list = modem->devices; list; list = list->next) {
			struct device_info *info = list->data;

			DBG("%s", info->devnode);
			device_info_free(info);
		}

		g_slist_free(modem->devices);
		break;
	case MODEM_TYPE_SERIAL:
		serial_device_info_free(modem->serial);
		break;
	}

	g_free(modem->syspath);
	g_free(modem->devname);
	g_free(modem->driver);
	g_free(modem->vendor);
	g_free(modem->model);
	g_free(modem);
}

static gboolean check_remove(gpointer key, gpointer value, gpointer user_data)
{
	struct modem_info *modem = value;
	const char *devpath = user_data;
	GSList *list;

	switch (modem->type) {
	case MODEM_TYPE_USB:
	case MODEM_TYPE_PCIE:
	case MODEM_TYPE_MHI:
		for (list = modem->devices; list; list = list->next) {
			struct device_info *info = list->data;
			const char *syspath =
				udev_device_get_syspath(info->udev_device);

			if (g_strcmp0(syspath, devpath) == 0)
				return TRUE;
		}
		break;
	case MODEM_TYPE_SERIAL:
		if (g_strcmp0(modem->serial->devpath, devpath) == 0)
			return TRUE;
		break;
	case MODEM_TYPE_EMBEDDED:
		/* Embedded modems cannot be removed. */
		break;
	}

	return FALSE;
}

static void remove_device(struct udev_device *device)
{
	const char *syspath;

	syspath = udev_device_get_syspath(device);
	if (syspath == NULL)
		return;

	DBG("%s", syspath);

	g_hash_table_foreach_remove(modem_list, check_remove,
						(char *) syspath);
}

static gint compare_device(gconstpointer a, gconstpointer b)
{
	const struct device_info *info1 = a;
	const struct device_info *info2 = b;

	return g_strcmp0(info1->number, info2->number);
}

/*
 * Here we try to find the "modem device".
 *
 * In this variant we identify the "modem device" as simply the device
 * that has the OFONO_DRIVER property.  If the device node doesn't
 * have this property itself, then we do a brute force search for it
 * through the device hierarchy.
 *
 */
static struct udev_device *get_serial_modem_device(struct udev_device *dev)
{
	const char *driver;

	while (dev) {
		driver = udev_device_get_property_value(dev, "OFONO_DRIVER");
		if (driver)
			return dev;

		dev = udev_device_get_parent(dev);
	}

	return NULL;
}

/*
 * Add 'legacy' device
 *
 * The term legacy is a bit misleading, but this adds devices according
 * to the original ofono model.
 *
 * - We cannot assume that these are USB devices
 * - The modem consists of only a single interface
 * - The device must have an OFONO_DRIVER property from udev
 */
static void add_serial_device(struct udev_device *dev)
{
	const char *syspath, *devpath, *devname, *devnode;
	struct modem_info *modem;
	struct serial_device_info *info;
	const char *subsystem;
	struct udev_device *mdev;
	const char *driver;

	mdev = get_serial_modem_device(dev);
	if (!mdev)
		return;

	driver = udev_device_get_property_value(mdev, "OFONO_DRIVER");

	syspath = udev_device_get_syspath(mdev);
	devname = udev_device_get_devnode(mdev);
	devpath = udev_device_get_devpath(mdev);

	devnode = udev_device_get_devnode(dev);

	if (!syspath || !devpath)
		return;

	modem = g_hash_table_lookup(modem_list, syspath);
	if (modem == NULL) {
		modem = g_new0(struct modem_info, 1);

		modem->type = MODEM_TYPE_SERIAL;
		modem->syspath = g_strdup(syspath);
		modem->devname = g_strdup(devname);
		modem->driver = g_strdup(driver);

		g_hash_table_replace(modem_list, modem->syspath, modem);
	}

	subsystem = udev_device_get_subsystem(dev);

	DBG("%s", syspath);
	DBG("%s", devpath);
	DBG("%s (%s)", devnode, driver);

	info = g_new0(struct serial_device_info, 1);

	info->devpath = g_strdup(devpath);
	info->devnode = g_strdup(devnode);
	info->subsystem = g_strdup(subsystem);
	info->dev = udev_device_ref(dev);

	modem->serial = info;
}

static void add_device(const char *modem_syspath, const char *modem_devname,
			const char *modem_driver, const char *modem_vendor,
			const char *modem_model, enum modem_type modem_type,
			struct udev_device *device, const char *kernel_driver)
{
	struct udev_device *usb_interface;
	const char *devnode, *interface, *number;
	const char *label, *sysattr;
	struct modem_info *modem;
	struct device_info *info;
	struct udev_device *parent;

	if (udev_device_get_syspath(device) == NULL)
		return;

	modem = g_hash_table_lookup(modem_list, modem_syspath);
	if (modem == NULL) {
		modem = g_new0(struct modem_info, 1);

		modem->type = modem_type;
		modem->syspath = g_strdup(modem_syspath);
		modem->devname = g_strdup(modem_devname);
		modem->driver = g_strdup(modem_driver);
		modem->vendor = g_strdup(modem_vendor);
		modem->model = g_strdup(modem_model);

		modem->sysattr = get_sysattr(modem_driver);

		g_hash_table_replace(modem_list, modem->syspath, modem);
	}

	if (modem->type == MODEM_TYPE_USB) {
		devnode = udev_device_get_devnode(device);
		usb_interface = udev_device_get_parent_with_subsystem_devtype(
							device, "usb",
							"usb_interface");
		if (usb_interface == NULL)
			return;

		interface = udev_device_get_property_value(usb_interface,
							"INTERFACE");
		number = udev_device_get_property_value(device,
						"ID_USB_INTERFACE_NUM");

		label = udev_device_get_property_value(device, "OFONO_LABEL");
		if (!label)
			label = udev_device_get_property_value(usb_interface,
							"OFONO_LABEL");
	} else {
		devnode = NULL;
		interface = udev_device_get_property_value(device,
							"INTERFACE");
		number = NULL;
		label = NULL;
	}

	/* If environment variable is not set, get value from attributes (or parent's ones) */
	if (number == NULL) {
		number = udev_device_get_sysattr_value(device,
							"bInterfaceNumber");

		if (number == NULL) {
			parent = udev_device_get_parent(device);
			number = udev_device_get_sysattr_value(parent,
							"bInterfaceNumber");
		}
	}

	if (modem->sysattr != NULL)
		sysattr = udev_device_get_sysattr_value(device, modem->sysattr);
	else
		sysattr = NULL;

	DBG("modem:%s device:%s",
			modem->syspath, udev_device_get_syspath(device));
	DBG("%s (%s) %s [%s] ==> %s %s", devnode, modem->driver,
					interface, number, label, sysattr);

	info = g_new0(struct device_info, 1);

	info->devnode = g_strdup(devnode);
	info->interface = g_strdup(interface);
	info->number = g_strdup(number);
	info->label = g_strdup(label);
	info->sysattr = g_strdup(sysattr);
	info->kernel_driver = g_strdup(kernel_driver);
	info->udev_device = udev_device_ref(device);

	modem->devices = g_slist_insert_sorted(modem->devices, info,
							compare_device);
}

static struct {
	const char *driver;
	const char *drv;
	const char *vid;
	const char *pid;
} vendor_list[] = {
	{ "isiusb",	"cdc_phonet"			},
	{ "linktop",	"cdc_acm",	"230d"		},
	{ "icera",	"cdc_acm",	"19d2"		},
	{ "icera",	"cdc_ether",	"19d2"		},
	{ "icera",	"cdc_acm",	"04e8", "6872"	},
	{ "icera",	"cdc_ether",	"04e8", "6872"	},
	{ "icera",	"cdc_acm",	"0421", "0633"	},
	{ "icera",	"cdc_ether",	"0421", "0633"	},
	{ "mbm",	"cdc_acm",	"0bdb"		},
	{ "mbm",	"cdc_ether",	"0bdb"		},
	{ "mbm",	"cdc_ncm",	"0bdb"		},
	{ "mbm",	"cdc_acm",	"0fce"		},
	{ "mbm",	"cdc_ether",	"0fce"		},
	{ "mbm",	"cdc_ncm",	"0fce"		},
	{ "mbm",	"cdc_acm",	"413c"		},
	{ "mbm",	"cdc_ether",	"413c"		},
	{ "mbm",	"cdc_ncm",	"413c"		},
	{ "mbim",	"cdc_mbim"			},
	{ "mbm",	"cdc_acm",	"03f0"		},
	{ "mbm",	"cdc_ether",	"03f0"		},
	{ "mbm",	"cdc_ncm",	"03f0"		},
	{ "mbm",	"cdc_acm",	"0930"		},
	{ "mbm",	"cdc_ether",	"0930"		},
	{ "mbm",	"cdc_ncm",	"0930"		},
	{ "hso",	"hso"				},
	{ "gobi",	"qmi_wwan"			},
	{ "gobi",	"qcserial"			},
	{ "gobi",	"option"			},
	{ "sierra",	"qmi_wwan",	"1199"		},
	{ "sierra",	"qcserial",	"1199"		},
	{ "sierra",	"sierra"			},
	{ "sierra",	"sierra_net"			},
	{ "option",	"option",	"0af0"		},
	{ "huawei",	"option",	"201e"		},
	{ "huawei",	"cdc_wdm",	"12d1"		},
	{ "huawei",	"cdc_ether",	"12d1"		},
	{ "huawei",	"qmi_wwan",	"12d1"		},
	{ "huawei",	"option",	"12d1"		},
	{ "speedup",	"option",	"1c9e"		},
	{ "speedup",	"option",	"2020"		},
	{ "alcatel",	"option",	"1bbb", "0017"	},
	{ "novatel",	"option",	"1410"		},
	{ "zte",	"option",	"19d2"		},
	{ "simcom",	"option",	"05c6", "9000"	},
	{ "sim7x00",	"option",	"1e0e", "9001"	},
	{ "sim7x00",	"qmi_wwan",	"1e0e",	"9001"	},
	{ "sim76xx",	"option",	"1e0e", "9011"	},
	{ "telit",	"usbserial",	"1bc7"		},
	{ "telit",	"option",	"1bc7"		},
	{ "telit",	"cdc_acm",	"1bc7", "0021"	},
	{ "telitqmi",	"qmi_wwan",	"1bc7", "1201"	},
	{ "telitqmi",	"option",	"1bc7", "1201"	},
	{ "telitqmi",	"qmi_wwan",	"1bc7", "1070"	},
	{ "telitqmi",	"option",	"1bc7", "1070"	},
	{ "droid",	"qmi_wwan",	"22b8", "2a70"	},
	{ "droid",	"option",	"22b8", "2a70"	},
	{ "nokia",	"option",	"0421", "060e"	},
	{ "nokia",	"option",	"0421", "0623"	},
	{ "samsung",	"option",	"04e8", "6889"	},
	{ "samsung",	"kalmia"			},
	{ "quectel",	"option",	"05c6", "9090"	},
	{ "quectelqmi",	"qmi_wwan",	"2c7c", "0121"	},
	{ "quectelqmi",	"qcserial",	"2c7c", "0121"	},
	{ "quectelqmi",	"qmi_wwan",	"2c7c", "0125"	},
	{ "quectelqmi",	"qcserial",	"2c7c", "0125"	},
	{ "quectelqmi",	"qmi_wwan",	"2c7c", "0195"	},
	{ "quectelqmi",	"qcserial",	"2c7c", "0195"	},
	{ "quectelqmi",	"qmi_wwan",	"2c7c", "0296"	},
	{ "quectelqmi",	"qcserial",	"2c7c", "0296"	},
	{ "quectelqmi", "qmi_wwan",	"2c7c", "0800"	},
	{ "quectelqmi", "qcserial",	"2c7c", "0800"	},
	{ "quectelqmi", "option",	"2c7c", "0800"	},
	{ "quectelqmi", "qmi_wwan_q",	"2c7c", "0452"	},
	{ "ublox",	"cdc_acm",	"1546", "1010"	},
	{ "ublox",	"cdc_ncm",	"1546", "1010"	},
	{ "ublox",	"cdc_acm",	"1546", "1102"	},
	{ "ublox",	"cdc_acm",	"1546", "110a"	},
	{ "ublox",	"cdc_ncm",	"1546", "110a"	},
	{ "ublox",	"rndis_host",	"1546", "1146"	},
	{ "ublox",	"cdc_acm",	"1546", "1146"	},
	{ "gemalto",	"option",	"1e2d",	"0053"	},
	{ "gemalto",	"cdc_wdm",	"1e2d",	"0053"	},
	{ "gemalto",	"qmi_wwan",	"1e2d",	"0053"	},
	{ "gemalto",	"cdc_acm",	"1e2d",	"0058"	},
	{ "gemalto",	"cdc_acm",	"1e2d",	"0061"	},
	{ "gemalto",	"cdc_ether",	"1e2d",	"0061"	},
	{ "gemalto",	"cdc_acm",	"1e2d",	"005b"	},
	{ "gemalto",	"cdc_ether",	"1e2d",	"005b"	},
	{ "telit",	"cdc_ncm",	"1bc7", "0036"	},
	{ "telit",	"cdc_acm",	"1bc7", "0036"	},
	{ "xmm7xxx",	"cdc_acm",	"8087"		},
	{ "xmm7xxx",	"cdc_ncm",	"8087"		},
	{ }
};

static void check_usb_device(struct udev_device *device)
{
	struct udev_device *usb_device;
	const char *syspath, *devname, *driver;
	const char *vendor = NULL, *model = NULL;
	const char *kernel_driver;

	usb_device = udev_device_get_parent_with_subsystem_devtype(device,
							"usb", "usb_device");
	if (usb_device == NULL)
		return;

	syspath = udev_device_get_syspath(usb_device);
	if (syspath == NULL)
		return;

	devname = udev_device_get_devnode(usb_device);
	if (devname == NULL)
		return;

	vendor = udev_device_get_property_value(usb_device, "ID_VENDOR_ID");
	model = udev_device_get_property_value(usb_device, "ID_MODEL_ID");

	driver = udev_device_get_property_value(usb_device, "OFONO_DRIVER");
	if (!driver) {
		struct udev_device *usb_interface =
			udev_device_get_parent_with_subsystem_devtype(
				device, "usb", "usb_interface");

		if (usb_interface)
			driver = udev_device_get_property_value(
					usb_interface, "OFONO_DRIVER");
	}

	kernel_driver = udev_device_get_property_value(device, "ID_USB_DRIVER");
	if (kernel_driver == NULL) {
		kernel_driver = udev_device_get_driver(device);
		if (kernel_driver == NULL) {
			struct udev_device *parent;

			parent = udev_device_get_parent(device);
			if (parent == NULL)
				return;

			kernel_driver = udev_device_get_driver(parent);
			if (kernel_driver == NULL)
				return;
		}
	}

	if (driver == NULL) {
		unsigned int i;

		DBG("%s [%s:%s]", kernel_driver, vendor, model);

		if (vendor == NULL || model == NULL)
			return;

		for (i = 0; vendor_list[i].driver; i++) {
			if (g_strcmp0(vendor_list[i].drv, kernel_driver))
				continue;

			if (vendor_list[i].vid) {
				if (!g_str_equal(vendor_list[i].vid, vendor))
					continue;
			}

			if (vendor_list[i].pid) {
				if (!g_str_equal(vendor_list[i].pid, model))
					continue;
			}

			driver = vendor_list[i].driver;
		}

		if (driver == NULL)
			return;
	}

	add_device(syspath, devname, driver, vendor, model, MODEM_TYPE_USB,
			device, kernel_driver);
}

static const struct {
	const char *driver;
	const char *drv;
	const char *vid;
	const char *pid;
} pci_driver_list[] = {
	{ "xmm7xxx",	"imc_ipc",	"0x8086",	"0x7560"},
	{ }
};

static void check_pci_device(struct udev_device *device)
{
	const char *syspath, *devname, *driver;
	const char *vendor = NULL, *model = NULL;
	const char *kernel_driver;
	unsigned int i;

	syspath = udev_device_get_syspath(device);

	if (syspath == NULL)
		return;

	devname = udev_device_get_devnode(device);
	vendor = udev_device_get_sysattr_value(device, "vendor");
	model = udev_device_get_sysattr_value(device, "device");
	driver = udev_device_get_property_value(device, "OFONO_DRIVER");
	kernel_driver = udev_device_get_property_value(device, "DRIVER");
	DBG("%s [%s:%s]", kernel_driver, vendor, model);

	if (vendor == NULL || model == NULL || kernel_driver == NULL)
		return;

	for (i = 0; pci_driver_list[i].driver; i++) {
		if (g_strcmp0(pci_driver_list[i].drv, kernel_driver))
			continue;

		if (pci_driver_list[i].vid) {
			if (!g_str_equal(pci_driver_list[i].vid, vendor))
				continue;
		}

		if (pci_driver_list[i].pid) {
			if (!g_str_equal(pci_driver_list[i].pid, model))
				continue;
		}

		driver = pci_driver_list[i].driver;
	}

	if (driver == NULL)
		return;

	add_device(syspath, devname, driver, vendor, model, MODEM_TYPE_PCIE,
			device, kernel_driver);
}

static const struct {
	const char *driver;
	uint16_t vend;
	uint16_t dev;
	uint16_t subvend;
	uint16_t subdev;
} wwan_driver_list[] = {
	{ "mhi",		0x17cb, 0x0308, },
	{ }
};

static int parse_pci_id(const char *id, uint16_t *out_vend, uint16_t *out_dev)
{
	_auto_(l_strv_free) char **ids = l_strsplit(id, ':');
	int r;

	if (!ids || !ids[0] || !ids[1] || ids[2])
		return -EINVAL;

	r = l_safe_atox16(ids[0], out_vend);
	if (r < 0)
		return r;

	r = l_safe_atox16(ids[1], out_dev);
	if (r < 0)
		return r;

	return 0;
}

static bool add_mhi_device(struct udev_device *device,
						struct udev_device *parent)
{
	const char *syspath;
	const char *kernel_driver;
	const char *pci_id;
	const char *pci_subid;
	uint16_t vend, dev, subvend, subdev;
	unsigned int i;

	/* Use syspath of the MHI device as the modem path */
	syspath = udev_device_get_syspath(parent);
	if (!syspath)
		return false;

	kernel_driver = udev_device_get_property_value(device, "ID_NET_DRIVER");
	if (!kernel_driver)
		kernel_driver = udev_device_get_driver(device);

	/* vid / pid is on the parent of the MHI device */
	pci_id = udev_device_get_property_value(parent, "PCI_ID");
	pci_subid = udev_device_get_property_value(parent, "PCI_SUBSYS_ID");

	if (!pci_id || !pci_subid)
		return false;

	if (parse_pci_id(pci_id, &vend, &dev) < 0)
		return false;

	if (parse_pci_id(pci_subid, &subvend, &subdev) < 0)
		return false;

	for (i = 0; wwan_driver_list[i].driver; i++) {
		if (wwan_driver_list[i].vend != vend)
			continue;

		if (wwan_driver_list[i].dev != dev)
			continue;

		if (wwan_driver_list[i].subvend &&
				wwan_driver_list[i].subvend != subvend)
			continue;

		if (wwan_driver_list[i].subdev &&
				wwan_driver_list[i].subdev != subdev)
			continue;

		add_device(syspath, NULL, wwan_driver_list[i].driver,
				NULL, NULL, MODEM_TYPE_MHI,
				device, kernel_driver);
		return true;
	}

	return false;
}

static void check_wwan_device(struct udev_device *device)
{
	struct udev_device *parent;

	parent = udev_device_get_parent_with_subsystem_devtype(device,
								"mhi", NULL);
	if (!parent)
		return;

	/* If this is an MHI device, find the MHI parent */
	parent = udev_device_get_parent(parent);
	if (!parent)
		return;

	add_mhi_device(device, parent);
}

static void check_mhi_device(struct udev_device *device)
{
	struct udev_device *parent =
		udev_device_get_parent_with_subsystem_devtype(device,
								"pci", NULL);

	if (!parent)
		return;

	add_mhi_device(device, parent);
}

static void check_net_device(struct udev_device *device)
{
	char path[32];
	const char *name;
	const char *iflink;
	struct udev_device *parent;

	parent = udev_device_get_parent(device);
	if (parent && l_streq0(udev_device_get_subsystem(parent), "mhi")) {
		parent = udev_device_get_parent_with_subsystem_devtype(device,
								"pci", NULL);
		if (parent)
			add_mhi_device(device, parent);

		return;
	}

	name = udev_device_get_sysname(device);
	if (!l_str_has_prefix(name, "rmnet_"))
		return;

	iflink = udev_device_get_sysattr_value(device, "iflink");
	if (!iflink)
		return;

	/* Collect all rmnet devices with this iflink under a common path. */
	sprintf(path, "/embedded/qrtr/%s", iflink);
	add_device(path, NULL, "gobiqrtr", NULL, NULL, MODEM_TYPE_EMBEDDED,
							device, "qrtr");
}

static void check_device(struct udev_device *device)
{
	const char *subsystem = udev_device_get_subsystem(device);
	const char *bus = udev_device_get_property_value(device, "ID_BUS");

	if (l_streq0(subsystem, "net")) {
		/* Handle USB-connected network devices in check_usb_device */
		if (l_streq0(bus, "usb"))
			check_usb_device(device);
		else
			check_net_device(device);

		return;
	}

	if (l_streq0(subsystem, "usb") || l_streq0(subsystem, "usbmisc"))
		check_usb_device(device);
	else if (l_streq0(subsystem, "pci"))
		check_pci_device(device);
	else if (l_streq0(subsystem, "wwan"))
		check_wwan_device(device);
	else if (l_streq0(subsystem, "mhi"))
		check_mhi_device(device);
	else
		add_serial_device(device);
}

static gboolean create_modem(gpointer key, gpointer value, gpointer user_data)
{
	struct modem_info *modem = value;
	const char *syspath = key;
	unsigned int i;

	if (modem->modem != NULL)
		return FALSE;

	DBG("%s", syspath);

	if (modem->devices == NULL)
		return TRUE;

	DBG("driver=%s", modem->driver);

	modem->modem = ofono_modem_create(NULL, modem->driver);
	if (modem->modem == NULL)
		return TRUE;

	for (i = 0; driver_list[i].name; i++) {
		if (g_str_equal(driver_list[i].name, modem->driver) == FALSE)
			continue;

		if (driver_list[i].setup(modem) == TRUE) {
			ofono_modem_set_string(modem->modem, "SystemPath",
								syspath);
			if (ofono_modem_register(modem->modem) < 0) {
				DBG("could not register modem '%s'", modem->driver);
				return TRUE;
			}

			return FALSE;
		}
	}

	return TRUE;
}

static void enumerate_devices(struct udev *context)
{
	struct udev_enumerate *enumerate;
	struct udev_list_entry *entry;

	DBG("");

	enumerate = udev_enumerate_new(context);
	if (enumerate == NULL)
		return;

	udev_enumerate_add_match_subsystem(enumerate, "tty");
	udev_enumerate_add_match_subsystem(enumerate, "usb");
	udev_enumerate_add_match_subsystem(enumerate, "usbmisc");
	udev_enumerate_add_match_subsystem(enumerate, "net");
	udev_enumerate_add_match_subsystem(enumerate, "hsi");
	udev_enumerate_add_match_subsystem(enumerate, "pci");
	udev_enumerate_add_match_subsystem(enumerate, "wwan");
	udev_enumerate_add_match_subsystem(enumerate, "mhi");

	udev_enumerate_scan_devices(enumerate);

	entry = udev_enumerate_get_list_entry(enumerate);
	while (entry) {
		const char *syspath = udev_list_entry_get_name(entry);
		struct udev_device *device;

		device = udev_device_new_from_syspath(context, syspath);
		if (device != NULL) {
			check_device(device);
			udev_device_unref(device);
		}

		entry = udev_list_entry_get_next(entry);
	}

	udev_enumerate_unref(enumerate);

	g_hash_table_foreach_remove(modem_list, create_modem, NULL);
}

static struct udev *udev_ctx;
static struct udev_monitor *udev_mon;
static guint udev_watch = 0;
static guint udev_delay = 0;

static gboolean check_modem_list(gpointer user_data)
{
	udev_delay = 0;

	DBG("");

	g_hash_table_foreach_remove(modem_list, create_modem, NULL);

	return FALSE;
}

static gboolean udev_event(GIOChannel *channel, GIOCondition cond,
							gpointer user_data)
{
	struct udev_device *device;
	const char *action;

	if (cond & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
		ofono_warn("Error with udev monitor channel");
		udev_watch = 0;
		return FALSE;
	}

	device = udev_monitor_receive_device(udev_mon);
	if (device == NULL)
		return TRUE;

	action = udev_device_get_action(device);
	if (action == NULL)
		return TRUE;

	if (g_str_equal(action, "add") == TRUE) {
		if (udev_delay > 0)
			g_source_remove(udev_delay);

		check_device(device);

		udev_delay = g_timeout_add_seconds(1, check_modem_list, NULL);
	} else if (g_str_equal(action, "remove") == TRUE)
		remove_device(device);

	udev_device_unref(device);

	return TRUE;
}

static void udev_start(void)
{
	GIOChannel *channel;
	int fd;

	DBG("");

	if (udev_monitor_enable_receiving(udev_mon) < 0) {
		ofono_error("Failed to enable udev monitor");
		return;
	}

	enumerate_devices(udev_ctx);

	fd = udev_monitor_get_fd(udev_mon);

	channel = g_io_channel_unix_new(fd);
	if (channel == NULL)
		return;

	udev_watch = g_io_add_watch(channel,
				G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
							udev_event, NULL);

	g_io_channel_unref(channel);
}

static int detect_init(void)
{
	udev_ctx = udev_new();
	if (udev_ctx == NULL) {
		ofono_error("Failed to create udev context");
		return -EIO;
	}

	udev_mon = udev_monitor_new_from_netlink(udev_ctx, "udev");
	if (udev_mon == NULL) {
		ofono_error("Failed to create udev monitor");
		udev_unref(udev_ctx);
		udev_ctx = NULL;
		return -EIO;
	}

	modem_list = g_hash_table_new_full(g_str_hash, g_str_equal,
						NULL, destroy_modem);

	udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "tty", NULL);
	udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "usb", NULL);
	udev_monitor_filter_add_match_subsystem_devtype(udev_mon,
							"usbmisc", NULL);
	udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "net", NULL);
	udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "hsi", NULL);
	udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "wwan", NULL);
	udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "mhi", NULL);

	udev_monitor_filter_update(udev_mon);

	udev_start();

	return 0;
}

static void detect_exit(void)
{
	if (udev_delay > 0)
		g_source_remove(udev_delay);

	if (udev_watch > 0)
		g_source_remove(udev_watch);

	if (udev_ctx == NULL)
		return;

	udev_monitor_filter_remove(udev_mon);

	g_hash_table_destroy(modem_list);

	udev_monitor_unref(udev_mon);
	udev_unref(udev_ctx);
}

OFONO_PLUGIN_DEFINE(udevng, "udev hardware detection", VERSION,
		OFONO_PLUGIN_PRIORITY_DEFAULT, detect_init, detect_exit)
