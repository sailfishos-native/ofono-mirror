/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2016  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_NETMON_H
#define __OFONO_NETMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#include <ofono/types.h>

struct ofono_netmon;

typedef void (*ofono_netmon_cb_t)(const struct ofono_error *error, void *data);

struct ofono_netmon_driver {
	int (*probe)(struct ofono_netmon *netmon, unsigned int vendor,
					void *data);
	int (*probev)(struct ofono_netmon *netmon, unsigned int vendor,
					va_list args);
	void (*remove)(struct ofono_netmon *netmon);
	void (*request_update)(struct ofono_netmon *netmon,
					ofono_netmon_cb_t cb, void *data);
	void (*enable_periodic_update)(struct ofono_netmon *netmon,
					unsigned int enable,
					unsigned int period,
					ofono_netmon_cb_t cb, void *data);
	void (*neighbouring_cell_update)(struct ofono_netmon *netmon,
					ofono_netmon_cb_t cb, void *data);
};

enum ofono_netmon_cell_type {
	OFONO_NETMON_CELL_TYPE_GSM,
	OFONO_NETMON_CELL_TYPE_UMTS,
	OFONO_NETMON_CELL_TYPE_LTE,
};

enum ofono_netmon_info {
	OFONO_NETMON_INFO_MCC, /* char *, up to 3 digits + null */
	OFONO_NETMON_INFO_MNC, /* char *, up to 3 digits + null */
	OFONO_NETMON_INFO_LAC, /* int */
	OFONO_NETMON_INFO_CI, /* int */
	OFONO_NETMON_INFO_ARFCN, /* int */
	OFONO_NETMON_INFO_BSIC, /* int */
	OFONO_NETMON_INFO_RXLEV, /* int */
	OFONO_NETMON_INFO_BER, /* int */
	OFONO_NETMON_INFO_RSSI, /* int */
	OFONO_NETMON_INFO_TIMING_ADVANCE, /* int */
	OFONO_NETMON_INFO_PSC, /* int */
	OFONO_NETMON_INFO_RSCP, /* int */
	OFONO_NETMON_INFO_ECN0, /* int */
	OFONO_NETMON_INFO_RSRQ, /* int */
	OFONO_NETMON_INFO_RSRP, /* int */
	OFONO_NETMON_INFO_EARFCN, /* int */
	OFONO_NETMON_INFO_EBAND, /* int */
	OFONO_NETMON_INFO_CQI, /* int */
	OFONO_NETMON_INFO_PCI, /* int */
	OFONO_NETMON_INFO_TAC, /* int */
	OFONO_NETMON_INFO_SNR, /* int */
	OFONO_NETMON_INFO_INVALID,
};

/*
 * Examples:
 * ofono_netmon_serving_cell_notify(netmon, OFONO_NETMON_CELL_TYPE_GSM,
 *					OFONO_NETMON_INFO_MCC, "123",
 *					OFONO_NETMON_INFO_MNC, "456",
 *					OFONO_NETMON_INFO_LAC, lac,
 *					OFONO_NETMON_INFO_CI, ci,
 *					OFONO_NETMON_INFO_RSSI, rssi,
 *					OFONO_NETMON_INFO_RXLEV, rxlev,
 *					OFONO_NETMON_INFO_INVALID);
 */
void ofono_netmon_serving_cell_notify(struct ofono_netmon *netmon,
					enum ofono_netmon_cell_type type,
					int info_type, ...);

struct ofono_netmon *ofono_netmon_create(struct ofono_modem *modem,
						unsigned int vendor,
						const char *driver, ...);

void ofono_netmon_register(struct ofono_netmon *netmon);

void ofono_netmon_remove(struct ofono_netmon *netmon);

void ofono_netmon_set_data(struct ofono_netmon *netmon, void *data);

void *ofono_netmon_get_data(struct ofono_netmon *netmon);

void ofono_netmon_neighbouring_cell_notify(struct ofono_netmon *netmon,
					enum ofono_netmon_cell_type type,
					int info_type, ...);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_NETMON_H */
