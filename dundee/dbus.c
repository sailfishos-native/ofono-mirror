/*
 * oFono - Open Source Telephony
 * Copyright (C) 2012  BMW Car IT GmbH
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <gdbus.h>

#include "dundee.h"

#define DUNDEE_ERROR_INTERFACE "org.ofono.dundee.Error"

DBusMessage *__dundee_error_invalid_args(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DUNDEE_ERROR_INTERFACE
					".InvalidArguments",
					"Invalid arguments in method call");
}

DBusMessage *__dundee_error_failed(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DUNDEE_ERROR_INTERFACE
					".Failed",
					"Operation failed");
}

DBusMessage *__dundee_error_in_progress(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DUNDEE_ERROR_INTERFACE
					".InProgress",
					"Operation already in progress");
}

DBusMessage *__dundee_error_timed_out(DBusMessage *msg)
{
	return g_dbus_create_error(msg, DUNDEE_ERROR_INTERFACE ".Timedout",
			"Operation failure due to timeout");
}
