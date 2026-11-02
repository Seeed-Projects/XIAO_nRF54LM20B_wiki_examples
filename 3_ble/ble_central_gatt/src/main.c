/*
 * XIAO nRF54LM20B BLE Central / GATT Write Demo
 * ----------------------------------------------------------------------------
 * Ported from Zephyr samples/bluetooth/central_gatt_write, simplified for a
 * readable demo: scan -> connect -> discover characteristics -> periodically
 * write an incrementing byte to the first writable characteristic found.
 *
 * Pairing: connect this board (central) to a peripheral that exposes a
 * writable GATT characteristic -- e.g. a phone running nRF Connect in
 * peripheral mode with a custom service, or another XIAO running a peripheral
 * demo. No bonding is forced; if the peer requires encryption, CONFIG_BT_SMP
 * is enabled so the link can still come up.
 *
 * LED vocabulary:
 *   blue slow  = scanning
 *   green on   = connected / discovering
 *   green fast = GATT write completed
 *   red on     = error / disconnected
 *
 * USB CDC-ACM console is auto-initialised by the board; printk -> cu.usbmodem*.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include <string.h>

#include "led.h"

static K_SEM_DEFINE(sem_connected, 0, 1);
static K_SEM_DEFINE(sem_discovered, 0, 1);

static struct bt_conn *conn;
static struct bt_gatt_discover_params discover_params;

/* First writable characteristic value handle we find. */
static uint16_t write_handle;
static bool write_handle_found;

/* Only connect to our advertising demo. ble_advertising advertises the 16-bit
 * service UUID 0x18AA (ADV_SVC_UUID_VAL) in its ad[] payload. We parse the AD
 * data and require that UUID, so random phones/headphones nearby are skipped.
 */
#define TARGET_SVC_UUID_VAL 0x18AA

struct ad_match {
	bool uuid_ok;
};

static bool ad_parse_cb(struct bt_data *data, void *user_data)
{
	struct ad_match *m = user_data;

	if (data->type == BT_DATA_UUID16_ALL ||
	    data->type == BT_DATA_UUID16_SOME) {
		for (uint16_t i = 0; i + 1 < data->data_len; i += 2) {
			uint16_t u = data->data[i] | (data->data[i + 1] << 8);
			if (u == TARGET_SVC_UUID_VAL) {
				m->uuid_ok = true;
				return false;
			}
		}
	}
	return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char dev[BT_ADDR_LE_STR_LEN];
	struct ad_match m = {0};
	int err;

	/* Only connect to connectable advertising in close proximity. */
	if (type != BT_GAP_ADV_TYPE_ADV_IND &&
	    type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}
	if (rssi < -70) {
		return;
	}

	/* Only connect to peers advertising our target service UUID. */
	bt_data_parse(ad, ad_parse_cb, &m);
	if (!m.uuid_ok) {
		return;
	}

	bt_addr_le_to_str(addr, dev, sizeof(dev));
	printk("[DEVICE]: %s, AD evt type %u, AD len %u, RSSI %i (target UUID 0x%04X matched)\n",
	       dev, type, ad->len, rssi, TARGET_SVC_UUID_VAL);

	err = bt_le_scan_stop();
	if (err) {
		printk("Stop LE scan failed (err %d)\n", err);
		return;
	}

	printk("Connecting to %s ...\n", dev);
	led_set_state(LED_GREEN_ON);

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
				BT_LE_CONN_PARAM_DEFAULT, &conn);
	if (err) {
		printk("Create conn failed (err %d)\n", err);
		led_set_state(LED_BLUE_SLOW);
		(void)bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found);
	}
}

static void start_scan(void)
{
	int err;

	err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found);
	if (err) {
		printk("Scanning failed to start (err %d)\n", err);
		led_set_state(LED_RED_ON);
		return;
	}

	printk("Scanning...\n");
	led_set_state(LED_BLUE_SLOW);
}

static void mtu_exchange_cb(struct bt_conn *c, uint8_t err,
			    struct bt_gatt_exchange_params *params)
{
	printk("MTU exchange %s (%u)\n", err == 0U ? "success" : "failed",
	       bt_gatt_get_mtu(c));
}

static void write_cb(struct bt_conn *c, uint8_t err,
		     struct bt_gatt_write_params *params)
{
	if (err) {
		printk("Write failed (err 0x%02x)\n", err);
		led_set_state(LED_RED_ON);
	} else {
		printk("Write OK\n");
		led_set_state(LED_GREEN_FAST);
	}
}

static uint8_t discover_func(struct bt_conn *c, const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	struct bt_gatt_chrc *chrc;
	char str[BT_UUID_STR_LEN];

	if (!attr) {
		printk("Discovery complete.\n");
		if (!write_handle_found) {
			printk("No writable characteristic found on peer.\n");
			led_set_state(LED_RED_ON);
		}
		k_sem_give(&sem_discovered);
		return BT_GATT_ITER_STOP;
	}

	if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		chrc = (struct bt_gatt_chrc *)attr->user_data;
		bt_uuid_to_str(chrc->uuid, str, sizeof(str));
		printk("  char %s  handle 0x%04x (value 0x%04x)  props 0x%02x\n",
		       str, attr->handle, chrc->value_handle, chrc->properties);

		if (!write_handle_found &&
		    (chrc->properties & (BT_GATT_CHRC_WRITE |
					 BT_GATT_CHRC_WRITE_WITHOUT_RESP))) {
			write_handle = chrc->value_handle;
			write_handle_found = true;
			printk("  -> selected for GATT writes (handle 0x%04x)\n",
			       write_handle);
		}
	}

	return BT_GATT_ITER_CONTINUE;
}

static void connected(struct bt_conn *c, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	static struct bt_gatt_exchange_params mtu_params = { .func = mtu_exchange_cb };

	bt_addr_le_to_str(bt_conn_get_dst(c), addr, sizeof(addr));

	if (err) {
		printk("Failed to connect to %s (0x%02x)\n", addr, err);
		led_set_state(LED_RED_ON);
		conn = NULL;
		start_scan();
		return;
	}

	printk("Connected: %s\n", addr);
	led_set_state(LED_GREEN_ON);

	conn = bt_conn_ref(c);

	(void)bt_gatt_exchange_mtu(conn, &mtu_params);

	/* Discover all characteristics. */
	discover_params.uuid = NULL;
	discover_params.func = discover_func;
	discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

	err = bt_gatt_discover(conn, &discover_params);
	if (err) {
		printk("Discovery failed to start (err %d)\n", err);
		led_set_state(LED_RED_ON);
	}

	k_sem_give(&sem_connected);
}

static void disconnected(struct bt_conn *c, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(c), addr, sizeof(addr));
	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	bt_conn_unref(c);
	conn = NULL;
	write_handle_found = false;
	write_handle = 0U;

	start_scan();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

int main(void)
{
	int err;
	uint8_t counter = 0;

	printk("Starting BLE Central / GATT Write Demo\n");

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		led_set_state(LED_RED_ON);
		return 0;
	}
	printk("Bluetooth initialized\n");

	start_scan();

	k_sem_take(&sem_connected, K_FOREVER);
	k_sem_take(&sem_discovered, K_FOREVER);

	if (!write_handle_found) {
		/* Nothing to write; just hold the connection. */
		while (1) {
			k_sleep(K_SECONDS(1));
		}
	}

	printk("Starting periodic GATT writes every 1 s...\n");

	while (1) {
		struct bt_gatt_write_params wp;
		int werr;

		wp.func = write_cb;
		wp.handle = write_handle;
		wp.offset = 0;
		wp.data = &counter;
		wp.length = 1;

		printk("Write sent: 0x%02x -> handle 0x%04x\n", counter, write_handle);
		werr = bt_gatt_write(conn, &wp);
		if (werr) {
			printk("bt_gatt_write failed (err %d)\n", werr);
			led_set_state(LED_RED_ON);
		}

		counter++;
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
