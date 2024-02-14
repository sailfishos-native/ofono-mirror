/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2017 by sysmocom s.f.m.c. GmbH <info@sysmocom.de>
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
 */

#define QMI_VOICE_PARAM_USS_DATA 0x01

#define QMI_VOICE_PARAM_ASYNC_USSD_ERROR 0x10
#define QMI_VOICE_PARAM_ASYNC_USSD_FAILURE_CASE 0x11
#define QMI_VOICE_PARAM_ASYNC_USSD_DATA 0x12

#define QMI_VOICE_PARAM_USSD_IND_USER_ACTION 0x01
#define QMI_VOICE_PARAM_USSD_IND_DATA 0x10
#define QMI_VOICE_PARAM_USSD_IND_UCS2 0x11

/* according to GSM TS 23.038 section 5
 * coding group 1111, No message class, 8 bit data
 */
#define USSD_DCS_8BIT 0xf4
/* coding group 01xx, Class 0, UCS2 (16 bit) */
#define USSD_DCS_UCS2 0x48
/* default alphabet Language unspecific */
#define USSD_DCS_UNSPECIFIC 0x0f

/* based on qmi ussd definition */
enum qmi_ussd_dcs {
	QMI_USSD_DCS_ASCII = 0x1,
	QMI_USSD_DCS_8BIT,
	QMI_USSD_DCS_UCS2,
};

enum qmi_ussd_user_required {
	QMI_USSD_NO_USER_ACTION_REQUIRED = 0x1,
	QMI_USSD_USER_ACTION_REQUIRED,
};

/* QMI service voice. Using an enum to prevent doublicated entries */
enum voice_commands {
	QMI_VOICE_SUPS_NOTIFICATION_IND =	0x32,
	QMI_VOICE_SET_SUPS_SERVICE =		0x33,
	QMI_VOICE_GET_CALL_WAITING =		0x34,
	QMI_VOICE_GET_CLIP =			0x36,
	QMI_VOICE_GET_CLIR =			0x37,
	QMI_VOICE_CANCEL_USSD =			0x3c,
	QMI_VOICE_USSD_RELEASE_IND =		0x3d,
	QMI_VOICE_USSD_IND =			0x3e,
	QMI_VOICE_SUPS_IND =			0x42,
	QMI_VOICE_ASYNC_ORIG_USSD =		0x43,
	QMI_VOICE_GET_COLP =			0x4b,
	QMI_VOICE_GET_COLR =			0x4c,
	QMI_VOICE_GET_CNAP =			0x4d
};

struct qmi_ussd_data {
	uint8_t dcs;
	uint8_t length;
	uint8_t data[0];
} __attribute__((__packed__));

enum qmi_ss_action {
	QMI_VOICE_SS_ACTION_ACTIVATE =		0x01,
	QMI_VOICE_SS_ACTION_DEACTIVATE =	0x02,
	QMI_VOICE_SS_ACTION_REGISTER =		0x03,
	QMI_VOICE_SS_ACTION_ERASE =		0x04
};

enum qmi_ss_reason {
	QMI_VOICE_SS_RSN_FWD_UNCONDITIONAL =	0x01,
	QMI_VOICE_SS_RSN_FWD_MOBILE_BUSY =	0x02,
	QMI_VOICE_SS_RSN_FWD_NO_REPLY  =	0x03,
	QMI_VOICE_SS_RSN_FWD_UNREACHABLE =	0x04,
	QMI_VOICE_SS_RSN_FWD_ALL =		0x05,
	QMI_VOICE_SS_RSN_FWD_ALL_CONDITIONAL =	0x06,
	QMI_VOICE_SS_RSN_ALL_OUTGOING =		0x07,
	QMI_VOICE_SS_RSN_OUT_INT =		0x08,
	QMI_VOICE_SS_RSN_OUT_INT_EXT_TO_HOME =	0x09,
	QMI_VOICE_SS_RSN_ALL_IN =		0x0A,
	QMI_VOICE_SS_RSN_IN_ROAMING =		0x0B,
	QMI_VOICE_SS_RSN_BAR_ALL =		0x0C,
	QMI_VOICE_SS_RSN_BAR_ALL_OUTGOING =	0x0D,
	QMI_VOICE_SS_RSN_BAR_ALL_IN =		0x0E,
	QMI_VOICE_SS_RSN_CALL_WAITING =		0x0F,
	QMI_VOICE_SS_RSN_CLIP =			0x10,
	QMI_VOICE_SS_RSN_CLIR =			0x11
};
