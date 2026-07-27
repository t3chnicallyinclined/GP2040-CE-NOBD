#include "latency_probe.h"
#if TE_LATENCY_MEASURE          // production (flag off) -> the header provides inline no-ops; this TU is empty
#include "pico.h"
#include "hardware/timer.h"   // time_us_32

namespace {
// edge -> build (T1-T0): raw edge to report staged in DPRAM -- the DEVICE compute.
volatile uint32_t g_edge_us = 0;
volatile bool     g_armed   = false;
volatile uint32_t g_min = 0xFFFFFFFFu;
volatile uint32_t g_max = 0;
volatile uint64_t g_sum = 0;
volatile uint32_t g_cnt = 0;

// edge -> wire (T2-T0): raw edge to the report actually SHIPPED on the USB wire (the endpoint-
// complete IRQ). This is the TRUE controller latency; its average is the ~500us USB-FS poll floor.
volatile uint32_t g_wire_edge_us = 0;
volatile bool     g_wire_pending = false;
volatile uint32_t g_wmin = 0xFFFFFFFFu;
volatile uint32_t g_wmax = 0;
volatile uint64_t g_wsum = 0;
volatile uint32_t g_wcnt = 0;

uint32_t g_epoch = 0xFFFFFFFFu;   // last latency-config key; a change resets the stats (see epoch())
} // namespace

namespace LatencyProbe {

// T0: timestamp the raw edge FIRST thing in the ISR (least entry skew), then arm.
void __not_in_flash_func(edge)() {
    g_edge_us = time_us_32();
    g_armed = true;
}

// T1: the report is staged in DPRAM. Fold edge->build, then arm the edge->wire measurement --
// the report carrying this edge is now in DPRAM and ships on the next host poll.
void __not_in_flash_func(report)() {
    if (!g_armed) return;
    const uint32_t d = time_us_32() - g_edge_us;
    g_armed = false;
    // >10ms is never a real device compute: a stale arm (an edge that changed no report) or a wrap.
    if (d > 10000u) return;
    if (d < g_min) g_min = d;
    if (d > g_max) g_max = d;
    g_sum += d;
    g_cnt++;
    g_wire_edge_us = g_edge_us;
    g_wire_pending = true;
}

// T2: the staged report just went out on the wire (called from the USB IN endpoint-complete IRQ).
// This closes the edge->wire measurement -- the real controller latency including the poll wait.
void __not_in_flash_func(wire)() {
    if (!g_wire_pending) return;
    const uint32_t d = time_us_32() - g_wire_edge_us;
    g_wire_pending = false;
    // >30ms is never a real edge->wire (stale / wrap); a co-registration sync window still fits.
    if (d > 30000u) return;
    if (d < g_wmin) g_wmin = d;
    if (d > g_wmax) g_wmax = d;
    g_wsum += d;
    g_wcnt++;
}

uint32_t __not_in_flash_func(edgeTime)() { return g_edge_us; }

void stats(uint32_t &min_us, uint32_t &avg_us, uint32_t &max_us, uint32_t &count) {
    const uint32_t c = g_cnt;
    count  = c;
    min_us = c ? g_min : 0u;
    max_us = g_max;
    avg_us = c ? (uint32_t)(g_sum / c) : 0u;
}

void wireStats(uint32_t &min_us, uint32_t &avg_us, uint32_t &max_us, uint32_t &count) {
    const uint32_t c = g_wcnt;
    count  = c;
    min_us = c ? g_wmin : 0u;
    max_us = g_wmax;
    avg_us = c ? (uint32_t)(g_wsum / c) : 0u;
}

void reset() {
    g_min = 0xFFFFFFFFu; g_max = 0; g_sum = 0; g_cnt = 0; g_armed = false;
    g_wmin = 0xFFFFFFFFu; g_wmax = 0; g_wsum = 0; g_wcnt = 0; g_wire_pending = false;
}

// Reset the accumulated stats when the latency-relevant config changes. Input-mode swaps reboot (a
// fresh boot zeroes these already), so this mainly catches a HOT NOBD-sync or debounce toggle -- the
// HUD then reflects only the config currently in effect, never a blend of before + after.
void epoch(uint32_t key) {
    if (key == g_epoch) return;
    g_epoch = key;
    reset();
}

} // namespace LatencyProbe
#endif // TE_LATENCY_MEASURE
