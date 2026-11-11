/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include <platform/CHIPDeviceLayer.h>

struct k_timer;

class AppTask {
public:
	static AppTask &Instance()
	{
		static AppTask sAppTask;
		return sAppTask;
	};

	CHIP_ERROR StartApp();

	/* On startup failure, blink the on-board LED rapidly to signal the error;
	 * if the LED is not initialized yet (failure before Board::Init), just
	 * return and leave the serial console diagnosable for the caller.
	 */
	void IndicateFatalError();

private:
	CHIP_ERROR Init();
};
