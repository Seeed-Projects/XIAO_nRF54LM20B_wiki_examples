/*
 * sdk-edge-ai v3 / PlatformIO Zephyr-framework compatibility shim.
 * ----------------------------------------------------------------------------
 * The framework build defines BOTH NRF54LM20A_XXAA (pulled in implicitly,
 * because SOC_NRF54LM20B selects SOC_COMPATIBLE_NRF54LM20A) and
 * NRF54LM20B_XXAA. <mdk/nrf.h> tests the LM20A variant first in its #elif
 * chain, so every translation unit that includes it ends up with the
 * nrf54lm20a peripheral definitions — which predate the AXONS NPU
 * peripheral that nrf_axon_platform_zephyr.c (Edge AI runtime v3) needs
 * (NRF_AXONS_Type / AXONS_ENABLE_EN_* are only in nrf54lm20b_types.h).
 *
 * Fix: undefine the LM20A variant so <mdk/nrf.h> selects the LM20B headers.
 * This header is force-included ONLY into that single source file (see
 * zephyr/CMakeLists.txt), so nothing else in the build changes.
 *
 * If the framework is ever fixed to resolve the LM20B headers on its own,
 * this #undef becomes a harmless no-op.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#undef NRF54LM20A_XXAA
