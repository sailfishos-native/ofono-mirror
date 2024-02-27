/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2016  Endocode AG. All rights reserved.
 *  Copyright (C) 2018 Gemalto M2M
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
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>

#include <glib.h>
#include <gdbus.h>
#include <ell/ell.h>

#include "ofono.h"

#include "provisiondb.h"
#include "common.h"
#include "storage.h"

#define SETTINGS_STORE "lte"
#define SETTINGS_GROUP "Settings"
#define LTE_APN "DefaultAccessPointName"
#define LTE_PROTO "Protocol"
#define LTE_USERNAME "Username"
#define LTE_PASSWORD "Password"
#define LTE_AUTH_METHOD "AuthenticationMethod"

struct ofono_lte {
	const struct ofono_lte_driver *driver;
	void *driver_data;
	struct ofono_atom *atom;
	struct l_settings *settings;
	DBusMessage *pending;
	struct ofono_lte_default_attach_info pending_info;
	struct ofono_lte_default_attach_info info;
	unsigned int spn_watch;
};

static bool provision_default_attach_info(struct ofono_lte *lte,
						const char *mcc, const char *mnc,
						const char *spn)
{
	_auto_(l_free) struct provision_db_entry *settings = NULL;
	const struct provision_db_entry *ap = NULL;
	size_t count;
	size_t i;

	DBG("Provisioning default bearer info with mcc:'%s', mnc:'%s', spn:'%s'",
			mcc, mnc, spn);

	if (!__ofono_provision_get_settings(mcc, mnc, spn, &settings, &count))
		return false;

	DBG("Obtained %zu candidates", count);

	for (i = 0; i < count; i++) {
		if (settings[i].type & OFONO_GPRS_CONTEXT_TYPE_IA) {
			ap = &settings[i];
			break;
		}
	}

	if (!ap || !is_valid_apn(ap->apn))
		return false;

	if (ap->username && strlen(ap->username) >
			OFONO_GPRS_MAX_USERNAME_LENGTH)
		return false;

	if (ap->password && strlen(ap->password) >
			OFONO_GPRS_MAX_PASSWORD_LENGTH)
		return false;

	l_strlcpy(lte->info.apn, ap->apn, sizeof(lte->info.apn));
	l_strlcpy(lte->info.username, ap->username, sizeof(lte->info.username));
	l_strlcpy(lte->info.password, ap->password, sizeof(lte->info.password));
	lte->info.proto = ap->proto;
	lte->info.auth_method = ap->auth_method;

	DBG("Provisioned successfully");
	return true;
}

static int lte_load_settings(struct ofono_lte *lte)
{
	struct ofono_modem *modem = __ofono_atom_get_modem(lte->atom);
	struct ofono_sim *sim = __ofono_atom_find(OFONO_ATOM_TYPE_SIM, modem);
	const char *imsi = ofono_sim_get_imsi(sim);
	_auto_(l_free) char *path = NULL;
	_auto_(l_free) char *apn = NULL;
	const char *proto_str;
	const char *auth_method_str;
	_auto_(l_free) char *username = NULL;
	_auto_(l_free) char *password = NULL;

	if (L_WARN_ON(!sim || !imsi))
		return -ENOKEY;

	path = storage_get_file_path(imsi, SETTINGS_STORE);
	if (!l_settings_load_from_file(lte->settings, path))
		return -ENOENT;

	apn = l_settings_get_string(lte->settings, SETTINGS_GROUP, LTE_APN);
	proto_str = l_settings_get_value(lte->settings, SETTINGS_GROUP,
						LTE_PROTO);
	auth_method_str = l_settings_get_value(lte->settings, SETTINGS_GROUP,
						LTE_AUTH_METHOD);
	username = l_settings_get_string(lte->settings, SETTINGS_GROUP,
						LTE_USERNAME);
	password = l_settings_get_string(lte->settings, SETTINGS_GROUP,
						LTE_PASSWORD);

	if (!gprs_auth_method_from_string(auth_method_str,
							&lte->info.auth_method))
		lte->info.auth_method = OFONO_GPRS_AUTH_METHOD_NONE;

	if (!gprs_proto_from_string(proto_str, &lte->info.proto))
		lte->info.proto = OFONO_GPRS_PROTO_IP;

	if (apn && is_valid_apn(apn))
		strcpy(lte->info.apn, apn);

	if (username && strlen(username) <= OFONO_GPRS_MAX_USERNAME_LENGTH)
		strcpy(lte->info.username, username);

	if (password && strlen(password) <= OFONO_GPRS_MAX_PASSWORD_LENGTH)
		strcpy(lte->info.password, password);

	return 0;
}

static void lte_save_settings(struct ofono_lte *lte)
{
	struct ofono_modem *modem = __ofono_atom_get_modem(lte->atom);
	struct ofono_sim *sim = __ofono_atom_find(OFONO_ATOM_TYPE_SIM, modem);
	const char *imsi = ofono_sim_get_imsi(sim);
	_auto_(l_free) char *path = NULL;
	_auto_(l_free) char *data = NULL;
	size_t len;

	if (!imsi)
		return;

	data = l_settings_to_data(lte->settings, &len);
	if (!data)
		return;

	path = storage_get_file_path(imsi, SETTINGS_STORE);

	L_WARN_ON(write_file(data, len, "%s", path) < 0);
}

static DBusMessage *lte_get_properties(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct ofono_lte *lte = data;
	const char *proto = gprs_proto_to_string(lte->info.proto);
	const char *apn = lte->info.apn;
	const char* auth_method =
			gprs_auth_method_to_string(lte->info.auth_method);
	const char *username = lte->info.username;
	const char *password = lte->info.password;
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter dict;

	reply = dbus_message_new_method_return(msg);
	if (reply == NULL)
		return NULL;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					OFONO_PROPERTIES_ARRAY_SIGNATURE,
					&dict);
	ofono_dbus_dict_append(&dict, LTE_APN, DBUS_TYPE_STRING, &apn);
	ofono_dbus_dict_append(&dict, LTE_PROTO, DBUS_TYPE_STRING, &proto);
	ofono_dbus_dict_append(&dict, LTE_AUTH_METHOD, DBUS_TYPE_STRING,
					&auth_method);
	ofono_dbus_dict_append(&dict, LTE_USERNAME, DBUS_TYPE_STRING,
					&username);
	ofono_dbus_dict_append(&dict, LTE_PASSWORD, DBUS_TYPE_STRING,
					&password);
	dbus_message_iter_close_container(&iter, &dict);

	return reply;
}

static void lte_set_default_attach_info_cb(const struct ofono_error *error,
								void *data)
{
	struct ofono_lte *lte = data;
	const char *path = __ofono_atom_get_path(lte->atom);
	DBusConnection *conn = ofono_dbus_get_connection();
	DBusMessage *reply;
	char *key;
	char *value;
	const char *str;
	DBusMessageIter iter;
	DBusMessageIter var;

	DBG("%s error %d", path, error->type);

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		__ofono_dbus_pending_reply(&lte->pending,
				__ofono_error_failed(lte->pending));
		return;
	}

	/*
	 * Reparsing of the message to extract the key and value
	 * No error checking needed since we already validated pending
	 */
	dbus_message_iter_init(lte->pending, &iter);
	dbus_message_iter_get_basic(&iter, &str);
	key = l_strdup(str);

	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &var);
	dbus_message_iter_get_basic(&var, &str);
	value = l_strdup(str);

	memcpy(&lte->info, &lte->pending_info, sizeof(lte->info));

	reply = dbus_message_new_method_return(lte->pending);
	__ofono_dbus_pending_reply(&lte->pending, reply);

	if (lte->settings) {
		/*
		 * the following code removes from storage empty APN, user, pwd
		 * for proto and auth_method, given that they always
		 * have defaults, it will not do anything.
		 */
		if (!*value)
			/* Clear entry on empty string. */
			l_settings_remove_key(lte->settings,
						SETTINGS_GROUP, key);
		else
			l_settings_set_string(lte->settings,
						SETTINGS_GROUP, key, value);

		lte_save_settings(lte);
	}

	ofono_dbus_signal_property_changed(conn, path,
					OFONO_LTE_INTERFACE,
					key,
					DBUS_TYPE_STRING, &value);

	l_free(value);
	l_free(key);
}

static DBusMessage *lte_set_property(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct ofono_lte *lte = data;
	DBusMessageIter iter;
	DBusMessageIter var;
	const char *property;
	const char *str;
	enum ofono_gprs_auth_method auth_method;
	enum ofono_gprs_proto proto;

	if (lte->driver->set_default_attach_info == NULL)
		return __ofono_error_not_implemented(msg);

	if (lte->pending)
		return __ofono_error_busy(msg);

	if (!dbus_message_iter_init(msg, &iter))
		return __ofono_error_invalid_args(msg);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_get_basic(&iter, &property);
	dbus_message_iter_next(&iter);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_recurse(&iter, &var);

	if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_get_basic(&var, &str);

	memcpy(&lte->pending_info, &lte->info, sizeof(lte->info));

	if ((strcmp(property, LTE_APN) == 0)) {
		if (l_streq0(str, lte->info.apn))
			return dbus_message_new_method_return(msg);

		/* We do care about empty value: it can be used for reset. */
		if (is_valid_apn(str) == FALSE && str[0] != '\0')
			return __ofono_error_invalid_format(msg);

		l_strlcpy(lte->pending_info.apn, str,
					OFONO_GPRS_MAX_APN_LENGTH + 1);
	} else if ((strcmp(property, LTE_PROTO) == 0)) {
		if (!gprs_proto_from_string(str, &proto))
			return __ofono_error_invalid_format(msg);

		if (proto == lte->info.proto)
			return dbus_message_new_method_return(msg);

		lte->pending_info.proto = proto;
	} else if (strcmp(property, LTE_AUTH_METHOD) == 0) {
		if (!gprs_auth_method_from_string(str, &auth_method))
			return __ofono_error_invalid_format(msg);

		if (auth_method == lte->info.auth_method)
			return dbus_message_new_method_return(msg);

		lte->pending_info.auth_method = auth_method;
	} else if (strcmp(property, LTE_USERNAME) == 0) {
		if (strlen(str) > OFONO_GPRS_MAX_USERNAME_LENGTH)
			return __ofono_error_invalid_format(msg);

		if (l_streq0(str, lte->info.username))
			return dbus_message_new_method_return(msg);

		l_strlcpy(lte->pending_info.username, str,
					OFONO_GPRS_MAX_USERNAME_LENGTH + 1);
	} else if (strcmp(property, LTE_PASSWORD) == 0) {
		if (strlen(str) > OFONO_GPRS_MAX_PASSWORD_LENGTH)
			return __ofono_error_invalid_format(msg);

		if (l_streq0(str, lte->info.password))
			return dbus_message_new_method_return(msg);

		l_strlcpy(lte->pending_info.password, str,
					OFONO_GPRS_MAX_PASSWORD_LENGTH + 1);
	} else
		return __ofono_error_invalid_args(msg);

	lte->pending = dbus_message_ref(msg);
	lte->driver->set_default_attach_info(lte, &lte->pending_info,
					lte_set_default_attach_info_cb, lte);

	return NULL;
}

static const GDBusMethodTable lte_methods[] = {
	{ GDBUS_METHOD("GetProperties",
			NULL, GDBUS_ARGS({ "properties", "a{sv}" }),
			lte_get_properties) },
	{ GDBUS_ASYNC_METHOD("SetProperty",
			GDBUS_ARGS({ "property", "s" }, { "value", "v" }),
			NULL, lte_set_property) },
	{ }
};

static const GDBusSignalTable lte_signals[] = {
	{ GDBUS_SIGNAL("PropertyChanged",
			GDBUS_ARGS({ "name", "s" }, { "value", "v" })) },
	{ }
};

static void lte_remove(struct ofono_atom *atom)
{
	struct ofono_lte *lte = __ofono_atom_get_data(atom);

	DBG("atom: %p", atom);

	l_settings_free(lte->settings);

	if (lte->driver && lte->driver->remove)
		lte->driver->remove(lte);

	g_free(lte);
}

OFONO_DEFINE_ATOM_CREATE(lte, OFONO_ATOM_TYPE_LTE, {
	atom->settings = l_settings_new();
	atom->info.proto = OFONO_GPRS_PROTO_IP;
	atom->info.auth_method = OFONO_GPRS_AUTH_METHOD_NONE;
})

static void lte_atom_unregister(struct ofono_atom *atom)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_modem *modem = __ofono_atom_get_modem(atom);
	const char *path = __ofono_atom_get_path(atom);
	struct ofono_lte *lte = __ofono_atom_get_data(atom);
	struct ofono_sim *sim = __ofono_atom_find(OFONO_ATOM_TYPE_SIM, modem);

	if (lte->spn_watch)
		ofono_sim_remove_spn_watch(sim, &lte->spn_watch);

	ofono_modem_remove_interface(modem, OFONO_LTE_INTERFACE);
	g_dbus_unregister_interface(conn, path, OFONO_LTE_INTERFACE);
}

static void ofono_lte_finish_register(struct ofono_lte *lte)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_modem *modem = __ofono_atom_get_modem(lte->atom);
	const char *path = __ofono_atom_get_path(lte->atom);

	if (!g_dbus_register_interface(conn, path,
				OFONO_LTE_INTERFACE,
				lte_methods, lte_signals, NULL,
				lte, NULL)) {
		ofono_error("could not create %s interface",
				OFONO_LTE_INTERFACE);
		return;
	}

	ofono_modem_add_interface(modem, OFONO_LTE_INTERFACE);

	__ofono_atom_register(lte->atom, lte_atom_unregister);
}

static void lte_init_default_attach_info_cb(const struct ofono_error *error,
						void *data)
{
	struct ofono_lte *lte = data;

	ofono_lte_finish_register(lte);
}

static void spn_read_cb(const char *spn, const char *dc, void *data)
{
	struct ofono_lte *lte = data;
	struct ofono_modem *modem = __ofono_atom_get_modem(lte->atom);
	struct ofono_sim *sim = __ofono_atom_find(OFONO_ATOM_TYPE_SIM, modem);
	bool r;

	ofono_sim_remove_spn_watch(sim, &lte->spn_watch);

	r = provision_default_attach_info(lte, ofono_sim_get_mcc(sim),
						ofono_sim_get_mnc(sim), spn);
	if (r) {
		const char *str;

		if (lte->info.apn[0])
			l_settings_set_string(lte->settings, SETTINGS_GROUP,
						LTE_APN, lte->info.apn);

		if (lte->info.username[0])
			l_settings_set_string(lte->settings, SETTINGS_GROUP,
					LTE_USERNAME, lte->info.username);

		if (lte->info.password[0])
			l_settings_set_string(lte->settings, SETTINGS_GROUP,
					LTE_PASSWORD, lte->info.password);

		str = gprs_proto_to_string(lte->info.proto);
		l_settings_set_string(lte->settings, SETTINGS_GROUP,
					LTE_PROTO, str);

		str = gprs_auth_method_to_string(lte->info.auth_method);
		l_settings_set_string(lte->settings, SETTINGS_GROUP,
					LTE_AUTH_METHOD, str);

		lte_save_settings(lte);
	}

	if (lte->driver->set_default_attach_info) {
		lte->driver->set_default_attach_info(lte, &lte->info,
					lte_init_default_attach_info_cb, lte);
		return;
	}

	ofono_lte_finish_register(lte);
}

void ofono_lte_register(struct ofono_lte *lte)
{
	/* Wait for SPN to be read in order to try provisioning */
	if (lte_load_settings(lte) < 0) {
		struct ofono_modem *modem = __ofono_atom_get_modem(lte->atom);
		struct ofono_sim *sim = __ofono_atom_find(OFONO_ATOM_TYPE_SIM,
								modem);

		ofono_sim_add_spn_watch(sim, &lte->spn_watch,
						spn_read_cb, lte, NULL);
		return;
	}

	if (lte->driver->set_default_attach_info) {
		lte->driver->set_default_attach_info(lte, &lte->info,
					lte_init_default_attach_info_cb, lte);
		return;
	}

	ofono_lte_finish_register(lte);
}

void ofono_lte_remove(struct ofono_lte *lte)
{
	if (!lte)
		return;

	__ofono_atom_free(lte->atom);
}

void ofono_lte_set_data(struct ofono_lte *lte, void *data)
{
	lte->driver_data = data;
}

void *ofono_lte_get_data(const struct ofono_lte *lte)
{
	return lte->driver_data;
}

struct ofono_modem *ofono_lte_get_modem(const struct ofono_lte *lte)
{
	return __ofono_atom_get_modem(lte->atom);
}
