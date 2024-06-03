/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <fcntl.h>
#include <sys/types.h>

int create_dirs(const char *filename);

ssize_t read_file(void *buffer, size_t len, const char *path_fmt, ...)
	__attribute__((format(printf, 3, 4)));

ssize_t write_file(const void *buffer, size_t len, const char *path_fmt, ...)
	__attribute__((format(printf, 3, 4)));

char *storage_get_file_path(const char *imsi, const char *store);
GKeyFile *storage_open(const char *imsi, const char *store);
void storage_sync(const char *imsi, const char *store, GKeyFile *keyfile);
void storage_close(const char *imsi, const char *store, GKeyFile *keyfile,
			gboolean save);
