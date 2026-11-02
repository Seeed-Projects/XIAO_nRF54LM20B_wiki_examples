/*
 * XIAO nRF54LM20B Sense — 5-class IMU gesture recognition (Axon NPU)
 * ----------------------------------------------------------------------------
 * Model: Nordic Edge AI Lab solution 96964 / session 64392
 *   - 6 input channels (ax, ay, az, gx, gy, gz), FLOAT32, micro-SI units
 *   - sliding window: 200 samples (~2 s @ 100 Hz), shift 33 samples (~330 ms)
 *   - 5 classes: 0=IDLE 1=SWIPE_LEFT 2=SWIPE_RIGHT 3=ROTATION_CW 4=ROTATION_CCW
 *
 * Data path: 100 Hz IMU sampling (k_timer -> semaphore, non-blocking) ->
 * nrf_edgeai_feed_inputs() in micro-SI (matching the Data Forwarder units the
 * model was trained on) -> when the runtime reports the window is ready,
 * nrf_edgeai_run_inference() on the Axon NPU -> decoded classification.
 *
 * Feedback: gesture (non-IDLE) predictions above the confidence threshold are
 * printed and flashed on the green LED at most once per report interval
 * (3 s default), while every raw inference can be logged in debug mode.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/dfu/mcuboot.h>

#include <nrf_edgeai/nrf_edgeai.h>

#include "imu.h"
#include "nrf_edgeai_user_model.h"

/* ============================ Model constants ============================ */

#define INPUT_FEATURES   6
#define CLASS_NUM        5
/* Expected model geometry — checked at startup, mismatch is fatal. */
#define EXPECTED_WINDOW  200
#define EXPECTED_SHIFT   33

/* Class-ID -> label map (must match the training label order exactly). */
static const char *const class_labels[CLASS_NUM] = {
	"IDLE",
	"SWIPE_LEFT",
	"SWIPE_RIGHT",
	"ROTATION_CW",
	"ROTATION_CCW",
};

/* ============================ Board resources ============================ */

/* Feedback LED: the on-board user green LED (common anode, GPIO_ACTIVE_LOW).
 * Lit by driving the GPIO to its active level; turned off from a timer so the
 * flash never blocks the 100 Hz sampling loop. */
static const struct gpio_dt_spec gesture_led =
	GPIO_DT_SPEC_GET(DT_NODELABEL(green_led), gpios);

/* USB CDC-ACM console (zephyr,console = cdc_acm_uart, ready at boot). */
static const struct device *const cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

#define LED_FLASH_MS 150

static void led_off_timer_cb(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	gpio_pin_set_dt(&gesture_led, 0);
}
K_TIMER_DEFINE(led_off_timer, led_off_timer_cb, NULL);

static void gesture_led_flash(void)
{
	gpio_pin_set_dt(&gesture_led, 1);
	k_timer_start(&led_off_timer, K_MSEC(LED_FLASH_MS), K_FOREVER);
}

/* ============================ Application state ============================ */

static struct k_sem imu_ready_sem;
static nrf_edgeai_t *model;

static int64_t last_report_ms;   /* last gesture feedback time (throttle) */
static bool banner_printed;      /* startup banner printed once after DTR */

/* ============================ Result handling ============================ */

static void handle_inference_result(uint32_t infer_us)
{
	const uint16_t predicted = model->decoded_output.classif.predicted_class;
	const flt32_t *probabilities = model->decoded_output.classif.probabilities.p_f32;

	if (predicted >= CLASS_NUM) {
		printk("[AI] ERROR: predicted class %u out of range (model outputs %u)\r\n",
		       predicted, model->decoded_output.classif.num_classes);
		return;
	}

	const float confidence = (float)probabilities[predicted];

	if (IS_ENABLED(CONFIG_GESTURE_DEMO_DEBUG)) {
		printk("[INFER] class=%u label=%s confidence=%.2f time=%u.%03u ms\r\n",
		       predicted, class_labels[predicted], (double)confidence,
		       infer_us / 1000U, infer_us % 1000U);
	}

	/* Gesture feedback: only non-IDLE classes, above the confidence
	 * threshold, at most once per report interval. IDLE keeps the device
	 * quiet (debug mode above still shows every window). */
	if (predicted == 0U) {
		return;
	}
	if ((confidence * 100.0f) < (float)CONFIG_GESTURE_DEMO_MIN_CONFIDENCE_PCT) {
		return;
	}

	const int64_t now = k_uptime_get();
	if ((now - last_report_ms) < (int64_t)CONFIG_GESTURE_DEMO_REPORT_INTERVAL_MS) {
		return;
	}
	last_report_ms = now;

	printk("[AI] Gesture: %s\r\n", class_labels[predicted]);
	printk("[AI] Confidence: %.2f\r\n", (double)confidence);
	printk("[AI] Inference time: %u.%03u ms\r\n",
	       infer_us / 1000U, infer_us % 1000U);

	gesture_led_flash();
}

/* Print the startup banner once, after the host has opened the serial port
 * (DTR asserted) so it is not lost while the CDC port is closed. */
static void print_banner_once(void)
{
	uint32_t dtr = 0U;

	if (banner_printed || cons == NULL || !device_is_ready(cons)) {
		return;
	}
	if (uart_line_ctrl_get(cons, UART_LINE_CTRL_DTR, &dtr) != 0 || dtr == 0U) {
		return;
	}
	k_msleep(50); /* let the host finish port setup */

	printk("*** XIAO M20B Edge AI Gesture Demo ***\r\n");
	printk("Board: XIAO nRF54LM20B Sense\r\n");
	printk("Model: XIAO_M20B_Gesture_5Class (solution %s)\r\n",
	       nrf_edgeai_solution_id_str(model));
	printk("Runtime: Nordic Edge AI / Axon NPU\r\n");
	printk("[AI] Model initialized\r\n");
	printk("[IMU] Sampling rate: 100 Hz (6ch, micro-SI, window %u shift %u)\r\n",
	       EXPECTED_WINDOW, EXPECTED_SHIFT);
	printk("[AXON] NPU initialized\r\n");

	banner_printed = true;
}

/* ============================ Main flow ============================ */

static void imu_ready(void)
{
	k_sem_give(&imu_ready_sem);
}

int main(void)
{
	nrf_edgeai_err_t err;
	static uint32_t imu_fail_count;

	/* 0) Self-confirm the running mcuboot image so it becomes permanent. */
	(void)boot_write_img_confirmed();

	/* 1) Feedback LED: output, initially off. */
	if (!gpio_is_ready_dt(&gesture_led)) {
		printk("[LED] ERROR: green LED not available in devicetree\r\n");
		return -1;
	}
	gpio_pin_configure_dt(&gesture_led, GPIO_OUTPUT_INACTIVE);

	/* 2) IMU: 100 Hz, +/-4 g, +/-1000 dps — the same full-scale ranges used
	 *    to capture the training dataset. */
	k_sem_init(&imu_ready_sem, 0, 1);
	const imu_config_t imu_config = {
		.accel_fs_g = IMU_ACCEL_SCALE_4G,
		.gyro_fs_dps = IMU_GYRO_SCALE_1000DPS,
		.data_rate_hz = 100,
	};
	if (imu_init(&imu_config, imu_ready) != STATUS_SUCCESS) {
		printk("[IMU] ERROR: failed to initialize the LSM6DS3TR-C IMU "
		       "(not ready / power rail down)\r\n");
		return -1;
	}

	/* 3) Edge AI runtime + model geometry checks (fail loudly, never
	 *    silently, per FR-07). */
	model = nrf_edgeai_user_model();
	if (model == NULL) {
		printk("[AI] ERROR: generated model is NULL\r\n");
		return -1;
	}
	if (nrf_edgeai_input_window_size(model) != EXPECTED_WINDOW ||
	    nrf_edgeai_uniq_inputs_num(model) != INPUT_FEATURES ||
	    nrf_edgeai_model_outputs_num(model) != CLASS_NUM) {
		printk("[AI] ERROR: model geometry mismatch (window=%u inputs=%u outputs=%u, "
		       "expected %u/%u/%u)\r\n",
		       nrf_edgeai_input_window_size(model),
		       nrf_edgeai_uniq_inputs_num(model),
		       nrf_edgeai_model_outputs_num(model),
		       EXPECTED_WINDOW, INPUT_FEATURES, CLASS_NUM);
		return -1;
	}
	if (!nrf_edgeai_is_runtime_compatible(model)) {
		printk("[AI] ERROR: generated model is incompatible with the Edge AI "
		       "runtime version\r\n");
		return -1;
	}
	err = nrf_edgeai_init(model);
	if (err != NRF_EDGEAI_ERR_SUCCESS) {
		printk("[AI] ERROR: nrf_edgeai_init failed: %d\r\n", (int)err);
		return -1;
	}
	if (model->model.type != NRF_EDGEAI_MODEL_AXON) {
		printk("[AI] ERROR: model is not an Axon NPU model (type %d)\r\n",
		       (int)model->model.type);
		return -1;
	}
	/* The version union has const members: must be initialized, not assigned. */
	nrf_edgeai_rt_version_t v = nrf_edgeai_runtime_version();
	printk("[AI] Edge AI runtime %d.%d.%d, model flash/ROM size %u bytes\r\n",
	       v.field.major, v.field.minor, v.field.patch,
	       nrf_edgeai_user_model_size());

	/* 4) Main loop: one sample per semaphore take — the k_timer ISR only
	 *    releases the semaphore, so inference (and printing) can never block
	 *    the sampling cadence. */
	flt32_t input[INPUT_FEATURES];
	imu_data_t sample;

	for (;;) {
		print_banner_once();
		k_sem_take(&imu_ready_sem, K_FOREVER);

		if (imu_read(&sample) != STATUS_SUCCESS) {
			if (++imu_fail_count % 100U == 1U) {
				printk("[IMU] WARNING: read failed %u times "
				       "(check IMU / power rail)\r\n", imu_fail_count);
			}
			continue;
		}

		/* Channel order must match the training CSV: ax, ay, az, gx, gy, gz.
		 * The training data (Data Forwarder host) is in micro-SI units
		 * (micro m/s^2, micro rad/s), so convert from driver SI units. */
		for (int i = 0; i < 3; ++i) {
			input[i] = (flt32_t)(sample.accel[i].phys * 1000000.0f);
			input[i + 3] = (flt32_t)(sample.gyro[i].phys * 1000000.0f);
		}

		err = nrf_edgeai_feed_inputs(model, input, INPUT_FEATURES);
		if (err == NRF_EDGEAI_ERR_SUCCESS) {
			/* Window ready — run the inference on the Axon NPU. */
			const uint32_t t0 = k_cycle_get_32();
			err = nrf_edgeai_run_inference(model);
			const uint32_t t1 = k_cycle_get_32();

			if (err == NRF_EDGEAI_ERR_SUCCESS) {
				handle_inference_result(
					k_cyc_to_us_floor32(t1 - t0));
			} else {
				printk("[AI] WARNING: inference failed: %d\r\n", (int)err);
			}
		} else if (err != NRF_EDGEAI_ERR_INPROGRESS) {
			printk("[AI] WARNING: input feed failed: %d\r\n", (int)err);
		}
	}

	return 0;
}
