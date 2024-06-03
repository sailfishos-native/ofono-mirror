/*
 * oFono - Open Source Telephony
 * Copyright (C) 2009-2010  Nokia Corporation and/or its subsidiary(-ies)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

struct sms_agent;

enum sms_agent_result {
	SMS_AGENT_RESULT_OK = 0,
	SMS_AGENT_RESULT_FAILED,
	SMS_AGENT_RESULT_TIMEOUT,
};

typedef void (*sms_agent_dispatch_cb)(struct sms_agent *agent,
					enum sms_agent_result result,
					void *data);

struct sms_agent *sms_agent_new(const char *interface,
					const char *service, const char *path);

void sms_agent_set_removed_notify(struct sms_agent *agent,
					ofono_destroy_func destroy,
					void *user_data);

ofono_bool_t sms_agent_matches(struct sms_agent *agent, const char *service,
				const char *path);

void sms_agent_free(struct sms_agent *agent);

int sms_agent_dispatch_datagram(struct sms_agent *agent, const char *method,
				const char *from,
				const struct tm *remote_sent_time,
				const struct tm *local_sent_time,
				const unsigned char *content, unsigned int len,
				sms_agent_dispatch_cb cb, void *user_data,
				ofono_destroy_func destroy);
