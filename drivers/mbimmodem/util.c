/*
 * oFono - Open Source Telephony
 * Copyright (C) 2017  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdint.h>
#include <stdbool.h>

#include "src/common.h"
#include "mbim.h"
#include "util.h"

int mbim_data_class_to_tech(uint32_t n)
{
	if (n & MBIM_DATA_CLASS_LTE)
		return ACCESS_TECHNOLOGY_EUTRAN;

	if (n & (MBIM_DATA_CLASS_HSUPA | MBIM_DATA_CLASS_HSDPA))
		return ACCESS_TECHNOLOGY_UTRAN_HSDPA_HSUPA;

	if (n & MBIM_DATA_CLASS_HSUPA)
		return ACCESS_TECHNOLOGY_UTRAN_HSUPA;

	if (n & MBIM_DATA_CLASS_HSDPA)
		return ACCESS_TECHNOLOGY_UTRAN_HSDPA;

	if (n & MBIM_DATA_CLASS_UMTS)
		return ACCESS_TECHNOLOGY_UTRAN;

	if (n & MBIM_DATA_CLASS_EDGE)
		return ACCESS_TECHNOLOGY_GSM_EGPRS;

	if (n & MBIM_DATA_CLASS_GPRS)
		return ACCESS_TECHNOLOGY_GSM;

	return -1;
}

