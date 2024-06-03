/*
 * oFono - Open Source Telephony
 * Copyright (C) 2009-2010  Nokia Corporation and/or its subsidiary(-ies)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __OFONO_ISI_INFOSERVER_H
#define __OFONO_ISI_INFOSERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ofono/types.h>

struct isi_infoserver;

struct isi_infoserver *isi_infoserver_create(struct ofono_modem *modem,
						void *data);

void isi_infoserver_destroy(struct isi_infoserver *self);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_ISI_INFOSERVER_H */
