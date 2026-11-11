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
/* Single-flash timing: on 120ms, in-group gap 120ms, between-group pause 450ms for easy counting. */
constexpr uint32_t kFlashOnMs  = 120;
constexpr uint32_t kFlashOffMs = 120;
constexpr uint32_t kFlashGapMs = 450;
/* Fast-flash timing used to signal a failure. */
constexpr uint32_t kFatalOnMs  = 80;
constexpr uint32_t kFatalOffMs = 80;

/* Only set to true after Board::Init() succeeds, ensuring the failure path never touches an uninitialized LED. */
bool sLedReady = false;

/* Synchronously blink count times as a stage-progress indicator. Only called after Board::Init().
 * Uses Set() + k_sleep instead of Blink() so the count is deterministic and unaffected by async timers,
 * and Set() also stops the status-indication timer started during Board init to avoid conflicts.
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
	FlashLedTimes(1); /* stage 1: on-board peripherals (LED/button) ready */

	/* Register Matter event handler that controls the connectivity status LED based on the captured Matter network
	 * state. */
	ReturnErrorOnFailure(Nrf::Matter::RegisterEventHandler(Nrf::Board::DefaultMatterEventHandler, 0));
	LOG_INF("stage 2/3: matter event handler registered");
	FlashLedTimes(2); /* stage 2: Matter event handler registered */

	ReturnErrorOnFailure(Nrf::Matter::StartServer());
	LOG_INF("stage 3/3: matter server started, entering main loop");
	FlashLedTimes(3); /* stage 3: server started, about to enter advertising/main loop; LED then handed to the connectivity-status indicator */

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
