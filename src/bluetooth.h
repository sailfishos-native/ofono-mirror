/*
 * oFono - Open Source Telephony
 * Copyright (C) 2013  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH		31
#define PF_BLUETOOTH		AF_BLUETOOTH
#endif

#define BTPROTO_SCO		2

#define SOL_SCO			17

#ifndef SOL_BLUETOOTH
#define SOL_BLUETOOTH		274
#endif

#define BT_DEFER_SETUP		7


#define BT_VOICE		11
struct bt_voice {
	uint16_t setting;
};

#define BT_VOICE_TRANSPARENT			0x0003
#define BT_VOICE_CVSD_16BIT			0x0060

/* BD Address */
typedef struct {
	uint8_t b[6];
} __attribute__((packed)) bdaddr_t;

#define BDADDR_ANY   (&(bdaddr_t) {{0, 0, 0, 0, 0, 0}})

/* RFCOMM socket address */
struct sockaddr_rc {
	sa_family_t	rc_family;
	bdaddr_t	rc_bdaddr;
	uint8_t		rc_channel;
};

/* SCO socket address */
struct sockaddr_sco {
	sa_family_t	sco_family;
	bdaddr_t	sco_bdaddr;
};

static inline void bt_bacpy(bdaddr_t *dst, const bdaddr_t *src)
{
	memcpy(dst, src, sizeof(bdaddr_t));
}

static inline int bt_ba2str(const bdaddr_t *ba, char *str)
{
	return sprintf(str, "%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X",
		ba->b[5], ba->b[4], ba->b[3], ba->b[2], ba->b[1], ba->b[0]);
}

static inline int bt_bacmp(const bdaddr_t *ba1, const bdaddr_t *ba2)
{
	return memcmp(ba1, ba2, sizeof(bdaddr_t));
}

static inline void bt_str2ba(const char *str, bdaddr_t *ba)
{
	int i;

	for (i = 5; i >= 0; i--, str += 3)
		ba->b[i] = strtol(str, NULL, 16);
}
