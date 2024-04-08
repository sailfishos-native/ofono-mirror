/*
 *  oFono - Open Source Telephony
 *  Copyright (C) 2024  Cruise, LLC
 *
 *  SPDX-License-Identifier: GPL-2.0-only
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "drivers/qmimodem/qmi.h"
#include <src/ofono.h>

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <ell/ell.h>

#include <sys/socket.h>
#include <linux/qrtr.h>

#define TEST_SERVICE_COUNT	2
#define TEST_TIMEOUT		5

struct test_info {
	int service_fds[TEST_SERVICE_COUNT];
	struct qmi_device *device;
	struct l_timeout *timeout;
	struct l_queue *services;

	/* Data sent to our test service */
	struct sockaddr_qrtr sender;
	size_t received_len;
	void *received;

	bool discovery_callback_called		: 1;
	bool service_send_callback_called	: 1;
};

static uint32_t unique_service_type(uint32_t index)
{
	/* Try to use a value that will not conflict with any real services. */
	return index + 10000;
}

static uint32_t unique_service_version(uint32_t index)
{
	return index + 10;
}

static uint32_t unique_service_instance(uint32_t index)
{
	return index + 20;
}

static int create_service(int i)
{
	int fd;
	struct sockaddr_qrtr addr;
	socklen_t addrlen;
	struct qrtr_ctrl_pkt packet;
	ssize_t bytes_sent;

	fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (fd == -1) {
		DBG("Could not create AF_QIPCRTR socket: %s", strerror(errno));
		return -errno;
	}

	/*
	 * The control node is configured by the system. Use getsockname to
	 * get its node.
	 */
	addrlen = sizeof(addr);
	if (getsockname(fd, (struct sockaddr *) &addr, &addrlen) == -1) {
		DBG("getsockname failed: %s", strerror(errno));
		goto error;
	}

	if (addr.sq_family != AF_QIPCRTR || addrlen != sizeof(addr)) {
		DBG("Unexpected sockaddr from getsockname. family: %d size: %d",
						addr.sq_family, addrlen);
		goto error;
	}

	/* Node and port are assigned automatically so no need to set them */
	memset(&packet, 0, sizeof(packet));
	packet.cmd = L_CPU_TO_LE32(QRTR_TYPE_NEW_SERVER);
	packet.server.service = L_CPU_TO_LE32(unique_service_type(i));
	packet.server.instance = L_CPU_TO_LE32(
					unique_service_instance(i) << 8 |
					unique_service_version(i));

	bytes_sent = sendto(fd, &packet, sizeof(packet), 0,
				(struct sockaddr *) &addr, addrlen);
	if (bytes_sent != sizeof(packet)) {
		DBG("sendto to set up the qrtr service failed: %s",
						strerror(errno));
		goto error;
	}

	return fd;

error:
	close(fd);

	return -errno;
}

static void setup_test_qrtr_services(struct test_info *info)
{
	int i;

	for (i = 0; i < TEST_SERVICE_COUNT; ++i) {
		info->service_fds[i] = create_service(i);
		assert(info->service_fds[i] >= 0);
	}
}

static void debug_log(const char *str, void *user_data)
{
	printf("%s\n", str);
}

static void test_timeout_cb(struct l_timeout *timeout, void *user_data)
{
	DBG("Test timed out!");
	assert(false);
}

static struct test_info *test_setup(void)
{
	struct test_info *info;

	l_main_init();

	info = l_new(struct test_info, 1);
	setup_test_qrtr_services(info);
	info->device = qmi_device_new_qrtr();
	assert(info->device);

	/* Enable ofono logging */
	qmi_device_set_debug(info->device, debug_log, NULL);

	info->services = l_queue_new();
	info->timeout = l_timeout_create(TEST_TIMEOUT, test_timeout_cb, info,
								NULL);

	return info;
}

static void test_cleanup(struct test_info *info)
{
	int i;

	l_free(info->received);
	l_timeout_remove(info->timeout);
	l_queue_destroy(info->services,
				(l_queue_destroy_func_t) qmi_service_unref);
	qmi_device_free(info->device);

	/* The qrtr services will be destroyed automatically. */
	for (i = 0; i < TEST_SERVICE_COUNT; ++i)
		close(info->service_fds[i]);

	l_free(info);

	l_main_exit();
}

static void test_create_qrtr_device(const void *data)
{
	struct test_info *info = test_setup();

	test_cleanup(info);
}

static void discovery_complete_cb(void *user_data)
{
	struct test_info *info = user_data;

	info->discovery_callback_called = true;
}

static void perform_discovery(struct test_info *info)
{
	qmi_device_discover(info->device, discovery_complete_cb, info, NULL);

	while (!info->discovery_callback_called)
		l_main_iterate(-1);
}

static void test_discovery(const void *data)
{
	struct test_info *info = test_setup();

	perform_discovery(info);

	test_cleanup(info);
}

static void create_service_cb(struct qmi_service *service, void *user_data)
{
	struct test_info *info = user_data;

	service = qmi_service_ref(service);
	l_queue_push_tail(info->services, service);
}

/* Callbacks could queue other callbacks so continue until there are no more. */
static void perform_all_pending_work(void)
{
	l_main_iterate(0);

	while (l_main_prepare() != -1)
		l_main_iterate(0);
}

static void test_create_services(const void *data)
{
	struct test_info *info = test_setup();
	uint32_t service_type;
	int i;

	perform_discovery(info);

	for (i = 0; i < TEST_SERVICE_COUNT; i++) {
		struct qmi_service *service;
		uint16_t major, minor;

		service_type = unique_service_type(i);
		assert(qmi_service_create(info->device, service_type,
						create_service_cb, info, NULL));
		perform_all_pending_work();

		assert(l_queue_length(info->services) == 1);
		service = l_queue_pop_head(info->services);
		assert(service);

		assert(qmi_service_get_version(service, &major, &minor));
		assert(major == unique_service_version(i));
		assert(minor == 0);

		qmi_service_unref(service);
	}

	/*
	 * Confirm that an unknown service cannot be created and does not
	 * call the callback.
	 */
	service_type = unique_service_type(TEST_SERVICE_COUNT);
	assert(!qmi_service_create(info->device, service_type,
					create_service_cb, info, NULL));
	perform_all_pending_work();
	assert(l_queue_isempty(info->services));

	test_cleanup(info);
}

static bool received_data(struct l_io *io, void *user_data)
{
	struct test_info *info = user_data;
	struct sockaddr_qrtr addr;
	unsigned char buf[2048];
	ssize_t bytes_read;
	socklen_t addr_size;

	addr_size = sizeof(addr);
	bytes_read = recvfrom(l_io_get_fd(io), buf, sizeof(buf), 0,
				(struct sockaddr *) &addr, &addr_size);

	/* Ignore control messages */
	if (addr.sq_port == QRTR_PORT_CTRL)
		return true;

	memcpy(&info->sender, &addr, sizeof(addr));

	assert(!info->received); /* Only expect one message */
	info->received_len = bytes_read;
	info->received = l_memdup(buf, bytes_read);

	return true;
}

#define TEST_TLV_TYPE		21	/* Its data value is 1 byte */
#define TEST_DATA_VALUE		0x89

static void send_test_data_cb(struct qmi_result *result, void *user_data)
{
	struct test_info *info = user_data;

	uint8_t data;

	assert(!qmi_result_set_error(result, NULL));
	assert(qmi_result_get_uint8(result, TEST_TLV_TYPE, &data));
	assert(data == TEST_DATA_VALUE);

	info->service_send_callback_called = true;
}

/*
 * We know exactly how the qmi data should be packed so we can hard code the
 * structure layout to simplify the tests.
 */

#define TEST_REQ_MESSAGE_ID	42
#define TEST_RESP_MESSAGE_ID	43
#define QMI_HDR_SIZE		7

struct qmi_test_service_request {
	uint8_t  type;
	uint16_t transaction;
	uint16_t message;
	uint16_t length;	/* Message size without header */
	uint8_t  data_type;
	uint16_t data_length;
	uint8_t  data_value;
} __attribute__ ((packed));

struct qmi_test_service_response {
	uint8_t  type;
	uint16_t transaction;
	uint16_t message;
	uint16_t length;	/* Message size without header */
	uint8_t  error_type;
	uint16_t error_length;
	uint16_t error_result;
	uint16_t error_error;
	uint8_t  data_type;
	uint16_t data_length;
	uint8_t  data_value;
} __attribute__ ((packed));

static void send_request_via_qmi(struct test_info *info,
						struct qmi_service *service)
{
	struct qmi_param *param;

	param = qmi_param_new();
	qmi_param_append_uint8(param, TEST_TLV_TYPE, TEST_DATA_VALUE);
	assert(qmi_service_send(service, TEST_REQ_MESSAGE_ID, param,
					send_test_data_cb, info, NULL));

	while (!info->received)
		l_main_iterate(-1);
}

static void send_response_via_socket(struct test_info *info, struct l_io *io)
{
	const struct qmi_test_service_request *request;
	struct qmi_test_service_response response;

	/* First validate that the qrtr code sent the qmi request properly. */
	assert(info->received_len == sizeof(*request));
	request = info->received;
	assert(request->type == 0x00);
	assert(request->message == L_CPU_TO_LE16(TEST_REQ_MESSAGE_ID));
	assert(request->length == L_CPU_TO_LE16(
					sizeof(*request) - QMI_HDR_SIZE));
	assert(request->data_type == TEST_TLV_TYPE);
	assert(request->data_length == L_CPU_TO_LE16(1));
	assert(request->data_value == TEST_DATA_VALUE);

	/*
	 * Now echo it back to the qrtr client. The qmi_service send callback
	 * will validate that the client processed this response correctly.
	 */
	memset(&response, 0, sizeof(response));
	response.type = 0x02;
	response.transaction = request->transaction;
	response.message = L_CPU_TO_LE16(TEST_RESP_MESSAGE_ID);
	response.length = L_CPU_TO_LE16(sizeof(response) - QMI_HDR_SIZE);
	response.error_type = 2;
	response.error_length = L_CPU_TO_LE16(4);
	response.data_type = request->data_type;
	response.data_length = request->data_length;
	response.data_value = request->data_value;

	sendto(l_io_get_fd(io), &response, sizeof(response), 0,
					(struct sockaddr *) &info->sender,
					sizeof(info->sender));

	while (!info->service_send_callback_called)
		l_main_iterate(-1);
}

/*
 * Initiates a send of the TLV data payload to the test service. The test
 * service will respond with the same data payload.
 */
static void test_send_data(const void *data)
{
	struct test_info *info = test_setup();
	struct l_io *io;
	uint32_t service_type;
	struct qmi_service *service;

	perform_discovery(info);

	service_type = unique_service_type(0); /* Use the first service */
	assert(qmi_service_create(info->device, service_type,
					create_service_cb, info, NULL));
	perform_all_pending_work();
	service = l_queue_pop_head(info->services);
	assert(service);

	io = l_io_new(info->service_fds[0]);
	assert(io);
	l_io_set_read_handler(io, received_data, info, NULL);

	send_request_via_qmi(info, service);
	send_response_via_socket(info, io);

	l_io_destroy(io);
	qmi_service_unref(service);

	test_cleanup(info);
}

static void exit_if_qrtr_not_supported(void)
{
	int fd;

	fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (fd == -1) {
		printf("Could not create AF_QIPCRTR socket: %s\n",
					strerror(errno));
		printf("Skipping tests...\n");
		exit(0);
	}

	close(fd);
}

int main(int argc, char **argv)
{
	int result;

	exit_if_qrtr_not_supported();

	/* Enable all DBG logging */
	__ofono_log_init(argv[0], "*", FALSE);

	l_test_init(&argc, &argv);
	l_test_add("QRTR device creation", test_create_qrtr_device, NULL);
	l_test_add("QRTR discovery", test_discovery, NULL);
	l_test_add("QRTR services may be created", test_create_services, NULL);
	l_test_add("QRTR service sends/responses", test_send_data, NULL);
	result = l_test_run();

	__ofono_log_cleanup();

	return result;
}