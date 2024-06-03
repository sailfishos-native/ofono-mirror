/*
 * oFono - Open Source Telephony
 * Copyright (C) 2009-2010  Nokia Corporation and/or its subsidiary(-ies)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __ISIMODEM_GSS_H
#define __ISIMODEM_GSS_H

#ifdef __cplusplus
extern "C" {
#endif

#define PN_GSS				0x32
#define GSS_TIMEOUT			5

enum gss_message_id {
	GSS_CS_SERVICE_REQ =		0x00,
	GSS_CS_SERVICE_RESP =		0x01,
	GSS_CS_SERVICE_FAIL_RESP =	0x02,
};

enum gss_subblock {
	GSS_RAT_INFO =			0x0B,
};

enum gss_selection_mode {
	GSS_DUAL_RAT =			0x00,
	GSS_GSM_RAT =			0x01,
	GSS_UMTS_RAT =			0x02,
};

enum gss_operation {
	GSS_SELECTED_RAT_WRITE =	0x0E,
	GSS_SELECTED_RAT_READ =		0x9C,
};

#ifdef __cplusplus
};
#endif

#endif /* !__ISIMODEM_GSS_H */
