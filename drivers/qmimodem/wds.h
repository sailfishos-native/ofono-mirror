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

/* Start WDS network interface */
#define QMI_WDS_PARAM_PROFILE_TYPE		0x01
#define QMI_WDS_PARAM_APN			0x14	/* string */
#define QMI_WDS_PARAM_IP_FAMILY			0x19	/* uint8 */
#define QMI_WDS_PARAM_USERNAME			0x17	/* string */
#define QMI_WDS_PARAM_PASSWORD			0x18	/* string */
#define QMI_WDS_PARAM_AUTHENTICATION_PREFERENCE	0x16	/* uint8 */

enum qmi_wds_authentication {
	QMI_WDS_AUTHENTICATION_PAP	= 0x1,
	QMI_WDS_AUTHENTICATION_CHAP	= 0x2,
};

enum qmi_wds_connection_status {
	QMI_WDS_CONNECTION_STATUS_DISCONNECTED =	0x01,
	QMI_WDS_CONNECTION_STATUS_CONNECTED =		0x02,
	QMI_WDS_CONNECTION_STATUS_SUSPENDED =		0x03,
	QMI_WDS_CONNECTION_STATUS_AUTHENTICATING =	0x04,
};

enum qmi_wds_pdp_type {
	QMI_WDS_PDP_TYPE_IPV4		= 0x00,
	QMI_WDS_PDP_TYPE_PPP		= 0x01,
	QMI_WDS_PDP_TYPE_IPV6		= 0x02,
	QMI_WDS_PDP_TYPE_IPV4V6		= 0x03,
};

enum qmi_wds_ip_family {
	QMI_WDS_IP_FAMILY_UNKNOWN = 0,
	QMI_WDS_IP_FAMILY_IPV4 = 4,
	QMI_WDS_IP_FAMILY_IPV6 = 6,
	QMI_WDS_IP_FAMILY_UNSPECIFIED = 8,
};

enum qmi_wds_client_type {
	QMI_WDS_CLIENT_TYPE_TETHERED = 0x01,
};

enum qmi_wds_profile_type {
	QMI_WDS_PROFILE_TYPE_3GPP =	0x00,
	QMI_WDS_PROFILE_TYPE_3GPP2 =	0x01,
	QMI_WDS_PROFILE_TYPE_EPC =	0x02,
};

enum qmi_wds_profile_family {
	QMI_WDS_PROFILE_FAMILY_EMBEDDED =	0x00,
	QMI_WDS_PROFILE_FAMILY_TETHERED =	0x01,
};

enum qmi_wds_command {
	QMI_WDS_RESET					= 0x00,
	QMI_WDS_EVENT_REPORT				= 0x01,
	QMI_WDS_ABORT					= 0x02,
	QMI_WDS_INDICATION_REGISTER			= 0x03,
	QMI_WDS_GET_SUPPORTED_MESSAGES			= 0x19,
	QMI_WDS_START_NETWORK				= 0x20,
	QMI_WDS_STOP_NETWORK				= 0x21,
	QMI_WDS_PACKET_SERVICE_STATUS			= 0x22,
	QMI_WDS_GET_CHANNEL_RATES			= 0x23,
	QMI_WDS_GET_PACKET_STATISTICS			= 0x24,
	QMI_WDS_GO_DORMANT				= 0x25,
	QMI_WDS_GO_ACTIVE				= 0x26,
	QMI_WDS_CREATE_PROFILE				= 0x27,
	QMI_WDS_MODIFY_PROFILE				= 0x28,
	QMI_WDS_DELETE_PROFILE				= 0x29,
	QMI_WDS_GET_PROFILE_LIST			= 0x2A,
	QMI_WDS_GET_PROFILE_SETTINGS			= 0x2B,
	QMI_WDS_GET_DEFAULT_SETTINGS			= 0x2C,
	QMI_WDS_GET_CURRENT_SETTINGS			= 0x2D,
	QMI_WDS_GET_DORMANCY_STATUS			= 0x30,
	QMI_WDS_GET_AUTOCONNECT_SETTINGS		= 0x34,
	QMI_WDS_GET_DATA_BEARER_TECHNOLOGY		= 0x37,
	QMI_WDS_GET_CURRENT_DATA_BEARER_TECHNOLOGY 	= 0x44,
	QMI_WDS_GET_DEFAULT_PROFILE_NUMBER		= 0x49,
	QMI_WDS_SET_DEFAULT_PROFILE_NUMBER		= 0x4A,
	QMI_WDS_RESET_PROFILE				= 0x4B,
	QMI_WDS_SET_IP_FAMILY				= 0x4D,
	QMI_WDS_SET_AUTOCONNECT_SETTINGS		= 0x51,
	QMI_WDS_GET_PDN_THROTTLE_INFO			= 0x6C,
	QMI_WDS_GET_LTE_ATTACH_PARAMETERS		= 0x85,
	QMI_WDS_BIND_DATA_PORT				= 0x89,
	QMI_WDS_EXTENDED_IP_CONFIG			= 0x8C,
	QMI_WDS_GET_MAX_LTE_ATTACH_PDN_NUMBER		= 0x92,
	QMI_WDS_SET_LTE_ATTACH_PDN_LIST			= 0x93,
	QMI_WDS_GET_LTE_ATTACH_PDN_LIST			= 0x94,
	QMI_WDS_BIND_MUX_DATA_PORT			= 0xA2,
	QMI_WDS_CONFIGURE_PROFILE_EVENT_LIST		= 0xA7,
	QMI_WDS_PROFILE_CHANGED				= 0xA8,
};

int qmi_wds_auth_from_ofono(enum ofono_gprs_auth_method method);
int qmi_wds_pdp_type_from_ofono(enum ofono_gprs_proto proto);
