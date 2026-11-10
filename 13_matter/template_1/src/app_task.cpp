/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"

#include "app/matter_init.h"
#include "app/task_executor.h"
#include "board/board.h"
#include "lib/core/CHIPError.h"
#include "lib/support/CodeUtils.h"

#include <setup_payload/OnboardingCodesUtil.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::DeviceLayer;

namespace {
/* 单次闪烁时序：亮 120ms，组内间隔 120ms，组间停顿 450ms 方便计数。 */
constexpr uint32_t kFlashOnMs  = 120;
constexpr uint32_t kFlashOffMs = 120;
constexpr uint32_t kFlashGapMs = 450;
/* 失败时报错的快闪时序。 */
constexpr uint32_t kFatalOnMs  = 80;
constexpr uint32_t kFatalOffMs = 80;

/* Board::Init() 成功后才置 true，确保失败路径不会去操作未初始化的 LED。 */
bool sLedReady = false;

/* 同步闪烁 count 次，用作阶段进度灯。仅在 Board::Init() 之后调用。
 * 用 Set() + k_sleep 而不是 Blink()，这样次数确定、不受异步 timer 影响，
 * 且 Set() 会停掉 Board 初始化时启动的状态指示 timer，避免冲突。
 */
void FlashLedTimes(uint8_t count)
{
	if (!sLedReady) {
		return;
	}
	Nrf::LEDWidget &led = Nrf::GetBoard().GetLED(Nrf::DeviceLeds::LED1);
	for (uint8_t i = 0; i < count; i++) {
		led.Set(true);
		k_sleep(K_MSEC(kFlashOnMs));
		led.Set(false);
		k_sleep(K_MSEC((i + 1 < count) ? kFlashOffMs : kFlashGapMs));
	}
}
} /* namespace */

CHIP_ERROR AppTask::Init()
{
	/* Initialize Matter stack */
	ReturnErrorOnFailure(Nrf::Matter::PrepareServer());

	if (!Nrf::GetBoard().Init()) {
		LOG_ERR("User interface initialization failed.");
		return CHIP_ERROR_INCORRECT_STATE;
	}
	sLedReady = true;
	LOG_INF("stage 1/3: board UI ready");
	FlashLedTimes(1); /* 阶段 1：板载外设（LED/按键）就绪 */

	/* Register Matter event handler that controls the connectivity status LED based on the captured Matter network
	 * state. */
	ReturnErrorOnFailure(Nrf::Matter::RegisterEventHandler(Nrf::Board::DefaultMatterEventHandler, 0));
	LOG_INF("stage 2/3: matter event handler registered");
	FlashLedTimes(2); /* 阶段 2：Matter 事件处理已注册 */

	ReturnErrorOnFailure(Nrf::Matter::StartServer());
	LOG_INF("stage 3/3: matter server started, entering main loop");
	FlashLedTimes(3); /* 阶段 3：服务器已启动，即将进入广播/主循环，之后 LED 交给连接状态指示 */

	return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::StartApp()
{
	ReturnErrorOnFailure(Init());

	while (true) {
		Nrf::DispatchNextTask();
	}

	return CHIP_NO_ERROR;
}

void AppTask::IndicateFatalError()
{
	if (!sLedReady) {
		return;
	}
	Nrf::LEDWidget &led = Nrf::GetBoard().GetLED(Nrf::DeviceLeds::LED1);
	while (true) {
		led.Set(true);
		k_sleep(K_MSEC(kFatalOnMs));
		led.Set(false);
		k_sleep(K_MSEC(kFatalOffMs));
	}
}
