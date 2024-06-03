/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <gatchat.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/plugin.h>
#include <ofono/types.h>

#include "swmodem.h"

static int swmodem_init(void)
{
	sw_gprs_context_init();

	return 0;
}

static void swmodem_exit(void)
{
	sw_gprs_context_exit();
}

OFONO_PLUGIN_DEFINE(swmodem, "Sierra modem driver", VERSION,
			OFONO_PLUGIN_PRIORITY_DEFAULT,
			swmodem_init, swmodem_exit)
