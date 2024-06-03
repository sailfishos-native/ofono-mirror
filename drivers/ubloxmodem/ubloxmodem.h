/*
 * oFono - Open Source Telephony
 * Copyright (C) 2016  Endocode AG
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <drivers/atmodem/atutil.h>

#define UBLOXMODEM "ubloxmodem"

enum ublox_flags {
	UBLOX_F_TOBY_L2		= (1 << 0),
	UBLOX_F_TOBY_L4		= (1 << 1),
	UBLOX_F_LARA_R2		= (1 << 2),
	UBLOX_F_HAVE_USBCONF	= (1 << 3),
};

struct ublox_model {
	char *name;
	int flags;
};

const struct ublox_model *ublox_model_from_name(const char *name);
const struct ublox_model *ublox_model_from_id(int id);
int ublox_model_to_id(const struct ublox_model *model);
int ublox_is_toby_l2(const struct ublox_model *model);
int ublox_is_toby_l4(const struct ublox_model *model);
