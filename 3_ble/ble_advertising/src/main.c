/*
 * XIAO nRF54LM20B BLE Advertising Demo
 * ----------------------------------------------------------------------------
 * Ported from the Zephyr samples/bluetooth/beacon, made connectable so a phone
 * (nRF Connect) or another board can scan and connect to it.
 *
 * Behaviour:
 *   - advertises as a connectable peripheral with a custom 16-bit service UUID
 *     and the device name in the scan response;
 *   - on connect: stops advertising, turns the LED green, prints the peer addr;
 *   - on disconnect: resumes advertising, LED back to blue slow blink.
 *
 * Single-core BLE stack: host + Zephyr software controller (BT_LL_SW_SPLIT)
 * driving the nRF54LM20B RADIO via radio_nrf54lx (controller enabled through
 * the app.overlay bt_hci_controller node, see prj.conf notes).
 *
 * USB CDC-ACM console is auto-initialised by the board, so printk lands on
 * cu.usbmodem* with no extra usbd setup.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <string.h>

#include "led.h"

#define DEVICE_NAME      CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN  (sizeof(DEVICE_NAME) - 1)

/* Custom 16-bit service UUID advertised so centrals can recognise this demo. */
#define ADV_SVC_UUID_VAL 0x18AA

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS,
		      BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      (ADV_SVC_UUID_VAL & 0xff),
		      ((ADV_SVC_UUID_VAL >> 8) & 0xff)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

/* ---- GATT service: one writable characteristic so a central (the
 * ble_central_gatt demo, or nRF Connect) can connect and write an incrementing
 * byte to us. The service UUID matches ADV_SVC_UUID_VAL (0x18AA) advertised in
 * ad[], so the central's UUID filter finds this board among other BLE traffic.
 * The characteristic holds a 1-byte counter; each write is echoed on the log.
 */
static struct bt_uuid_16 gatt_svc_uuid = BT_UUID_INIT_16(ADV_SVC_UUID_VAL);
static struct bt_uuid_16 gatt_chr_uuid = BT_UUID_INIT_16(0x2AB1);

static uint8_t gatt_counter;

static ssize_t write_counter(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len, uint16_t offset,
			     uint8_t flags)
{
	uint8_t *value = attr->user_data;

	if (offset + len > sizeof(gatt_counter)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);
	printk("GATT write received: counter = 0x%02x\n", gatt_counter);
	return len;
}

BT_GATT_SERVICE_DEFINE(gatt_svc,
	BT_GATT_PRIMARY_SERVICE(&gatt_svc_uuid),
	BT_GATT_CHARACTERISTIC(&gatt_chr_uuid,
			       BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE,
			       NULL, write_counter,
			       &gatt_counter),
);

static void start_advertising(void)
{
	int err;
	char addr_s[BT_ADDR_LE_STR_LEN];
	bt_addr_le_t addr = {0};
	size_t count = 1;

	/* Zephyr 4.4 dropped the BT_LE_ADV_CONN shorthand; use the connectable
	 * FAST_1 param set (BT_LE_ADV_OPT_CONN + fast advertising intervals).
	 */
	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		led_set_state(LED_RED_ON);
		return;
	}

	bt_id_get(&addr, &count);
	bt_addr_le_to_str(&addr, addr_s, sizeof(addr_s));
	printk("Advertising started as %s (connectable, name \"%s\")\n",
	       addr_s, DEVICE_NAME);
	led_set_state(LED_BLUE_SLOW);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		printk("Failed to connect to %s (0x%02x)\n", addr, err);
		led_set_state(LED_RED_ON);
		return;
	}

	printk("Connected: %s\n", addr);
	led_set_state(LED_GREEN_ON);

	/* No need to keep a reference; the connection callbacks hold it. */
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	/* Do NOT restart advertising here. The stack still holds a reference to
	 * the connection object at this point, so the connection pool has no
	 * free slot and a connectable bt_le_adv_start() would fail with
	 * -ENOMEM (conn.h documents exactly this). The board would then stop
	 * advertising and become unscannable after the first disconnect.
	 * Restart from the recycled callback instead: it fires once the
	 * connection object has actually been returned to the pool.
	 */
	printk("Waiting for connection object to be recycled...\n");
	led_set_state(LED_RED_ON);
}

static void recycled(void)
{
	printk("Connection object recycled; restarting advertising\n");
	start_advertising();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
};

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		led_set_state(LED_RED_ON);
		return;
	}

	printk("Bluetooth initialized\n");
	start_advertising();
}

int main(void)
{
	int err;

	printk("Starting BLE Advertising Demo\n");

	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		led_set_state(LED_RED_ON);
	}

	return 0;
}
