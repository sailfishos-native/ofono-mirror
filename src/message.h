/*
 * oFono - Open Source Telephony
 * Copyright (C) 2008-2011  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <ofono/types.h>

enum message_state {
	MESSAGE_STATE_PENDING,
	MESSAGE_STATE_SENT,
	MESSAGE_STATE_FAILED,
	MESSAGE_STATE_CANCELLED,
};

struct ofono_atom;
struct message;

struct message *message_create(const struct ofono_uuid *uuid,
						struct ofono_atom *atom);

gboolean message_dbus_register(struct message *m);
void message_dbus_unregister(struct message *m);

const struct ofono_uuid *message_get_uuid(const struct message *m);

void message_set_state(struct message *m, enum message_state new_state);

void message_append_properties(struct message *m, DBusMessageIter *dict);

void message_emit_added(struct message *m, const char *interface);

void message_emit_removed(struct message *m, const char *interface);

void *message_get_data(struct message *m);

void message_set_data(struct message *m, void *data);

const char *message_path_from_uuid(struct ofono_atom *atom,
						const struct ofono_uuid *uuid);
