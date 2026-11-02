# XIAO nRF54LM20B Sense — 5 类 IMU 手势识别（Axon NPU）移植与部署实录

> 本文档记录在 PlatformIO + Zephyr 工程中完成 Nordic Edge AI 模型部署的全过程，
> 可作为编写教程的底稿。所有数字均为真实构建/实测结果。

---

## 一、总体架构

```
Edge AI Studio（云端训练）
        │  导出部署包：模型 C 代码 + prj_example.conf
        ▼
src/nrf_edgeai_generated/nrf_edgeai_user_model.c   ← 生成物，一行不改
        │  被 app 调用
        ▼
src/main.c + src/imu_lsm6dsl.c                     ← 手写应用
        │
        ▼
PlatformIO + Zephyr 4.4.0 + sdk-edge-ai v3（Axon NPU 运行时）
```

| 项目 | 说明 |
|---|---|
| 板子 | XIAO nRF54LM20B **Sense**（板载 LSM6DS3TR-C 六轴 IMU、nPM1300 PMIC） |
| 模型 | Edge AI Lab solution 96964 |
| 类别 | 5 类：IDLE / SWIPE_LEFT / SWIPE_RIGHT / ROTATION_CW / ROTATION_CCW |
| 输入 | 6 通道（ax, ay, az, gx, gy, gz），FLOAT32，**micro-SI 单位** |
| 窗口 | window = 200，shift = 33（78 个特征） |
| 网络 | Dense 64 → 32 → 16 → Softmax 5，跑在 Axon NPU 上 |
| 运行时 | sdk-edge-ai v3.0.0-6（`~/XIAO-EdgeAI/tools/sdk-edge-ai`，通过 `XIAO_EDGE_AI_DIR` 环境变量注册为 Zephyr 外部模块；框架自带 v2 不用） |

---

## 二、模型侧（纯搬运，零修改）

1. Edge AI Studio 导出部署包，`nrf_edgeai_user_model.c/.h` 放进 `src/nrf_edgeai_generated/`。
2. 部署包里的 `prj_example.conf` 抄两条进工程 `zephyr/prj.conf`（Axon 缓冲大小必须与模型匹配）：

```ini
CONFIG_NRF_AXON_INTERLAYER_BUFFER_SIZE=80
CONFIG_NRF_AXON_PSUM_BUFFER_SIZE=0
```

**红线约束（教程必须强调）：**

- 模型权重、特征算法、输入归一化逻辑 **一律不改**；
- 输入通道顺序与单位必须与训练 CSV 完全一致，错了模型全废；
- 置信度（max probability）≠ 真实准确率，不能混为一谈。

---

## 三、应用代码（全部手写）

### `src/main.c` 职责链

1. **启动自检**：调 `nrf_edgeai_input_window_size` / `model_outputs_num` /
   `is_runtime_compatible` 等真实 API 校验模型几何（6 通道、窗口 200、5 输出），
   不匹配即报错停住，绝不静默失败。
2. **100 Hz 非阻塞采样**：信号量驱动，IMU 数据一到就收，不用 sleep 轮询。
3. **单位换算**：加速度 → **µm/s²**、陀螺仪 → **µrad/s**（× 1000000.0f），
   与训练 CSV 的 micro-SI 单位对齐。
4. **滑窗填充**：window = 200、shift = 33，向 `nrf_edgeai_input_window` 填数据。
5. **推理 + 输出**：调真实 `nrf_edgeai` API，输出 5 类结果 + 置信度 +
   `k_cycle_get_32()` 实测推理耗时（**实测 1.25 ms/次**）。
6. **交互**：识别到手势 LED 闪一下（k_timer one-shot）；串口输出 3 s 节流
   （`CONFIG_GESTURE_DEMO_REPORT_INTERVAL_MS`）；`CONFIG_GESTURE_DEMO_DEBUG`
   打开原始推理数据；banner 等 DTR 拉高才打。
7. **MCUboot 自确认**：启动即 `boot_write_img_confirmed()`，
   否则上传的测试镜像重启时会被 bootloader 回滚。

### `src/imu_lsm6dsl.c`

LSM6DS3TR-C 驱动（`st,lsm6dsl` 兼容层，走板子 `imu0` 别名 + bit-bang I2C）。

---

## 四、两个平台级集成坑（移植最难的部分）

### 坑 1：PlatformIO 的 Zephyr 构建根本不跑 ninja

PIO 的构建流程是：cmake configure → 从 **CMake file API 读源码清单** →
用 SCons 自己编译。**模块 CMakeLists 里 `add_custom_command()` 的产物永远不会被生成。**

sdk-edge-ai v3 的 banner（`edge_ai_banner.c`）依赖 `edge_ai_version.h` /
`edge_ai_commit.h`，两者都是 custom command 生成的 → pio 构建永远编译失败
（直接 cmake + ninja 则一切正常，极具迷惑性）。

**修法**（`zephyr/CMakeLists.txt` 中 configure 阶段生成，见 "generate
version/commit headers" 块）：

- `edge_ai_version.h`：用 `execute_process` 调 Zephyr 自带的
  `gen_version_h.cmake`（与模块原调用完全一致）；
- `edge_ai_commit.h`：上游脚本本机缺失，按 NCS 上游格式复刻，
  取模块仓库真实 git 短哈希（`git rev-parse HEAD` 截 12 位）。

> 经验：凡是要接入带 custom command 的 Zephyr 模块，都必须在 configure
> 阶段补生成；往 build 目录里手工塞文件无效（build 目录会被整擦重建）。

### 坑 2：MDK 双定义，Axon NPU 头编译不过

选 `SOC_NRF54LM20B` 时，因为 Kconfig 里 `select SOC_COMPATIBLE_NRF54LM20A`，
**NRF54LM20A_XXAA 与 NRF54LM20B_XXAA 会被同时定义**。`<mdk/nrf.h>` 先命中
A 分支，于是模块的 `nrf_axon_platform_zephyr.c` 里 LM20B 专属的 AXONS NPU
定义（`NRF_AXONS_Type`、`AXONS_ENABLE_EN_*` 等）全部缺失，编译失败。

**修法**：源文件级属性跨不了模块作用域，所以用 **target 级属性**——
把这一个 .c 从 zephyr target 的 `SOURCES` 属性里换出，替换成生成的 wrapper：

```c
/* nrf_axon_platform_wrapper.c（configure_file 生成） */
#include "<工程>/zephyr/mdk_lm20b_compat.h"   /* 就一句 #undef NRF54LM20A_XXAA */
#include "<sdk-edge-ai>/lib/axon/platform/src/zephyr/nrf_axon_platform_zephyr.c"
```

**SDK 源码一行没动**，只有这一个编译单元换了芯片头的选择。

### 小坑 3：生成模型头用 C11 `static_assert`

newlib 需要先见到 `<assert.h>` 才定义它：

```cmake
target_compile_options(app PRIVATE "-include" "assert.h")
```

---

## 五、devicetree（`zephyr/app.overlay`）

三条就够：

```dts
&imu_vdd { regulator-boot-on; };        /* nPM1300 LDO1 给 IMU 供电，开机即上电 */
&lsm6ds3tr_c {                          /* 延迟探测，等电源轨稳定后 app 再 device_init() */
    zephyr,deferred-init;
    status = "okay";
};
&axon { status = "okay"; };             /* 打开 NPU 节点 */
```

---

## 六、烧录链路（macOS，`./flash.sh` 一键）

这块板出厂 bootloader 是 **MCUboot + 固件加载器（BOOT_FIRMWARE_LOADER）** 架构，
与普通 MCUboot DFU 不同：slot1 里放的是出厂的 USB MCUmgr loader，
按住 B0 + 复位时 MCUboot 会链式加载它来接收固件、写入 slot0。

| 分区 | 偏移 | 大小 | 内容 |
|---|---|---|---|
| mcuboot | 0x0 | 24 KB | 出厂 bootloader（**不动它**） |
| slot0 (image-0) | 0x6000 | 1784 KB (0x1BE000) | **应用镜像（烧录目标）** |
| slot1 (image-1) | 0x1c4000 | 116 KB | 出厂 USB MCUmgr loader |
| storage | 0x1e1000 | 16 KB | 设置存储 |

### 三步流程（`flash.sh` 全自动化）

```bash
PORT=/dev/cu.usbmodem1101 ./flash.sh
```

1. **`pio run`** 构建（`XIAO_EDGE_AI_DIR` 环境变量指向 v3 模块）；
2. **imgtool 签名**：框架自带 `root-ed25519.pem` + `--pure`
   （此板 MCUboot 校验模式要求 IMAGE_TLV_PURE）+
   `--header-size 0x800 --align 16 --slot-size 0x1BE000`（与分区表严格对应）+
   `--version` **必须递增**；
3. **上传**：板子进 DFU（按住 **B0 (P0.09) → 点一下 RST → 松开 B0**），
   `nrfutil mcu-manager serial image-upload` 上传 → `serial reset` 复位启动。

### 教程必写的三个坑

- **1200 bps 软触发进 DFU 无效**（需要 app 实现配合，本板未实现），必须手动按键；
- **版本号不递增 → mcuboot 拒收**（"same version"）；
- **烧完 USB CDC 不出现**（偶发“串口消失”）：点一下 RST 即可——
  应用跑起来 CDC 自然枚举为 `XIAO_NRF54LM20B`。

---

## 七、验证结果（真实实测数据）

```
*** XIAO M20B Edge AI Gesture Demo ***
Board: XIAO nRF54LM20B Sense
Model: XIAO_M20B_Gesture_5Class (solution 96964)
Runtime: Nordic Edge AI / Axon NPU
[AI] Model initialized
[IMU] Sampling rate: 100 Hz (6ch, micro-SI, window 200 shift 33)
[AXON] NPU initialized
[AI] Gesture: ROTATION_CCW
[AI] Confidence: 0.62
[AI] Inference time: 1.248 ms
```

| 指标 | 值 |
|---|---|
| Flash 占用 | 140 744 B / 2 084 864 B（**6.8 %**） |
| RAM 占用 | 27 592 B / 524 288 B（**5.3 %**） |
| 推理耗时 | ~1.25 ms/次（Axon NPU） |
| USB CDC 稳定性 | 连续 10 分钟在线无掉线 |
| 串口 | 115200 baud，DTR 拉高后打 banner |

### 手势测试姿势（PRD 规定）

握住板子 **正面朝向使用者、USB 接口朝下**，依次做：

- 向左挥（SWIPE_LEFT） / 向右挥（SWIPE_RIGHT）
- 顺时针转腕（ROTATION_CW） / 逆时针转腕（ROTATION_CCW）
- 静置（IDLE）

看串口输出即可：

```bash
screen /dev/cu.usbmodem1101 115200   # 退出：Ctrl+A 再按 K
```

### 尚未验证（Not verified）

- 每类 20 次、共 100 次的量化混淆矩阵测试尚未执行；
- **置信度不能当作真实准确率**——只能说明模型对自己判断的确信程度。

---

## 附：文件清单

| 文件 | 角色 |
|---|---|
| `src/main.c` | 应用主逻辑（采样/滑窗/推理/LED/串口） |
| `src/imu_lsm6dsl.c` | LSM6DS3TR-C IMU 驱动 |
| `src/nrf_edgeai_generated/` | Edge AI Studio 生成物（不改） |
| `zephyr/CMakeLists.txt` | 模块接入 + configure 期头文件生成 + MDK wrapper |
| `zephyr/mdk_lm20b_compat.h` + `nrf_axon_platform_wrapper.c.in` | LM20B MDK 兼容 wrapper |
| `zephyr/app.overlay` | IMU 电源 / 延迟探测 / NPU 节点 |
| `zephyr/prj.conf` / `zephyr/Kconfig` | 应用配置 |
| `platformio.ini` | PIO 工程定义 |
| `flash.sh` | 构建 + 签名 + 上传一键脚本 |
