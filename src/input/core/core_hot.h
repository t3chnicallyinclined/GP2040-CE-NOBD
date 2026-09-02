#ifndef NOBD_CORE_HOT_H
#define NOBD_CORE_HOT_H
/*
 * CORE_HOT(fn) -- optional hot-path placement for the latency-critical stages.
 *
 * Portable no-op by default (host builds, and any platform that doesn't opt in). On the
 * RP2040 it resolves to the SDK's __not_in_flash_func, so the hottest core stages run from
 * SRAM instead of XIP flash -- no cache-miss jitter on the input path. Another platform
 * (e.g. the CH32H417) can predefine CORE_HOT before this header to use its own fast-memory
 * attribute.
 *
 * It never changes behavior -- only where the code lives -- so the differential/parity
 * fuzzer (host build, no-op) still proves the exact same function byte-for-byte.
 */
#ifndef CORE_HOT
#  if defined(PICO_RP2040)
#    include "pico.h"
#    define CORE_HOT(fn) __not_in_flash_func(fn)
#  else
#    define CORE_HOT(fn) fn
#  endif
#endif

#endif /* NOBD_CORE_HOT_H */
