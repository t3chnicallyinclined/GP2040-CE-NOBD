/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2023 Brian S. Stephan <bss@incorporeal.org>
 */

#ifndef TYPES_H_
#define TYPES_H_

#include <stdint.h>
#include "hardware/platform_defs.h"	// NUM_BANK0_GPIOS

// common types
#define	Pin_t		int32_t		// signed to accommodate for -1

// NOBD: a pin mask only needs 64 bits on a chip that has more than 32 GPIOs (RP2350B, 48).
// RP2040 and RP2350A have 30, so keep the mask one word wide there: the per-edge input path
// (debouncedGpio, the doorbell, the sync window, every pinMask test in Gamepad::read) stays a
// single 32-bit op on Cortex-M0+, exactly as before upstream widened it to uint64_t.
#if NUM_BANK0_GPIOS > 32
#define Mask_t		uint64_t
#else
#define Mask_t		uint32_t
#endif

#endif
