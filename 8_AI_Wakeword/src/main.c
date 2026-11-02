/*
 * XIAO nRF54LM20B — "hi seeed" wakeword demo (Axon NPU)
 * ----------------------------------------------------------------------------
 * Model: Edge AI Lab solution "97021" (user-trained wakeword, "hi seeed")
 *   - Input:  int16 mono PCM @ 16 kHz, 1 channel
 *   - Window: 160 samples (10 ms), shift 160 (streaming)
 *   - DSP:    40 mel features, extracted inside the Edge AI runtime
 *   - Output: 3 classes (OTHER / SILENCE / seeed), one probability each
 *
 * Data path (mirrors the official sdk-edge-ai applications/ww_kws sample):
 *   PDM2 mic (dmic, 160-sample blocks) -> nrf_edgeai_feed_inputs() with raw
 *   PCM -> nrf_edgeai_run_inference() on the Axon NPU every full window ->
 *   predicted class == seeed with probability above threshold, debounced by
 *   a sliding window -> LED flash + serial.
 *
 * The mic block size is intentionally equal to the model window (both 160
 * samples / 10 ms), so every dmic_read() completes exactly one window and
 * feeds it in a single call.
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
#include <zephyr/dfu/mcuboot.h>

#include <nrf_edgeai/nrf_edgeai.h>

#include "nrf_edgeai_generated/nrf_edgeai_user_model.h"
#include "nrf_edgeai_generated/nrf_edgeai_user_model_labels.h"

/* ============================ Model constants ============================ */

#define INPUT_CHANNELS   1
#define MODEL_OUTPUTS    3
/* Expected model geometry — checked at startup, mismatch is fatal. */
#define EXPECTED_WINDOW  160

/* ============================ Audio parameters ============================ */

#define SAMPLE_RATE      16000U
#define SAMPLE_BITS      16U
#define BYTES_PER_SAMPLE 2U
#define CHANNELS         1U

/* One block per model window: 16000 Hz * 10 ms = 160 samples = 320 bytes. */
#define BLOCK_MS         10U
#define BLOCK_SAMPLES    (SAMPLE_RATE / 1000U * BLOCK_MS)
#define BLOCK_SIZE       (BLOCK_SAMPLES * BYTES_PER_SAMPLE * CHANNELS)
/* 16 blocks = 160 ms of buffering. The 3-class model takes ~9.2 ms of the
 * 10 ms window for inference, so the loop has very little headroom: the
 * 4-block (40 ms) slab ran dry on any jitter and the nrfx PDM driver stops
 * the stream permanently when that happens. */
#define BLOCK_COUNT      16U
K_MEM_SLAB_DEFINE_STATIC(mic_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

BUILD_ASSERT(BLOCK_SAMPLES == EXPECTED_WINDOW,
	     "dmic block size must match the model input window");

/* ============================ Board resources ============================ */

/* Feedback LED: the on-board user green LED (common anode, GPIO_ACTIVE_LOW).
 * Lit by driving the GPIO to its active level; turned off from a timer so the
 * flash never blocks the 10 ms audio loop. */
static const struct gpio_dt_spec ww_led =
	GPIO_DT_SPEC_GET(DT_NODELABEL(green_led), gpios);

/* USB CDC-ACM console (zephyr,console = cdc_acm_uart, ready at boot). */
static const struct device *const cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* PDM2 digital microphone: pdm20 (nordic,nrf-pdm). */
static const struct device *const dmic = DEVICE_DT_GET(DT_NODELABEL(pdm20));

#define LED_FLASH_MS 300

static void led_off_timer_cb(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	gpio_pin_set_dt(&ww_led, 0);
}
K_TIMER_DEFINE(led_off_timer, led_off_timer_cb, NULL);

static void ww_led_flash(void)
{
	gpio_pin_set_dt(&ww_led, 1);
	k_timer_start(&led_off_timer, K_MSEC(LED_FLASH_MS), K_FOREVER);
}

/* ============================ Application state ============================ */

static nrf_edgeai_t *model;
static bool banner_printed;      /* startup banner printed once after DTR */
static uint32_t infer_count;     /* total inferences (windows processed) */
static uint32_t infer_us_sum;    /* accumulated inference time (us) */
static float prob_peak;          /* peak wakeword probability since last stats */
/* Diagnostics: how many blocks were consumed and how long run_inference()
 * takes on the INPROGRESS path (mel window not complete). Reveals whether
 * the loop cadence or the driver is at fault when the stream stops. */
static uint32_t block_count;     /* dmic_read() blocks since last stats */
static uint32_t inprog_count;    /* INPROGRESS returns since last stats */
static uint32_t inprog_us_sum;   /* time spent in those calls (us) */

/* ============================ Wakeword postprocess ============================ */

/*
 * Sliding-window debounce, same scheme as the official ww_kws sample.
 * This is a 3-class model (OTHER / SILENCE / seeed): a window is "positive"
 * only when the predicted class is seeed AND its probability exceeds the
 * threshold (checking the probability alone would count high-confidence
 * silence as positive). The wakeword is confirmed when at least
 * COUNT_THRESHOLD of the last HISTORY_SIZE windows were positive. State
 * resets on confirmation, which gives a natural refractory period (a new
 * utterance has to accumulate again).
 */
static bool wakeword_postprocess(void)
{
	static uint32_t ww_count;
	static uint32_t ww_history;

	const float threshold =
		(float)CONFIG_WW_WAKE_PROBABILITY_THRESHOLD_MILLI / 1000.0f;

	const uint16_t predicted = model->decoded_output.classif.predicted_class;
	const float probability =
		model->decoded_output.classif.probabilities.p_f32[predicted];
	const bool positive =
		predicted == MODEL_USER_LABEL_SEEED && probability > threshold;

	/* Track the seeed-class probability (not the predicted one) so the
	 * [STAT] peak stays meaningful for threshold tuning during silence. */
	const float seeed_prob =
		model->decoded_output.classif.probabilities.p_f32[MODEL_USER_LABEL_SEEED];
	if (seeed_prob > prob_peak) {
		prob_peak = seeed_prob;
	}

	const bool oldest_entry =
		(bool)(ww_history & BIT(CONFIG_WW_WAKE_HISTORY_SIZE - 1));

	ww_count = ww_count + positive - oldest_entry;
	ww_history = (ww_history << 1) | positive;

	if (ww_count >= CONFIG_WW_WAKE_COUNT_THRESHOLD) {
		ww_count = 0;
		ww_history = 0;
		return true;
	}
	return false;
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

	printk("*** XIAO M20B Edge AI Wakeword Demo ***\r\n");
	printk("Board: XIAO nRF54LM20B Sense\r\n");
	printk("Model: hi seeed — %s (solution %s, %u classes)\r\n",
	       NRF_EDGEAI_USER_LABELS_NAME[MODEL_USER_LABEL_SEEED],
	       nrf_edgeai_solution_id_str(model),
	       nrf_edgeai_model_outputs_num(model));
	printk("Runtime: Nordic Edge AI / Axon NPU\r\n");
	printk("[AI] Model initialized\r\n");
	printk("[MIC] Sampling rate: %u Hz (int16 mono, window %u = %u ms)\r\n",
	       SAMPLE_RATE, EXPECTED_WINDOW, BLOCK_MS);
	printk("[AXON] NPU initialized\r\n");

	banner_printed = true;
}

/* Periodic stats line: average inference time + peak probability since the
 * previous line. The peak makes threshold tuning easy without debug spam. */
static void print_stats(void)
{
	static int64_t last_stats_ms;

	const int64_t now = k_uptime_get();
	if (now - last_stats_ms < 5000) {
		return;
	}
	last_stats_ms = now;

	if (infer_count > 0U) {
		printk("[STAT] %u blocks, %u infer (avg %u us), "
		       "%u inprog (avg %u us), peak prob %.2f\r\n",
		       block_count, infer_count, infer_us_sum / infer_count,
		       inprog_count,
		       inprog_count > 0U ? inprog_us_sum / inprog_count : 0U,
		       (double)prob_peak);
	}
	block_count = 0U;
	infer_count = 0U;
	infer_us_sum = 0U;
	inprog_count = 0U;
	inprog_us_sum = 0U;
	prob_peak = 0.0f;
}

/* Debug: raw probability every ~20 windows (200 ms). */
static void print_debug(void)
{
	static uint32_t dbg_count;

	if (!IS_ENABLED(CONFIG_WW_WAKE_DEBUG)) {
		return;
	}
	if (++dbg_count % 20U != 0U) {
		return;
	}
	const uint16_t predicted = model->decoded_output.classif.predicted_class;
	printk("[WW] seeed prob %.2f (predicted: %s)\r\n",
	       (double)model->decoded_output.classif.probabilities.p_f32[MODEL_USER_LABEL_SEEED],
	       NRF_EDGEAI_USER_LABELS_NAME[predicted]);
}

/* ============================ Main flow ============================ */

static int mic_start(void)
{
	static struct pcm_stream_cfg stream;
	static struct dmic_cfg dcfg;

	if (!device_is_ready(dmic)) {
		printk("[MIC] ERROR: pdm20 (dmic) not ready — power rail down?\r\n");
		return -1;
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

	if (dmic_configure(dmic, &dcfg) != 0) {
		printk("[MIC] ERROR: dmic_configure failed\r\n");
		return -1;
	}
	if (dmic_trigger(dmic, DMIC_TRIGGER_START) != 0) {
		printk("[MIC] ERROR: dmic_trigger(START) failed\r\n");
		return -1;
	}
	return 0;
}

int main(void)
{
	nrf_edgeai_err_t err;

	/* 0) Self-confirm the running mcuboot image so it becomes permanent. */
	(void)boot_write_img_confirmed();

	/* 1) Feedback LED: output, initially off. */
	if (!gpio_is_ready_dt(&ww_led)) {
		printk("[LED] ERROR: green LED not available in devicetree\r\n");
		return -1;
	}
	gpio_pin_configure_dt(&ww_led, GPIO_OUTPUT_INACTIVE);

	/* 2) Edge AI runtime + model geometry checks (fail loudly, never
	 *    silently). */
	model = nrf_edgeai_user_model();
	if (model == NULL) {
		printk("[AI] ERROR: generated model is NULL\r\n");
		return -1;
	}
	if (nrf_edgeai_input_window_size(model) != EXPECTED_WINDOW ||
	    nrf_edgeai_uniq_inputs_num(model) != INPUT_CHANNELS ||
	    nrf_edgeai_model_outputs_num(model) != MODEL_OUTPUTS) {
		printk("[AI] ERROR: model geometry mismatch (window=%u inputs=%u outputs=%u, "
		       "expected %u/%u/%u)\r\n",
		       nrf_edgeai_input_window_size(model),
		       nrf_edgeai_uniq_inputs_num(model),
		       nrf_edgeai_model_outputs_num(model),
		       EXPECTED_WINDOW, INPUT_CHANNELS, MODEL_OUTPUTS);
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

	/* 3) Microphone: 16 kHz / 16-bit / mono PDM capture. */
	if (mic_start() != 0) {
		return -1;
	}

	/* 4) Main loop: one 10 ms block per dmic_read() — each block is exactly
	 *    one model window, so feed it whole and infer. */
	uint32_t mic_fail_count = 0U;

	for (;;) {
		print_banner_once();

		void *buffer = NULL;
		uint32_t size = 0U;

		if (dmic_read(dmic, 0, &buffer, &size, 100) != 0 ||
		    buffer == NULL || size == 0U) {
			/* The nrfx PDM driver stops the stream permanently when
			 * its block slab runs dry (the app stalled > ~40 ms).
			 * Recover by restarting the capture: stop, drain the
			 * driver queues, start again. */
			if (++mic_fail_count % 2U == 0U) {
				printk("[MIC] WARNING: dmic_read failed %u times, "
				       "restarting capture\r\n", mic_fail_count);
				dmic_trigger(dmic, DMIC_TRIGGER_STOP);
				k_sleep(K_MSEC(20));
				if (dmic_trigger(dmic, DMIC_TRIGGER_START) == 0) {
					mic_fail_count = 0U;
					printk("[MIC] capture restarted\r\n");
				}
			}
			continue;
		}

		const int16_t *pcm = (const int16_t *)buffer;
		const uint32_t samples = size / BYTES_PER_SAMPLE;
		block_count++;

		err = nrf_edgeai_feed_inputs(model, (void *)pcm,
					     (uint16_t)samples);
		k_mem_slab_free(&mic_slab, buffer);

		if (err == NRF_EDGEAI_ERR_INPROGRESS) {
			continue; /* window not complete yet */
		} else if (err != NRF_EDGEAI_ERR_SUCCESS) {
			printk("[AI] WARNING: input feed failed: %d\r\n", (int)err);
			continue;
		}

		/* Window ready — run the inference on the Axon NPU. */
		const uint32_t t0 = k_cycle_get_32();
		err = nrf_edgeai_run_inference(model);
		const uint32_t infer_us = k_cyc_to_us_floor32(k_cycle_get_32() - t0);

		if (err == NRF_EDGEAI_ERR_INPROGRESS) {
			/* Streaming DSP pipeline: the mel feature window is not
			 * complete yet. Normal flow control — skip silently,
			 * exactly like the official ww_kws sample. */
			inprog_count++;
			inprog_us_sum += infer_us;
			continue;
		} else if (err != NRF_EDGEAI_ERR_SUCCESS) {
			printk("[AI] WARNING: inference failed: %d\r\n", (int)err);
			continue;
		}

		infer_count++;
		infer_us_sum += infer_us;

		print_debug();

		if (wakeword_postprocess()) {
			const uint16_t predicted =
				model->decoded_output.classif.predicted_class;
			const float probability =
				model->decoded_output.classif.probabilities.p_f32[predicted];

			printk("[AI] Wakeword detected: \"%s\"\r\n",
			       NRF_EDGEAI_USER_LABELS_NAME[predicted]);
			printk("[AI] Probability: %.2f\r\n", (double)probability);
			ww_led_flash();
		}

		print_stats();
	}

	return 0;
}
