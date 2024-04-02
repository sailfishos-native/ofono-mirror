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

#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <glib.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/plugin.h>
#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/emulator.h>

#include "ofono.h"

#define DUN_PORT 12346
#define HFP_PORT 12347

static unsigned int modemwatch_id;
static guint dun_watch;
static guint hfp_watch;
static struct l_queue *modem_infos;
static unsigned int n_powered;

struct modem_info {
	struct ofono_modem *modem;
	unsigned int watch_id;
};

static bool modem_matches(const void *data, const void *user_data)
{
	const struct modem_info *info = data;

	return info->modem == user_data;
}

static void modem_info_free(struct modem_info *info)
{
	if (!info)
		return;

	if (info->watch_id)
		__ofono_modem_remove_powered_watch(info->modem, info->watch_id);

	l_free(info);
}

static struct ofono_modem *find_first_powered(void)
{
	const struct l_queue_entry *entry;

	for (entry = l_queue_get_entries(modem_infos); entry;
							entry = entry->next) {
		struct ofono_modem *modem = entry->data;

		if (ofono_modem_get_powered(modem))
			return modem;
	}

	return NULL;
}

static gboolean on_socket_connected(GIOChannel *chan, GIOCondition cond,
							gpointer user)
{
	struct sockaddr saddr;
	unsigned int len = sizeof(saddr);
	int fd;
	struct ofono_emulator *em;
	struct ofono_modem *modem;

	if (cond != G_IO_IN)
		return FALSE;

	fd = accept(g_io_channel_unix_get_fd(chan), &saddr, &len);
	if (fd == -1)
		return FALSE;

	/* Pick the first powered modem */
	modem = find_first_powered();
	if (!modem)
		goto error;

	DBG("Picked modem %p for emulator", modem);

	em = ofono_emulator_create(modem, GPOINTER_TO_INT(user));
	if (!em)
		goto error;

	ofono_emulator_register(em, fd);
	return TRUE;

error:
	close(fd);
	return TRUE;
}

static guint create_tcp(short port, enum ofono_emulator_type type)
{
	struct sockaddr_in addr;
	int sk;
	int reuseaddr = 1;
	GIOChannel *server;
	guint server_watch;

	sk = socket(PF_INET, SOCK_STREAM, 0);
	if (sk < 0)
		return 0;

	memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	setsockopt(sk, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));

	if (bind(sk, (struct sockaddr *) &addr, sizeof(struct sockaddr)) < 0)
		goto err;

	if (listen(sk, 1) < 0)
		goto err;

	server = g_io_channel_unix_new(sk);
	g_io_channel_set_close_on_unref(server, TRUE);

	server_watch = g_io_add_watch_full(server, G_PRIORITY_DEFAULT,
				G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
				on_socket_connected, GINT_TO_POINTER(type),
				NULL);
	g_io_channel_unref(server);

	DBG("Created server_watch: %u", server_watch);
	return server_watch;

err:
	close(sk);
	return 0;
}

static void powered_watch_destroy(void *user)
{
	struct modem_info *info = user;

	DBG("");

	info->watch_id = 0;
}

static void powered_watch(struct ofono_modem *modem, gboolean powered,
				void *user)
{
	DBG("powered: %d", powered);

	if (powered == FALSE) {
		n_powered -= 1;

		if (n_powered)
			return;

		g_source_remove(dun_watch);
		dun_watch = 0;

		g_source_remove(hfp_watch);
		hfp_watch = 0;

		return;
	}

	n_powered += 1;

	if (!dun_watch)
		dun_watch = create_tcp(DUN_PORT, OFONO_EMULATOR_TYPE_DUN);

	if (!hfp_watch)
		hfp_watch = create_tcp(HFP_PORT, OFONO_EMULATOR_TYPE_HFP);
}

static void modem_watch(struct ofono_modem *modem, gboolean added, void *user)
{
	struct modem_info *info;

	DBG("modem: %p, added: %d", modem, added);

	if (added == FALSE) {
		info = l_queue_remove_if(modem_infos, modem_matches, modem);
		DBG("Removing modem %p, info %p, from the list", modem, info);
		modem_info_free(info);
		return;
	}

	info = l_new(struct modem_info, 1);
	info->modem = modem;
	info->watch_id = __ofono_modem_add_powered_watch(modem, powered_watch,
							info,
							powered_watch_destroy);

	l_queue_push_tail(modem_infos, info);

	if (ofono_modem_get_powered(modem) == TRUE)
		powered_watch(modem, TRUE, NULL);
}

static void call_modemwatch(struct ofono_modem *modem, void *user)
{
	modem_watch(modem, TRUE, user);
}

static int example_emulator_init(void)
{
	DBG("");

	modem_infos = l_queue_new();
	modemwatch_id = __ofono_modemwatch_add(modem_watch, NULL, NULL);

	__ofono_modem_foreach(call_modemwatch, NULL);

	return 0;
}

static void example_emulator_exit(void)
{
	DBG("");

	__ofono_modemwatch_remove(modemwatch_id);
	l_queue_destroy(modem_infos, (l_queue_destroy_func_t) modem_info_free);

	if (dun_watch)
		g_source_remove(dun_watch);

	if (hfp_watch)
		g_source_remove(hfp_watch);
}

OFONO_PLUGIN_DEFINE(example_emulator, "Example AT Modem Emulator Plugin",
			VERSION, OFONO_PLUGIN_PRIORITY_DEFAULT,
			example_emulator_init, example_emulator_exit)
