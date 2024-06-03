/*
 * oFono - Open Source Telephony
 * Copyright (C) 2010  Nokia Corporation and/or its subsidiary(-ies)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

enum power_state {
	POWER_STATE_NONE,
	POWER_STATE_ON_STARTED,
	POWER_STATE_ON,
	POWER_STATE_ON_RESET,
	POWER_STATE_ON_FAILED,
	POWER_STATE_OFF_STARTED,
	POWER_STATE_OFF_WAITING,
	POWER_STATE_OFF,
};

typedef void (*gpio_finished_cb_t)(enum power_state value, void *opaque);

int gpio_probe(GIsiModem *idx, unsigned addr, gpio_finished_cb_t cb, void *data);
int gpio_enable(void *opaque);
int gpio_disable(void *opaque);
int gpio_remove(void *opaque);

char const *gpio_power_state_name(enum power_state value);
