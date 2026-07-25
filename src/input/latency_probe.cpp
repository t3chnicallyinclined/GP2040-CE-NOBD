#include "latency_probe.h"
#include "pico.h"
#include "hardware/timer.h"   // time_us_32

namespace {
volatile uint32_t g_edge_us = 0;
volatile bool     g_armed   = false;
volatile uint32_t g_min = 0xFFFFFFFFu;
volatile uint32_t g_max = 0;
volatile uint64_t g_sum = 0;
volatile uint32_t g_cnt = 0;
} // namespace

namespace LatencyProbe {

// Timestamp the raw edge FIRST thing in the ISR (least entry skew), then arm.
void __not_in_flash_func(edge)() {
    g_edge_us = time_us_32();
    g_armed = true;
}

// Called when the report is loaded into the USB endpoint: fold the delta into the stats.
void __not_in_flash_func(report)() {
    if (!g_armed) return;
    const uint32_t d = time_us_32() - g_edge_us;
    g_armed = false;
    // >10ms is never a real device latency: it's a stale arm (a button edge that produced no
    // report change left its timestamp behind, read by a much later report) or a timer wrap.
    // Discard it so it can't poison min/avg/max.
    if (d > 10000u) return;
    if (d < g_min) g_min = d;
    if (d > g_max) g_max = d;
    g_sum += d;
    g_cnt++;
}

void stats(uint32_t &min_us, uint32_t &avg_us, uint32_t &max_us, uint32_t &count) {
    const uint32_t c = g_cnt;
    count  = c;
    min_us = c ? g_min : 0u;
    max_us = g_max;
    avg_us = c ? (uint32_t)(g_sum / c) : 0u;
}

void reset() {
    g_min = 0xFFFFFFFFu; g_max = 0; g_sum = 0; g_cnt = 0; g_armed = false;
}

} // namespace LatencyProbe
