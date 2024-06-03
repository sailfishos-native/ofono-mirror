/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 * Copyright (C) 2011  ST-Ericsson AB
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_GNSS_H
#define __OFONO_GNSS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ofono/types.h>

struct ofono_gnss;

typedef void (*ofono_gnss_cb_t)(const struct ofono_error *error, void *data);

struct ofono_gnss_driver {
	int (*probe)(struct ofono_gnss *gnss, unsigned int vendor, void *data);
	void (*remove)(struct ofono_gnss *gnss);
	void (*send_element)(struct ofono_gnss *gnss,
				const char *xml,
				ofono_gnss_cb_t cb, void *data);
	void (*set_position_reporting)(struct ofono_gnss *gnss,
					ofono_bool_t enable,
					ofono_gnss_cb_t cb,
					void *data);
};

void ofono_gnss_notify_posr_request(struct ofono_gnss *gnss, const char *xml);
void ofono_gnss_notify_posr_reset(struct ofono_gnss *gnss);

struct ofono_gnss *ofono_gnss_create(struct ofono_modem *modem,
					unsigned int vendor,
					const char *driver, void *data);

void ofono_gnss_register(struct ofono_gnss *gnss);
void ofono_gnss_remove(struct ofono_gnss *gnss);

void ofono_gnss_set_data(struct ofono_gnss *gnss, void *data);
void *ofono_gnss_get_data(struct ofono_gnss *gnss);


#ifdef __cplusplus
}
#endif

#endif /* __OFONO_GNSS_H */
