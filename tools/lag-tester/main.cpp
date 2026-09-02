/*
 * lag-tester -- external controller-latency rig on RP2040 + MAX3421E (USB Host FeatherWing).
 *
 * Mirrors the inputlag.science method (a microcontroller + a MAX3421E USB Host Shield that both
 * presses a button and hosts the controller under test) but times on the RP2040's 1us clock
 * instead of 1ms histogram buckets. One clock -> no host/device offset ambiguity; external ->
 * the DUT is not measuring itself.
 *
 * Per cycle: drive one DUT button input LOW (open-drain), timestamp the press, and -- while
 * hosting the DUT as an XInput controller over the MAX3421E -- timestamp the first report whose
 * button bit flips. latency = t_report - t_press. Press timing is deliberately de-synced from the
 * host's 1ms poll so the poll-wait distribution is honest. Results print over USB serial.
 *
 * Wiring (Pico  <->  MAX3421E FeatherWing  <->  DUT): see README.md.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "tusb.h"
#include "host/usbh.h"
#include "host/usbh_pvt.h"   // usbh_class_driver_t (registering the custom XInput host driver)
#include "xinput_host.h"

// ------------------------------------------------------------------ wiring
#define MAX3421_SPI        spi0
#define MAX3421_SCK_PIN    18     // SPI0 SCK
#define MAX3421_MOSI_PIN   19     // SPI0 TX  -> FeatherWing MOSI
#define MAX3421_MISO_PIN   16     // SPI0 RX  <- FeatherWing MISO
#define MAX3421_CS_PIN     17     // FeatherWing CS   (driven as plain GPIO)
#define MAX3421_INTR_PIN   20     // FeatherWing IRQ  (falling-edge -> tuh_int_handler)
#define STIMULUS_PIN       14     // open-drain -> DUT B1 input
// Onboard-LED heartbeat is optional: Pico W / Pico 2 W route the LED through the wireless chip
// (no GPIO), so it simply has none here -- the serial log is the liveness signal.
#ifdef PICO_DEFAULT_LED_PIN
#define LED_PIN            PICO_DEFAULT_LED_PIN
#endif

// ------------------------------------------------------------- what to watch
// XInput report bytes: [0]=type [1]=len [2]=buttons1 [3]=buttons2 ...  B1 == Xbox "A" == buttons2 0x10
#define RPT_BTN2           3
#define MASK_A             0x10

// ---------------------------------------------------------- measurement knobs
#define N_SAMPLES          1000       // print a report + reset after this many good presses
#define PRESS_TIMEOUT_US   50000u     // give up on a press the DUT never reports (stuck/disconnected)
#define RELEASE_TIMEOUT_US 50000u
#define NBUCKET            25         // 50us buckets: 0..1200us (bucket 24 = overflow >=1200us)
#define BUCKET_US          50

// =============================================================== MAX3421E glue
// (RP2040 SPI/INT/CS, modeled on tinyusb hw/bsp/rp2040/family.c -- the proven reference)
extern "C" {

static void max3421_int_handler(uint gpio, uint32_t events) {
    if (gpio == MAX3421_INTR_PIN && (events & GPIO_IRQ_EDGE_FALL))
        tuh_int_handler(BOARD_TUH_RHPORT, true);
}

void tuh_max3421_int_api(uint8_t rhport, bool enabled) {
    (void)rhport;
    irq_set_enabled(IO_IRQ_BANK0, enabled);
}

void tuh_max3421_spi_cs_api(uint8_t rhport, bool active) {
    (void)rhport;
    gpio_put(MAX3421_CS_PIN, !active);     // CS is active-low
}

bool tuh_max3421_spi_xfer_api(uint8_t rhport, uint8_t const* tx, uint8_t* rx, size_t n) {
    (void)rhport;
    if (!tx && !rx) return false;
    int ret;
    if (!tx)      ret = spi_read_blocking(MAX3421_SPI, 0, rx, n);
    else if (!rx) ret = spi_write_blocking(MAX3421_SPI, tx, n);
    else          ret = spi_write_read_blocking(MAX3421_SPI, tx, rx, n);
    return ret == (int)n;
}

// Register our XInput host class driver with TinyUSB.
usbh_class_driver_t const* usbh_app_driver_get_cb(uint8_t* driver_count) {
    static const usbh_class_driver_t drv = {
#if CFG_TUSB_DEBUG >= 2
        .name = "xinput",
#endif
        .init       = xinputh_init,
        .open       = xinputh_open,
        .set_config = xinputh_set_config,
        .xfer_cb    = xinputh_xfer_cb,
        .close      = xinputh_close,
    };
    *driver_count = 1;
    return &drv;
}

} // extern "C"

static void max3421_hw_init(void) {
    gpio_init(MAX3421_CS_PIN);
    gpio_set_dir(MAX3421_CS_PIN, GPIO_OUT);
    gpio_put(MAX3421_CS_PIN, 1);                 // deselected

    gpio_init(MAX3421_INTR_PIN);
    gpio_set_dir(MAX3421_INTR_PIN, GPIO_IN);
    gpio_pull_up(MAX3421_INTR_PIN);
    gpio_set_irq_enabled_with_callback(MAX3421_INTR_PIN, GPIO_IRQ_EDGE_FALL, true, max3421_int_handler);

    spi_init(MAX3421_SPI, 4 * 1000000ul);
    gpio_set_function(MAX3421_SCK_PIN,  GPIO_FUNC_SPI);
    gpio_set_function(MAX3421_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(MAX3421_MISO_PIN, GPIO_FUNC_SPI);
    spi_set_format(MAX3421_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

// =============================================================== stimulus
// Open-drain: press = actively pull the DUT's button line to GND; release = go hi-Z and let the
// DUT's own pull-up restore it. We never drive the line high, so we can't fight the DUT.
static inline void stim_press(void)   { gpio_set_dir(STIMULUS_PIN, GPIO_OUT); gpio_put(STIMULUS_PIN, 0); }
static inline void stim_release(void) { gpio_set_dir(STIMULUS_PIN, GPIO_IN); }   // hi-Z

// =============================================================== measurement
enum { IDLE, PRESSING, RELEASING };
static volatile int      s_state = IDLE;
static volatile bool     s_mounted = false;
static volatile bool     s_last_a = false;        // most recent A-bit seen (guards a stuck start)
static volatile uint32_t s_t_press = 0;
static uint32_t s_deadline = 0;                   // main-loop timeout for the current state
static uint32_t s_next_press = 0;                 // when IDLE may start the next press

// stats
static uint32_t s_count = 0, s_miss = 0;
static uint32_t s_min = 0xFFFFFFFFu, s_max = 0;
static uint64_t s_sum = 0, s_sumsq = 0;
static uint32_t s_bucket[NBUCKET];
static uint32_t s_run = 0;

static void stats_reset(void) {
    s_count = s_miss = 0; s_min = 0xFFFFFFFFu; s_max = 0; s_sum = s_sumsq = 0;
    memset(s_bucket, 0, sizeof(s_bucket));
}

static uint32_t pctile_us(uint32_t target) {   // approx percentile from the 50us histogram
    uint32_t cum = 0;
    for (int i = 0; i < NBUCKET; i++) { cum += s_bucket[i]; if (cum >= target) return (i + 1) * BUCKET_US; }
    return NBUCKET * BUCKET_US;
}

static void report_and_reset(void) {
    double mean = (double)s_sum / s_count;
    double var  = (double)s_sumsq / s_count - mean * mean;
    double sd   = var > 0 ? sqrt(var) : 0;
    printf("\n==== run %lu : edge -> USB report latency (us), n=%lu, misses=%lu ====\n",
           (unsigned long)++s_run, (unsigned long)s_count, (unsigned long)s_miss);
    printf("  min=%lu  median~%lu  mean=%.0f  p99~%lu  max=%lu  stddev=%.0f\n",
           (unsigned long)s_min, (unsigned long)pctile_us(s_count / 2), mean,
           (unsigned long)pctile_us((uint32_t)(s_count * 0.99)), (unsigned long)s_max, sd);
    for (int i = 0; i < NBUCKET; i++) {
        if (!s_bucket[i]) continue;
        int bar = (int)(50.0 * s_bucket[i] / s_count);
        char line[64]; int n = 0;
        for (; n < bar && n < 50; n++) line[n] = '#';
        line[n] = 0;
        if (i < NBUCKET - 1) printf("  %4d-%4dus | %-50s %lu\n", i*BUCKET_US, (i+1)*BUCKET_US, line, (unsigned long)s_bucket[i]);
        else                 printf("     >=%4dus | %-50s %lu\n", i*BUCKET_US, line, (unsigned long)s_bucket[i]);
    }
    stats_reset();
}

static void record(uint32_t lat_us) {
    if (lat_us < s_min) s_min = lat_us;
    if (lat_us > s_max) s_max = lat_us;
    s_sum += lat_us; s_sumsq += (uint64_t)lat_us * lat_us;
    int b = lat_us / BUCKET_US; if (b >= NBUCKET) b = NBUCKET - 1;
    s_bucket[b]++;
    if (++s_count >= N_SAMPLES) report_and_reset();
}

// schedule the next press at a varying, non-1ms-aligned offset so presses don't lock to the poll
static void schedule_next_press(uint32_t now) {
    s_next_press = now + 7000u + (s_count * 2731u) % 6000u;   // ~7..13ms, drifts vs the 1ms SOF
}

// ---- XInput host callbacks (invoked by the driver) ----
extern "C" void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t type, uint8_t subtype) {
    (void)subtype;
    s_mounted = true; s_state = IDLE; s_last_a = false;
    schedule_next_press(time_us_32());
    printf("# DUT mounted (addr=%u type=%u) -- measuring\n", dev_addr, type);
}

extern "C" void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance) {
    (void)dev_addr; (void)instance;
    s_mounted = false; s_state = IDLE; stim_release();
    printf("# DUT unmounted\n");
}

extern "C" void tuh_xinput_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                              uint8_t const* report, uint16_t len) {
    (void)dev_addr; (void)instance;
    if (len <= RPT_BTN2) return;                     // (driver re-arms the IN transfer itself)
    const uint32_t now = time_us_32();
    const bool a = (report[RPT_BTN2] & MASK_A) != 0;
    s_last_a = a;

    if (s_state == PRESSING && a) {                  // the press just showed up on the wire
        record(now - s_t_press);
        stim_release();
        s_state = RELEASING;
        s_deadline = now + RELEASE_TIMEOUT_US;
    } else if (s_state == RELEASING && !a) {         // released state confirmed -> next cycle
        s_state = IDLE;
        schedule_next_press(now);
    }
}

// =============================================================== main
int main(void) {
#ifdef LED_PIN
    gpio_init(LED_PIN); gpio_set_dir(LED_PIN, GPIO_OUT);
#endif
    gpio_init(STIMULUS_PIN); gpio_disable_pulls(STIMULUS_PIN); stim_release();   // start hi-Z (released)

    // SPI/CS/INT must be live BEFORE the host stack initializes -- stdio_init_all() may call
    // tusb_init(), which brings up the MAX3421E host and immediately talks to it over SPI.
    max3421_hw_init();
    stdio_init_all();                    // brings up + services the native-USB CDC (device, rhport 0)
    tuh_init(BOARD_TUH_RHPORT);          // bring up the MAX3421E host (rhport 1)

    printf("\nlag-tester ready. Waiting for DUT on the MAX3421E host port...\n");

    uint32_t led_t = 0;
    while (true) {
        tuh_task();                                  // service the USB host (drives the polling)

        const uint32_t now = time_us_32();

        if (s_mounted) {
            if (s_state == IDLE && !s_last_a && (int32_t)(now - s_next_press) >= 0) {
                stim_press();
                s_t_press = time_us_32();             // stamp AFTER the drive, tightest t0
                s_state = PRESSING;
                s_deadline = s_t_press + PRESS_TIMEOUT_US;
            } else if (s_state == PRESSING && (int32_t)(now - s_deadline) >= 0) {
                s_miss++; stim_release(); s_state = RELEASING; s_deadline = now + RELEASE_TIMEOUT_US;
            } else if (s_state == RELEASING && (int32_t)(now - s_deadline) >= 0) {
                s_state = IDLE; schedule_next_press(now);   // give up waiting for release, move on
            }
        }

#ifdef LED_PIN
        if (now - led_t > 250000u) { led_t = now; gpio_xor_mask(1u << LED_PIN); }
#else
        (void)led_t;
#endif
    }
}
