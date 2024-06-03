/*
 * oFono - Open Source Telephony
 * Copyright (C) 2014  Canonical Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

int ril_create(struct ofono_modem *modem, enum ofono_ril_vendor vendor);
void ril_remove(struct ofono_modem *modem);
int ril_enable(struct ofono_modem *modem);
int ril_disable(struct ofono_modem *modem);
void ril_pre_sim(struct ofono_modem *modem);
void ril_post_sim(struct ofono_modem *modem);
void ril_post_online(struct ofono_modem *modem);
void ril_set_online(struct ofono_modem *modem, ofono_bool_t online,
			ofono_modem_online_cb_t callback, void *data);
