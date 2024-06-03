/*
 * oFono - Open Source Telephony
 * Copyright (C) 2013  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#define BLUEZ_SERVICE			"org.bluez"
#define BLUEZ_MANAGER_PATH		"/"
#define BLUEZ_PROFILE_INTERFACE		BLUEZ_SERVICE ".Profile1"
#define BLUEZ_DEVICE_INTERFACE		BLUEZ_SERVICE ".Device1"
#define BLUEZ_ERROR_INTERFACE		BLUEZ_SERVICE ".Error"

#define DUN_GW_UUID	"00001103-0000-1000-8000-00805f9b34fb"
#define HFP_HS_UUID	"0000111e-0000-1000-8000-00805f9b34fb"
#define HFP_AG_UUID	"0000111f-0000-1000-8000-00805f9b34fb"

int bt_register_profile(DBusConnection *conn, const char *uuid,
					uint16_t version, const char *name,
					const char *object, const char *role,
					uint16_t features);

void bt_unregister_profile(DBusConnection *conn, const char *object);

typedef void (*bt_finish_cb)(gboolean success, gpointer user_data);

void bt_connect_profile(DBusConnection *conn,
				const char *device, const char *uuid,
				bt_finish_cb cb, gpointer user_data);
