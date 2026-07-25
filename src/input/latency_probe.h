/*
 * latency_probe -- instrument-free device-side latency + jitter measurement.
 *
 * edge() is called the instant a button changes (first thing in the GPIO doorbell ISR);
 * report() is called the instant that state is loaded into the USB IN endpoint. The delta
 * is EXACTLY the device's contribution -- raw edge -> report-ready -- with the 1 ms host
 * poll wait excluded. Tracked as min / avg / max microseconds; the **min..max range is the
 * device jitter**. This proves A1+A4b put the device at ~tens of us and flags any hidden
 * stall (a surprisingly high max = a real target).
 *
 * Resolution is 1 us (hardware timer). State is tiny + volatile; edge() runs in ISR context
 * and report() on the Core0 loop (the ISR preempts the loop, so no concurrent stat writes).
 */
#pragma once
#include <stdint.h>

namespace LatencyProbe {
    void edge();     // arm + timestamp: FIRST thing in the button-edge ISR
    void report();   // measure: when the report is loaded into the USB endpoint
    void stats(uint32_t &min_us, uint32_t &avg_us, uint32_t &max_us, uint32_t &count);
    void reset();
}
