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

	/* 启动失败时让板载 LED 持续快闪报错；若 LED 尚未初始化（Board::Init 之前就失败）
	 * 则直接返回，交由调用方保持串口可诊断。
	 */
	void IndicateFatalError();

private:
	CHIP_ERROR Init();
};
