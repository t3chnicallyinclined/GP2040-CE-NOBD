#pragma once

#include <stdint.h>
#include "drivers/dreamcast/maple_bus.h"
#include "drivers/dreamcast/DreamcastVMU.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

class Gamepad;

#define DC_BTN_C      0x0001
#define DC_BTN_B      0x0002
#define DC_BTN_A      0x0004
#define DC_BTN_START  0x0008
#define DC_BTN_UP     0x0010
#define DC_BTN_DOWN   0x0020
#define DC_BTN_LEFT   0x0040
#define DC_BTN_RIGHT  0x0080
#define DC_BTN_Z      0x0100
#define DC_BTN_Y      0x0200
#define DC_BTN_X      0x0400

class DreamcastDriver {
public:
    DreamcastDriver();

    bool init(uint pin_a, uint pin_b);
    void process(Gamepad* gamepad);
    virtual void processAux();
    bool isConnected() { return connected; }

    bool enableDiagnostics = false;

    // Debug counters
    uint32_t debugRxCount = 0;
    uint32_t debugTxCount = 0;
    uint32_t debugXorFail = 0;
    uint32_t debugCmd1Count = 0;
    uint32_t debugCmd9Count = 0;
    uint32_t debugConsecutivePolls = 0;
    uint32_t debugMaxConsecutivePolls = 0;
    uint32_t debugResendCount = 0;
    uint32_t debugTableHits = 0;

    // Response timing
    volatile uint32_t respLast = 0;
    volatile uint32_t respMin = 0xFFFFFFFF;
    volatile uint32_t respMax = 0;
    volatile uint32_t respCount = 0;

    // Frame interval tracking (CMD9-to-CMD9 delta)
    volatile uint32_t prevCmd9Timestamp = 0;
    volatile uint32_t frameIntervalLast = 0;
    volatile uint32_t frameIntervalMin = 0xFFFFFFFF;
    volatile uint32_t frameIntervalMax = 0;
    volatile uint32_t frameCount = 0;
    volatile uint32_t droppedPollCount = 0;     // Intervals > 20ms

    // CMD14 (vibrate / SET_CONDITION) tracking
    volatile uint32_t cmd14Count = 0;           // Total CMD14s received
    volatile uint32_t cmd14LastTimestamp = 0;    // When last CMD14 arrived
    volatile uint32_t cmd14PrevTimestamp = 0;    // Previous CMD14 arrival (for interval)
    volatile uint32_t cmd14IntervalLast = 0;     // Last CMD14-to-CMD14 interval (us)
    volatile uint16_t cmd14LastPayload = 0;      // Last vibrate motor values (m0<<8|m1)
    volatile uint32_t cmd14BurstCount = 0;       // CMD14s within 100ms of each other
    volatile uint32_t cmd14BurstStart = 0;       // First CMD14 timestamp in current burst
    volatile uint32_t cmd14LongestBurst = 0;     // Longest burst seen (count)
    volatile uint32_t cmd14SilenceSince = 0;     // Timestamp of last CMD9 after CMD14 silence begins

    // CMD14 event log ring buffer (for vibrate fingerprinting)
    static constexpr uint8_t CMD14_LOG_SIZE = 64;
    struct Cmd14LogEntry {
        uint32_t timestamp;     // us since boot
        uint16_t payload;       // motor0 << 8 | motor1
        uint16_t intervalUs;    // time since previous CMD14 (capped at 0xFFFF)
    };
    Cmd14LogEntry cmd14Log[CMD14_LOG_SIZE];
    volatile uint8_t cmd14LogIdx = 0;
    volatile uint8_t cmd14LogCount = 0;

    // Button-to-poll latency
    volatile uint32_t buttonChangeTimestamp = 0;
    volatile uint32_t b2pLast = 0;
    volatile uint32_t b2pMin = 0xFFFFFFFF;
    volatile uint32_t b2pMax = 0;
    volatile uint32_t b2pCount = 0;
    volatile bool b2pPending = false;

    bool disableVMU = false;
    bool vmuReady = false;

    DreamcastVMU vmu;

    // GPIO pin → DC button mapping (built at init time)
    uint16_t gpioDcButtonMap[30];
    uint32_t triggerLTMask;
    uint32_t triggerRTMask;
    uint32_t buttonGpioMask;
    void buildGpioDcMap();

    // Pre-computed CMD9 buttons word (W3 = triggers + buttons), indexed by the compressed
    // GPIO button state (sized to the actual button count at init).
    uint32_t* cmd9TableW3 = nullptr;
    uint32_t  cmd9TableSize = 0;
    uint8_t   cmd9GpioPins[13];
    uint8_t   cmd9NumBits = 0;
    // What the CMD9 ISR sends. The loop fills the idle slot and flips cmd9ActiveSnapshot, so
    // the ISR always reads buttons, analog and checksum that belong together.
    struct Cmd9Snapshot {
        uint32_t buttonsWord;    // word 3: LT, RT, buttons
        uint32_t analogWord;     // word 4: analog axes
        uint8_t  bodyChecksum;   // XOR fold of words 2, 3 and 4 (everything except the header)
    };
    Cmd9Snapshot cmd9Snapshots[2] = {};
    volatile uint32_t cmd9ActiveSnapshot = 0;
    uint32_t cmd9LoopButtonsWord = 0x0000FFFF;   // loop-side copies; the ISR never reads these
    uint32_t cmd9LoopAnalogWord  = 0x80808080;
    void publishCmd9Snapshot();
    uint32_t lastFilteredGpio = 0xFFFFFFFF; // Cache for early-return in updateCmd9FromGpio
    void buildCmd9LookupTable();
    void updateCmd9FromGpio(uint32_t filteredGpio);
    void updateAnalogFromGamepad(Gamepad* gamepad);

    uint8_t  diagPage = 0;           // 0=maple, 1=state

    // Cached port from CMD1 — never changes after initial handshake.
    // Used to pre-compute all packet headers and CRCs.
    uint8_t cachedPort = 0;
    bool portKnown = false;

    // Pre-built packet buffers — all public for ISR access.
    // Headers and CRCs are pre-computed with cachedPort after first CMD1.
    MapleBus bus;
    uint32_t controllerPacketBuf[6];
    uint32_t infoPacketBuf[31];
    uint32_t extInfoBuf[51];
    uint32_t ackPacketBuf[3];
    uint32_t resendPacketBuf[3];
    uint32_t unknownCmdBuf[3];

    // Public for ISR callback access
    uint16_t mapRawGpioToDC(uint32_t rawGpio, uint8_t* outLT, uint8_t* outRT);

    // Rebuild all static packet headers+CRCs after port is known from CMD1.
    void rebuildAllPacketsForPort(uint8_t port);

    // ISR command dispatch — handles ALL Maple Bus commands from ISR context.
    // Registered via bus.enableIsrDispatch() at init.
    static void __no_inline_not_in_flash_func(isrCommandDispatch)(MapleBus* bus);

    // ISR VMU sub-dispatch
    void isrHandleVmuCommand(int8_t cmd, uint32_t hdr, uint8_t port, MapleBus* bus);

private:
    bool connected;

    void buildInfoPacket();
    void buildExtInfoPacket();
    void buildControllerPacket();
    void buildACKPacket();
    void buildResendPacket();
    void buildUnknownCmdPacket();

    void waitTxFlushRx();
    uint16_t mapButtonsToDC(uint32_t gpButtons, uint8_t dpad);
};
