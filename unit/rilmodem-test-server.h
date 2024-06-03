/*
 * oFono - Open Source Telephony
 * Copyright (C) 2015 Canonical Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#define RIL_SERVER_SOCK_PATH    "/tmp/unittestril"

struct server_data;

struct rilmodem_test_data {
	const unsigned char *req_data;

	const size_t req_size;

	uint32_t rsp_error;
	const unsigned char *rsp_data;
	const size_t rsp_size;
	gboolean unsol_test;
};

typedef void (*ConnectFunc)(void *data);

void rilmodem_test_server_close(struct server_data *sd);

struct server_data *rilmodem_test_server_create(ConnectFunc connect,
				const struct rilmodem_test_data *test_data,
				void *data);

void rilmodem_test_server_write(struct server_data *sd,
						const unsigned char *buf,
						const size_t buf_len);
