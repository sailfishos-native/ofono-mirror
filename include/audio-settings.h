/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_AUDIO_SETTINGS_H
#define __OFONO_AUDIO_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#include <ofono/types.h>

struct ofono_audio_settings;

struct ofono_audio_settings_driver {
	int (*probe)(struct ofono_audio_settings *as,
				unsigned int vendor, void *data);
	int (*probev)(struct ofono_audio_settings *as,
				unsigned int vendor, va_list args);
	void (*remove)(struct ofono_audio_settings *as);
};

void ofono_audio_settings_active_notify(struct ofono_audio_settings *as,
						ofono_bool_t active);
void ofono_audio_settings_mode_notify(struct ofono_audio_settings *as,
						const char *mode);

struct ofono_audio_settings *ofono_audio_settings_create(
						struct ofono_modem *modem,
						unsigned int vendor,
						const char *driver, ...);

void ofono_audio_settings_register(struct ofono_audio_settings *as);
void ofono_audio_settings_remove(struct ofono_audio_settings *as);

void ofono_audio_settings_set_data(struct ofono_audio_settings *as, void *data);
void *ofono_audio_settings_get_data(struct ofono_audio_settings *as);

struct ofono_modem *ofono_audio_settings_get_modem(
					struct ofono_audio_settings *as);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_AUDIO_SETTINGS_H */
