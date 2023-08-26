/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2009-2010  Nokia Corporation and/or its subsidiary(-ies).
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

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/plugin.h>
#include <ofono/log.h>

#include "isimodem.h"

static int isimodem_init(void)
{
	isi_netreg_init();
	isi_gprs_init();
	isi_gprs_context_init();

	return 0;
}

static void isimodem_exit(void)
{
	isi_netreg_exit();
	isi_gprs_exit();
	isi_gprs_context_exit();
}

OFONO_PLUGIN_DEFINE(isimodem, "PhoNet / ISI modem driver", VERSION,
		OFONO_PLUGIN_PRIORITY_DEFAULT, isimodem_init, isimodem_exit)
