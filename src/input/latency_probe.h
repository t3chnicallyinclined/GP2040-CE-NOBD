/*
 * latency_probe -- instrument-free latency + jitter measurement, from the RP2040's own clock.
 *
 * Three timestamps off one 1us hardware timer (zero cross-device sync error):
 *   edge()   -- raw button edge, FIRST thing in the GPIO doorbell ISR                 (T0)
 *   report() -- the report is staged in USB DPRAM (ready to ship)                     (T1)
 *   wire()   -- the report actually SHIPPED on the USB wire (endpoint-complete IRQ)   (T2)
 *
 * Two measurements:
 *   stats()      = T1 - T0 = edge -> build  = the DEVICE compute (~1us fast path). min..max is the
 *                  device jitter. Excludes the host poll wait.
 *   wireStats()  = T2 - T0 = edge -> wire   = the TRUE controller latency (button -> USB wire). Its
 *                  AVERAGE is the ~500us USB-FS poll floor -- the number to compare against other
 *                  boards, measured on the wire with NO external rig. Calibrate T2 once against a
 *                  USB analyzer and it is ground truth from then on.
 *
 * State is tiny + volatile; edge()/wire() run in ISR context, report() on the loop or the ISR.
 *
 * TE_LATENCY_MEASURE gates the whole instrument. OFF (default) = production: every call below is
 * an inline no-op, so the probe, the report edge-time stamp, and the OLED D/W HUD compile out to
 * nothing and the shipping fast path carries zero probe overhead. Build a measurement image with
 * -DTE_LATENCY_MEASURE=1 (see docs/latency-testing/).
 */
#pragma once
#include <stdint.h>

#ifndef TE_LATENCY_MEASURE
#define TE_LATENCY_MEASURE 0
#endif

namespace LatencyProbe {
#if TE_LATENCY_MEASURE
    void edge();     // T0: arm + timestamp, FIRST thing in the button-edge ISR
    void report();   // T1: report staged in DPRAM (edge->build), and arm the edge->wire measure
    void wire();     // T2: report shipped on the wire (edge->wire), from the endpoint-complete IRQ
    uint32_t edgeTime();   // T0 device-us of the last edge -- stamp into the report reserved bytes
                           // so a USB (Wireshark) capture can correlate edge -> host-receive.
    void stats(uint32_t &min_us, uint32_t &avg_us, uint32_t &max_us, uint32_t &count);      // build
    void wireStats(uint32_t &min_us, uint32_t &avg_us, uint32_t &max_us, uint32_t &count);  // wire
    void reset();
    void epoch(uint32_t key);   // reset the stats whenever `key` changes -- pass a hash of the
                                // latency-relevant config (NOBD sync on/off + delay, debounce). A hot
                                // swap then starts a FRESH measurement instead of mixing two configs.
#else
    // Disabled: inline no-ops so every call site folds to nothing (no symbols, no ISR cost).
    inline void edge() {}
    inline void report() {}
    inline void wire() {}
    inline uint32_t edgeTime() { return 0; }
    inline void stats(uint32_t &mn, uint32_t &av, uint32_t &mx, uint32_t &c) { mn = av = mx = c = 0; }
    inline void wireStats(uint32_t &mn, uint32_t &av, uint32_t &mx, uint32_t &c) { mn = av = mx = c = 0; }
    inline void reset() {}
    inline void epoch(uint32_t) {}
#endif
}
