#ifndef _FAULTCAPTURE_H_
#define _FAULTCAPTURE_H_

#include <cstdint>

// Diagnostic: latch the address of a HardFault across the watchdog reset so the
// failing PC can be shown on the OLED after the controller reboots.
//
// Why this exists: the SDK's default fault handler is a bare `bkpt` that locks the
// core up until the 2s watchdog resets the chip, telling us nothing. With this,
// a random reboot leaves a breadcrumb:
//   * a STABLE PC across reboots  -> a deterministic bug at that address (map it).
//   * SCATTERED / garbage PCs     -> memory corruption cascading (e.g. stack smash).
//   * NO fault captured, just a 2s watchdog timeout -> a true hang/deadlock, not a
//     fault (the handler never ran).
namespace FaultCapture {
    // Call once, as early as possible on Core 0 boot, before anything else touches
    // watchdog scratch[6]/[7]. Latches a fault captured on the previous run.
    void readAtBoot();
    bool hadFault();     // previous reset was a captured HardFault
    uint32_t faultPC();  // faulting program counter (valid when hadFault())
    uint8_t core();      // which core faulted (0 or 1)

    // Hang diagnostics: when the reset was NOT a fault (a lock-up + 2s watchdog),
    // these show the last phase each core reached before it froze.
    bool wasWatchdogReset(); // previous reset was a watchdog/software reset, not power-on
    uint32_t core0Step();    // Core 0's last hang breadcrumb (see gp2040.cpp run loop)
    uint32_t core1Step();    // Core 1's last hang breadcrumb (see gp2040aux.cpp run loop)
    uint32_t i2cTimeouts();  // I2C transactions that timed out during the last session
}

#endif
