#include "faultcapture.h"

#include <hardware/watchdog.h>
#include <hardware/structs/watchdog.h>
#include <hardware/structs/sio.h>

// scratch[5] holds BootMode (system.cpp). scratch[6]/[7] are otherwise unused, and
// watchdog_reboot(0,0,0) preserves them (with pc==0 it only clears scratch[4]).
// Low bit of the magic carries the faulting core id, so it must be even.
static const uint32_t FAULT_MAGIC = 0xFA017CE4u; // "FAULTCE4", bit0 reserved for core

static uint32_t s_faultPC = 0;
static uint8_t  s_core = 0;
static bool     s_hadFault = false;
static bool     s_wasWatchdog = false;
static uint32_t s_core0Step = 0;
static uint32_t s_core1Step = 0;
static uint32_t s_i2cTimeouts = 0;

extern "C" void hardfault_capture(uint32_t *frame) {
    // frame = stacked exception frame: [r0,r1,r2,r3,r12,LR,PC,xPSR]. Write the PC
    // first, then the magic, so a torn write can never present a stale PC as valid.
    watchdog_hw->scratch[7] = frame[6];                       // stacked PC
    watchdog_hw->scratch[6] = FAULT_MAGIC | (sio_hw->cpuid & 1u);
    watchdog_reboot(0, 0, 0);                                 // immediate reset
    while (true) { }
}

// Overrides the SDK's weak `isr_hardfault` (a bkpt stub) in crt0.S, on both cores.
// Naked so we can read the raw exception context: pick MSP/PSP from EXC_RETURN bit
// 2, then tail-call the C capture with the frame pointer.
extern "C" __attribute__((naked)) void isr_hardfault(void) {
    __asm volatile(
        "movs r0, #4                 \n"
        "mov  r1, lr                 \n"
        "tst  r0, r1                 \n"
        "beq  1f                     \n"
        "mrs  r0, psp                \n"
        "b    2f                     \n"
        "1:                          \n"
        "mrs  r0, msp                \n"
        "2:                          \n"
        "ldr  r2, =hardfault_capture \n"
        "bx   r2                     \n"
    );
}

void FaultCapture::readAtBoot() {
    if (watchdog_caused_reboot()) {
        s_wasWatchdog = true;
        s_core0Step = watchdog_hw->scratch[0];   // last phase Core 0 reached
        s_core1Step = watchdog_hw->scratch[1];   // last phase Core 1 reached
        s_i2cTimeouts = watchdog_hw->scratch[2]; // I2C timeouts before the reset
        if ((watchdog_hw->scratch[6] & ~1u) == FAULT_MAGIC) {
            s_hadFault = true;
            s_faultPC = watchdog_hw->scratch[7];
            s_core = watchdog_hw->scratch[6] & 1u;
        }
    }
    watchdog_hw->scratch[6] = 0; // one-shot: a later clean reset won't re-report the fault
    watchdog_hw->scratch[2] = 0; // reset the I2C timeout counter for this new session
}

bool FaultCapture::hadFault() { return s_hadFault; }
uint32_t FaultCapture::faultPC() { return s_faultPC; }
uint8_t FaultCapture::core() { return s_core; }
bool FaultCapture::wasWatchdogReset() { return s_wasWatchdog; }
uint32_t FaultCapture::core0Step() { return s_core0Step; }
uint32_t FaultCapture::core1Step() { return s_core1Step; }
uint32_t FaultCapture::i2cTimeouts() { return s_i2cTimeouts; }
