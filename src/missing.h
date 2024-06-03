/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef HAVE_G_MEMDUP2
#define g_memdup2(mem, size) g_memdup((mem), (size))
#endif
