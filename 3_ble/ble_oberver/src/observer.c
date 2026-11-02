/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ported from NCS SDK samples/bluetooth/observer/src/observer.c (faithful reproduction).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

#include <string.h>

#include "led.h"

/* Briefly flash green on each device found, then return to the blue scanning
 * pattern. Rescheduled on every device_found callback.
 */
static void led_back_to_scan(struct k_work *w);
static K_WORK_DELAYABLE_DEFINE(led_back_dwork, led_back_to_scan);

static void led_back_to_scan(struct k_work *w)
{
	led_set_state(LED_BLUE_SLOW);
}

/* ---- Enhancement: parse the advertised name + de-duplicate per address. -----
 * The stock device_found() callback prints every single packet, which floods
 * the console (one device is reported on each of the 3 adv channels, again on
 * its scan response, and repeatedly thereafter). We:
 *   - parse the AD payload for a local-name field so the log is useful, and
 *   - print each distinct peer address only once (a small fixed table).
 * The green flash still fires on every packet so scanning activity stays
 * visible on the LED.
 */
#define NAME_LEN_LP 30

struct ad_name {
	char buf[NAME_LEN_LP];
	bool found;
};

static bool name_cb(struct bt_data *data, void *user_data)
{
	struct ad_name *n = user_data;
	size_t len;

	if (data->type != BT_DATA_NAME_SHORTENED &&
	    data->type != BT_DATA_NAME_COMPLETE) {
		return true;
	}
	len = MIN(data->data_len, sizeof(n->buf) - 1);
	memcpy(n->buf, data->data, len);
	n->buf[len] = '\0';
	n->found = true;
	return false;
}

#define MAX_SEEN 16
struct seen_dev {
	bt_addr_le_t addr;
	bool used;
};
static struct seen_dev seen[MAX_SEEN];

static bool already_seen(const bt_addr_le_t *addr)
{
	for (int i = 0; i < MAX_SEEN; i++) {
		if (seen[i].used && bt_addr_le_cmp(&seen[i].addr, addr) == 0) {
			return true;
		}
	}
	return false;
}

static void mark_seen(const bt_addr_le_t *addr)
{
	for (int i = 0; i < MAX_SEEN; i++) {
		if (!seen[i].used) {
			bt_addr_le_copy(&seen[i].addr, addr);
			seen[i].used = true;
			return;
		}
	}
	/* Table full: drop everything and start over with just this device. */
	memset(seen, 0, sizeof(seen));
	bt_addr_le_copy(&seen[0].addr, addr);
	seen[0].used = true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	struct ad_name name = {0};

	/* Green flash on every received packet so scanning activity stays
	 * visible even when we suppress the log line below.
	 */
	led_set_state(LED_GREEN_FAST);
	k_work_reschedule(&led_back_dwork, K_MSEC(300));

	if (already_seen(addr)) {
		return;	/* already reported this address — keep the log clean */
	}
	mark_seen(addr);

	bt_data_parse(ad, name_cb, &name);
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	printk("Device found: %s (RSSI %d), type %u, AD len %u, name \"%s\"\n",
	       addr_str, rssi, type, ad->len, name.found ? name.buf : "(none)");
}

#if defined(CONFIG_BT_EXT_ADV)
#define NAME_LEN 30

static bool data_cb(struct bt_data *data, void *user_data)
{
	char *name = user_data;
	uint8_t len;

	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
		len = MIN(data->data_len, NAME_LEN - 1);
		(void)memcpy(name, data->data, len);
		name[len] = '\0';
		return false;
	default:
		return true;
	}
}

static const char *phy2str(uint8_t phy)
{
	switch (phy) {
	case BT_GAP_LE_PHY_NONE: return "No packets";
	case BT_GAP_LE_PHY_1M: return "LE 1M";
	case BT_GAP_LE_PHY_2M: return "LE 2M";
	case BT_GAP_LE_PHY_CODED: return "LE Coded";
	default: return "Unknown";
	}
}

static void scan_recv(const struct bt_le_scan_recv_info *info,
		      struct net_buf_simple *buf)
{
	char le_addr[BT_ADDR_LE_STR_LEN];
	char name[NAME_LEN];
	uint8_t data_status;
	uint16_t data_len;

	(void)memset(name, 0, sizeof(name));

	data_len = buf->len;
	bt_data_parse(buf, data_cb, name);

	data_status = BT_HCI_LE_ADV_EVT_TYPE_DATA_STATUS(info->adv_props);

	bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));
	printk("[DEVICE]: %s, AD evt type %u, Tx Pwr: %i, RSSI %i "
	       "Data status: %u, AD data len: %u Name: %s "
	       "C:%u S:%u D:%u SR:%u E:%u Pri PHY: %s, Sec PHY: %s, "
	       "Interval: 0x%04x (%u ms), SID: %u\n",
	       le_addr, info->adv_type, info->tx_power, info->rssi,
	       data_status, data_len, name,
	       (info->adv_props & BT_GAP_ADV_PROP_CONNECTABLE) != 0,
	       (info->adv_props & BT_GAP_ADV_PROP_SCANNABLE) != 0,
	       (info->adv_props & BT_GAP_ADV_PROP_DIRECTED) != 0,
	       (info->adv_props & BT_GAP_ADV_PROP_SCAN_RESPONSE) != 0,
	       (info->adv_props & BT_GAP_ADV_PROP_EXT_ADV) != 0,
	       phy2str(info->primary_phy), phy2str(info->secondary_phy),
	       info->interval, info->interval * 5 / 4, info->sid);
}

static struct bt_le_scan_cb scan_callbacks = {
	.recv = scan_recv,
};
#endif /* CONFIG_BT_EXT_ADV */

static int scan_start(void)
{
	/* 30 ms continuous active scanning with duplicate filtering. */
	struct bt_le_scan_param scan_param = {
		.type       = BT_LE_SCAN_TYPE_ACTIVE,
		.options    = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
		.interval   = BT_GAP_SCAN_FAST_INTERVAL_MIN,
		.window     = BT_GAP_SCAN_FAST_WINDOW,
	};
	int err;

#if defined(CONFIG_BT_EXT_ADV)
	scan_param.options |= BT_LE_SCAN_OPT_CODED;
#endif /* CONFIG_BT_EXT_ADV */

scan_start_retry:
	printk("Starting scanning...\n");
	err = bt_le_scan_start(&scan_param, device_found);
	if (err) {
		if ((scan_param.options & BT_LE_SCAN_OPT_CODED) != 0U) {
			printk("Failed to start scanning with Coded PHY (err %d), retrying "
			       "without...\n", err);

			scan_param.options &= ~BT_LE_SCAN_OPT_CODED;

			goto scan_start_retry;
		}

		printk("Start scanning failed (err %d)\n", err);
		led_set_state(LED_RED_ON);

		return err;
	}

	printk("success.\n");
	led_set_state(LED_BLUE_SLOW);

	return 0;
}

int observer_start(void)
{
	int err;

#if defined(CONFIG_BT_EXT_ADV)
	bt_le_scan_cb_register(&scan_callbacks);
	printk("Registered scan callbacks\n");
#endif /* CONFIG_BT_EXT_ADV */

	err = scan_start();
	if (err != 0) {
		return err;
	}

	return 0;
}
