/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 * Copyright (C) 2011  ST-Ericsson AB
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

struct gnss_agent;

struct gnss_agent *gnss_agent_new(const char *path, const char *sender);

void gnss_agent_free(struct gnss_agent *agent);

void gnss_agent_receive_request(struct gnss_agent *agent, const char *xml);

void gnss_agent_receive_reset(struct gnss_agent *agent);

void gnss_agent_set_removed_notify(struct gnss_agent *agent,
					ofono_destroy_func removed_cb,
					void *user_data);

ofono_bool_t gnss_agent_matches(struct gnss_agent *agent,
				const char *path, const char *sender);

ofono_bool_t gnss_agent_sender_matches(struct gnss_agent *agent,
					const char *sender);
