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

	/* LED 可用时持续快闪报错（内部自带 k_sleep 让出 CPU，USB CDC / shell 仍可诊断）；
	 * LED 未初始化（Board::Init 之前就失败）时直接返回，落到下面的 sleep 循环。
	 */
	AppTask::Instance().IndicateFatalError();

	/* Keep USB CDC and the shell alive when Matter startup fails so the board
	 * remains diagnosable over its serial console instead of halting here.
	 */
	while (true) {
		k_sleep(K_SECONDS(1));
	}
}
