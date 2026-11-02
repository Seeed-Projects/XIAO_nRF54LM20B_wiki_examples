/*
 * XIAO nRF54LM20B -- RTC demo 1: timestamp + System OFF + GRTC alarm wake
 * ----------------------------------------------------------------------------
 * Port of the official XIAO nRF54LM20A wiki demo (rtc-main.c) to the
 * nRF54LM20B:
 *
 *   - on power-up the RTC starts counting from the BUILD time (__DATE__ /
 *     __TIME__), no external RTC module needed (on-chip GRTC);
 *   - the current timestamp is printed once per second;
 *   - after ~10 s of runtime the board enters System OFF, and a GRTC alarm
 *     wakes it ~5 s later -- timekeeping continues across the power-off via
 *     the retained-memory reference point (GRTC SYSCOUNTER keeps running).
 *
 * Differences vs the 20A original (only what the 20B board requires):
 *   - main() self-confirms the mcuboot image at startup (like the other
 *     projects in this repo, same proven workflow);
 *   - \r\n line endings and a 20B banner (printk console, no other change);
 *   - 0.0.121: the console suspend before System OFF (wiki original) was
 *     removed -- it deadlocks on the 20B (see comment in the poweroff path).
 *
 * The retained-memory overlay is the wiki overlay verbatim: 20A and 20B
 * share nrf54lm20_a_b.dtsi, so the SRAM layout (511 KB at 0x20000000, top
 * 4 KB carved out at 0x2007ec00) is identical.
 *
 * Source: https://wiki.seeedstudio.com/cn/xiao_nrf54lm20a_with_onboard/#rtc
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/devicetree.h>
#include <stdint.h>

/*
 * Retained memory data structure.
 * This data survives System OFF but is lost on power-cycle or pin reset.
 * CRC32 validation ensures integrity detection.
 */
struct retained_data {
	int64_t  ref_unix_time;   /* Reference UNIX timestamp at last save */
	uint64_t ref_grtc_ticks;  /* GRTC counter value at reference point */
	uint32_t boots;           /* Total boot count (cold + warm) */
	uint32_t off_count;       /* System OFF entry count */
	uint32_t crc;             /* CRC32 for structure validation */
};

/* Retained memory device */
#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(retainedmemdevice))
static const struct device *retained_dev =
	DEVICE_DT_GET(DT_ALIAS(retainedmemdevice));
static bool retained_available;

#define RETAINED_CRC_OFFSET  offsetof(struct retained_data, crc)
#define RETAINED_CHECKED_SIZE \
	(RETAINED_CRC_OFFSET + sizeof(((struct retained_data *)0)->crc))
#endif

static struct retained_data retained;

/* Reset causes that indicate a cold boot (not a System OFF wakeup) */
#define COLD_BOOT_MASK (RESET_PIN | RESET_SOFTWARE | RESET_POR | RESET_DEBUG)

/* System OFF sleep duration in seconds */
#define SLEEP_DURATION_SEC 5

/* Number of seconds between System OFF entries */
#define SYSTEM_OFF_INTERVAL_SEC 10

/* GRTC ticks per second (SYSCOUNTER runs at 1 MHz on nRF54L) */
#define GRTC_TICKS_PER_SEC 1000000ULL

/*
 * Convert broken-down UTC time to UNIX timestamp.
 * Pure integer arithmetic, no library dependencies.
 * Uses Howard Hinnant's date algorithm.
 */
static int64_t tm_to_unix(int year, int mon, int mday,
			  int hour, int min, int sec)
{
	/* mon: 1..12, mday: 1..31 */
	if (mon <= 2) {
		year--;
		mon += 12;
	}

	int32_t era = (year >= 0 ? year : year - 399) / 400;
	int32_t yoe = year - era * 400;
	int32_t doy = (153 * (mon - 3) + 2) / 5 + mday - 1;
	int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	int64_t days = era * 146097LL + doe - 719468LL;

	return days * 86400LL + hour * 3600LL + min * 60LL + sec;
}

/*
 * Convert UNIX timestamp to broken-down UTC time.
 * Pure integer arithmetic, no library dependencies.
 * Uses Howard Hinnant's date algorithm.
 */
static void unix_to_tm(int64_t ts, int *year, int *mon, int *mday,
		       int *hour, int *min, int *sec, int *wday)
{
	int64_t days = ts / 86400LL;
	int64_t rem = ts % 86400LL;

	if (rem < 0) {
		days--;
		rem += 86400LL;
	}

	*hour = (int)(rem / 3600);
	rem %= 3600;
	*min  = (int)(rem / 60);
	*sec  = (int)(rem % 60);

	/* Monday = 0 for the algorithm, adjust to Sunday = 0 later */
	int32_t wday_tmp = (int32_t)((days + 4) % 7);
	if (wday_tmp < 0) {
		wday_tmp += 7;
	}
	*wday = wday_tmp;

	days += 719468LL;
	int32_t era = (int32_t)((days >= 0 ? days : days - 146096LL) / 146097LL);
	int32_t doe = (int32_t)(days - era * 146097LL);
	int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int32_t y   = yoe + era * 400;
	int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int32_t mp  = (5 * doy + 2) / 153;

	*mday = doy - (153 * mp + 2) / 5 + 1;
	*mon  = mp + (mp < 10 ? 3 : -9);
	*year = y + (*mon <= 2 ? 1 : 0);
}

/*
 * Validate retained data using CRC32 residue check.
 * Returns true if the retained data is valid (warm boot from System OFF).
 */
#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(retainedmemdevice))
static bool retained_validate(void)
{
	int rc = retained_mem_read(retained_dev, 0,
				   (uint8_t *)&retained, sizeof(retained));
	if (rc < 0) {
		printk("retained_mem_read failed (rc=%d), "
		       "treating as cold boot\r\n", rc);
		memset(&retained, 0, sizeof(retained));
		return false;
	}

	/* CRC-32/ISO-HDLC post-final-xor residue for a valid message+crc */
	const uint32_t residue = 0x2144df1c;
	uint32_t crc = crc32_ieee((const uint8_t *)&retained,
				  RETAINED_CHECKED_SIZE);

	if (crc != residue) {
		memset(&retained, 0, sizeof(retained));
		return false;
	}

	return true;
}

static void retained_update(void)
{
	if (!retained_available) {
		return;
	}

	uint32_t crc = crc32_ieee((const uint8_t *)&retained,
				  RETAINED_CRC_OFFSET);
	retained.crc = sys_cpu_to_le32(crc);

	int rc = retained_mem_write(retained_dev, 0,
				    (uint8_t *)&retained, sizeof(retained));
	if (rc < 0) {
		printk("retained_mem_write failed (rc=%d)\r\n", rc);
	}
}
#else
static bool retained_validate(void)
{
	memset(&retained, 0, sizeof(retained));
	return false;
}

static void retained_update(void) { /* no-op */ }
#endif /* DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(retainedmemdevice)) */

/*
 * Parse a 2-digit decimal number from a string pointer.
 */
static int parse_2digits(const char *s)
{
	return (s[0] - '0') * 10 + (s[1] - '0');
}

/*
 * Parse build date/time into UNIX timestamp.
 * __DATE__ format: "Mmm dd yyyy"  e.g. "May 18 2026"
 * __TIME__ format: "hh:mm:ss"     e.g. "18:50:12"
 *
 * Uses NO library functions - direct character parsing only,
 * to avoid any newlib bare-metal runtime issues.
 */
static int64_t build_time_to_unix(void)
{
	const char *d = __DATE__;
	const char *t = __TIME__;
	int mon, mday, year, hour, min, sec;

	/* Parse month from first 3 characters */
	switch (d[0]) {
	case 'J':
		mon = (d[1] == 'a') ? 1 : ((d[2] == 'n') ? 6 : 7);
		break;
	case 'F': mon = 2;  break;
	case 'M': mon = (d[2] == 'r') ? 3 : 5; break;
	case 'A': mon = (d[1] == 'p') ? 4 : 8; break;
	case 'S': mon = 9;  break;
	case 'O': mon = 10; break;
	case 'N': mon = 11; break;
	case 'D': mon = 12; break;
	default:  mon = 1;  break;
	}

	/* Parse day: __DATE__[4..5], handle leading space (e.g. " 1") */
	if (d[4] == ' ') {
		mday = d[5] - '0';
	} else {
		mday = parse_2digits(&d[4]);
	}

	/* Parse year: __DATE__[7..10] */
	year = (d[7] - '0') * 1000 + (d[8] - '0') * 100
	     + (d[9] - '0') * 10   + (d[10] - '0');

	/* Parse time: __TIME__ = "hh:mm:ss" */
	hour = parse_2digits(&t[0]);
	min  = parse_2digits(&t[3]);
	sec  = parse_2digits(&t[6]);

	return tm_to_unix(year, mon, mday, hour, min, sec);
}

/*
 * Synchronize the reference point: record current UNIX time and GRTC ticks.
 */
static void sync_reference_point(int64_t unix_time)
{
	retained.ref_unix_time  = unix_time;
	retained.ref_grtc_ticks = z_nrf_grtc_timer_read();
}

/*
 * Get the current UNIX timestamp from GRTC counter.
 */
static int64_t get_current_unix_time(void)
{
	uint64_t now_ticks = z_nrf_grtc_timer_read();
	uint64_t tick_diff = now_ticks - retained.ref_grtc_ticks;

	return retained.ref_unix_time + (int64_t)(tick_diff / GRTC_TICKS_PER_SEC);
}

int main(void)
{
	uint32_t reset_cause;
	bool retained_valid;
	int64_t current_time;
	int year, mon, mday, hour, min, sec, wday;

	/* 0) Self-confirm the running mcuboot image so it becomes permanent. */
	(void)boot_write_img_confirmed();

	printk("\r\n=== XIAO nRF54LM20B RTC Demo ===\r\n");

	/* Check retained memory device readiness */
#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(retainedmemdevice))
	if (!device_is_ready(retained_dev)) {
		printk("Retained memory device not ready\r\n");
		retained_available = false;
	} else {
		retained_available = true;
		printk("Retained memory device ready\r\n");
	}
#endif

	/* Check retained data integrity (warm boot detection) */
	printk("Validating retained data...\r\n");
	retained_valid = retained_validate();

	/* Get hardware reset cause */
	if (hwinfo_get_reset_cause(&reset_cause) == 0) {
		printk("Reset cause: 0x%08X\r\n", reset_cause);
	} else {
		printk("Could not read reset cause, assuming cold boot\r\n");
		reset_cause = RESET_PIN;
	}

	if (retained_valid && !(reset_cause & COLD_BOOT_MASK)) {
		/*
		 * Warm boot: woke from System OFF.
		 * GRTC kept running, recover current time from reference point.
		 */
		current_time = get_current_unix_time();
		retained.boots += 1;

		printk("Woke from System OFF\r\n");
		printk("Boot count: %u, Off count: %u\r\n",
		       retained.boots, retained.off_count);
	} else {
		/*
		 * Cold boot: power-on, pin reset, or debugger reset.
		 * Initialize time from build date/time.
		 */
		printk("Cold boot, initializing time from build date...\r\n");

		retained.boots = 1;
		retained.off_count = 0;

		printk("Parsing build time...\r\n");
		current_time = build_time_to_unix();
		printk("Build time parsed: %lld\r\n",
		       (long long)current_time);

		printk("Syncing reference point...\r\n");
		sync_reference_point(current_time);
		printk("Reference point synced\r\n");
	}

	/* Print initial time */
	unix_to_tm(current_time,
		   &year, &mon, &mday, &hour, &min, &sec, &wday);

	printk("Current time: %04d-%02d-%02d %02d:%02d:%02d (UTC)\r\n",
	       year, mon, mday, hour, min, sec);

	/* Clear reset cause for next cycle */
	hwinfo_clear_reset_cause();

	while (1) {
		/* Read current time from GRTC */
		current_time = get_current_unix_time();
		unix_to_tm(current_time,
			   &year, &mon, &mday, &hour, &min, &sec, &wday);

		printk("[%lld] %04d-%02d-%02d %02d:%02d:%02d\r\n",
		       (long long)current_time,
		       year, mon, mday, hour, min, sec);

		/*
		 * Enter System OFF after SYSTEM_OFF_INTERVAL_SEC of runtime
		 * in this boot session.
		 */
		static uint32_t session_start_time;
		static bool session_started;

		if (!session_started) {
			session_start_time = (uint32_t)current_time;
			session_started = true;
		}

		if ((uint32_t)current_time - session_start_time >=
		    SYSTEM_OFF_INTERVAL_SEC) {

			printk("Preparing to enter System OFF "
			       "for %d seconds...\r\n",
			       SLEEP_DURATION_SEC);

			/* Update the reference point before sleep */
			sync_reference_point(current_time);
			retained.off_count += 1;

			/* Save to retained memory (if available) */
			retained_update();

			/* Configure GRTC wakeup after SLEEP_DURATION_SEC */
			int err = z_nrf_grtc_wakeup_prepare(
				SLEEP_DURATION_SEC * USEC_PER_SEC);

			if (err < 0) {
				printk("GRTC wakeup prepare failed "
				       "(err %d)\r\n", err);
				k_sleep(K_SECONDS(1));
				continue;
			}

			/*
			 * 0.0.121: no console suspend here.
			 * Suspending the CDC console before sys_poweroff()
			 * deadlocks on this board (USB stays enumerated, SoC
			 * never powers off, no reset -- reproduced 2x on
			 * 2026-10-27). It is not needed: System OFF powers
			 * the USB controller down with the SoC anyway, and
			 * keeping the console live makes the "ERROR:
			 * sys_poweroff() returned!" line observable.
			 */
			printk("Entering System OFF...\r\n");

			/* Small delay to let the console flush */
			k_sleep(K_MSEC(100));

			sys_poweroff();

			/* Should not reach here */
			printk("ERROR: sys_poweroff() returned!\r\n");
			break;
		}

		k_sleep(K_SECONDS(1));
	}

	return 0;
}
