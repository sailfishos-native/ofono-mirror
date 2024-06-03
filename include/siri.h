/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2013  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_SIRI_H
#define __OFONO_SIRI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ofono/types.h>

struct ofono_siri;

typedef void (*ofono_siri_cb_t)(const struct ofono_error *error,
					struct ofono_siri *siri);

struct ofono_siri_driver {
	int (*probe)(struct ofono_siri *siri, unsigned int vendor, void *data);
	void (*remove)(struct ofono_siri *siri);
	void (*set_eyes_free_mode) (struct ofono_siri *siri, ofono_siri_cb_t cb,
					unsigned int val);
};

void ofono_siri_set_status(struct ofono_siri *siri, int value);

struct ofono_siri *ofono_siri_create(struct ofono_modem *modem,
					unsigned int vendor,
					const char *driver, void *data);

void ofono_siri_register(struct ofono_siri *siri);
void ofono_siri_remove(struct ofono_siri *siri);

void ofono_siri_set_data(struct ofono_siri *siri, void *data);
void *ofono_siri_get_data(struct ofono_siri *siri);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_SIRI_H */
