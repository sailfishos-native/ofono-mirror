/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2011-2012  Intel Corporation. All rights reserved.
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

#define _GNU_SOURCE
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>

#include <ell/ell.h>

#include <ofono/log.h>

#include "qmi.h"
#include "ctl.h"

typedef void (*qmi_message_func_t)(uint16_t message, uint16_t length,
					const void *buffer, void *user_data);

struct discovery {
	qmi_destroy_func_t destroy;
};

struct qmi_version {
	uint8_t type;
	uint16_t major;
	uint16_t minor;
	const char *name;
};

struct qmi_device_ops {
	int (*discover)(struct qmi_device *device,
			qmi_discover_func_t discover_func,
			void *user, qmi_destroy_func_t destroy);
	int (*client_create)(struct qmi_device *device,
				uint16_t service_type,
				qmi_create_func_t func,
				void *user, qmi_destroy_func_t destroy);
	void (*client_release)(struct qmi_device *device,
				uint16_t service_type, uint16_t client_id);
	int (*shutdown)(struct qmi_device *device,
			qmi_shutdown_func_t shutdown_func,
			void *user, qmi_destroy_func_t destroy);
	void (*destroy)(struct qmi_device *device);
};

struct qmi_device {
	struct l_io *io;
	struct l_queue *req_queue;
	struct l_queue *control_queue;
	struct l_queue *service_queue;
	struct l_queue *discovery_queue;
	uint16_t next_service_tid;
	qmi_debug_func_t debug_func;
	void *debug_data;
	struct qmi_version *version_list;
	uint8_t version_count;
	struct l_hashmap *service_list;
	const struct qmi_device_ops *ops;
	bool writer_active : 1;
	bool shutting_down : 1;
	bool destroyed : 1;
};

struct qmi_device_qmux {
	struct qmi_device super;
	uint16_t control_major;
	uint16_t control_minor;
	char *version_str;
	qmi_shutdown_func_t shutdown_func;
	void *shutdown_user_data;
	qmi_destroy_func_t shutdown_destroy;
	struct l_idle *shutdown_idle;
	unsigned int release_users;
	uint8_t next_control_tid;
};

struct qmi_service {
	int ref_count;
	struct qmi_device *device;
	uint8_t type;
	uint16_t major;
	uint16_t minor;
	uint8_t client_id;
	uint16_t next_notify_id;
	struct l_queue *notify_list;
};

struct qmi_param {
	void *data;
	uint16_t length;
};

struct qmi_result {
	uint16_t message;
	uint16_t result;
	uint16_t error;
	const void *data;
	uint16_t length;
};

struct qmi_request {
	uint16_t tid;
	uint8_t client;
	qmi_message_func_t callback;
	void *user_data;
	uint16_t len;
	uint8_t data[];
};

struct qmi_notify {
	uint16_t id;
	uint16_t message;
	qmi_result_func_t callback;
	void *user_data;
	qmi_destroy_func_t destroy;
};

struct qmi_mux_hdr {
	uint8_t  frame;		/* Always 0x01 */
	uint16_t length;	/* Packet size without frame byte */
	uint8_t  flags;		/* Either 0x00 or 0x80 */
	uint8_t  service;	/* Service type (0x00 for control) */
	uint8_t  client;	/* Client identifier (0x00 for control) */
} __attribute__ ((packed));
#define QMI_MUX_HDR_SIZE 6

struct qmi_control_hdr {
	uint8_t  type;		/* Bit 1 = response, Bit 2 = indication */
	uint8_t  transaction;	/* Transaction identifier */
} __attribute__ ((packed));
#define QMI_CONTROL_HDR_SIZE 2

struct qmi_service_hdr {
	uint8_t  type;		/* Bit 2 = response, Bit 3 = indication */
	uint16_t transaction;	/* Transaction identifier */
} __attribute__ ((packed));
#define QMI_SERVICE_HDR_SIZE 3

struct qmi_message_hdr {
	uint16_t message;	/* Message identifier */
	uint16_t length;	/* Message size without header */
	uint8_t data[0];
} __attribute__ ((packed));
#define QMI_MESSAGE_HDR_SIZE 4

struct qmi_tlv_hdr {
	uint8_t type;
	uint16_t length;
	uint8_t value[0];
} __attribute__ ((packed));
#define QMI_TLV_HDR_SIZE 3

void qmi_free(void *ptr)
{
	l_free(ptr);
}

static struct qmi_request *__request_alloc(uint8_t service,
				uint8_t client, uint16_t message,
				const void *data,
				uint16_t length, qmi_message_func_t func,
				void *user_data)
{
	struct qmi_request *req;
	struct qmi_mux_hdr *hdr;
	struct qmi_message_hdr *msg;
	uint16_t hdrlen = QMI_MUX_HDR_SIZE;
	uint16_t msglen;

	if (service == QMI_SERVICE_CONTROL)
		hdrlen += QMI_CONTROL_HDR_SIZE;
	else
		hdrlen += QMI_SERVICE_HDR_SIZE;

	msglen = hdrlen + QMI_MESSAGE_HDR_SIZE + length;
	req = l_malloc(sizeof(struct qmi_request) + msglen);
	req->tid = 0;
	req->len = msglen;
	req->client = client;

	hdr = (struct qmi_mux_hdr *) req->data;

	hdr->frame = 0x01;
	hdr->length = L_CPU_TO_LE16(req->len - 1);
	hdr->flags = 0x00;
	hdr->service = service;
	hdr->client = client;

	msg = (struct qmi_message_hdr *) &req->data[hdrlen];

	msg->message = L_CPU_TO_LE16(message);
	msg->length = L_CPU_TO_LE16(length);

	if (data && length > 0)
		memcpy(req->data + hdrlen + QMI_MESSAGE_HDR_SIZE, data, length);

	req->callback = func;
	req->user_data = user_data;

	return req;
}

static void __request_free(void *data)
{
	struct qmi_request *req = data;

	l_free(req);
}

static bool __request_compare(const void *a, const void *b)
{
	const struct qmi_request *req = a;
	uint16_t tid = L_PTR_TO_UINT(b);

	return req->tid == tid;
}

static void __discovery_free(void *data)
{
	struct discovery *d = data;
	qmi_destroy_func_t destroy = d->destroy;

	destroy(d);
}

static void __notify_free(void *data)
{
	struct qmi_notify *notify = data;

	if (notify->destroy)
		notify->destroy(notify->user_data);

	l_free(notify);
}

static bool __notify_compare(const void *data, const void *user_data)
{
	const struct qmi_notify *notify = data;
	uint16_t id = L_PTR_TO_UINT(user_data);

	return notify->id == id;
}

struct service_find_by_type_data {
	unsigned int type;
	struct qmi_service *found_service;
};

static void __service_find_by_type(const void *key, void *value,
					void *user_data)
{
	struct qmi_service *service = value;
	struct service_find_by_type_data *data = user_data;

	/* ignore those that are in process of creation */
	if (L_PTR_TO_UINT(key) & 0x80000000)
		return;

	if (service->type == data->type)
		data->found_service = service;
}

static const char *__service_type_to_string(uint8_t type)
{
	switch (type) {
	case QMI_SERVICE_CONTROL:
		return "CTL";
	case QMI_SERVICE_WDS:
		return "WDS";
	case QMI_SERVICE_DMS:
		return "DMS";
	case QMI_SERVICE_NAS:
		return "NAS";
	case QMI_SERVICE_QOS:
		return "QOS";
	case QMI_SERVICE_WMS:
		return "WMS";
	case QMI_SERVICE_PDS:
		return "PDS";
	case QMI_SERVICE_AUTH:
		return "AUTH";
	case QMI_SERVICE_AT:
		return "AT";
	case QMI_SERVICE_VOICE:
		return "VOICE";
	case QMI_SERVICE_CAT:
		return "CAT";
	case QMI_SERVICE_UIM:
		return "UIM";
	case QMI_SERVICE_PBM:
		return "PBM";
	case QMI_SERVICE_QCHAT:
		return "QCHAT";
	case QMI_SERVICE_RMTFS:
		return "RMTFS";
	case QMI_SERVICE_TEST:
		return "TEST";
	case QMI_SERVICE_LOC:
		return "LOC";
	case QMI_SERVICE_SAR:
		return "SAR";
	case QMI_SERVICE_CSD:
		return "CSD";
	case QMI_SERVICE_EFS:
		return "EFS";
	case QMI_SERVICE_TS:
		return "TS";
	case QMI_SERVICE_TMD:
		return "TMD";
	case QMI_SERVICE_WDA:
		return "WDA";
	case QMI_SERVICE_CSVT:
		return "CSVT";
	case QMI_SERVICE_COEX:
		return "COEX";
	case QMI_SERVICE_PDC:
		return "PDC";
	case QMI_SERVICE_RFRPE:
		return "RFRPE";
	case QMI_SERVICE_DSD:
		return "DSD";
	case QMI_SERVICE_SSCTL:
		return "SSCTL";
	case QMI_SERVICE_CAT_OLD:
		return "CAT";
	case QMI_SERVICE_RMS:
		return "RMS";
	case QMI_SERVICE_OMA:
		return "OMA";
	}

	return NULL;
}

static const struct {
	uint16_t err;
	const char *str;
} __error_table[] = {
	{ 0x0000, "NONE"			},
	{ 0x0001, "MALFORMED_MSG"		},
	{ 0x0002, "NO_MEMORY"			},
	{ 0x0003, "INTERNAL"			},
	{ 0x0004, "ABORTED"			},
	{ 0x0005, "CLIENT_IDS_EXHAUSTED"	},
	{ 0x0006, "UNABORTABLE_TRANSACTION"	},
	{ 0x0007, "INVALID_CLIENT_ID"		},
	{ 0x0008, "NO_THRESHOLDS"		},
	{ 0x0009, "INVALID_HANDLE"		},
	{ 0x000a, "INVALID_PROFILE"		},
	{ 0x000b, "INVALID_PINID"		},
	{ 0x000c, "INCORRECT_PIN"		},
	{ 0x000d, "NO_NETWORK_FOUND"		},
	{ 0x000e, "CALL_FAILED"			},
	{ 0x000f, "OUT_OF_CALL"			},
	{ 0x0010, "NOT_PROVISIONED"		},
	{ 0x0011, "MISSING_ARG"			},
	{ 0x0013, "ARG_TOO_LONG"		},
	{ 0x0016, "INVALID_TX_ID"		},
	{ 0x0017, "DEVICE_IN_USE"		},
	{ 0x0018, "OP_NETWORK_UNSUPPORTED"	},
	{ 0x0019, "OP_DEVICE_UNSUPPORTED"	},
	{ 0x001a, "NO_EFFECT"			},
	{ 0x001b, "NO_FREE_PROFILE"		},
	{ 0x001c, "INVALID_PDP_TYPE"		},
	{ 0x001d, "INVALID_TECH_PREF"		},
	{ 0x001e, "INVALID_PROFILE_TYPE"	},
	{ 0x001f, "INVALID_SERVICE_TYPE"	},
	{ 0x0020, "INVALID_REGISTER_ACTION"	},
	{ 0x0021, "INVALID_PS_ATTACH_ACTION"	},
	{ 0x0022, "AUTHENTICATION_FAILED"	},
	{ 0x0023, "PIN_BLOCKED"			},
	{ 0x0024, "PIN_PERM_BLOCKED"		},
	{ 0x0025, "UIM_NOT_INITIALIZED"		},
	{ 0x0026, "MAX_QOS_REQUESTS_IN_USE"	},
	{ 0x0027, "INCORRECT_FLOW_FILTER"	},
	{ 0x0028, "NETWORK_QOS_UNAWARE"		},
	{ 0x0029, "INVALID_QOS_ID/INVALID_ID"	},
	{ 0x002a, "REQUESTED_NUM_UNSUPPORTED"	},
	{ 0x002b, "INTERFACE_NOT_FOUND"		},
	{ 0x002c, "FLOW_SUSPENDED"		},
	{ 0x002d, "INVALID_DATA_FORMAT"		},
	{ 0x002e, "GENERAL"			},
	{ 0x002f, "UNKNOWN"			},
	{ 0x0030, "INVALID_ARG"			},
	{ 0x0031, "INVALID_INDEX"		},
	{ 0x0032, "NO_ENTRY"			},
	{ 0x0033, "DEVICE_STORAGE_FULL"		},
	{ 0x0034, "DEVICE_NOT_READY"		},
	{ 0x0035, "NETWORK_NOT_READY"		},
	{ 0x0036, "CAUSE_CODE"			},
	{ 0x0037, "MESSAGE_NOT_SENT"		},
	{ 0x0038, "MESSAGE_DELIVERY_FAILURE"	},
	{ 0x0039, "INVALID_MESSAGE_ID"		},
	{ 0x003a, "ENCODING"			},
	{ 0x003b, "AUTHENTICATION_LOCK"		},
	{ 0x003c, "INVALID_TRANSACTION"		},
	{ 0x0041, "SESSION_INACTIVE"		},
	{ 0x0042, "SESSION_INVALID"		},
	{ 0x0043, "SESSION_OWNERSHIP"		},
	{ 0x0044, "INSUFFICIENT_RESOURCES"	},
	{ 0x0045, "DISABLED"			},
	{ 0x0046, "INVALID_OPERATION"		},
	{ 0x0047, "INVALID_QMI_CMD"		},
	{ 0x0048, "TPDU_TYPE"			},
	{ 0x0049, "SMSC_ADDR"			},
	{ 0x004a, "INFO_UNAVAILABLE"		},
	{ 0x004b, "SEGMENT_TOO_LONG"		},
	{ 0x004c, "SEGEMENT_ORDER"		},
	{ 0x004d, "BUNDLING_NOT_SUPPORTED"	},
	{ 0x004f, "POLICY_MISMATCH"		},
	{ 0x0050, "SIM_FILE_NOT_FOUND"		},
	{ 0x0051, "EXTENDED_INTERNAL"		},
	{ 0x0052, "ACCESS_DENIED"		},
	{ 0x0053, "HARDWARE_RESTRICTED"		},
	{ 0x0054, "ACK_NOT_SENT"		},
	{ 0x0055, "INJECT_TIMEOUT"		},
	{ 0x005c, "SUPS_FAILURE_CAUSE"		},
	{ }
};

static const char *__error_to_string(uint16_t error)
{
	int i;

	for (i = 0; __error_table[i].str; i++) {
		if (__error_table[i].err == error)
			return __error_table[i].str;
	}

	return NULL;
}

int qmi_error_to_ofono_cme(int qmi_error)
{
	switch (qmi_error) {
	case 0x0019:
		return 4; /* Not Supported */
	case 0x0052:
		return 32; /* Access Denied */
	default:
		return -1;
	}
}

static void __debug_msg(const char dir, const void *buf, size_t len,
				qmi_debug_func_t function, void *user_data)
{
	const struct qmi_mux_hdr *hdr;
	const struct qmi_message_hdr *msg;
	const char *service;
	const void *ptr;
	uint16_t offset;
	char strbuf[72 + 16], *str;
	bool pending_print = false;

	if (!function || !len)
		return;

	hdr = buf;

	str = strbuf;
	service = __service_type_to_string(hdr->service);
	if (service)
		str += sprintf(str, "%c   %s", dir, service);
	else
		str += sprintf(str, "%c   %d", dir, hdr->service);

	if (hdr->service == QMI_SERVICE_CONTROL) {
		const struct qmi_control_hdr *ctl;
		const char *type;

		ctl = buf + QMI_MUX_HDR_SIZE;
		msg = buf + QMI_MUX_HDR_SIZE + QMI_CONTROL_HDR_SIZE;
		ptr = buf + QMI_MUX_HDR_SIZE + QMI_CONTROL_HDR_SIZE +
							QMI_MESSAGE_HDR_SIZE;

		switch (ctl->type) {
		case 0x00:
			type = "_req";
			break;
		case 0x01:
			type = "_resp";
			break;
		case 0x02:
			type = "_ind";
			break;
		default:
			type = "";
			break;
		}

		str += sprintf(str, "%s msg=%d len=%d", type,
					L_LE16_TO_CPU(msg->message),
					L_LE16_TO_CPU(msg->length));

		str += sprintf(str, " [client=%d,type=%d,tid=%d,len=%d]",
					hdr->client, ctl->type,
					ctl->transaction,
					L_LE16_TO_CPU(hdr->length));
	} else {
		const struct qmi_service_hdr *srv;
		const char *type;

		srv = buf + QMI_MUX_HDR_SIZE;
		msg = buf + QMI_MUX_HDR_SIZE + QMI_SERVICE_HDR_SIZE;
		ptr = buf + QMI_MUX_HDR_SIZE + QMI_SERVICE_HDR_SIZE +
							QMI_MESSAGE_HDR_SIZE;

		switch (srv->type) {
		case 0x00:
			type = "_req";
			break;
		case 0x02:
			type = "_resp";
			break;
		case 0x04:
			type = "_ind";
			break;
		default:
			type = "";
			break;
		}

		str += sprintf(str, "%s msg=%d len=%d", type,
					L_LE16_TO_CPU(msg->message),
					L_LE16_TO_CPU(msg->length));

		str += sprintf(str, " [client=%d,type=%d,tid=%d,len=%d]",
					hdr->client, srv->type,
					L_LE16_TO_CPU(srv->transaction),
					L_LE16_TO_CPU(hdr->length));
	}

	function(strbuf, user_data);

	if (!msg->length)
		return;

	str = strbuf;
	str += sprintf(str, "      ");
	offset = 0;

	while (offset + QMI_TLV_HDR_SIZE < L_LE16_TO_CPU(msg->length)) {
		const struct qmi_tlv_hdr *tlv = ptr + offset;
		uint16_t tlv_length = L_LE16_TO_CPU(tlv->length);

		if (tlv->type == 0x02 && tlv_length == QMI_RESULT_CODE_SIZE) {
			const struct qmi_result_code *result = ptr + offset +
							QMI_TLV_HDR_SIZE;
			uint16_t error = L_LE16_TO_CPU(result->error);
			const char *error_str;

			error_str = __error_to_string(error);
			if (error_str)
				str += sprintf(str, " {type=%d,error=%s}",
							tlv->type, error_str);
			else
				str += sprintf(str, " {type=%d,error=%d}",
							tlv->type, error);
		} else {
			str += sprintf(str, " {type=%d,len=%d}", tlv->type,
								tlv_length);
		}

		if (str - strbuf > 60) {
			function(strbuf, user_data);

			str = strbuf;
			str += sprintf(str, "      ");

			pending_print = false;
		} else
			pending_print = true;

		offset += QMI_TLV_HDR_SIZE + tlv_length;
	}

	if (pending_print)
		function(strbuf, user_data);
}

static void __debug_device(struct qmi_device *device,
					const char *format, ...)
{
	char strbuf[72 + 16];
	va_list ap;

	if (!device->debug_func)
		return;

	va_start(ap, format);
	vsnprintf(strbuf, sizeof(strbuf), format, ap);
	va_end(ap);

	device->debug_func(strbuf, device->debug_data);
}

static bool can_write_data(struct l_io *io, void *user_data)
{
	struct qmi_device *device = user_data;
	struct qmi_mux_hdr *hdr;
	struct qmi_request *req;
	ssize_t bytes_written;

	req = l_queue_pop_head(device->req_queue);
	if (!req)
		return false;

	bytes_written = write(l_io_get_fd(device->io), req->data, req->len);
	if (bytes_written < 0)
		return false;

	l_util_hexdump(false, req->data, bytes_written,
			device->debug_func, device->debug_data);

	__debug_msg(' ', req->data, bytes_written,
				device->debug_func, device->debug_data);

	hdr = (struct qmi_mux_hdr *) req->data;

	if (hdr->service == QMI_SERVICE_CONTROL)
		l_queue_push_tail(device->control_queue, req);
	else
		l_queue_push_tail(device->service_queue, req);

	if (l_queue_length(device->req_queue) > 0)
		return true;

	return false;
}

static void write_watch_destroy(void *user_data)
{
	struct qmi_device *device = user_data;

	device->writer_active = false;
}

static void wakeup_writer(struct qmi_device *device)
{
	if (device->writer_active)
		return;

	l_io_set_write_handler(device->io, can_write_data, device,
				write_watch_destroy);

	device->writer_active = true;
}

static uint16_t __request_submit(struct qmi_device *device,
				struct qmi_request *req)
{
	struct qmi_service_hdr *hdr =
		(struct qmi_service_hdr *) &req->data[QMI_MUX_HDR_SIZE];

	hdr->type = 0x00;
	hdr->transaction = device->next_service_tid++;

	if (device->next_service_tid < 256)
		device->next_service_tid = 256;

	req->tid = hdr->transaction;

	l_queue_push_tail(device->req_queue, req);
	wakeup_writer(device);

	return req->tid;
}

static void service_notify_if_message_matches(void *data, void *user_data)
{
	struct qmi_notify *notify = data;
	struct qmi_result *result = user_data;

	if (notify->message == result->message)
		notify->callback(result, notify->user_data);
}

static void service_notify(const void *key, void *value, void *user_data)
{
	struct qmi_service *service = value;
	struct qmi_result *result = user_data;

	/* ignore those that are in process of creation */
	if (L_PTR_TO_UINT(key) & 0x80000000)
		return;

	l_queue_foreach(service->notify_list, service_notify_if_message_matches,
				result);
}

static void handle_indication(struct qmi_device *device,
			uint8_t service_type, uint8_t client_id,
			uint16_t message, uint16_t length, const void *data)
{
	struct qmi_service *service;
	struct qmi_result result;
	unsigned int hash_id;

	if (service_type == QMI_SERVICE_CONTROL)
		return;

	result.result = 0;
	result.error = 0;
	result.message = message;
	result.data = data;
	result.length = length;

	if (client_id == 0xff) {
		l_hashmap_foreach(device->service_list, service_notify,
					&result);
		return;
	}

	hash_id = service_type | (client_id << 8);

	service = l_hashmap_lookup(device->service_list,
					L_UINT_TO_PTR(hash_id));

	if (!service)
		return;

	service_notify(NULL, service, &result);
}

static void __rx_message(struct qmi_device *device,
				uint8_t service_type, uint8_t client_id,
				const void *buf)
{
	const struct qmi_service_hdr *service = buf;
	const struct qmi_message_hdr *msg = buf + QMI_SERVICE_HDR_SIZE;
	const void *data = buf + QMI_SERVICE_HDR_SIZE + QMI_MESSAGE_HDR_SIZE;
	struct qmi_request *req;
	unsigned int tid;
	uint16_t message;
	uint16_t length;

	message = L_LE16_TO_CPU(msg->message);
	length = L_LE16_TO_CPU(msg->length);
	tid = L_LE16_TO_CPU(service->transaction);

	if (service->type == 0x04) {
		handle_indication(device, service_type, client_id,
					message, length, data);
		return;
	}

	req = l_queue_remove_if(device->service_queue, __request_compare,
						L_UINT_TO_PTR(tid));
	if (!req)
		return;

	if (req->callback)
		req->callback(message, length, data, req->user_data);

	__request_free(req);
}

static void __qmi_device_discovery_started(struct qmi_device *device,
						struct discovery *d)
{
	l_queue_push_tail(device->discovery_queue, d);
}

static void __qmi_device_discovery_complete(struct qmi_device *device,
						struct discovery *d)
{
	if (!l_queue_remove(device->discovery_queue, d))
		return;

	__discovery_free(d);
}

static void service_destroy(void *data)
{
	struct qmi_service *service = data;

	if (!service->device)
		return;

	service->device = NULL;
}

static int qmi_device_init(struct qmi_device *device, int fd,
					const struct qmi_device_ops *ops)
{
	long flags;

	__debug_device(device, "device %p new", device);

	flags = fcntl(fd, F_GETFL, NULL);
	if (flags < 0)
		return -EIO;

	if (!(flags & O_NONBLOCK)) {
		int r = fcntl(fd, F_SETFL, flags | O_NONBLOCK);

		if (r < 0)
			return -errno;
	}

	device->io = l_io_new(fd);
	l_io_set_close_on_destroy(device->io, true);

	device->req_queue = l_queue_new();
	device->control_queue = l_queue_new();
	device->service_queue = l_queue_new();
	device->discovery_queue = l_queue_new();

	device->service_list = l_hashmap_new();

	device->next_service_tid = 256;

	device->ops = ops;

	return 0;
}

static void __qmi_device_shutdown_finished(struct qmi_device *device)
{
	if (device->destroyed)
		device->ops->destroy(device);
}

void qmi_device_free(struct qmi_device *device)
{
	if (!device)
		return;

	__debug_device(device, "device %p free", device);

	l_queue_destroy(device->control_queue, __request_free);
	l_queue_destroy(device->service_queue, __request_free);
	l_queue_destroy(device->req_queue, __request_free);
	l_queue_destroy(device->discovery_queue, __discovery_free);

	l_io_destroy(device->io);

	l_hashmap_destroy(device->service_list, service_destroy);

	l_free(device->version_list);

	if (device->shutting_down)
		device->destroyed = true;
	else
		device->ops->destroy(device);
}

void qmi_device_set_debug(struct qmi_device *device,
				qmi_debug_func_t func, void *user_data)
{
	if (device == NULL)
		return;

	device->debug_func = func;
	device->debug_data = user_data;
}

void qmi_result_print_tlvs(struct qmi_result *result)
{
	const void *ptr = result->data;
	uint16_t len = result->length;

	while (len > QMI_TLV_HDR_SIZE) {
		const struct qmi_tlv_hdr *tlv = ptr;
		uint16_t tlv_length = L_LE16_TO_CPU(tlv->length);

		DBG("tlv: 0x%02x len 0x%04x", tlv->type, tlv->length);

		ptr += QMI_TLV_HDR_SIZE + tlv_length;
		len -= QMI_TLV_HDR_SIZE + tlv_length;
	}
}

static const void *tlv_get(const void *data, uint16_t size,
					uint8_t type, uint16_t *length)
{
	const void *ptr = data;
	uint16_t len = size;

	while (len > QMI_TLV_HDR_SIZE) {
		const struct qmi_tlv_hdr *tlv = ptr;
		uint16_t tlv_length = L_LE16_TO_CPU(tlv->length);

		if (tlv->type == type) {
			if (length)
				*length = tlv_length;

			return ptr + QMI_TLV_HDR_SIZE;
		}

		ptr += QMI_TLV_HDR_SIZE + tlv_length;
		len -= QMI_TLV_HDR_SIZE + tlv_length;
	}

	return NULL;
}

bool qmi_device_get_service_version(struct qmi_device *device, uint16_t type,
					uint16_t *major, uint16_t *minor)
{
	struct qmi_version *info;
	int i;

	for (i = 0, info = device->version_list;
			i < device->version_count;
			i++, info++) {
		if (info->type == type) {
			*major = info->major;
			*minor = info->minor;
			return true;
		}
	}

	return false;
}

bool qmi_device_has_service(struct qmi_device *device, uint16_t type)
{
	struct qmi_version *info;
	int i;

	for (i = 0, info = device->version_list;
			i < device->version_count;
			i++, info++) {
		if (info->type == type)
			return true;
	}

	return false;
}

struct discover_data {
	struct discovery super;
	struct qmi_device *device;
	qmi_discover_func_t func;
	void *user_data;
	qmi_destroy_func_t destroy;
	uint16_t tid;
	struct l_timeout *timeout;
};

static void discover_data_free(void *user_data)
{
	struct discover_data *data = user_data;

	if (data->timeout)
		l_timeout_remove(data->timeout);

	if (data->destroy)
		data->destroy(data->user_data);

	l_free(data);
}

int qmi_device_discover(struct qmi_device *device, qmi_discover_func_t func,
				void *user_data, qmi_destroy_func_t destroy)
{
	if (!device)
		return false;

	if (!device->ops->discover)
		return -ENOTSUP;

	return device->ops->discover(device, func, user_data, destroy);
}

int qmi_device_shutdown(struct qmi_device *device, qmi_shutdown_func_t func,
				void *user_data, qmi_destroy_func_t destroy)
{
	if (!device)
		return false;

	if (!device->ops->shutdown)
		return -ENOTSUP;

	return device->ops->shutdown(device, func, user_data, destroy);
}

static bool get_device_file_name(struct qmi_device *device,
					char *file_name, int size)
{
	pid_t pid;
	char temp[100];
	ssize_t result;
	int fd = l_io_get_fd(device->io);

	if (size <= 0)
		return false;

	pid = getpid();

	snprintf(temp, 100, "/proc/%d/fd/%d", (int) pid, fd);
	temp[99] = 0;

	result = readlink(temp, file_name, size - 1);

	if (result == -1 || result >= size - 1) {
		DBG("Error %d in readlink", errno);
		return false;
	}

	file_name[result] = 0;

	return true;
}

static char *get_first_dir_in_directory(char *dir_path)
{
	DIR *dir;
	struct dirent *dir_entry;
	char *dir_name = NULL;

	dir = opendir(dir_path);

	if (!dir)
		return NULL;

	dir_entry = readdir(dir);

	while ((dir_entry != NULL)) {
		if (dir_entry->d_type == DT_DIR &&
				strcmp(dir_entry->d_name, ".") != 0 &&
				strcmp(dir_entry->d_name, "..") != 0) {
			dir_name = l_strdup(dir_entry->d_name);
			break;
		}

		dir_entry = readdir(dir);
	}

	closedir(dir);
	return dir_name;
}

static char *get_device_interface(struct qmi_device *device)
{
	char * const driver_names[] = { "usbmisc", "usb" };
	unsigned int i;
	char file_path[PATH_MAX];
	char *file_name;
	char *interface = NULL;

	if (!get_device_file_name(device, file_path, sizeof(file_path)))
		return NULL;

	file_name = basename(file_path);

	for (i = 0; i < L_ARRAY_SIZE(driver_names) && !interface; i++) {
		char *sysfs_path;

		sysfs_path = l_strdup_printf("/sys/class/%s/%s/device/net/",
						driver_names[i], file_name);
		interface = get_first_dir_in_directory(sysfs_path);
		l_free(sysfs_path);
	}

	return interface;
}

enum qmi_device_expected_data_format qmi_device_get_expected_data_format(
						struct qmi_device *device)
{
	char *sysfs_path = NULL;
	char *interface = NULL;
	int fd = -1;
	char value;
	enum qmi_device_expected_data_format expected =
					QMI_DEVICE_EXPECTED_DATA_FORMAT_UNKNOWN;

	if (!device)
		goto done;

	interface = get_device_interface(device);

	if (!interface) {
		DBG("Error while getting interface name");
		goto done;
	}

	/* Build sysfs file path and open it */
	sysfs_path = l_strdup_printf("/sys/class/net/%s/qmi/raw_ip", interface);

	fd = open(sysfs_path, O_RDONLY);
	if (fd < 0) {
		/* maybe not supported by kernel */
		DBG("Error %d in open(%s)", errno, sysfs_path);
		goto done;
	}

	if (read(fd, &value, 1) != 1) {
		DBG("Error %d in read(%s)", errno, sysfs_path);
		goto done;
	}

	if (value == 'Y')
		expected = QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP;
	else if (value == 'N')
		expected = QMI_DEVICE_EXPECTED_DATA_FORMAT_802_3;
	else
		DBG("Unexpected sysfs file contents");

done:
	if (fd >= 0)
		close(fd);

	if (sysfs_path)
		l_free(sysfs_path);

	if (interface)
		l_free(interface);

	return expected;
}

bool qmi_device_set_expected_data_format(struct qmi_device *device,
			enum qmi_device_expected_data_format format)
{
	bool res = false;
	char *sysfs_path = NULL;
	char *interface = NULL;
	int fd = -1;
	char value;

	if (!device)
		goto done;

	switch (format) {
	case QMI_DEVICE_EXPECTED_DATA_FORMAT_802_3:
		value = 'N';
		break;
	case QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP:
		value = 'Y';
		break;
	default:
		DBG("Unhandled format: %d", (int) format);
		goto done;
	}

	interface = get_device_interface(device);

	if (!interface) {
		DBG("Error while getting interface name");
		goto done;
	}

	/* Build sysfs file path and open it */
	sysfs_path = l_strdup_printf("/sys/class/net/%s/qmi/raw_ip", interface);

	fd = open(sysfs_path, O_WRONLY);
	if (fd < 0) {
		/* maybe not supported by kernel */
		DBG("Error %d in open(%s)", errno, sysfs_path);
		goto done;
	}

	if (write(fd, &value, 1) != 1) {
		DBG("Error %d in write(%s)", errno, sysfs_path);
		goto done;
	}

	res = true;

done:
	if (fd >= 0)
		close(fd);

	if (sysfs_path)
		l_free(sysfs_path);

	if (interface)
		l_free(interface);

	return res;
}

static void __rx_ctl_message(struct qmi_device_qmux *qmux,
				uint8_t service_type, uint8_t client_id,
				const void *buf)
{
	const struct qmi_control_hdr *control = buf;
	const struct qmi_message_hdr *msg = buf + QMI_CONTROL_HDR_SIZE;
	const void *data = buf + QMI_CONTROL_HDR_SIZE + QMI_MESSAGE_HDR_SIZE;
	struct qmi_request *req;
	uint16_t message;
	uint16_t length;
	unsigned int tid;

	/* Ignore control messages with client identifier */
	if (client_id != 0x00)
		return;

	message = L_LE16_TO_CPU(msg->message);
	length = L_LE16_TO_CPU(msg->length);
	tid = control->transaction;

	if (control->type == 0x02 && control->transaction == 0x00) {
		handle_indication(&qmux->super, service_type, client_id,
					message, length, data);
		return;
	}

	req = l_queue_remove_if(qmux->super.control_queue, __request_compare,
						L_UINT_TO_PTR(tid));
	if (!req)
		return;

	if (req->callback)
		req->callback(message, length, data, req->user_data);

	__request_free(req);
}

static bool received_qmux_data(struct l_io *io, void *user_data)
{
	struct qmi_device_qmux *qmux = user_data;
	struct qmi_mux_hdr *hdr;
	unsigned char buf[2048];
	ssize_t bytes_read;
	uint16_t offset;

	bytes_read = read(l_io_get_fd(qmux->super.io), buf, sizeof(buf));
	if (bytes_read < 0)
		return true;

	l_util_hexdump(true, buf, bytes_read,
			qmux->super.debug_func, qmux->super.debug_data);

	offset = 0;

	while (offset < bytes_read) {
		uint16_t len;
		const void *msg;

		/* Check if QMI mux header fits into packet */
		if (bytes_read - offset < QMI_MUX_HDR_SIZE)
			break;

		hdr = (void *) (buf + offset);

		/* Check for fixed frame and flags value */
		if (hdr->frame != 0x01 || hdr->flags != 0x80)
			break;

		len = L_LE16_TO_CPU(hdr->length) + 1;

		/* Check that packet size matches frame size */
		if (bytes_read - offset < len)
			break;

		__debug_msg(' ', buf + offset, len,
				qmux->super.debug_func, qmux->super.debug_data);

		msg = buf + offset + QMI_MUX_HDR_SIZE;

		if (hdr->service == QMI_SERVICE_CONTROL)
			__rx_ctl_message(qmux, hdr->service, hdr->client, msg);
		else
			__rx_message(&qmux->super,
					hdr->service, hdr->client, msg);

		offset += len;
	}

	return true;
}

struct service_create_shared_data {
	struct discovery super;
	struct qmi_service *service;
	struct qmi_device *device;
	qmi_create_func_t func;
	void *user_data;
	qmi_destroy_func_t destroy;
	struct l_idle *idle;
};

static uint8_t __ctl_request_submit(struct qmi_device_qmux *qmux,
					struct qmi_request *req)
{
	struct qmi_control_hdr *hdr =
		(struct qmi_control_hdr *) &req->data[QMI_MUX_HDR_SIZE];

	hdr->type = 0x00;
	hdr->transaction = qmux->next_control_tid++;

	if (qmux->next_control_tid == 0)
		qmux->next_control_tid = 1;

	req->tid = hdr->transaction;

	l_queue_push_tail(qmux->super.req_queue, req);
	wakeup_writer(&qmux->super);

	return req->tid;
}

static void service_create_shared_reply(struct l_idle *idle, void *user_data)
{
	struct service_create_shared_data *data = user_data;

	l_idle_remove(data->idle);
	data->idle = NULL;
	data->func(data->service, data->user_data);

	__qmi_device_discovery_complete(data->device, &data->super);
}

static void service_create_shared_pending_reply(struct qmi_device *device,
						unsigned int type,
						struct qmi_service *service)
{
	void *key = L_UINT_TO_PTR(type | 0x80000000);
	struct l_queue *shared = l_hashmap_remove(device->service_list, key);
	const struct l_queue_entry *entry;

	for (entry = l_queue_get_entries(shared); entry; entry = entry->next) {
		struct service_create_shared_data *shared_data = entry->data;

		shared_data->service = qmi_service_ref(service);
		shared_data->idle = l_idle_create(service_create_shared_reply,
							shared_data, NULL);
	}

	l_queue_destroy(shared, NULL);
}

static void service_create_shared_data_free(void *user_data)
{
	struct service_create_shared_data *data = user_data;

	if (data->idle)
		l_idle_remove(data->idle);

	qmi_service_unref(data->service);

	if (data->destroy)
		data->destroy(data->user_data);

	l_free(data);
}

static struct qmi_request *find_control_request(struct qmi_device *device,
						uint16_t tid)
{
	struct qmi_request *req = NULL;
	unsigned int _tid = tid;

	if (_tid != 0) {
		req = l_queue_remove_if(device->req_queue, __request_compare,
						L_UINT_TO_PTR(_tid));

		if (!req)
			req = l_queue_remove_if(device->control_queue,
							__request_compare,
							L_UINT_TO_PTR(_tid));
	}

	return req;
}

static void qmux_sync_callback(uint16_t message, uint16_t length,
					const void *buffer, void *user_data)
{
	struct discover_data *data = user_data;

	if (data->func)
		data->func(data->user_data);

	__qmi_device_discovery_complete(data->device, &data->super);
}

/* sync will release all previous clients */
static bool qmi_device_qmux_sync(struct qmi_device_qmux *qmux,
					struct discover_data *data)
{
	struct qmi_request *req;

	__debug_device(&qmux->super, "Sending sync to reset QMI");

	req = __request_alloc(QMI_SERVICE_CONTROL, 0x00,
				QMI_CTL_SYNC, NULL, 0,
				qmux_sync_callback, data);

	__ctl_request_submit(qmux, req);

	return true;
}

static void qmux_discover_callback(uint16_t message, uint16_t length,
					const void *buffer, void *user_data)
{
	struct discover_data *data = user_data;
	struct qmi_device *device = data->device;
	struct qmi_device_qmux *qmux =
		l_container_of(device, struct qmi_device_qmux, super);
	const struct qmi_result_code *result_code;
	const struct qmi_service_list *service_list;
	const void *ptr;
	uint16_t len;
	struct qmi_version *list;
	uint8_t count;
	unsigned int i;

	count = 0;
	list = NULL;

	result_code = tlv_get(buffer, length, 0x02, &len);
	if (!result_code)
		goto done;

	if (len != QMI_RESULT_CODE_SIZE)
		goto done;

	service_list = tlv_get(buffer, length, 0x01, &len);
	if (!service_list)
		goto done;

	if (len < QMI_SERVICE_LIST_SIZE)
		goto done;

	list = l_malloc(sizeof(struct qmi_version) * service_list->count);

	for (i = 0; i < service_list->count; i++) {
		uint16_t major =
			L_LE16_TO_CPU(service_list->services[i].major);
		uint16_t minor =
			L_LE16_TO_CPU(service_list->services[i].minor);
		uint8_t type = service_list->services[i].type;
		const char *name = __service_type_to_string(type);

		if (name)
			__debug_device(device, "found service [%s %d.%d]",
					name, major, minor);
		else
			__debug_device(device, "found service [%d %d.%d]",
					type, major, minor);

		if (type == QMI_SERVICE_CONTROL) {
			qmux->control_major = major;
			qmux->control_minor = minor;
			continue;
		}

		list[count].type = type;
		list[count].major = major;
		list[count].minor = minor;
		list[count].name = name;

		count++;
	}

	ptr = tlv_get(buffer, length, 0x10, &len);
	if (!ptr)
		goto done;

	qmux->version_str = l_strndup(ptr + 1, *((uint8_t *) ptr));
	__debug_device(device, "version string: %s", qmux->version_str);

done:
	device->version_list = list;
	device->version_count = count;

	/* if the device support the QMI call SYNC over the CTL interface */
	if ((qmux->control_major == 1 && qmux->control_minor >= 5) ||
			qmux->control_major > 1) {
		qmi_device_qmux_sync(qmux, data);
		return;
	}

	if (data->func)
		data->func(data->user_data);

	__qmi_device_discovery_complete(data->device, &data->super);
}

static void qmux_discover_reply_timeout(struct l_timeout *timeout,
							void *user_data)
{
	struct discover_data *data = user_data;
	struct qmi_device *device = data->device;
	struct qmi_request *req;

	l_timeout_remove(data->timeout);
	data->timeout = NULL;

	/* remove request from queues */
	req = find_control_request(device, data->tid);

	if (data->func)
		data->func(data->user_data);

	__qmi_device_discovery_complete(device, &data->super);

	if (req)
		__request_free(req);
}

static int qmi_device_qmux_discover(struct qmi_device *device,
					qmi_discover_func_t func,
					void *user_data,
					qmi_destroy_func_t destroy)
{
	struct qmi_device_qmux *qmux =
		l_container_of(device, struct qmi_device_qmux, super);
	struct discover_data *data;
	struct qmi_request *req;

	__debug_device(device, "device %p discover", device);

	if (device->version_list)
		return -EALREADY;

	data = l_new(struct discover_data, 1);

	data->super.destroy = discover_data_free;
	data->device = device;
	data->func = func;
	data->user_data = user_data;
	data->destroy = destroy;

	req = __request_alloc(QMI_SERVICE_CONTROL, 0x00,
			QMI_CTL_GET_VERSION_INFO,
			NULL, 0, qmux_discover_callback, data);

	data->tid = __ctl_request_submit(qmux, req);
	data->timeout = l_timeout_create(5, qmux_discover_reply_timeout,
								data, NULL);

	__qmi_device_discovery_started(device, &data->super);

	return 0;
}

struct qmux_client_create_data {
	struct discovery super;
	struct qmi_device *device;
	uint8_t type;
	uint16_t major;
	uint16_t minor;
	qmi_create_func_t func;
	void *user_data;
	qmi_destroy_func_t destroy;
	struct l_timeout *timeout;
	uint16_t tid;
};

static void qmux_client_create_data_free(void *user_data)
{
	struct qmux_client_create_data *data = user_data;

	if (data->timeout)
		l_timeout_remove(data->timeout);

	if (data->destroy)
		data->destroy(data->user_data);

	l_free(data);
}

static void qmux_client_create_reply(struct l_timeout *timeout, void *user_data)
{
	struct qmux_client_create_data *data = user_data;
	struct qmi_device *device = data->device;
	struct qmi_request *req;

	DBG("");

	service_create_shared_pending_reply(device, data->type, NULL);

	/* remove request from queues */
	req = find_control_request(device, data->tid);

	l_timeout_remove(data->timeout);
	data->timeout = NULL;

	if (data->func)
		data->func(NULL, data->user_data);

	__qmi_device_discovery_complete(device, &data->super);

	if (req)
		__request_free(req);
}

static void qmux_client_create_callback(uint16_t message, uint16_t length,
					const void *buffer, void *user_data)
{
	struct qmux_client_create_data *data = user_data;
	struct qmi_device *device = data->device;
	struct qmi_service *service = NULL;
	struct qmi_service *old_service = NULL;
	const struct qmi_result_code *result_code;
	const struct qmi_client_id *client_id;
	uint16_t len;
	unsigned int hash_id;

	result_code = tlv_get(buffer, length, 0x02, &len);
	if (!result_code)
		goto done;

	if (len != QMI_RESULT_CODE_SIZE)
		goto done;

	client_id = tlv_get(buffer, length, 0x01, &len);
	if (!client_id)
		goto done;

	if (len != QMI_CLIENT_ID_SIZE)
		goto done;

	if (client_id->service != data->type)
		goto done;

	service = l_new(struct qmi_service, 1);

	service->ref_count = 1;
	service->device = data->device;

	service->type = data->type;
	service->major = data->major;
	service->minor = data->minor;

	service->client_id = client_id->client;
	service->notify_list = l_queue_new();

	__debug_device(device, "service created [client=%d,type=%d]",
					service->client_id, service->type);

	hash_id = service->type | (service->client_id << 8);

	l_hashmap_replace(device->service_list, L_UINT_TO_PTR(hash_id),
				service, (void **) &old_service);

	if (old_service)
		service_destroy(old_service);

done:
	service_create_shared_pending_reply(device, data->type, service);

	data->func(service, data->user_data);
	qmi_service_unref(service);

	__qmi_device_discovery_complete(data->device, &data->super);
}

static int qmi_device_qmux_client_create(struct qmi_device *device,
					uint16_t service_type,
					qmi_create_func_t func, void *user_data,
					qmi_destroy_func_t destroy)
{
	struct qmi_device_qmux *qmux =
		l_container_of(device, struct qmi_device_qmux, super);
	unsigned char client_req[] = { 0x01, 0x01, 0x00, service_type };
	struct qmi_request *req;
	struct qmux_client_create_data *data;
	struct l_queue *shared;
	unsigned int type_val = service_type;
	int i;

	if (!device->version_list)
		return -ENOENT;

	shared = l_queue_new();
	data = l_new(struct qmux_client_create_data, 1);

	data->super.destroy = qmux_client_create_data_free;
	data->device = device;
	data->type = service_type;
	data->func = func;
	data->user_data = user_data;
	data->destroy = destroy;

	__debug_device(device, "service create [type=%d]", service_type);

	for (i = 0; i < device->version_count; i++) {
		if (device->version_list[i].type == data->type) {
			data->major = device->version_list[i].major;
			data->minor = device->version_list[i].minor;
			break;
		}
	}

	req = __request_alloc(QMI_SERVICE_CONTROL, 0x00,
			QMI_CTL_GET_CLIENT_ID,
			client_req, sizeof(client_req),
			qmux_client_create_callback, data);

	data->tid = __ctl_request_submit(qmux, req);
	data->timeout = l_timeout_create(8, qmux_client_create_reply,
								data, NULL);

	__qmi_device_discovery_started(device, &data->super);

	/* Mark service creation as pending */
	l_hashmap_insert(device->service_list,
			L_UINT_TO_PTR(type_val | 0x80000000), shared);

	return 0;
}

static void qmux_client_release_callback(uint16_t message, uint16_t length,
					const void *buffer, void *user_data)
{
	struct qmi_device_qmux *qmux = user_data;

	qmux->release_users--;
}

static void qmi_device_qmux_client_release(struct qmi_device *device,
						uint16_t service_type,
						uint16_t client_id)
{
	struct qmi_device_qmux *qmux =
		l_container_of(device, struct qmi_device_qmux, super);
	uint8_t release_req[] = { 0x01, 0x02, 0x00, service_type, client_id };
	struct qmi_request *req;

	qmux->release_users++;

	req = __request_alloc(QMI_SERVICE_CONTROL, 0x00,
			QMI_CTL_RELEASE_CLIENT_ID,
			release_req, sizeof(release_req),
			qmux_client_release_callback, qmux);

	__ctl_request_submit(qmux, req);
}

static void qmux_shutdown_destroy(void *user_data)
{
	struct qmi_device_qmux *qmux = user_data;

	if (qmux->shutdown_destroy)
		qmux->shutdown_destroy(qmux->shutdown_user_data);

	qmux->shutdown_idle = NULL;

	__qmi_device_shutdown_finished(&qmux->super);
}

static void qmux_shutdown_callback(struct l_idle *idle, void *user_data)
{
	struct qmi_device_qmux *qmux = user_data;

	if (qmux->release_users > 0)
		return;

	qmux->super.shutting_down = true;

	if (qmux->shutdown_func)
		qmux->shutdown_func(qmux->shutdown_user_data);

	qmux->super.shutting_down = false;

	l_idle_remove(qmux->shutdown_idle);
}

static int qmi_device_qmux_shutdown(struct qmi_device *device,
					qmi_shutdown_func_t func,
					void *user_data,
					qmi_destroy_func_t destroy)
{
	struct qmi_device_qmux *qmux =
		l_container_of(device, struct qmi_device_qmux, super);

	if (qmux->shutdown_idle)
		return -EALREADY;

	__debug_device(&qmux->super, "device %p shutdown", &qmux->super);

	qmux->shutdown_idle = l_idle_create(qmux_shutdown_callback, qmux,
						qmux_shutdown_destroy);

	if (!qmux->shutdown_idle)
		return -EIO;

	qmux->shutdown_func = func;
	qmux->shutdown_user_data = user_data;
	qmux->shutdown_destroy = destroy;

	return 0;
}

static void qmi_device_qmux_destroy(struct qmi_device *device)
{
	struct qmi_device_qmux *qmux =
		l_container_of(device, struct qmi_device_qmux, super);

	if (qmux->shutdown_idle)
		l_idle_remove(qmux->shutdown_idle);

	l_free(qmux->version_str);
	l_free(qmux);
}

static const struct qmi_device_ops qmux_ops = {
	.discover = qmi_device_qmux_discover,
	.client_create = qmi_device_qmux_client_create,
	.client_release = qmi_device_qmux_client_release,
	.shutdown = qmi_device_qmux_shutdown,
	.destroy = qmi_device_qmux_destroy,
};

struct qmi_device *qmi_device_new_qmux(const char *device)
{
	struct qmi_device_qmux *qmux;
	int fd;

	fd = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return NULL;

	qmux = l_new(struct qmi_device_qmux, 1);

	if (qmi_device_init(&qmux->super, fd, &qmux_ops) < 0) {
		close(fd);
		l_free(qmux);
		return NULL;
	}

	qmux->next_control_tid = 1;
	l_io_set_read_handler(qmux->super.io, received_qmux_data, qmux, NULL);

	return &qmux->super;
}

struct qmi_param *qmi_param_new(void)
{
	struct qmi_param *param;

	param = l_new(struct qmi_param, 1);
	if (!param)
		return NULL;

	return param;
}

void qmi_param_free(struct qmi_param *param)
{
	if (!param)
		return;

	l_free(param->data);
	l_free(param);
}

bool qmi_param_append(struct qmi_param *param, uint8_t type,
					uint16_t length, const void *data)
{
	struct qmi_tlv_hdr *tlv;
	void *ptr;

	if (!param || !type)
		return false;

	if (!length)
		return true;

	if (!data)
		return false;

	if (param->data)
		ptr = l_realloc(param->data,
				param->length + QMI_TLV_HDR_SIZE + length);
	else
		ptr = l_malloc(QMI_TLV_HDR_SIZE + length);

	tlv = ptr + param->length;

	tlv->type = type;
	tlv->length = L_CPU_TO_LE16(length);
	memcpy(tlv->value, data, length);

	param->data = ptr;
	param->length += QMI_TLV_HDR_SIZE + length;

	return true;
}

bool qmi_param_append_uint8(struct qmi_param *param, uint8_t type,
							uint8_t value)
{
	unsigned char buf[1] = { value };

	return qmi_param_append(param, type, sizeof(buf), buf);
}

bool qmi_param_append_uint16(struct qmi_param *param, uint8_t type,
							uint16_t value)
{
	unsigned char buf[2] = { value & 0xff, (value & 0xff00) >> 8 };

	return qmi_param_append(param, type, sizeof(buf), buf);
}

bool qmi_param_append_uint32(struct qmi_param *param, uint8_t type,
							uint32_t value)
{
	unsigned char buf[4] = { value & 0xff, (value & 0xff00) >> 8,
					(value & 0xff0000) >> 16,
					(value & 0xff000000) >> 24 };

	return qmi_param_append(param, type, sizeof(buf), buf);
}

struct qmi_param *qmi_param_new_uint8(uint8_t type, uint8_t value)
{
	struct qmi_param *param;

	param = qmi_param_new();

	if (!qmi_param_append_uint8(param, type, value)) {
		qmi_param_free(param);
		return NULL;
	}

	return param;
}

struct qmi_param *qmi_param_new_uint16(uint8_t type, uint16_t value)
{
	struct qmi_param *param;

	param = qmi_param_new();

	if (!qmi_param_append_uint16(param, type, value)) {
		qmi_param_free(param);
		return NULL;
	}

	return param;
}

struct qmi_param *qmi_param_new_uint32(uint8_t type, uint32_t value)
{
	struct qmi_param *param;

	param = qmi_param_new();

	if (!qmi_param_append_uint32(param, type, value)) {
		qmi_param_free(param);
		return NULL;
	}

	return param;
}

bool qmi_result_set_error(struct qmi_result *result, uint16_t *error)
{
	if (!result) {
		if (error)
			*error = 0xffff;
		return true;
	}

	if (result->result == 0x0000)
		return false;

	if (error)
		*error = result->error;

	return true;
}

const char *qmi_result_get_error(struct qmi_result *result)
{
	if (!result)
		return NULL;

	if (result->result == 0x0000)
		return NULL;

	return __error_to_string(result->error);
}

const void *qmi_result_get(struct qmi_result *result, uint8_t type,
							uint16_t *length)
{
	if (!result || !type)
		return NULL;

	return tlv_get(result->data, result->length, type, length);
}

char *qmi_result_get_string(struct qmi_result *result, uint8_t type)
{
	const void *ptr;
	uint16_t len;

	if (!result || !type)
		return NULL;

	ptr = tlv_get(result->data, result->length, type, &len);
	if (!ptr)
		return NULL;

	return l_strndup(ptr, len);
}

bool qmi_result_get_uint8(struct qmi_result *result, uint8_t type,
							uint8_t *value)
{
	const unsigned char *ptr;
	uint16_t len;

	if (!result || !type)
		return false;

	ptr = tlv_get(result->data, result->length, type, &len);
	if (!ptr)
		return false;

	if (value)
		*value = *ptr;

	return true;
}

bool qmi_result_get_int16(struct qmi_result *result, uint8_t type,
							int16_t *value)
{
	const unsigned char *ptr;
	uint16_t len, tmp;

	if (!result || !type)
		return false;

	ptr = tlv_get(result->data, result->length, type, &len);
	if (!ptr)
		return false;

	memcpy(&tmp, ptr, 2);

	if (value)
		*value = L_LE16_TO_CPU(tmp);

	return true;
}

bool qmi_result_get_uint16(struct qmi_result *result, uint8_t type,
							uint16_t *value)
{
	const unsigned char *ptr;
	uint16_t len, tmp;

	if (!result || !type)
		return false;

	ptr = tlv_get(result->data, result->length, type, &len);
	if (!ptr)
		return false;

	memcpy(&tmp, ptr, 2);

	if (value)
		*value = L_LE16_TO_CPU(tmp);

	return true;
}

bool qmi_result_get_uint32(struct qmi_result *result, uint8_t type,
							uint32_t *value)
{
	const unsigned char *ptr;
	uint16_t len;
	uint32_t tmp;

	if (!result || !type)
		return false;

	ptr = tlv_get(result->data, result->length, type, &len);
	if (!ptr)
		return false;

	memcpy(&tmp, ptr, 4);

	if (value)
		*value = L_LE32_TO_CPU(tmp);

	return true;
}

bool qmi_result_get_uint64(struct qmi_result *result, uint8_t type,
							uint64_t *value)
{
	const unsigned char *ptr;
	uint16_t len;
	uint64_t tmp;

	if (!result || !type)
		return false;

	ptr = tlv_get(result->data, result->length, type, &len);
	if (!ptr)
		return false;

	memcpy(&tmp, ptr, 8);

	if (value)
		*value = L_LE64_TO_CPU(tmp);

	return true;
}

bool qmi_service_create_shared(struct qmi_device *device, uint16_t type,
			qmi_create_func_t func, void *user_data,
			qmi_destroy_func_t destroy)
{
	struct l_queue *shared;
	struct qmi_service *service = NULL;
	unsigned int type_val = type;
	int r;

	if (!device || !func)
		return false;

	if (type == QMI_SERVICE_CONTROL)
		return false;

	shared = l_hashmap_lookup(device->service_list,
					L_UINT_TO_PTR(type_val | 0x80000000));

	if (!shared) {
		/*
		 * There is no way to find in an l_hashmap using a custom
		 * function. Instead we use a temporary struct to store the
		 * found service. This is not very clean, but we expect this
		 * code to be refactored soon.
		 */
		struct service_find_by_type_data data;

		data.type = type_val;
		data.found_service = NULL;
		l_hashmap_foreach(device->service_list,	__service_find_by_type,
					&data);
		service = data.found_service;
	} else
		type_val |= 0x80000000;

	if (shared || service) {
		struct service_create_shared_data *data;

		data = l_new(struct service_create_shared_data, 1);

		data->super.destroy = service_create_shared_data_free;
		data->device = device;
		data->func = func;
		data->user_data = user_data;
		data->destroy = destroy;

		if (!(type_val & 0x80000000)) {
			data->service = qmi_service_ref(service);
			data->idle = l_idle_create(service_create_shared_reply,
							data, NULL);
		} else
			l_queue_push_head(shared, data);

		__qmi_device_discovery_started(device, &data->super);

		return true;
	}

	if (!device->ops->client_create)
		return -ENOTSUP;

	r = device->ops->client_create(device, type, func, user_data, destroy);
	return r == 0;
}

bool qmi_service_create(struct qmi_device *device,
				uint16_t type, qmi_create_func_t func,
				void *user_data, qmi_destroy_func_t destroy)
{
	return qmi_service_create_shared(device, type, func,
						user_data, destroy);
}

struct qmi_service *qmi_service_ref(struct qmi_service *service)
{
	if (!service)
		return NULL;

	__sync_fetch_and_add(&service->ref_count, 1);

	return service;
}

void qmi_service_unref(struct qmi_service *service)
{
	struct qmi_device *device;
	unsigned int hash_id;

	if (!service)
		return;

	if (__sync_sub_and_fetch(&service->ref_count, 1))
		return;

	device = service->device;
	if (!device) {
		l_free(service);
		return;
	}

	qmi_service_cancel_all(service);
	qmi_service_unregister_all(service);

	hash_id = service->type | (service->client_id << 8);

	l_hashmap_remove(device->service_list, L_UINT_TO_PTR(hash_id));

	if (device->ops->client_release)
		device->ops->client_release(device, service->type,
							service->client_id);

	l_free(service);
}

const char *qmi_service_get_identifier(struct qmi_service *service)
{
	if (!service)
		return NULL;

	return __service_type_to_string(service->type);
}

bool qmi_service_get_version(struct qmi_service *service,
					uint16_t *major, uint16_t *minor)
{
	if (!service)
		return false;

	if (major)
		*major = service->major;

	if (minor)
		*minor = service->minor;

	return true;
}

struct service_send_data {
	qmi_result_func_t func;
	void *user_data;
	qmi_destroy_func_t destroy;
};

static void service_send_free(struct service_send_data *data)
{
	if (data->destroy)
		data->destroy(data->user_data);

	l_free(data);
}

static void service_send_callback(uint16_t message, uint16_t length,
					const void *buffer, void *user_data)
{
	struct service_send_data *data = user_data;
	const struct qmi_result_code *result_code;
	uint16_t len;
	struct qmi_result result;

	result.message = message;
	result.data = buffer;
	result.length = length;

	result_code = tlv_get(buffer, length, 0x02, &len);
	if (!result_code)
		goto done;

	if (len != QMI_RESULT_CODE_SIZE)
		goto done;

	result.result = L_LE16_TO_CPU(result_code->result);
	result.error = L_LE16_TO_CPU(result_code->error);

done:
	if (data->func)
		data->func(&result, data->user_data);

	service_send_free(data);
}

uint16_t qmi_service_send(struct qmi_service *service,
				uint16_t message, struct qmi_param *param,
				qmi_result_func_t func,
				void *user_data, qmi_destroy_func_t destroy)
{
	struct qmi_device *device;
	struct service_send_data *data;
	struct qmi_request *req;
	uint16_t tid;

	if (!service)
		return 0;

	if (!service->client_id)
		return 0;

	device = service->device;
	if (!device)
		return 0;

	data = l_new(struct service_send_data, 1);

	data->func = func;
	data->user_data = user_data;
	data->destroy = destroy;

	req = __request_alloc(service->type, service->client_id,
				message,
				param ? param->data : NULL,
				param ? param->length : 0,
				service_send_callback, data);

	qmi_param_free(param);

	tid = __request_submit(device, req);

	return tid;
}

bool qmi_service_cancel(struct qmi_service *service, uint16_t id)
{
	unsigned int tid = id;
	struct qmi_device *device;
	struct qmi_request *req;

	if (!service || !tid)
		return false;

	if (!service->client_id)
		return false;

	device = service->device;
	if (!device)
		return false;

	req = l_queue_remove_if(device->req_queue, __request_compare,
					L_UINT_TO_PTR(tid));
	if (!req) {
		req = l_queue_remove_if(device->service_queue,
						__request_compare,
						L_UINT_TO_PTR(tid));
		if (!req)
			return false;
	}

	service_send_free(req->user_data);

	__request_free(req);

	return true;
}

static bool remove_req_if_match(void *data, void *user_data)
{
	struct qmi_request *req = data;
	uint8_t client = L_PTR_TO_UINT(user_data);

	if (!req->client || req->client != client)
		return false;

	service_send_free(req->user_data);
	__request_free(req);

	return true;
}

static void remove_client(struct l_queue *queue, uint8_t client)
{
	l_queue_foreach_remove(queue, remove_req_if_match,
				L_UINT_TO_PTR(client));
}

bool qmi_service_cancel_all(struct qmi_service *service)
{
	struct qmi_device *device;

	if (!service)
		return false;

	if (!service->client_id)
		return false;

	device = service->device;
	if (!device)
		return false;

	remove_client(device->req_queue, service->client_id);
	remove_client(device->service_queue, service->client_id);

	return true;
}

uint16_t qmi_service_register(struct qmi_service *service,
				uint16_t message, qmi_result_func_t func,
				void *user_data, qmi_destroy_func_t destroy)
{
	struct qmi_notify *notify;

	if (!service || !func)
		return 0;

	notify = l_new(struct qmi_notify, 1);

	if (service->next_notify_id < 1)
		service->next_notify_id = 1;

	notify->id = service->next_notify_id++;
	notify->message = message;
	notify->callback = func;
	notify->user_data = user_data;
	notify->destroy = destroy;

	l_queue_push_tail(service->notify_list, notify);

	return notify->id;
}

bool qmi_service_unregister(struct qmi_service *service, uint16_t id)
{
	unsigned int nid = id;
	struct qmi_notify *notify;

	if (!service || !id)
		return false;

	notify = l_queue_remove_if(service->notify_list, __notify_compare,
					L_UINT_TO_PTR(nid));

	if (!notify)
		return false;

	__notify_free(notify);

	return true;
}

bool qmi_service_unregister_all(struct qmi_service *service)
{
	if (!service)
		return false;

	l_queue_destroy(service->notify_list, __notify_free);
	service->notify_list = NULL;

	return true;
}
