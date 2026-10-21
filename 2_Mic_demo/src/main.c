/*
 * XIAO nRF54LM20B Mic Demo
 * ----------------------------------------------------------------------------
 *  Bring up the PDM2 (pdm20, nordic,nrf-pdm) driver and stream audio out:
 *
 *  - On power-up the RGB green LED is solid on (standby)
 *  - Press User Button (sw0 = P0.09) to start recording: green LED blinks
 *    (~2.5Hz), PDM starts capturing
 *  - Press again to stop recording: green LED returns to solid on, PDM stops
 *  - During recording, 16kHz/16bit/mono PCM is streamed over the USB CDC
 *    serial port (=console) using a frame protocol; the PC-side tools/capture.py
 *    receives it and wraps it into a wav saved in the project folder
 *
 *  Frame protocol (little-endian, all raw bytes sent over serial):
 *    Start: "XIAOWAV1" + uint32 sample_rate + uint16 bits + uint16 channels
 *    Data:  raw PCM stream
 *    Stop:  "XIAOWAV0"
 *
 *  Note: printk is not used during recording, to avoid text contaminating the
 *  PCM stream; status-change notices are only sent on serial when not recording.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/sys/printk.h>

/* ============================ Board resource bindings ============================ */

/* RGB green LED: alias led2 = green_led (GPIO_ACTIVE_LOW, active-low / lit on low) */
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

/* User Button: alias sw0 = button0 (P0.09, active when pressed) */
static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

/* USB CDC-ACM console (zephyr,console = cdc_acm_uart, over USBHS, auto-ready at boot) */
static const struct device *const cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* PDM2 digital microphone: pdm20 (nordic,nrf-pdm) */
static const struct device *const dmic = DEVICE_DT_GET(DT_NODELABEL(pdm20));

/* ============================ Audio parameters ============================ */

#define SAMPLE_RATE     16000U
#define SAMPLE_BITS     16U
#define BYTES_PER_SAMPLE 2U
#define CHANNELS        1U

/* One block per 100 ms: 16000/10 * 2 * 1 = 3200 bytes */
#define BLOCK_MS        100U
#define BLOCK_SIZE      (SAMPLE_RATE / 1000U * BLOCK_MS * BYTES_PER_SAMPLE * CHANNELS)
#define BLOCK_COUNT     4U
K_MEM_SLAB_DEFINE_STATIC(mic_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

/* Recording frame-protocol magic */
#define WAV_MAGIC_START "XIAOWAV1"   /* 8 bytes: start + header */
#define WAV_MAGIC_STOP  "XIAOWAV0"   /* 8 bytes: stop */

/* ============================ Serial output ============================ */

/* Send byte-by-byte via poll_out: simple, shares the same channel as console
 * printk, best compatibility. 16kHz/16bit/mono = 32 KB/s; the nRF54 clock is
 * fast enough and the CDC driver packs automatically. */
static void cons_write(const void *data, size_t len)
{
	const uint8_t *p = data;
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(cons, p[i]);
	}
}

/* ============================ Button (polled debounce + edge) ============================ */

static bool btn_filtered;        /* Debounced stable logic level (pressed = 1) */
static uint8_t btn_count;       /* Count of consecutive identical readings */
static bool btn_prev;           /* Previous stable state, for edge detection */

/* Returns true if a new "press" edge was detected since the last call */
static bool button_pressed(void)
{
	bool raw = (bool)gpio_pin_get_dt(&btn);
	bool edge = false;

	/* Debounce: 3 consecutive identical readings update the stable state
	 * (poll interval ~10ms => ~30ms) */
	if (raw == btn_filtered) {
		btn_count = 0;
	} else {
		btn_count++;
		if (btn_count >= 3) {
			btn_filtered = raw;
			btn_count = 0;
			if (btn_filtered && !btn_prev) {
				edge = true;          /* 0 -> 1: a valid press */
			}
			btn_prev = btn_filtered;
		}
	}
	return edge;
}

/* ============================ Recording control ============================ */

static struct pcm_stream_cfg stream;
static struct dmic_cfg dcfg;
static bool dmic_started;

static void recording_start(void)
{
	if (dmic_started) {
		return;
	}

	stream.pcm_width   = SAMPLE_BITS;
	stream.mem_slab    = &mic_slab;
	stream.pcm_rate    = SAMPLE_RATE;
	stream.block_size  = BLOCK_SIZE;

	dcfg.io.min_pdm_clk_freq = 1000000;
	dcfg.io.max_pdm_clk_freq = 3500000;
	dcfg.io.min_pdm_clk_dc   = 40;
	dcfg.io.max_pdm_clk_dc   = 60;
	dcfg.streams = &stream;
	dcfg.channel.req_num_streams = 1U;
	dcfg.channel.req_num_chan    = CHANNELS;
	dcfg.channel.req_chan_map_lo =
		dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);

	(void)dmic_configure(dmic, &dcfg);
	if (dmic_trigger(dmic, DMIC_TRIGGER_START) == 0) {
		dmic_started = true;
		/* Send frame header: magic + sample_rate + bits + channels (all little-endian) */
		cons_write(WAV_MAGIC_START, 8);
		uint32_t rate = SAMPLE_RATE;
		uint16_t bits = SAMPLE_BITS;
		uint16_t ch   = CHANNELS;
		cons_write(&rate, sizeof(rate));
		cons_write(&bits, sizeof(bits));
		cons_write(&ch, sizeof(ch));
	}
}

static void recording_stop(void)
{
	if (!dmic_started) {
		return;
	}
	dmic_started = false;
	(void)dmic_trigger(dmic, DMIC_TRIGGER_STOP);
	cons_write(WAV_MAGIC_STOP, 8);
}

/* During recording, pull one PCM frame and send it out; also keep the green LED blinking */
static void recording_pump(void)
{
	void *buffer = NULL;
	uint32_t size = 0U;

	if (dmic_read(dmic, 0, &buffer, &size, 200) == 0 && buffer != NULL && size > 0) {
		cons_write(buffer, size);
	}
	if (buffer != NULL) {
		k_mem_slab_free(&mic_slab, buffer);
	}
}

/* ============================ Green LED indicator ============================ */

#define BLINK_PERIOD_MS 400U   /* Toggle every 400ms => ~2.5Hz blink */

static void led_update(bool recording)
{
	static int64_t last_toggle;
	static bool led_on;

	if (recording) {
		int64_t now = k_uptime_get();
		if (now - last_toggle >= BLINK_PERIOD_MS) {
			last_toggle = now;
			led_on = !led_on;
			gpio_pin_set_dt(&led_green, led_on ? 1 : 0);
		}
	} else {
		/* Stopped/standby: green LED solid on */
		gpio_pin_set_dt(&led_green, 1);
	}
}

/* ============================ Main flow ============================ */

int main(void)
{
	bool recording = false;

	/* 1) Green LED: configure as output, initially solid on */
	if (!gpio_is_ready_dt(&led_green)) {
		printk("green LED not ready\r\n");
		return 0;
	}
	gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&led_green, 1);

	/* 2) User Button: configure as input (dt flags already include pull-up/active-low) */
	if (!gpio_is_ready_dt(&btn)) {
		printk("button not ready\r\n");
		return 0;
	}
	gpio_pin_configure_dt(&btn, GPIO_INPUT);

	/* 3) Console: wait for the PC to open the serial port (DTR), then announce ready */
	if (cons != NULL && device_is_ready(cons)) {
		printk("Mic demo ready. Open this port, press User Button to record.\r\n");
	}

	/* 4) PDM microphone ready check */
	if (!device_is_ready(dmic)) {
		printk("pdm20(dmic) not ready!\r\n");
	}

	/* Main loop: poll the button + drive the recording state machine + LED */
	while (1) {
		if (button_pressed()) {
			recording = !recording;
			if (recording) {
				recording_start();
			} else {
				recording_stop();
			}
		}

		if (recording) {
			recording_pump();
		} else {
			k_msleep(10);
		}

		led_update(recording);
	}

	return 0;
}
