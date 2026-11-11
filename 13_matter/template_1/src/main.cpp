/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(app, CONFIG_CHIP_APP_LOG_LEVEL);

int main()
{
	LOG_INF("Matter application starting");

	CHIP_ERROR err = AppTask::Instance().StartApp();

	LOG_ERR("Matter startup failed with code %" CHIP_ERROR_FORMAT, err.Format());

	/* When the LED is available, blink it rapidly to signal the error (the loop's k_sleep yields the CPU,
	 * so USB CDC / shell remain diagnosable); if the LED was never initialized (failure before Board::Init),
	 * this returns immediately and we fall through to the sleep loop below.
	 */
	AppTask::Instance().IndicateFatalError();

	/* Keep USB CDC and the shell alive when Matter startup fails so the board
	 * remains diagnosable over its serial console instead of halting here.
	 */
	while (true) {
		k_sleep(K_SECONDS(1));
	}
}
