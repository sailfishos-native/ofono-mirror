/*
 * oFono - Open Source Telephony
 * Copyright (C) 2016  Endocode AG
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib.h>
#include <gatchat.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/plugin.h>
#include <ofono/types.h>
#include <ofono/modem.h>

#include "ubloxmodem.h"

const struct ublox_model ublox_models[] = {
	{
		.name = "SARA-G270",
	},
	/* TOBY L2 series */
	{
		.name = "TOBY-L200",
		.flags = UBLOX_F_TOBY_L2|UBLOX_F_HAVE_USBCONF,
	},
	{
		.name = "TOBY-L201",
		.flags = UBLOX_F_TOBY_L2|UBLOX_F_HAVE_USBCONF,
	},
	{
		.name = "TOBY-L210",
		.flags = UBLOX_F_TOBY_L2|UBLOX_F_HAVE_USBCONF,
	},
	{
		.name = "TOBY-L220",
		.flags = UBLOX_F_TOBY_L2|UBLOX_F_HAVE_USBCONF,
	},
	{
		.name = "TOBY-L280",
		.flags = UBLOX_F_TOBY_L2|UBLOX_F_HAVE_USBCONF,
	},
	/* TOBY L4 series */
	{
		.name = "TOBY-L4006",
		.flags = UBLOX_F_TOBY_L4,
	},
	{
		.name = "TOBY-L4106",
		.flags = UBLOX_F_TOBY_L4,
	},
	{
		.name = "TOBY-L4206",
		.flags = UBLOX_F_TOBY_L4,
	},
	{
		.name = "TOBY-L4906",
		.flags = UBLOX_F_TOBY_L4,
	},
	/* LARA L2 series */
	{
		.name = "LARA-R202",
		.flags = UBLOX_F_LARA_R2,
	},
	{
		.name = "LARA-R211",
		.flags = UBLOX_F_LARA_R2,
	},
	{ /* sentinel */ },
};

const struct ublox_model *ublox_model_from_name(const char *name)
{
	const struct ublox_model *m;

	for (m = ublox_models; m->name; m++) {
		if (!strcmp(name, m->name))
			return m;
	}

	return NULL;
}

const struct ublox_model *ublox_model_from_id(int id)
{
	return ublox_models + id;
}

int ublox_model_to_id(const struct ublox_model *model)
{
	return model - ublox_models;
}

int ublox_is_toby_l2(const struct ublox_model *model)
{
	return model->flags & UBLOX_F_TOBY_L2;
}

int ublox_is_toby_l4(const struct ublox_model *model)
{
	return model->flags & UBLOX_F_TOBY_L4;
}
