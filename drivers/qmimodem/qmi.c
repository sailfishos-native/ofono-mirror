/*
 * oFono - Open Source Telephony
 * Copyright (C) 2011-2012  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stddef.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>
#include <linux/qrtr.h>
#include <sys/socket.h>

#include <ell/ell.h>

#include <ofono/log.h>

#include "qmi.h"
#include "ctl.h"

#define DISCOVER_TIMEOUT 5

#define DEBUG(debug, fmt, args...)					\
	l_util_debug((debug)->func, (debug)->user_data, "%s:%i " fmt,	\
			__func__, __LINE__, ## args)

#define SERVICE_VERSION(major, minor)				\
	(((major) << 16) | (minor))

struct qmi_transport;
struct qmi_request;

typedef void (*response_func_t)(struct qmi_request *, uint16_t message,
				uint16_t length, const void *buffer);

struct qmi_service_info {
	uint32_t service_type;
	uint32_t qrtr_port;		/* Always 0 on qmux */
	uint32_t qrtr_node;		/* Always 0 on qmux */
	uint16_t major;
	uint16_t minor;			/* Always 0 on qrtr */
	uint32_t instance;		/* Always 0 on qmux */
};

struct qmi_request {
	uint16_t tid;
	unsigned int group_id;		/* Always 0 for control */
	unsigned int service_handle;	/* Always 0 for control */
	uint8_t client;			/* Always 0 for control and qrtr */
	struct qmi_service_info info;	/* Not used for control requests */
	void *user_data;
	response_func_t callback;
	void (*free_request)(struct qmi_request *req);
	uint16_t len;
	uint8_t data[];
};

struct qmi_service_request {
	qmi_service_result_func_t func;
	void *user_data;
	qmi_destroy_func_t destroy;
	struct qmi_request super;
};

struct debug_data {
	qmi_debug_func_t func;
	void *user_data;
};

struct qmi_transport_ops {
	int (*write)(struct qmi_transport *transport, struct qmi_request *req);
};

struct qmi_transport {
	struct l_io *io;
	struct l_queue *req_queue;
	struct l_queue *service_queue;
	uint16_t next_service_tid;
	struct l_hashmap *family_list;
	const struct qmi_transport_ops *ops;
	struct debug_data debug;
	bool writer_active : 1;
};

struct qmi_qmux_device {
	struct qmi_service_info *service_list;
	uint16_t n_services;
	struct qmi_transport transport;
	char *version_str;
	struct debug_data debug;
	struct {
		qmi_qmux_device_discover_func_t func;
		void *user_data;
		qmi_destroy_func_t destroy;
		struct l_timeout *timeout;
		uint16_t tid;
	} discover;
	struct {
		qmi_qmux_device_shutdown_func_t func;
		void *user_data;
		qmi_destroy_func_t destroy;
		struct l_idle *idle;
		unsigned int release_users;
	} shutdown;
	uint8_t next_control_tid;
	unsigned int next_group_id;	/* Matches requests with services */
	struct l_queue *control_queue;
	bool shutting_down : 1;
	bool destroyed : 1;
};

struct service_family {
	int ref_count;
	struct qmi_transport *transport;
	struct qmi_service_info info;
	unsigned int group_id;
	uint8_t client_id;
	uint16_t next_notify_id;
	unsigned int next_service_handle;
	struct l_queue *notify_list;
	void (*free_family)(struct service_family *family);
};

struct qmi_service {
	unsigned int handle;	/* Uniquely identifies this client's reqs */
	struct service_family *family;
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

struct qmi_notify {
	uint16_t id;
	uint16_t message;
	unsigned int service_handle;
	qmi_service_result_func_t callback;
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

static unsigned int next_id(unsigned int *id)
{
	if (*id == 0) /* 0 is reserved for control */
		*id = 1;

	return *id++;
}

static bool qmi_service_info_matches(const void *data, const void *user)
{
	const struct qmi_service_info *info = data;
	const struct qmi_service_info *match = user;

	if (info->service_type != match->service_type)
		return false;

	if (info->qrtr_node != match->qrtr_node)
		return false;

	if (info->qrtr_port != match->qrtr_port)
		return false;

	return true;
}

static void *__request_alloc(uint32_t service_type,
					uint8_t client, uint16_t message,
					const void *data, uint16_t length,
					size_t offset)
{
	void *mem;
	struct qmi_request *req;
	struct qmi_mux_hdr *hdr;
	struct qmi_message_hdr *msg;
	uint16_t hdrlen = QMI_MUX_HDR_SIZE;
	uint16_t msglen;

	if (service_type == QMI_SERVICE_CONTROL)
		hdrlen += QMI_CONTROL_HDR_SIZE;
	else
		hdrlen += QMI_SERVICE_HDR_SIZE;

	msglen = hdrlen + QMI_MESSAGE_HDR_SIZE + length;
	mem = l_malloc(offset + sizeof(struct qmi_request) + msglen);
	req = mem + offset;
	memset(req, 0, sizeof(struct qmi_request));
	req->len = msglen;
	req->client = client;

	hdr = (struct qmi_mux_hdr *) req->data;

	hdr->frame = 0x01;
	hdr->length = L_CPU_TO_LE16(req->len - 1);
	hdr->flags = 0x00;
	hdr->service = service_type; /* qmux service types are 8 bits */
	hdr->client = client;

	msg = (struct qmi_message_hdr *) &req->data[hdrlen];

	msg->message = L_CPU_TO_LE16(message);
	msg->length = L_CPU_TO_LE16(length);

	if (data && length > 0)
		memcpy(req->data + hdrlen + QMI_MESSAGE_HDR_SIZE, data, length);

	return mem;
}

static void *__control_request_alloc(uint16_t message,
					const void *data, uint16_t length,
					size_t offset)
{
	return __request_alloc(QMI_SERVICE_CONTROL, 0x00, message,
					data, length, offset);
}

static void __request_free(void *data)
{
	struct qmi_request *req = data;

	if (req->free_request)
		req->free_request(req);
	else
		l_free(req);
}

static bool __request_compare(const void *a, const void *b)
{
	const struct qmi_request *req = a;
	uint16_t tid = L_PTR_TO_UINT(b);

	return req->tid == tid;
}

static void __notify_free(void *data)
{
	struct qmi_notify *notify = data;

	if (notify->destroy)
		notify->destroy(notify->user_data);

	l_free(notify);
}

struct notify_compare_details {
	uint16_t id;
	unsigned int service_handle;
};

static bool __notify_compare(const void *data, const void *user_data)
{
	const struct qmi_notify *notify = data;
	const struct notify_compare_details *details = user_data;

	return notify->id == details->id &&
			notify->service_handle == details->service_handle;
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

static void __debug_data_init(struct debug_data *debug,
				qmi_debug_func_t func, void *user_data)
{
	debug->func = func;
	debug->user_data = user_data;
}

static void __debug_msg(char dir, const struct qmi_message_hdr *msg,
			uint32_t service_type, uint8_t transaction_type,
			uint16_t tid, uint8_t client, uint16_t overall_length,
			qmi_debug_func_t function, void *user_data)
{
	const char *service;
	const void *ptr = msg + 1;
	uint16_t offset;
	char strbuf[72 + 16], *str;
	bool pending_print = false;
	const char *transaction_type_string;

	if (!function)
		return;

	str = strbuf;
	service = __service_type_to_string(service_type);
	if (service)
		str += sprintf(str, "%c   %s", dir, service);
	else
		str += sprintf(str, "%c   %d", dir, service_type);

	switch (transaction_type) {
	case 0x00:
		transaction_type_string = "_req";
		break;
	case 0x01:
		transaction_type_string = "_resp";
		break;
	case 0x02:
		transaction_type_string = "_ind";
		break;
	default:
		transaction_type_string = "";
		break;
	}

	str += sprintf(str, "%s msg=%d len=%d", transaction_type_string,
				L_LE16_TO_CPU(msg->message),
				L_LE16_TO_CPU(msg->length));

	str += sprintf(str, " [client=%d,type=%d,tid=%d,len=%d]",
				client, transaction_type, tid, overall_length);

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

static void __qmux_debug_msg(const char dir, const void *buf, size_t len,
				qmi_debug_func_t function, void *user_data)
{
	const struct qmi_mux_hdr *hdr;
	const struct qmi_message_hdr *msg;
	uint8_t transaction_type;
	uint16_t tid;

	if (!len)
		return;

	hdr = buf;

	if (hdr->service == QMI_SERVICE_CONTROL) {
		const struct qmi_control_hdr *ctl;

		ctl = buf + QMI_MUX_HDR_SIZE;
		msg = buf + QMI_MUX_HDR_SIZE + QMI_CONTROL_HDR_SIZE;

		transaction_type = ctl->type;
		tid = ctl->transaction;
	} else {
		const struct qmi_service_hdr *srv;

		srv = buf + QMI_MUX_HDR_SIZE;
		msg = buf + QMI_MUX_HDR_SIZE + QMI_SERVICE_HDR_SIZE;

		transaction_type = srv->type >> 1;
		tid = L_LE16_TO_CPU(srv->transaction);
	}

	__debug_msg(dir, msg, hdr->service, transaction_type, tid, hdr->client,
			L_LE16_TO_CPU(hdr->length), function, user_data);
}

static void __qrtr_debug_msg(const char dir, const void *buf, size_t len,
				uint32_t service_type,
				const struct debug_data *debug)
{
	const struct qmi_service_hdr *srv;
	const struct qmi_message_hdr *msg;
	uint16_t tid;

	if (!len)
		return;

	srv = buf;
	msg = buf + QMI_SERVICE_HDR_SIZE;

	tid = L_LE16_TO_CPU(srv->transaction);

	__debug_msg(dir, msg, service_type, srv->type >> 1, tid, 0, len,
						debug->func, debug->user_data);
}

static bool can_write_data(struct l_io *io, void *user_data)
{
	struct qmi_transport *transport = user_data;
	struct qmi_request *req;
	int r;

	req = l_queue_pop_head(transport->req_queue);
	if (!req)
		return false;

	r = transport->ops->write(transport, req);
	if (r < 0) {
		__request_free(req);
		return false;
	}

	if (l_queue_length(transport->req_queue) > 0)
		return true;

	return false;
}

static void write_watch_destroy(void *user_data)
{
	struct qmi_transport *transport = user_data;

	transport->writer_active = false;
}

static void wakeup_writer(struct qmi_transport *transport)
{
	if (transport->writer_active)
		return;

	l_io_set_write_handler(transport->io, can_write_data, transport,
				write_watch_destroy);

	transport->writer_active = true;
}

static uint16_t __service_request_submit(struct qmi_transport *transport,
						struct qmi_service *service,
						struct qmi_request *req)
{
	struct qmi_service_hdr *hdr =
		(struct qmi_service_hdr *) &req->data[QMI_MUX_HDR_SIZE];

	req->tid = transport->next_service_tid++;

	if (transport->next_service_tid < 256)
		transport->next_service_tid = 256;

	req->group_id = service->family->group_id;
	req->service_handle = service->handle;

	hdr->type = 0x00;
	hdr->transaction = L_CPU_TO_LE16(req->tid);

	l_queue_push_tail(transport->req_queue, req);
	wakeup_writer(transport);

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
	struct service_family *family = value;
	struct qmi_result *result = user_data;

	l_queue_foreach(family->notify_list, service_notify_if_message_matches,
				result);
}

static unsigned int family_list_create_hash(uint16_t service_type,
							uint8_t client_id)
{
	return (service_type | (client_id << 16));
}

static void handle_indication(struct qmi_transport *transport,
			uint32_t service_type, uint8_t client_id,
			uint16_t message, uint16_t length, const void *data)
{
	struct service_family *family;
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
		l_hashmap_foreach(transport->family_list, service_notify,
					&result);
		return;
	}

	hash_id = family_list_create_hash(service_type, client_id);
	family = l_hashmap_lookup(transport->family_list,
					L_UINT_TO_PTR(hash_id));

	if (!family)
		return;

	service_notify(NULL, family, &result);
}

static void __rx_message(struct qmi_transport *transport,
				uint32_t service_type, uint8_t client_id,
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
		handle_indication(transport, service_type, client_id,
					message, length, data);
		return;
	}

	req = l_queue_remove_if(transport->service_queue, __request_compare,
						L_UINT_TO_PTR(tid));
	if (!req)
		return;

	if (req->callback)
		req->callback(req, message, length, data);

	__request_free(req);
}

static void family_destroy(void *data)
{
	struct service_family *family = data;

	if (!family->transport)
		return;

	family->transport = NULL;
}

static int qmi_transport_open(struct qmi_transport *transport, int fd,
				const struct qmi_transport_ops *ops)
{
	long flags;

	flags = fcntl(fd, F_GETFL, NULL);
	if (flags < 0)
		return -EIO;

	if (!(flags & O_NONBLOCK)) {
		int r = fcntl(fd, F_SETFL, flags | O_NONBLOCK);

		if (r < 0)
			return -errno;
	}

	transport->io = l_io_new(fd);
	l_io_set_close_on_destroy(transport->io, true);

	transport->req_queue = l_queue_new();
	transport->service_queue = l_queue_new();
	transport->next_service_tid = 256;
	transport->ops = ops;
	transport->family_list = l_hashmap_new();

	return 0;
}

static void qmi_transport_close(struct qmi_transport *transport)
{
	l_queue_destroy(transport->service_queue, __request_free);
	l_queue_destroy(transport->req_queue, __request_free);

	l_io_destroy(transport->io);

	l_hashmap_destroy(transport->family_list, family_destroy);
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

static const struct qmi_service_info *__qmux_service_info_find(
				struct qmi_qmux_device *qmux, uint16_t type)
{
	unsigned int i;

	for (i = 0; i < qmux->n_services; i++) {
		if (qmux->service_list[i].service_type == type)
			return qmux->service_list + i;
	}

	return NULL;
}

bool qmi_qmux_device_get_service_version(struct qmi_qmux_device *qmux,
					uint16_t type,
					uint16_t *major, uint16_t *minor)
{
	const struct qmi_service_info *info;

	if (!qmux)
		return false;

	info = __qmux_service_info_find(qmux, type);
	if (!info)
		return false;

	*major = info->major;
	*minor = info->minor;
	return true;
}

bool qmi_qmux_device_has_service(struct qmi_qmux_device *qmux, uint16_t type)
{
	if (!qmux)
		return false;

	return __qmux_service_info_find(qmux, type);
}

static int qmi_qmux_device_write(struct qmi_transport *transport,
					struct qmi_request *req)
{
	struct qmi_qmux_device *qmux =
		l_container_of(transport, struct qmi_qmux_device, transport);
	struct qmi_mux_hdr *hdr;
	ssize_t bytes_written;

	bytes_written = write(l_io_get_fd(transport->io), req->data, req->len);
	if (bytes_written < 0)
		return -errno;

	l_util_hexdump(false, req->data, bytes_written,
			transport->debug.func, transport->debug.user_data);

	__qmux_debug_msg(' ', req->data, bytes_written,
			transport->debug.func, transport->debug.user_data);

	hdr = (struct qmi_mux_hdr *) req->data;

	if (hdr->service == QMI_SERVICE_CONTROL)
		l_queue_push_tail(qmux->control_queue, req);
	else
		l_queue_push_tail(transport->service_queue, req);

	return 0;
}

static void __rx_ctl_message(struct qmi_qmux_device *qmux,
				uint8_t service_type, uint8_t client_id,
				const void *buf)
{
	const struct qmi_control_hdr *control = buf;
	const struct qmi_message_hdr *msg = buf + QMI_CONTROL_HDR_SIZE;
	const void *data = buf + QMI_CONTROL_HDR_SIZE + QMI_MESSAGE_HDR_SIZE;
	struct qmi_request *req;
	uint16_t message;
	uint16_t length;

	/* Ignore control messages with client identifier */
	if (client_id != 0x00)
		return;

	message = L_LE16_TO_CPU(msg->message);
	length = L_LE16_TO_CPU(msg->length);

	if (control->type == 0x02 && control->transaction == 0x00) {
		handle_indication(&qmux->transport, service_type, client_id,
					message, length, data);
		return;
	}

	req = l_queue_remove_if(qmux->control_queue, __request_compare,
					L_UINT_TO_PTR(control->transaction));
	if (!req)
		return;

	if (req->callback)
		req->callback(req, message, length, data);

	__request_free(req);
}

static bool received_qmux_data(struct l_io *io, void *user_data)
{
	struct qmi_qmux_device *qmux = user_data;
	struct qmi_transport *transport = &qmux->transport;
	struct qmi_mux_hdr *hdr;
	unsigned char buf[2048];
	ssize_t bytes_read;
	uint16_t offset;

	bytes_read = read(l_io_get_fd(qmux->transport.io), buf, sizeof(buf));
	if (bytes_read < 0)
		return true;

	l_util_hexdump(true, buf, bytes_read,
			transport->debug.func, transport->debug.user_data);

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

		__qmux_debug_msg(' ', buf + offset, len,
				transport->debug.func,
				transport->debug.user_data);

		msg = buf + offset + QMI_MUX_HDR_SIZE;

		if (hdr->service == QMI_SERVICE_CONTROL)
			__rx_ctl_message(qmux, hdr->service, hdr->client, msg);
		else
			__rx_message(&qmux->transport,
					hdr->service, hdr->client, msg);

		offset += len;
	}

	return true;
}

static struct service_family *service_family_ref(struct service_family *family)
{
	family->ref_count++;

	return family;
}

static void service_family_unref(struct service_family *family)
{
	struct qmi_transport *transport;

	if (--family->ref_count)
		return;

	transport = family->transport;
	if (!transport)
		goto done;

	if (family->client_id) {
		unsigned int hash_id =
			family_list_create_hash(family->info.service_type,
							family->client_id);
		l_hashmap_remove(transport->family_list, L_UINT_TO_PTR(hash_id));
	}

	l_hashmap_remove(transport->family_list,
				L_UINT_TO_PTR(family->info.service_type));

done:
	l_queue_destroy(family->notify_list, NULL);
	family->free_family(family);
}

static uint8_t __ctl_request_submit(struct qmi_qmux_device *qmux,
					struct qmi_request *req,
					response_func_t callback)
{
	struct qmi_control_hdr *hdr =
		(struct qmi_control_hdr *) &req->data[QMI_MUX_HDR_SIZE];

	hdr->type = 0x00;
	hdr->transaction = qmux->next_control_tid++;

	if (qmux->next_control_tid == 0)
		qmux->next_control_tid = 1;

	req->tid = hdr->transaction;
	req->callback = callback;

	l_queue_push_tail(qmux->transport.req_queue, req);
	wakeup_writer(&qmux->transport);

	return req->tid;
}

static void __service_family_init(struct service_family *family,
					struct qmi_transport *transport,
					unsigned int group_id,
					const struct qmi_service_info *info,
					uint8_t client_id)
{
	family->ref_count = 0;
	family->transport = transport;
	family->client_id = client_id;
	family->notify_list = l_queue_new();
	family->group_id = group_id;
	memcpy(&family->info, info, sizeof(family->info));
}

static struct qmi_service *service_create(struct service_family *family)
{
	struct qmi_service *service;

	if (family->next_service_handle == 0) /* 0 is reserved for control */
		family->next_service_handle = 1;

	service = l_new(struct qmi_service, 1);
	service->handle = family->next_service_handle++;
	service->family = service_family_ref(family);

	return service;
}

static struct qmi_request *find_control_request(struct qmi_qmux_device *qmux,
						uint16_t tid)
{
	struct qmi_request *req;

	if (!tid)
		return NULL;

	req = l_queue_remove_if(qmux->transport.req_queue,
					__request_compare, L_UINT_TO_PTR(tid));
	if (req)
		return req;

	req = l_queue_remove_if(qmux->control_queue,
					__request_compare, L_UINT_TO_PTR(tid));
	return req;
}

static void __qmux_discovery_finished(struct qmi_qmux_device *qmux)
{
	l_timeout_remove(qmux->discover.timeout);
	qmux->discover.func(qmux->discover.user_data);

	if (qmux->discover.destroy)
		qmux->discover.destroy(qmux->discover.user_data);

	memset(&qmux->discover, 0, sizeof(qmux->discover));
}

static void qmux_sync_callback(struct qmi_request *req, uint16_t message,
					uint16_t length, const void *buffer)
{
	struct qmi_qmux_device *qmux = req->user_data;

	__qmux_discovery_finished(qmux);
}

static void qmux_discover_callback(struct qmi_request *req, uint16_t message,
					uint16_t length, const void *buffer)
{
	struct qmi_qmux_device *qmux = req->user_data;
	const struct qmi_result_code *result_code;
	const struct qmi_service_list *service_list;
	const void *ptr;
	uint16_t len;
	unsigned int i;
	uint32_t control_version = 0;

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

	if (!service_list->count)
		goto done;

	l_free(qmux->service_list);
	qmux->n_services = 0;
	qmux->service_list = l_new(struct qmi_service_info, service_list->count);

	for (i = 0; i < service_list->count; i++) {
		uint16_t major =
			L_LE16_TO_CPU(service_list->services[i].major);
		uint16_t minor =
			L_LE16_TO_CPU(service_list->services[i].minor);
		uint8_t type = service_list->services[i].type;
		const char *name = __service_type_to_string(type);

		if (name)
			DEBUG(&qmux->debug, "discovered service [%s %d.%d]",
					name, major, minor);
		else
			DEBUG(&qmux->debug, "discovered service [%d %d.%d]",
					type, major, minor);

		if (type == QMI_SERVICE_CONTROL) {
			control_version = SERVICE_VERSION(major, minor);
			continue;
		}

		qmux->service_list[qmux->n_services].service_type = type;
		qmux->service_list[qmux->n_services].major = major;
		qmux->service_list[qmux->n_services].minor = minor;
		qmux->n_services += 1;
	}

	ptr = tlv_get(buffer, length, 0x10, &len);
	if (!ptr)
		goto done;

	qmux->version_str = l_strndup(ptr + 1, *((uint8_t *) ptr));
	DEBUG(&qmux->debug, "version string: %s", qmux->version_str);

done:
	/*
	 * If the device support the QMI call SYNC over the CTL interface,
	 * invoke it to reset the state, including release all previously
	 * allocated clients
	 */
	if (control_version >= SERVICE_VERSION(1, 5)) {
		struct qmi_request *req =
			__control_request_alloc(QMI_CTL_SYNC, NULL, 0, 0);

		req->user_data = qmux;

		DEBUG(&qmux->debug, "Sending sync to reset QMI");
		qmux->discover.tid = __ctl_request_submit(qmux, req,
							qmux_sync_callback);

		return;
	}

	__qmux_discovery_finished(qmux);
}

static void qmux_discover_reply_timeout(struct l_timeout *timeout,
							void *user_data)
{
	struct qmi_qmux_device *qmux = user_data;
	struct qmi_request *req;

	/* remove request from queues */
	req = find_control_request(qmux, qmux->discover.tid);
	if (req)
		__request_free(req);

	__qmux_discovery_finished(qmux);
}

int qmi_qmux_device_discover(struct qmi_qmux_device *qmux,
				qmi_qmux_device_discover_func_t func,
				void *user_data, qmi_destroy_func_t destroy)
{
	struct qmi_request *req;

	if (!qmux)
		return -EINVAL;

	DEBUG(&qmux->debug, "device %p", qmux);

	if (qmux->n_services || qmux->discover.tid)
		return -EALREADY;

	req = __control_request_alloc(QMI_CTL_GET_VERSION_INFO, NULL, 0, 0);
	req->user_data = qmux;

	qmux->discover.func = func;
	qmux->discover.user_data = user_data;
	qmux->discover.destroy = destroy;
	qmux->discover.tid = __ctl_request_submit(qmux, req,
							qmux_discover_callback);
	qmux->discover.timeout =
		l_timeout_create(DISCOVER_TIMEOUT, qmux_discover_reply_timeout,
						qmux, NULL);

	return 0;
}

static void qmux_client_release_callback(struct qmi_request *req,
					uint16_t message, uint16_t length,
					const void *buffer)
{
	struct qmi_qmux_device *qmux = req->user_data;

	qmux->shutdown.release_users--;
}

static void qmux_service_family_free(struct service_family *family)
{
	struct qmi_qmux_device *qmux = l_container_of(family->transport,
							struct qmi_qmux_device,
							transport);
	uint8_t release_req[] = { 0x01, 0x02, 0x00,
				family->info.service_type, family->client_id };
	struct qmi_request *req;

	if (family->transport) {
		qmux->shutdown.release_users++;
		req = __control_request_alloc(QMI_CTL_RELEASE_CLIENT_ID,
					release_req, sizeof(release_req), 0);
		req->user_data = qmux;

		__ctl_request_submit(qmux, req, qmux_client_release_callback);
	}

	l_free(family);
}

static struct service_family *qmux_service_family_new(
					struct qmi_transport *transport,
					unsigned int group_id,
					const struct qmi_service_info *info,
					uint8_t client_id)
{
	struct service_family *family = l_new(struct service_family, 1);

	__service_family_init(family, transport, group_id, info, client_id);
	family->free_family = qmux_service_family_free;

	return family;
}

struct qmux_create_client_request {
	struct qmi_qmux_device *qmux;
	uint8_t type;
	uint16_t major;
	uint16_t minor;
	qmi_qmux_device_create_client_func_t func;
	void *user_data;
	qmi_destroy_func_t destroy;
	struct l_timeout *timeout;
	struct qmi_request super;
};

static void qmux_create_client_request_free(struct qmi_request *r)
{
	struct qmux_create_client_request *req =
		l_container_of(r, struct qmux_create_client_request, super);

	if (req->timeout)
		l_timeout_remove(req->timeout);

	if (req->destroy)
		req->destroy(req->user_data);

	l_free(req);
}

static void qmux_create_client_timeout(struct l_timeout *timeout,
							void *user_data)
{
	struct qmux_create_client_request *req = user_data;
	struct qmi_qmux_device *qmux = req->qmux;

	DEBUG(&qmux->debug, "");

	l_timeout_remove(req->timeout);
	req->timeout = NULL;

	/* remove request from queues */
	find_control_request(qmux, req->super.tid);
	__request_free(&req->super);
}

static void qmux_create_client_callback(struct qmi_request *r,
					uint16_t message, uint16_t length,
					const void *buffer)
{
	struct qmux_create_client_request *req =
		l_container_of(r, struct qmux_create_client_request, super);
	struct qmi_qmux_device *qmux = req->qmux;
	struct service_family *family = NULL;
	struct qmi_service *service = NULL;
	struct qmi_service_info info;
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

	if (client_id->service != req->type)
		goto done;

	memset(&info, 0, sizeof(family->info));
	info.service_type = req->type;
	info.major = req->major;
	info.minor = req->minor;

	family = qmux_service_family_new(&qmux->transport,
					next_id(&qmux->next_group_id),
					&info, client_id->client);
	DEBUG(&qmux->debug, "service family created [client=%d,type=%d]",
			family->client_id, family->info.service_type);

	family = service_family_ref(family);
	hash_id = family_list_create_hash(family->info.service_type,
							family->client_id);
	l_hashmap_insert(qmux->transport.family_list,
					L_UINT_TO_PTR(hash_id), family);

done:
	if (family)
		service = service_create(family);

	if (req->func)
		req->func(service, req->user_data);

	if (family)
		service_family_unref(family);
}

bool qmi_qmux_device_create_client(struct qmi_qmux_device *qmux,
				uint16_t service_type,
				qmi_qmux_device_create_client_func_t func,
				void *user_data, qmi_destroy_func_t destroy)
{
	unsigned char client_req[] = { 0x01, 0x01, 0x00, service_type };
	const struct qmi_service_info *info;
	struct qmux_create_client_request *req;

	if (!qmux || !func)
		return false;

	if (service_type == QMI_SERVICE_CONTROL)
		return false;

	info = __qmux_service_info_find(qmux, service_type);
	if (!info)
		return false;

	DEBUG(&qmux->debug, "creating client [type=%d]", service_type);

	req = __control_request_alloc(QMI_CTL_GET_CLIENT_ID,
				client_req, sizeof(client_req),
				offsetof(struct qmux_create_client_request,
						super));
	req->super.free_request = qmux_create_client_request_free;
	req->qmux = qmux;
	req->type = service_type;
	req->major = info->major;
	req->minor = info->minor;
	req->func = func;
	req->user_data = user_data;
	req->destroy = destroy;
	req->timeout = l_timeout_create(8, qmux_create_client_timeout,
					req, NULL);

	__ctl_request_submit(qmux, &req->super, qmux_create_client_callback);

	return true;
}

static void __qmux_device_free(struct qmi_qmux_device *qmux)
{
	l_queue_destroy(qmux->control_queue, __request_free);

	l_timeout_remove(qmux->discover.timeout);

	if (qmux->discover.destroy)
		qmux->discover.destroy(qmux->discover.user_data);

	if (qmux->shutdown.idle)
		l_idle_remove(qmux->shutdown.idle);

	l_free(qmux->service_list);
	l_free(qmux->version_str);
	l_free(qmux);
}

static void qmux_shutdown_destroy(void *user_data)
{
	struct qmi_qmux_device *qmux = user_data;

	if (qmux->shutdown.destroy)
		qmux->shutdown.destroy(qmux->shutdown.user_data);

	qmux->shutdown.idle = NULL;

	if (qmux->destroyed)
		__qmux_device_free(qmux);
}

static void qmux_shutdown_callback(struct l_idle *idle, void *user_data)
{
	struct qmi_qmux_device *qmux = user_data;

	if (qmux->shutdown.release_users > 0)
		return;

	qmux->shutting_down = true;

	if (qmux->shutdown.func)
		qmux->shutdown.func(qmux->shutdown.user_data);

	qmux->shutting_down = false;

	l_idle_remove(qmux->shutdown.idle);
}

int qmi_qmux_device_shutdown(struct qmi_qmux_device *qmux,
					qmi_qmux_device_shutdown_func_t func,
					void *user_data,
					qmi_destroy_func_t destroy)
{
	if (!qmux)
		return -EINVAL;

	if (qmux->shutdown.idle)
		return -EALREADY;

	DEBUG(&qmux->debug, "device %p", &qmux);

	qmux->shutdown.idle = l_idle_create(qmux_shutdown_callback, qmux,
						qmux_shutdown_destroy);

	if (!qmux->shutdown.idle)
		return -EIO;

	qmux->shutdown.func = func;
	qmux->shutdown.user_data = user_data;
	qmux->shutdown.destroy = destroy;

	return 0;
}

static const struct qmi_transport_ops qmux_ops = {
	.write = qmi_qmux_device_write,
};

struct qmi_qmux_device *qmi_qmux_device_new(const char *device)
{
	struct qmi_qmux_device *qmux;
	int fd;

	fd = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return NULL;

	qmux = l_new(struct qmi_qmux_device, 1);

	if (qmi_transport_open(&qmux->transport, fd, &qmux_ops) < 0) {
		close(fd);
		l_free(qmux);
		return NULL;
	}

	qmux->next_control_tid = 1;
	qmux->control_queue = l_queue_new();
	l_io_set_read_handler(qmux->transport.io,
				received_qmux_data, qmux, NULL);

	return qmux;
}

void qmi_qmux_device_free(struct qmi_qmux_device *qmux)
{
	if (!qmux)
		return;

	DEBUG(&qmux->debug, "device %p", qmux);
	qmi_transport_close(&qmux->transport);

	if (qmux->shutting_down) {
		qmux->destroyed = true;
		return;
	}

	__qmux_device_free(qmux);
}

void qmi_qmux_device_set_debug(struct qmi_qmux_device *qmux,
				qmi_debug_func_t func, void *user_data)
{
	if (!qmux)
		return;

	__debug_data_init(&qmux->debug, func, user_data);
}

void qmi_qmux_device_set_io_debug(struct qmi_qmux_device *qmux,
				qmi_debug_func_t func, void *user_data)
{
	if (!qmux)
		return;

	__debug_data_init(&qmux->transport.debug, func, user_data);
}

struct qmi_qrtr_node {
	unsigned int next_group_id;	/* Matches requests with services */
	struct l_queue *service_infos;
	struct debug_data debug;
	struct {
		qmi_qrtr_node_lookup_done_func_t func;
		void *user_data;
		qmi_destroy_func_t destroy;
		struct l_timeout *timeout;
	} lookup;
	struct qmi_transport transport;
};

static const struct qmi_service_info *__qrtr_service_info_find(
				struct qmi_qrtr_node *node, uint16_t type)
{
	const struct l_queue_entry *entry;

	for (entry = l_queue_get_entries(node->service_infos);
						entry; entry = entry->next) {
		struct qmi_service_info *info = entry->data;

		if (info->service_type == type)
			return info;
	}

	return NULL;
}

static void __qrtr_service_appeared(struct qmi_qrtr_node *node,
					const struct qmi_service_info *info)
{
	if (l_queue_find(node->service_infos, qmi_service_info_matches, info))
		return;

	l_queue_push_tail(node->service_infos,
				l_memdup(info, sizeof(struct qmi_service_info)));
}

static void __qrtr_lookup_finished(struct qmi_qrtr_node *node)
{
	if (!node->lookup.func) {
		DEBUG(&node->debug, "No lookup in progress");
		return;
	}

	l_timeout_remove(node->lookup.timeout);
	node->lookup.func(node->lookup.user_data);

	if (node->lookup.destroy)
		node->lookup.destroy(node->lookup.user_data);

	memset(&node->lookup, 0, sizeof(node->lookup));
}

static void qrtr_service_family_free(struct service_family *family)
{
	l_free(family);
}

static struct service_family *qrtr_service_family_new(
					struct qmi_transport *transport,
					unsigned int group_id,
					const struct qmi_service_info *info,
					uint8_t client_id)
{
	struct service_family *family = l_new(struct service_family, 1);

	__service_family_init(family, transport, group_id, info, client_id);
	family->free_family = qrtr_service_family_free;

	return family;
}

static int qmi_qrtr_node_write(struct qmi_transport *transport,
					struct qmi_request *req)
{
	struct sockaddr_qrtr addr;
	uint8_t *data;
	uint16_t len;
	ssize_t bytes_written;
	int fd = l_io_get_fd(transport->io);

	/* Skip the QMUX header */
	data = req->data + QMI_MUX_HDR_SIZE;
	len = req->len - QMI_MUX_HDR_SIZE;

	memset(&addr, 0, sizeof(addr));	/* Ensures internal padding is 0 */
	addr.sq_family = AF_QIPCRTR;
	addr.sq_node = req->info.qrtr_node;
	addr.sq_port = req->info.qrtr_port;

	bytes_written = sendto(fd, data, len, 0, (struct sockaddr *) &addr,
							sizeof(addr));
	if (bytes_written < 0) {
		DEBUG(&transport->debug, "sendto: %s", strerror(errno));
		return -errno;
	}

	l_util_hexdump(false, data, bytes_written,
			transport->debug.func, transport->debug.user_data);

	__qrtr_debug_msg(' ', data, bytes_written,
			req->info.service_type, &transport->debug);

	l_queue_push_tail(transport->service_queue, req);

	return 0;
}

static void qrtr_debug_ctrl_request(const struct qrtr_ctrl_pkt *packet,
					const struct debug_data *debug)
{
	char strbuf[72 + 16], *str;
	const char *type;

	if (!debug->func)
		return;

	str = strbuf;
	str += sprintf(str, "    %s",
			__service_type_to_string(QMI_SERVICE_CONTROL));

	type = "_pkt";

	str += sprintf(str, "%s cmd=%d", type,
				L_LE32_TO_CPU(packet->cmd));

	debug->func(strbuf, debug->user_data);
}

static void qrtr_received_control_packet(struct qmi_qrtr_node *node,
						const void *buf, size_t len)
{
	const struct qrtr_ctrl_pkt *packet = buf;
	struct qmi_service_info info;
	uint32_t cmd;
	uint32_t type;
	uint32_t instance;
	uint32_t version;
	uint32_t qrtr_node;
	uint32_t qrtr_port;

	if (len < sizeof(*packet)) {
		DEBUG(&node->debug, "packet is too small");
		return;
	}

	qrtr_debug_ctrl_request(packet, &node->debug);

	cmd = L_LE32_TO_CPU(packet->cmd);
	if (cmd != QRTR_TYPE_NEW_SERVER) {
		DEBUG(&node->debug, "Unknown command: %d", cmd);
		return;
	}

	if (!packet->server.service && !packet->server.instance &&
			!packet->server.node && !packet->server.port) {
		DEBUG(&node->debug, "Service lookup complete");
		__qrtr_lookup_finished(node);
		return;
	}

	type = L_LE32_TO_CPU(packet->server.service);
	version = L_LE32_TO_CPU(packet->server.instance) & 0xff;
	instance = L_LE32_TO_CPU(packet->server.instance) >> 8;

	qrtr_node = L_LE32_TO_CPU(packet->server.node);
	qrtr_port = L_LE32_TO_CPU(packet->server.port);

	DEBUG(&node->debug, "New server: %d Version: %d Node/Port: %d/%d",
			type, version, qrtr_node, qrtr_port);

	memset(&info, 0, sizeof(info));
	info.service_type = type;
	info.qrtr_port = qrtr_port;
	info.qrtr_node = qrtr_node;
	info.major = version;
	info.instance = instance;

	__qrtr_service_appeared(node, &info);

	if (!node->lookup.func)
		return;

	l_timeout_modify(node->lookup.timeout, DISCOVER_TIMEOUT);
}

static void qrtr_received_service_message(struct qmi_qrtr_node *node,
						uint32_t qrtr_node,
						uint32_t qrtr_port,
						const void *buf, size_t len)
{
	struct qmi_transport *transport = &node->transport;
	const struct l_queue_entry *entry;
	uint32_t service_type = 0;

	for (entry = l_queue_get_entries(node->service_infos);
				entry; entry = entry->next) {
		struct qmi_service_info *info = entry->data;

		if (info->qrtr_node == qrtr_node &&
				info->qrtr_port == qrtr_port) {
			service_type = info->service_type;
			break;
		}
	}

	if (!service_type) {
		DEBUG(&node->debug, "Message from unknown at node/port %d/%d",
				qrtr_node, qrtr_port);
		return;
	}

	__qrtr_debug_msg(' ', buf, len, service_type, &transport->debug);
	__rx_message(transport, service_type, 0, buf);
}

static bool qrtr_received_data(struct l_io *io, void *user_data)
{
	struct qmi_qrtr_node *qrtr = user_data;
	struct debug_data *debug = &qrtr->transport.debug;
	struct sockaddr_qrtr addr;
	unsigned char buf[2048];
	ssize_t bytes_read;
	socklen_t addr_size;
	int fd = l_io_get_fd(qrtr->transport.io);

	addr_size = sizeof(addr);
	bytes_read = recvfrom(fd, buf, sizeof(buf), 0,
				(struct sockaddr *) &addr, &addr_size);
	DEBUG(debug, "fd %d Received %zd bytes from Node: %d Port: %d",
			fd, bytes_read, addr.sq_node, addr.sq_port);

	if (bytes_read < 0)
		return true;

	l_util_hexdump(true, buf, bytes_read, debug->func, debug->user_data);

	if (addr.sq_port == QRTR_PORT_CTRL)
		qrtr_received_control_packet(qrtr, buf, bytes_read);
	else
		qrtr_received_service_message(qrtr, addr.sq_node,
						addr.sq_port, buf, bytes_read);

	return true;
}

static const struct qmi_transport_ops qrtr_ops = {
	.write = qmi_qrtr_node_write,
};

struct qmi_qrtr_node *qmi_qrtr_node_new(uint32_t node)
{
	struct qmi_qrtr_node *qrtr;
	int fd;

	fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (fd < 0)
		return NULL;

	qrtr = l_new(struct qmi_qrtr_node, 1);

	if (qmi_transport_open(&qrtr->transport, fd, &qrtr_ops) < 0) {
		close(fd);
		l_free(qrtr);
		return NULL;
	}

	qrtr->service_infos = l_queue_new();
	l_io_set_read_handler(qrtr->transport.io,
					qrtr_received_data, qrtr, NULL);

	return qrtr;
}

void qmi_qrtr_node_free(struct qmi_qrtr_node *node)
{
	if (!node)
		return;

	qmi_transport_close(&node->transport);
	l_queue_destroy(node->service_infos, l_free);

	if (node->lookup.destroy)
		node->lookup.destroy(node->lookup.user_data);

	l_free(node);
}

void qmi_qrtr_node_set_debug(struct qmi_qrtr_node *node,
				qmi_debug_func_t func, void *user_data)
{
	if (!node)
		return;

	__debug_data_init(&node->debug, func, user_data);
}

void qmi_qrtr_node_set_io_debug(struct qmi_qrtr_node *node,
				qmi_debug_func_t func, void *user_data)
{
	if (!node)
		return;

	__debug_data_init(&node->transport.debug, func, user_data);
}

static void qrtr_lookup_reply_timeout(struct l_timeout *timeout,
							void *user_data)
{
	struct qmi_qrtr_node *node = user_data;

	__qrtr_lookup_finished(node);
}

int qmi_qrtr_node_lookup(struct qmi_qrtr_node *node,
			qmi_qrtr_node_lookup_done_func_t func,
			void *user_data, qmi_destroy_func_t destroy)
{
	struct qrtr_ctrl_pkt packet;
	struct sockaddr_qrtr addr;
	socklen_t addr_len;
	ssize_t bytes_written;
	int fd;
	struct debug_data *debug = &node->transport.debug;

	if (!node || !func)
		return -EINVAL;

	if (node->lookup.func)
		return -EALREADY;

	DEBUG(&node->debug, "node %p", node);

	fd = l_io_get_fd(node->transport.io);

	/*
	 * The control node is configured by the system. Use getsockname to
	 * get its value.
	 */
	addr_len = sizeof(addr);
	if (getsockname(fd, (struct sockaddr *) &addr, &addr_len) < 0) {
		DEBUG(debug, "getsockname failed: %s", strerror(errno));
		return -errno;
	}

	if (addr.sq_family != AF_QIPCRTR || addr_len != sizeof(addr)) {
		DEBUG(debug, "Unexpected sockaddr family: %d size: %d",
				addr.sq_family, addr_len);
		return -EIO;
	}

	addr.sq_port = QRTR_PORT_CTRL;
	memset(&packet, 0, sizeof(packet));
	packet.cmd = L_CPU_TO_LE32(QRTR_TYPE_NEW_LOOKUP);

	bytes_written = sendto(fd, &packet,
				sizeof(packet), 0,
				(struct sockaddr *) &addr, addr_len);
	if (bytes_written < 0) {
		DEBUG(debug, "sendto failed: %s", strerror(errno));
		return -errno;
	}

	l_util_hexdump(false, &packet, bytes_written,
			debug->func, debug->user_data);

	node->lookup.func = func;
	node->lookup.user_data = user_data;
	node->lookup.destroy = destroy;
	node->lookup.timeout = l_timeout_create(DISCOVER_TIMEOUT,
						qrtr_lookup_reply_timeout,
						node, NULL);

	return 0;
}

struct qmi_service *qmi_qrtr_node_get_service(struct qmi_qrtr_node *node,
						uint32_t type)
{
	struct qmi_transport *transport;
	struct service_family *family;
	const struct qmi_service_info *info;

	if (!node)
		return NULL;

	if (type == QMI_SERVICE_CONTROL)
		return NULL;

	transport = &node->transport;

	family = l_hashmap_lookup(transport->family_list, L_UINT_TO_PTR(type));
	if (family)
		goto done;

	info = __qrtr_service_info_find(node, type);
	if (!info)
		return NULL;

	family = qrtr_service_family_new(transport,
						next_id(&node->next_group_id),
						info, 0);
	l_hashmap_insert(transport->family_list, L_UINT_TO_PTR(type), family);
done:
	return service_create(family);
}

bool qmi_qrtr_node_has_service(struct qmi_qrtr_node *node, uint16_t type)
{
	if (!node)
		return false;

	return __qrtr_service_info_find(node, type);
}

struct qrtr_service_family_dedicated {
	struct service_family super;
	struct qmi_transport transport;
};

static void qrtr_service_family_dedicated_free(struct service_family *family)
{
	struct qrtr_service_family_dedicated *dfamily =
		l_container_of(family,
				struct qrtr_service_family_dedicated, super);

	qmi_transport_close(&dfamily->transport);
	l_free(dfamily);
}

static bool qrtr_service_family_dedicated_rx(struct l_io *io, void *user_data)
{
	struct qrtr_service_family_dedicated *family = user_data;
	struct debug_data *debug = &family->transport.debug;
	const struct qmi_service_info *info = &family->super.info;
	struct sockaddr_qrtr addr;
	unsigned char buf[2048];
	ssize_t bytes_read;
	socklen_t addr_size;
	int fd = l_io_get_fd(family->transport.io);

	addr_size = sizeof(addr);
	bytes_read = recvfrom(fd, buf, sizeof(buf), 0,
				(struct sockaddr *) &addr, &addr_size);
	DEBUG(debug, "fd %d Received %zd bytes from Node: %d Port: %d",
			fd, bytes_read, addr.sq_node, addr.sq_port);

	if (bytes_read < 0)
		return true;

	if (addr.sq_port != info->qrtr_port && addr.sq_node != info->qrtr_node)
		return true;

	l_util_hexdump(true, buf, bytes_read, debug->func, debug->user_data);
	__qrtr_debug_msg(' ', buf, bytes_read, info->service_type, debug);
	__rx_message(&family->transport, info->service_type, 0, buf);

	return true;
}

struct qmi_service *qmi_qrtr_node_get_dedicated_service(
						struct qmi_qrtr_node *node,
						uint16_t type)
{
	struct qrtr_service_family_dedicated *family;
	const struct qmi_service_info *info;
	int fd;

	if (!node)
		return NULL;

	if (type == QMI_SERVICE_CONTROL)
		return NULL;

	info = __qrtr_service_info_find(node, type);
	if (!info)
		return NULL;

	fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (fd < 0)
		return NULL;

	family = l_new(struct qrtr_service_family_dedicated, 1);

	if (qmi_transport_open(&family->transport, fd, &qrtr_ops) < 0) {
		close(fd);
		l_free(family);
		return NULL;
	}

	DEBUG(&node->debug, "Opening dedicated service for %u", type);
	__service_family_init(&family->super, &family->transport,
				next_id(&node->next_group_id), info, 0);
	family->super.free_family = qrtr_service_family_dedicated_free;
	l_hashmap_insert(family->transport.family_list,
					L_UINT_TO_PTR(type), family);

	l_io_set_read_handler(family->transport.io,
					qrtr_service_family_dedicated_rx,
					family, NULL);
	memcpy(&family->transport.debug, &node->transport.debug,
						sizeof(struct debug_data));

	return service_create(&family->super);
}

struct qmi_param *qmi_param_new(void)
{
	return l_new(struct qmi_param, 1);
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

const char *qmi_service_get_identifier(struct qmi_service *service)
{
	if (!service)
		return NULL;

	return __service_type_to_string(service->family->info.service_type);
}

/**
 * qmi_service_get_version:
 * @service: lightweight service handle
 * @out_version: version output
 *
 * Returns the version of the service this handle is currently referring to.
 * On QMUX this corresponds to the 'major' version of the service.  On QRTR,
 * this corresponds to the lower 8 bits of the 'instance' attribute and is thus
 * limited to uint8_t.
 *
 * Returns: #false if the service handle is NULL, #true on success.
 */
bool qmi_service_get_version(struct qmi_service *service, uint8_t *out_version)
{
	if (!service)
		return false;

	if (out_version)
		*out_version = service->family->info.major;

	return true;
}

static void qmi_service_request_free(struct qmi_request *req)
{
	struct qmi_service_request *sreq = l_container_of(req,
					struct qmi_service_request, super);

	if (sreq->destroy)
		sreq->destroy(sreq->user_data);

	l_free(sreq);
}

static void service_send_callback(struct qmi_request *req, uint16_t message,
					uint16_t length, const void *buffer)
{
	struct qmi_service_request *sreq = l_container_of(req,
					struct qmi_service_request, super);
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
	if (sreq->func)
		sreq->func(&result, sreq->user_data);
}

uint16_t qmi_service_send(struct qmi_service *service,
				uint16_t message, struct qmi_param *param,
				qmi_service_result_func_t func,
				void *user_data, qmi_destroy_func_t destroy)
{
	struct service_family *family;
	struct qmi_transport *transport;
	struct qmi_service_request *sreq;
	struct qmi_service_info *info;

	if (!service)
		return 0;

	family = service->family;
	if (!family->group_id)
		return 0;

	transport = family->transport;
	if (!transport)
		return 0;

	info = &family->info;
	sreq = __request_alloc(info->service_type, family->client_id, message,
				param ? param->data : NULL,
				param ? param->length : 0,
				offsetof(struct qmi_service_request, super));
	qmi_param_free(param);

	memcpy(&sreq->super.info, info, sizeof(*info));
	sreq->super.callback = service_send_callback;
	sreq->super.free_request = qmi_service_request_free;
	sreq->func = func;
	sreq->user_data = user_data;
	sreq->destroy = destroy;

	return __service_request_submit(transport, service, &sreq->super);
}

struct request_lookup {
	uint16_t tid;
	unsigned int service_handle;
	unsigned int group_id;
};

static bool __request_compare_for_cancel(const void *a, const void *b)
{
	const struct qmi_request *req = a;
	const struct request_lookup *lookup = b;

	if (req->service_handle != lookup->service_handle)
		return false;

	if (req->group_id != lookup->group_id)
		return false;

	return req->tid == lookup->tid;
}

bool qmi_service_cancel(struct qmi_service *service, uint16_t id)
{
	struct qmi_transport *transport;
	struct qmi_request *req;
	struct service_family *family;
	struct request_lookup lookup = { .tid = id };

	if (!service || !id)
		return false;

	family = service->family;

	if (!family->client_id)
		return false;

	transport = family->transport;
	if (!transport)
		return false;

	lookup.group_id = family->group_id;
	lookup.service_handle = service->handle;

	req = l_queue_remove_if(transport->req_queue,
				__request_compare_for_cancel, &lookup);
	if (!req) {
		req = l_queue_remove_if(transport->service_queue,
						__request_compare_for_cancel,
						&lookup);
		if (!req)
			return false;
	}

	__request_free(req);

	return true;
}

static bool remove_req_if_match(void *data, void *user_data)
{
	struct qmi_request *req = data;
	struct request_lookup *lookup = user_data;

	if (req->service_handle != lookup->service_handle)
		return false;

	if (req->group_id != lookup->group_id)
		return false;

	__request_free(req);

	return true;
}

static void remove_client(struct l_queue *queue, unsigned int group_id,
						unsigned int service_handle)
{
	struct request_lookup lookup = {
		.group_id = group_id,
		.service_handle = service_handle,
	};

	l_queue_foreach_remove(queue, remove_req_if_match, &lookup);
}

static bool qmi_service_cancel_all(struct qmi_service *service)
{
	struct qmi_transport *transport;

	if (!service)
		return false;

	if (!service->family->group_id)
		return false;

	transport = service->family->transport;
	if (!transport)
		return false;

	remove_client(transport->req_queue,
				service->family->group_id, service->handle);
	remove_client(transport->service_queue,
				service->family->group_id, service->handle);

	return true;
}

uint16_t qmi_service_register(struct qmi_service *service,
				uint16_t message, qmi_service_result_func_t func,
				void *user_data, qmi_destroy_func_t destroy)
{
	struct qmi_notify *notify;
	struct service_family *family;

	if (!service || !func)
		return 0;

	family = service->family;

	notify = l_new(struct qmi_notify, 1);

	if (family->next_notify_id < 1)
		family->next_notify_id = 1;

	notify->id = family->next_notify_id++;
	notify->message = message;
	notify->service_handle = service->handle;
	notify->callback = func;
	notify->user_data = user_data;
	notify->destroy = destroy;

	l_queue_push_tail(family->notify_list, notify);

	return notify->id;
}

bool qmi_service_unregister(struct qmi_service *service, uint16_t id)
{
	struct qmi_notify *notify;
	struct notify_compare_details details;

	if (!service || !id)
		return false;

	details.id = id;
	details.service_handle = service->handle;

	notify = l_queue_remove_if(service->family->notify_list,
						__notify_compare, &details);

	if (!notify)
		return false;

	__notify_free(notify);

	return true;
}

static bool remove_notify_if_handle_match(void *data, void *user_data)
{
	struct qmi_notify *notify = data;
	unsigned int handle = L_PTR_TO_UINT(user_data);

	if (notify->service_handle != handle)
		return false;

	__notify_free(notify);

	return true;
}

static bool qmi_service_unregister_all(struct qmi_service *service)
{
	if (!service)
		return false;

	l_queue_foreach_remove(service->family->notify_list,
					remove_notify_if_handle_match,
					L_UINT_TO_PTR(service->handle));

	return true;
}

struct qmi_service *qmi_service_clone(struct qmi_service *service)
{
	if (!service)
		return NULL;

	return service_create(service->family);
}

void qmi_service_free(struct qmi_service *service)
{
	if (!service)
		return;

	qmi_service_cancel_all(service);
	qmi_service_unregister_all(service);

	service_family_unref(service->family);

	l_free(service);
}
