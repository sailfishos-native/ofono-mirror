/*
 *  oFono - Open Source Telephony
 *  Copyright (C) 2023  Cruise, LLC
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <ell/ell.h>

#include "ofono.h"

extern struct ofono_module_desc __start___ofono_module[];
extern struct ofono_module_desc __stop___ofono_module[];

int __ofono_modules_init(void)
{
	struct ofono_module_desc *desc;
	size_t i;
	int r;
	size_t n_modules = __stop___ofono_module - __start___ofono_module;

	DBG("");

	for (i = 0; i < n_modules; i++) {
		desc = __start___ofono_module + i;
		r = desc->init();

		if (r < 0) {
			ofono_error("Module %s failed to start: %s(%d)",
					desc->name, strerror(-r), -r);
			return r;
		}
	}

	return 0;
}

void __ofono_modules_cleanup(void)
{
	struct ofono_module_desc *desc;
	size_t i;
	size_t n_modules = __stop___ofono_module - __start___ofono_module;

	l_debug("");

	for (i = n_modules; i > 0; i--) {
		desc = __start___ofono_module + i - 1;
		desc->exit();
	}
}
