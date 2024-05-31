/*
 * oFono - Open Source Telephony
 * Copyright (C) 2011-2012  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#define QMI_PDS_RESET		0	/* Reset PDS service state variables */
#define QMI_PDS_EVENT		1	/* PDS report indication */
#define QMI_PDS_SET_EVENT	1	/* Set PDS report conditions */

#define QMI_PDS_GET_STATE	32	/* Return PDS service state */
#define QMI_PDS_STATE_IND	32	/* PDS service state indication */

#define QMI_PDS_GET_AUTOTRACK	48	/* Get the service auto-tracking state */
#define QMI_PDS_SET_AUTOTRACK	49	/* Set the service auto-tracking state */


/* PDS report indication */
#define QMI_PDS_NOTIFY_NMEA			0x10	/* string */
#define QMI_PDS_NOTIFY_NMEA_DEBUG		0x25	/* string */

/* Set PDS report conditions */
#define QMI_PDS_PARAM_REPORT_NMEA		0x10	/* bool */
#define QMI_PDS_PARAM_REPORT_NMEA_DEBUG		0x22	/* bool */

/* Get the service auto-tracking state */
#define QMI_PDS_RESULT_AUTO_TRACKING		0x01	/* bool */

/* Set the service auto-tracking state */
#define QMI_PDS_PARAM_AUTO_TRACKING		0x01	/* bool */
