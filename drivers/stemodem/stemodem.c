/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 * Copyright (C) 2010  ST-Ericsson AB
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

#include "caif_rtnl.h"

static int stemodem_init(void)
{
	caif_rtnl_init();

	return 0;
}

static void stemodem_exit(void)
{
	caif_rtnl_exit();
}

OFONO_PLUGIN_DEFINE(stemodem, "STE modem driver", VERSION,
			OFONO_PLUGIN_PRIORITY_DEFAULT,
			stemodem_init, stemodem_exit)
