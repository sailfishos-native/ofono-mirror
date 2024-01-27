/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2008-2011  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ofono/storage.h>

#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>
#include <ell/ell.h>

#include "storage.h"

const char *ofono_config_dir(void)
{
	return CONFIGDIR;
}

const char *ofono_storage_dir(void)
{
	return STORAGEDIR;
}

int create_dirs(const char *filename, const mode_t mode)
{
	struct stat st;
	char *dir;
	const char *prev, *next;
	int err;

	if (filename[0] != '/')
		return -1;

	err = stat(filename, &st);
	if (!err && S_ISREG(st.st_mode))
		return 0;

	dir = l_malloc(strlen(filename) + 1);
	strcpy(dir, "/");

	for (prev = filename; (next = strchr(prev + 1, '/')); prev = next) {
		/* Skip consecutive '/' characters */
		if (next - prev == 1)
			continue;

		strncat(dir, prev + 1, next - prev);

		if (mkdir(dir, mode) == -1 && errno != EEXIST) {
			l_free(dir);
			return -1;
		}
	}

	l_free(dir);
	return 0;
}

ssize_t read_file(unsigned char *buffer, size_t len,
			const char *path_fmt, ...)
{
	va_list ap;
	char *path;
	ssize_t r;
	int fd;

	va_start(ap, path_fmt);
	path = l_strdup_vprintf(path_fmt, ap);
	va_end(ap);

	fd = L_TFR(open(path, O_RDONLY));

	l_free(path);

	if (fd == -1)
		return -1;

	r = L_TFR(read(fd, buffer, len));

	L_TFR(close(fd));

	return r;
}

/*
 * Write a buffer to a file in a transactionally safe form
 *
 * Given a buffer, write it to a file named after
 * @path_fmt+args. However, to make sure the file contents are
 * consistent (ie: a crash right after opening or during write()
 * doesn't leave a file half baked), the contents are written to a
 * file with a temporary name and when closed, it is renamed to the
 * specified name (@path_fmt+args).
 */
ssize_t write_file(const unsigned char *buffer, size_t len,
			const char *path_fmt, ...)
{
	va_list ap;
	char *tmp_path, *path;
	ssize_t r;
	int fd;
	static const mode_t mode = 0600;

	va_start(ap, path_fmt);
	path = l_strdup_vprintf(path_fmt, ap);
	va_end(ap);

	tmp_path = l_strdup_printf("%s.XXXXXX.tmp", path);

	r = -1;
	if (create_dirs(path, mode | S_IXUSR) != 0)
		goto error_create_dirs;

	fd = L_TFR(g_mkstemp_full(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, mode));
	if (fd == -1)
		goto error_mkstemp_full;

	r = L_TFR(write(fd, buffer, len));

	L_TFR(close(fd));

	if (r != (ssize_t) len) {
		r = -1;
		goto error_write;
	}

	/*
	 * Now that the file contents are written, rename to the real
	 * file name; this way we are uniquely sure that the whole
	 * thing is there.
	 */
	unlink(path);

	/* conserve @r's value from 'write' */
	if (link(tmp_path, path) == -1)
		r = -1;

error_write:
	unlink(tmp_path);
error_mkstemp_full:
error_create_dirs:
	l_free(tmp_path);
	l_free(path);
	return r;
}

char *storage_get_file_path(const char *imsi, const char *store)
{
	if (store == NULL)
		return NULL;

	if (imsi)
		return l_strdup_printf(STORAGEDIR "/%s/%s", imsi, store);
	else
		return l_strdup_printf(STORAGEDIR "/%s", store);
}

GKeyFile *storage_open(const char *imsi, const char *store)
{
	GKeyFile *keyfile;
	char *path = storage_get_file_path(imsi, store);

	if (!path)
		return NULL;

	keyfile = g_key_file_new();
	g_key_file_load_from_file(keyfile, path, 0, NULL);
	l_free(path);

	return keyfile;
}

void storage_sync(const char *imsi, const char *store, GKeyFile *keyfile)
{
	char *path = storage_get_file_path(imsi, store);
	char *data;
	gsize length = 0;

	if (path == NULL)
		return;

	if (create_dirs(path, S_IRUSR | S_IWUSR | S_IXUSR) != 0) {
		l_free(path);
		return;
	}

	data = g_key_file_to_data(keyfile, &length, NULL);

	g_file_set_contents(path, data, length, NULL);

	g_free(data);
	l_free(path);
}

void storage_close(const char *imsi, const char *store, GKeyFile *keyfile,
			gboolean save)
{
	if (save == TRUE)
		storage_sync(imsi, store, keyfile);

	g_key_file_free(keyfile);
}
