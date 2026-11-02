/*
 * XIAO nRF54LM20B Sense -- IMU demo 3: "Electronic Ocean"
 * ----------------------------------------------------------------------------
 * Port of the official XIAO nRF54LM20A wiki demo (imu_ocean_main.c) to the
 * nRF54LM20B, same 6x10 WS2812 matrix accessory on SPI24 (MOSI P1.0 = D0):
 *
 *   - tilt the board left/right: the water level rolls between WATER_MIN and
 *     WATER_MAX with real fluid inertia (overshoot + sloshing);
 *   - the surface is rendered from 3 interfering wave octaves per column with
 *     edge reflection, foam at the crest and depth-based coloring;
 *   - flipping the board mirrors the display;
 *   - each column slowly cycles through an ocean color palette;
 *   - IMU data + water level are printed over the serial console.
 *
 * Differences vs the 20A original (only what the 20B board requires):
 *   - output uses printk() (like 1_6axis_data) instead of the logging
 *     subsystem: the first port used LOG/LOG_MODE_IMMEDIATE and the board
 *     came up dead after flashing (no serial, no LEDs), so the whole app was
 *     rebuilt around the 1_6axis_data configuration;
 *   - main() self-confirms the mcuboot image at startup (like 8_AI_Wakeword);
 *   - IMU power: on the 20B imu_vdd (nPM1300 LDO1) is regulator-boot-on in
 *     the board dtsi (repeated in app.overlay) and the sensor driver init
 *     order is fixed via CONFIG_SENSOR_INIT_PRIORITY=95, so the manual
 *     deferred-init path in the original is not needed (the device_init()
 *     fallback below still compiles and is simply skipped);
 *   - the overlay adds the same spi24 WS2812 node as the wiki, because D0 is
 *     gpio1.0 on the 20B XIAO connector too.
 *
 * Source: https://wiki.seeedstudio.com/cn/xiao_nrf54lm20a_with_onboard/
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/printk.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Simple LCG PRNG -- no kernel entropy subsystem needed */
static uint32_t rand_state = 12345u;
static inline uint32_t simple_rand(void)
{
	rand_state = rand_state * 1664525u + 1013904223u;
	return rand_state;
}

/* IMU via the board alias (imu0 = lsm6ds3tr_c), like 1_6axis_data */
#define IMU_NODE DT_ALIAS(imu0)
#define STRIP_NODE DT_ALIAS(led_strip)

#if DT_NODE_EXISTS(DT_NODELABEL(power_en))
static const struct device *const power_en_dev =
	DEVICE_DT_GET(DT_NODELABEL(power_en));
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(imu_vdd))
static const struct device *const imu_vdd_dev =
	DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
#endif

static int enable_imu_power(void)
{
	int ret;

#if DT_NODE_EXISTS(DT_NODELABEL(power_en))
	if (!device_is_ready(power_en_dev)) {
		printk("[OCEAN] power_en regulator not ready\r\n");
		return -ENODEV;
	}
	ret = regulator_enable(power_en_dev);
	if (ret < 0 && ret != -EALREADY) {
		printk("[OCEAN] power_en enable failed: %d\r\n", ret);
		return ret;
	}
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(imu_vdd))
	if (!device_is_ready(imu_vdd_dev)) {
		printk("[OCEAN] imu_vdd regulator not ready\r\n");
		return -ENODEV;
	}
	ret = regulator_enable(imu_vdd_dev);
	if (ret < 0 && ret != -EALREADY) {
		printk("[OCEAN] imu_vdd enable failed: %d\r\n", ret);
		return ret;
	}
#endif

	k_sleep(K_MSEC(20));
	return 0;
}

/* Matrix layout
 * Only these five macros need configuring; led_idx() adapts automatically.
 *
 * COLS Number of columns (horizontal)
 * ROWS Number of rows (vertical)
 *
 * MATRIX_SERPENTINE 1 = serpentine routing (odd columns reverse direction)
 * 0 = all columns same direction
 *
 * MATRIX_COL0_DIR 1 = column 0 goes row0 -> ROWS-1 (bottom -> top)
 * 0 = column 0 goes ROWS-1 -> row0 (top -> bottom)
 *
 * BRIGHTNESS Overall brightness 0-100 (100 = full power; mind supply current)
 */
#define COLS 10
#define ROWS 6
#define MATRIX_SERPENTINE 1
#define MATRIX_COL0_DIR 1
#define BRIGHTNESS 5 /* 0-100: overall brightness (100 = full power) */

#define NUM_LEDS (COLS * ROWS)

/* Water level range
 * Water surface centered when board is flat (WATER_CENTER).
 * Tilting moves water level between WATER_MIN and WATER_MAX.
 * Current config: waves stay in rows 2~4.
 */
#define WATER_CENTER 2.0f
#define WATER_MIN 1.0f
#define WATER_MAX 6.0f

/* Water slosh physics
 * Simulates real fluid inertia: water surface has velocity and mass; it does not
 * snap to target when tilted but overshoots and sloshes back -- inspired by FLIP
 * particle momentum behavior.
 */
static float slosh_velocity = 0.0f; /* water level change rate */
static float slosh_prev_tilt = 0.0f; /* previous tilt for jerk detection */

/* Per-column wave state
 * Each column maintains independent wave height and velocity for 2D wave
 * propagation -- inspired by FLIP particle density fields spreading across a grid.
 */
static float wave_h[COLS]; /* wave height offset per column */
static float wave_v[COLS]; /* wave velocity per column */
static float wave_phase[COLS]; /* organic noise phase per column */

/* Color palette
 * Fill in logical RGB; hardware GBR conversion is handled in set_pixel().
 */
static const struct led_rgb palette[] = {
	/* Blues */
	{ .r = 0, .g = 5, .b = 45 },
	{ .r = 0, .g = 20, .b = 95 },
	{ .r = 5, .g = 60, .b = 150 },
	{ .r = 10, .g =100, .b = 185 },
	/* Teals & cyans */
	{ .r = 0, .g =110, .b = 130 },
	{ .r = 15, .g =160, .b = 155 },
	{ .r = 40, .g =185, .b = 195 },
	/* Sea greens */
	{ .r = 0, .g = 90, .b = 35 },
	{ .r = 15, .g =145, .b = 55 },
	/* Warm: sunset / bioluminescence */
	{ .r =170, .g = 50, .b = 0 },
	{ .r =210, .g = 90, .b = 10 },
	{ .r =150, .g = 25, .b = 75 },
	{ .r =190, .g = 70, .b = 110 },
	/* Purples */
	{ .r = 75, .g = 5, .b = 140 },
	{ .r =110, .g = 20, .b = 180 },
	/* Amber / gold */
	{ .r =180, .g =130, .b = 0 },
};
#define PALETTE_N ARRAY_SIZE(palette)

static struct led_rgb pixels[NUM_LEDS];

/* LED index mapping
 * Automatically computes chain-order index from MATRIX_SERPENTINE
 * and MATRIX_COL0_DIR.
 *
 * Current board wiring (first LED at bottom-left, column-major serpentine):
 * MATRIX_SERPENTINE = 1, MATRIX_COL0_DIR = 1
 *
 * When switching boards, only change the macros above; nothing here.
 */
static inline int led_idx(int col, int row)
{
	bool rev;

#if MATRIX_SERPENTINE
	/* Serpentine: even and odd columns go opposite directions */
	rev = (MATRIX_COL0_DIR == 1) ? (col % 2 != 0) /* even cols forward, odd cols reversed */
				 : (col % 2 == 0); /* odd cols forward, even cols reversed */
#else
	/* Non-serpentine: all columns same direction */
	rev = (MATRIX_COL0_DIR == 0);
#endif

	return col * ROWS + (rev ? (ROWS - 1 - row) : row);
}

static inline uint8_t u8clamp(int v)
{
	return (v > 255) ? 255 : (v < 0) ? 0 : (uint8_t)v;
}

static inline uint8_t dim(int v)
{
	return u8clamp(v * BRIGHTNESS / 100);
}

/* Write one pixel (unified GBR color-order conversion)
 * This board uses hardware byte order G-B-R, not standard R-G-B.
 * struct.r -> hardware G channel
 * struct.g -> hardware B channel
 * struct.b -> hardware R channel
 */
static inline void set_pixel(int col, int row, struct led_rgb c)
{
	pixels[led_idx(col, row)] = (struct led_rgb){
		.r = dim(c.g), /* HW G <- logical g */
		.g = dim(c.b), /* HW B <- logical b */
		.b = dim(c.r), /* HW R <- logical r */
	};
}

/* Ocean wave simulation
 * Updates wave state once per frame: propagation, damping, and edge reflection.
 *
 * Inspired by FLIP particle simulation:
 * - Wave energy diffuses along the grid (like particle density smoothing)
 * - Edge reflections create interference waves (like boundary collision handling)
 * - Velocity damping prevents infinite oscillation
 */
static void wave_step(float dt, float excitation, float wave_phase_offset)
{
	const float wave_speed = 4.5f; /* wave propagation speed */
	const float wave_damping = 0.15f; /* velocity damping per second */
	const float wave_edge_reflect = 0.6f; /* edge reflection coefficient */

	for (int col = 0; col < COLS; col++) {
		/* Wave equation: acceleration from height difference with neighbors */
		float left = (col > 0) ? wave_h[col - 1] : 0.0f;
		float right = (col < COLS - 1) ? wave_h[col + 1] : 0.0f;
		float center = wave_h[col];

		/* Laplacian: curvature drives acceleration */
		float laplacian = left + right - 2.0f * center;

		/* Edge reflection: bounce energy back from boundaries */
		if (col == 0) {
			laplacian += wave_edge_reflect * (right - center);
		}
		if (col == COLS - 1) {
			laplacian += wave_edge_reflect * (left - center);
		}

		/* External excitation from IMU tilt changes (slosh impulse) */
		float excitation_col = excitation *
			(1.0f + 0.3f * sinf(col * 0.7f + wave_phase_offset));

		float accel = wave_speed * wave_speed * laplacian
			 - wave_damping * wave_v[col]
			 + excitation_col;

		wave_v[col] += accel * dt;
		wave_h[col] += wave_v[col] * dt;

		/* Clamp wave amplitude */
		if (wave_h[col] > 1.5f) wave_h[col] = 1.5f;
		if (wave_h[col] < -1.5f) wave_h[col] = -1.5f;
	}

	/* Drift phase for organic secondary ripples */
	for (int col = 0; col < COLS; col++) {
		wave_phase[col] += 0.03f + 0.01f * fabsf(wave_v[col]);
		if (wave_phase[col] > 200.0f) {
			wave_phase[col] -= 200.0f;
		}
	}
}

/* Frame renderer
 * water_h : current water surface height (in rows; 0=bottom, ROWS=top)
 * wave_t : accumulated time, drives wave animation
 * flipped : true = board flipped, mirror row direction
 * col_pal : per-column palette index
 * wave_phase_offset: wave phase offset controlled by pitch
 *
 * Rendering enhancements (inspired by FLIP simulation visuals):
 * - Multi-octave waves: 3 octaves interfere for an organic surface
 * - Dynamic foam: thicker foam where waves are steep (denser particles -> brighter)
 * - Depth coloring: deeper areas are dark blue, shallower are teal (density->color)
 * - Caustic sparkles: bright spots where wave crests converge (light focusing)
 */
static void render(float water_h, float wave_t, bool flipped,
		 const uint8_t col_pal[], float wave_phase_offset,
		 const float col_wave_h[])
{
	for (int col = 0; col < COLS; col++) {
		/* Multi-octave wave surface
		 * 3 frequency layers create an organic surface (like FLIP particle surfaces).
		 * - Primary: low freq, large amplitude (water level + per-column wave state)
		 * - Secondary: medium freq & amplitude (time-driven)
		 * - Ripple: high freq, small amplitude (phase-offset-driven)
		 */
		float w0 = water_h + col_wave_h[col];
		float w1 = 0.45f * sinf(wave_t * 2.8f + col * 1.05f + wave_phase_offset);
		float w2 = 0.22f * sinf(wave_t * 5.1f + col * 0.70f + wave_phase[col]);
		float w3 = 0.10f * sinf(wave_t * 9.3f + col * 1.90f - wave_phase_offset * 0.5f);

		float surf = w0 + w1 + w2 + w3;
		surf = fmaxf(0.5f, fminf((float)ROWS - 0.5f, surf));

		/* Wave steepness at this column -- drives foam width */
		float steepness = fabsf(2.8f * 0.45f * cosf(wave_t * 2.8f + col * 1.05f + wave_phase_offset)
				 + 5.1f * 0.22f * cosf(wave_t * 5.1f + col * 0.70f + wave_phase[col]));
		float foam_width = 0.3f + 0.35f * steepness;

		const struct led_rgb *p = &palette[col_pal[col]];

		/* Surface glow: brighter at wave crest */
		float crest_factor = 0.6f + 0.4f * ((surf - water_h + 1.0f) / 2.0f);
		if (crest_factor > 1.0f) crest_factor = 1.0f;
		if (crest_factor < 0.0f) crest_factor = 0.0f;

		struct led_rgb csurface = {
			.r = u8clamp((int)(p->r * crest_factor) + 65),
			.g = u8clamp((int)(p->g * crest_factor) + 90),
			.b = u8clamp((int)(p->b * crest_factor) + 85),
		};
		struct led_rgb cfoam = {
			.r = u8clamp((int)(p->r * 0.3f) + 170),
			.g = u8clamp((int)(p->g * 0.3f) + 170),
			.b = u8clamp((int)(p->b * 0.3f) + 170),
		};

		/* Caustic sparkle: bright spots at wave convergence */
		float caustic = 0.0f;
		if (col > 0 && col < COLS - 1) {
			float curvature = col_wave_h[col - 1] + col_wave_h[col + 1]
					- 2.0f * col_wave_h[col];
			caustic = fmaxf(0.0f, -curvature * 1.5f);
		}

		/* Determine if this column is physically reversed (serpentine odd columns) */
		bool col_rev = (MATRIX_SERPENTINE &&
				((MATRIX_COL0_DIR == 1) ? (col % 2 != 0)
						 : (col % 2 == 0)));
		/* Board flipped XOR column reversed -> final physical direction */
		bool want_rev = flipped ^ col_rev;

		for (int row = 0; row < ROWS; row++) {
			int phys = want_rev ? (ROWS - 1 - row) : row;
			float pr = (float)phys + 0.5f;
			struct led_rgb c;

			if (pr < surf - foam_width) {
				/* Under water: depth-based color modulation
				 * Deeper pixels get more saturated blue, shallower
				 * get brighter teal -- inspired by FLIP density->color
				 */
				float depth = surf - pr;
				float depth_norm = fminf(1.0f, depth / (float)ROWS);
				float depth_factor = 1.0f - 0.35f * depth_norm;

				c.r = u8clamp((int)(p->r * depth_factor));
				c.g = u8clamp((int)(p->g * depth_factor));
				c.b = u8clamp((int)(p->b * (1.0f + 0.2f * depth_norm)));

				/* Caustic sparkle on underwater pixels near crests */
				if (caustic > 0.0f && depth < 1.5f) {
					float sparkle = caustic * (1.0f - depth / 1.5f)
						 * (0.7f + 0.3f * sinf(wave_t * 15.0f + col * 3.0f + row));
					c.r = u8clamp(c.r + (int)(sparkle * 80.0f));
					c.g = u8clamp(c.g + (int)(sparkle * 80.0f));
					c.b = u8clamp(c.b + (int)(sparkle * 60.0f));
				}
			} else if (pr < surf) {
				/* Gradient zone: water body -> surface glow */
				float t = (pr - (surf - foam_width)) / foam_width;
				c.r = u8clamp((int)(p->r + t * (csurface.r - p->r)));
				c.g = u8clamp((int)(p->g + t * (csurface.g - p->g)));
				c.b = u8clamp((int)(p->b + t * (csurface.b - p->b)));
			} else if (pr < surf + 0.5f) {
				/* Foam layer at surface */
				float ft = (pr - surf) / 0.5f;
				float foam_fade = 1.0f - ft;
				c.r = u8clamp((int)(cfoam.r * foam_fade));
				c.g = u8clamp((int)(cfoam.g * foam_fade));
				c.b = u8clamp((int)(cfoam.b * foam_fade));

				/* Extra sparkle at the foam line */
				float sparkle = steepness * foam_fade *
					(0.6f + 0.4f * sinf(wave_t * 20.0f + col * 4.0f));
				c.r = u8clamp(c.r + (int)(sparkle * 80.0f));
				c.g = u8clamp(c.g + (int)(sparkle * 80.0f));
				c.b = u8clamp(c.b + (int)(sparkle * 60.0f));
			} else {
				c.r = 0; c.g = 0; c.b = 0;
			}

			set_pixel(col, row, c);
		}
	}
}

int main(void)
{
	const struct device *imu = DEVICE_DT_GET(IMU_NODE);
	const struct device *strip = DEVICE_DT_GET(STRIP_NODE);

	/* 0) Self-confirm the running mcuboot image so it becomes permanent.
	 * Without this the uploaded image stays in "test" state and mcuboot
	 * reverts (or fails to boot cleanly) after the next reset -- which is
	 * what made the USB serial port and the LED matrix both disappear
	 * after flashing (same fix as 8_AI_Wakeword). */
	(void)boot_write_img_confirmed();

	if (enable_imu_power() < 0) {
		return 1;
	}

	if (!device_is_ready(imu)) {
		int ret = device_init(imu);
		if (ret < 0 && ret != -EALREADY) {
			printk("[OCEAN] IMU init failed: %d\r\n", ret);
			return 1;
		}
	}
	if (!device_is_ready(imu)) {
		printk("[OCEAN] IMU not ready -- check power overlay / Sense variant\r\n");
		return 1;
	}

	struct sensor_value odr = { .val1 = 104, .val2 = 0 };
	sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ,
			SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
	sensor_attr_set(imu, SENSOR_CHAN_GYRO_XYZ,
			SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);

	if (!device_is_ready(strip)) {
		printk("[OCEAN] LED strip not ready\r\n");
		return 1;
	}

	/* Initialize per-column wave state */
	for (int i = 0; i < COLS; i++) {
		wave_h[i] = 0.0f;
		wave_v[i] = 0.0f;
		wave_phase[i] = simple_rand() * (1.0f / 4294967295.0f) * 6.28f;
	}

	/* Pick a random initial color for each column */
	uint8_t col_pal[COLS];
	for (int i = 0; i < COLS; i++) {
		col_pal[i] = (uint8_t)(simple_rand() % PALETTE_N);
	}

	float smooth_h = WATER_CENTER;
	float wave_t = 0.0f;
	uint32_t ctick = 0;

	printk("\r\nXIAO nRF54LM20B Electronic Ocean (IMU + 6x10 WS2812)\r\n");
	printk("water range %.1f ~ %.1f / %d rows -- tilt me\r\n\r\n",
		(double)WATER_MIN, (double)WATER_MAX, ROWS);

	while (1) {
		struct sensor_value ax_v, ay_v, az_v;

		sensor_sample_fetch(imu);
		sensor_channel_get(imu, SENSOR_CHAN_ACCEL_X, &ax_v);
		sensor_channel_get(imu, SENSOR_CHAN_ACCEL_Y, &ay_v);
		sensor_channel_get(imu, SENSOR_CHAN_ACCEL_Z, &az_v);

		float ax = ax_v.val1 + ax_v.val2 * 1e-6f;
		float ay = ay_v.val1 + ay_v.val2 * 1e-6f;
		float az = az_v.val1 + az_v.val2 * 1e-6f;

		float g = sqrtf(ax * ax + ay * ay + az * az);
		if (g < 1.0f) { g = 9.81f; }

		/* Flipped hysteresis: +/-1.5 m/s^2 dead zone to prevent flickering when az crosses zero */
		static bool flipped = false;
		if (!flipped && az > 1.5f) { flipped = true; }
		if ( flipped && az < -1.5f) { flipped = false; }

		/* When az is too small the board is near-vertical; roll/pitch unstable -- clamp to minimum */
		float az_safe = (fabsf(az) > 2.0f) ? az : (az >= 0 ? 2.0f : -2.0f);

		float pitch = atan2f(ax, sqrtf(ay * ay + az_safe * az_safe));
		float roll = atan2f(ay, sqrtf(ax * ax + az_safe * az_safe));

		float pitch_norm = pitch / (M_PI / 2.0f);
		float roll_norm = roll / (M_PI / 2.0f);

		/* Water slosh physics
		 * Simulate fluid inertia (inspired by FLIP particle momentum conservation)
		 *
		 * When tilted, the water surface does not snap to target instantly:
		 * 1. Spring force proportional to angle error (elastic restoration)
		 * 2. Has velocity (momentum)
		 * 3. Subject to damping (viscous decay)
		 * 4. Overshoots and sloshes back (oscillation)
		 */
		float half_range = (WATER_MAX - WATER_MIN) / 2.0f;
		float static_target = WATER_CENTER + roll_norm * half_range;

		/* Jerk detection: rapid tilt change excites slosh */
		float tilt_jerk = (roll_norm - slosh_prev_tilt) * 50.0f; /* 50 Hz effective */
		slosh_prev_tilt = roll_norm;

		/* Slosh driving force: spring toward target + impulse from jerk */
		const float slosh_spring = 2.8f; /* spring constant */
		const float slosh_damping = 3.0f; /* viscous damping */
		const float slosh_jerk_gain = 0.10f; /* how much jerk excites slosh */

		float spring_force = slosh_spring * (static_target - smooth_h);
		float damping_force = -slosh_damping * slosh_velocity;
		float jerk_force = slosh_jerk_gain * tilt_jerk;

		float slosh_accel = spring_force + damping_force + jerk_force;
		slosh_velocity += slosh_accel * 0.02f; /* dt = 20ms */
		smooth_h += slosh_velocity * 0.02f;

		/* Clamp to valid range, kill velocity pushing past boundaries */
		if (smooth_h < WATER_MIN) {
			smooth_h = WATER_MIN;
			if (slosh_velocity < 0.0f) slosh_velocity = 0.0f;
		}
		if (smooth_h > WATER_MAX) {
			smooth_h = WATER_MAX;
			if (slosh_velocity > 0.0f) slosh_velocity = 0.0f;
		}
		float target_h = static_target;

		float wave_phase_offset = pitch_norm * 2.0f;

		/* Wave simulation step
		 * Slosh velocity drives wave excitation (faster = taller waves).
		 * Analogous to FLIP particles driven by gravity to produce wave surfaces.
		 */
		float wave_excitation = slosh_velocity * 1.2f
				 + tilt_jerk * 0.06f;
		wave_step(0.02f, wave_excitation, wave_phase_offset);

		/* Color cycling
		 * Cycle one column color every ~800ms for slow ocean color shifts.
		 */
		if (++ctick >= 40) {
			ctick = 0;
			uint32_t r = simple_rand();
			col_pal[r % COLS] = (uint8_t)((r >> 8) % PALETTE_N);
		}

		/* Print IMU data every 25 frames (~500ms) */
		static uint32_t log_tick;
		if (++log_tick >= 25) {
			log_tick = 0;
			printk("ax=%7.3f ay=%7.3f az=%7.3f m/s^2 | "
				"water=%.2f (target=%.2f) vel=%.3f %s\r\n",
				(double)ax, (double)ay, (double)az,
				(double)smooth_h, (double)target_h,
				(double)slosh_velocity,
				flipped ? "[FLIP]" : "");
		}

		wave_t += 0.03f;
		render(smooth_h, wave_t, flipped, col_pal, wave_phase_offset, wave_h);

		int rc = led_strip_update_rgb(strip, pixels, NUM_LEDS);
		if (rc < 0) {
			printk("[OCEAN] LED strip update failed: %d\r\n", rc);
		}

		k_sleep(K_MSEC(20));
	}

	return 0;
}
