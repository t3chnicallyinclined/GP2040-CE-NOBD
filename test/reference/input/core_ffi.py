"""
cffi bridge to the portable C core -- lets input/dst.py fuzz the SHIPPING C
(`socd_clean`, `sync_window_step`, ...) instead of only the Python re-implementation,
closing the model/impl gap (the whole point of the C-not-Rust decision).

Strategy: compile the firmware/core/ sources (`_SRCS`) into one shared library with
our host compiler (zig cc / any cc), then bind it via cffi **ABI mode** (`dlopen`)
-- so Python needs no compiler at *runtime*, just the prebuilt lib. If no C compiler
is on PATH the loader returns (None, None) and the C DST tiers are SKIPPED, not
failed -- matching firmware/core/build.sh's graceful "no compiler" behaviour.

The shared lib is built at plain -O2 (NDEBUG on, asserts compiled out) on purpose:
the differential oracle in dst.py catches any divergence with a clean, replayable
seed, whereas a C assert() firing would abort() the Python process with no seed.
The core's asserts are exercised separately, under -UNDEBUG, by build.sh.
"""
import pathlib
import subprocess
import sys
from shutil import which

import cffi

_HERE = pathlib.Path(__file__).resolve().parent
_CORE = _HERE.parent.parent.parent / "src" / "input" / "core"  # test/reference/input -> repo root -> src/input/core
_BUILD = _HERE / "build"                                        # keep build artifacts out of the source tree

# name <-> bit map (mirrors firmware/core/buttons.h: P1..P4 == BTN_B1..B4). SOCD only
# touches the four directions; the action bits let the fuzzer exercise passthrough,
# turbo, and macro step/trigger buttons.
BIT = {"UP": 1 << 0, "DOWN": 1 << 1, "LEFT": 1 << 2, "RIGHT": 1 << 3,
       "P1": 1 << 4, "P2": 1 << 5, "P3": 1 << 6, "P4": 1 << 7}

# Python SOCD mode name -> C socd_mode_t enum value (same order as socd.h).
MODE = {"neutral": 0, "up_priority": 1, "last_win": 2, "first_win": 3, "bypass": 4}

# The portable-core sources compiled into the shared lib (add a module here as it
# ports to C). Every .c file in firmware/core/ except the host test.
_SRCS = ("remap.c", "socd.c", "sync_window.c", "turbo.c", "macros.c", "analog.c",
         "reverse.c", "focus.c", "input_pipeline.c", "buffer.c", "hotkeys.c")

# cdef mirrors the core headers exactly (ABI mode: struct layout + enum size must
# match the compiler's). Bools are opaque here -- we only pass the struct by pointer.
_CDEF = """
typedef uint32_t buttons_t;

typedef enum { SOCD_NEUTRAL, SOCD_UP_PRIORITY, SOCD_LAST_WIN, SOCD_FIRST_WIN, SOCD_BYPASS } socd_mode_t;
typedef struct { socd_mode_t mode; buttons_t prev; buttons_t win_v; buttons_t win_h; } socd_t;
void      socd_init(socd_t *s, socd_mode_t mode);
buttons_t socd_clean(socd_t *s, buttons_t pressed);

typedef struct {
    uint32_t  window;
    bool      release_debounce;
    buttons_t committed;
    buttons_t pending;
    bool      open;
    bool      started;
    uint32_t  deadline;
    uint32_t  last_now;
    buttons_t pending_release;
    bool      release_open;
    uint32_t  release_deadline;
} sync_window_t;
void      sync_window_init(sync_window_t *s, uint32_t window, bool release_debounce);
buttons_t sync_window_step(sync_window_t *s, uint32_t now, buttons_t raw);

typedef struct {
    buttons_t buttons;
    uint32_t  on_ticks;
    uint32_t  off_ticks;
    uint32_t  period;
    buttons_t tracking;
    uint32_t  since[32];
    bool      started;
    uint32_t  last_now;
} turbo_t;
void      turbo_init(turbo_t *t, buttons_t buttons, uint32_t on_ticks, uint32_t off_ticks);
buttons_t turbo_step(turbo_t *t, uint32_t now, buttons_t pressed);

typedef struct { buttons_t buttons; uint32_t ticks; } macro_step_t;
typedef struct {
    buttons_t    trigger;
    uint32_t     nsteps;
    uint32_t     duration;
    macro_step_t steps[64];
} macro_t;
typedef struct {
    uint32_t  nmacros;
    macro_t   macros[32];
    buttons_t triggers;
    buttons_t prev;
    int32_t   active;
    uint32_t  start;
    bool      started;
    uint32_t  last_now;
} macro_player_t;
void      macro_player_init(macro_player_t *p);
int32_t   macro_add(macro_player_t *p, buttons_t trigger);
int32_t   macro_add_step(macro_player_t *p, int32_t idx, buttons_t buttons, uint32_t ticks);
buttons_t macro_step(macro_player_t *p, uint32_t now, buttons_t pressed);

typedef enum { ANALOG_FIXED, ANALOG_RAPID } analog_mode_t;
typedef struct {
    analog_mode_t mode;
    uint8_t actuation;
    uint8_t hysteresis;
    uint8_t press_sens;
    uint8_t release_sens;
    uint8_t floor;
} analog_config_t;
typedef struct {
    analog_config_t cfg;
    bool    pressed;
    uint8_t ref;
} analog_t;
void analog_init(analog_t *a, const analog_config_t *cfg);
bool analog_step(analog_t *a, uint8_t value);

typedef struct {
    bool        sync_window_enabled;
    uint32_t    sync_window_ticks;
    bool        sync_release_debounce;
    socd_mode_t socd_mode;
    bool        turbo_enabled;
    buttons_t   turbo_buttons;
    uint32_t    turbo_on_ticks;
    uint32_t    turbo_off_ticks;
    bool        macros_enabled;
    bool        reverse_enabled; bool reverse_ud; bool reverse_lr; buttons_t reverse_trigger;
    bool        focus_enabled; buttons_t focus_trigger; buttons_t focus_disabled;
} input_config_t;
typedef struct { buttons_t logical_of[32]; } remap_t;
void      remap_init(remap_t *r);
void      remap_set(remap_t *r, uint32_t pin, buttons_t logical);
buttons_t remap_apply(const remap_t *r, buttons_t physical);
typedef struct {
    input_config_t cfg;
    remap_t        remap;
    sync_window_t  sync;
    socd_t         socd;
    turbo_t        turbo;
    macro_player_t macros;
} input_pipeline_t;
void            input_pipeline_init(input_pipeline_t *p, const input_config_t *cfg);
buttons_t       input_pipeline_step(input_pipeline_t *p, uint32_t now, buttons_t raw);
remap_t        *input_pipeline_remap(input_pipeline_t *p);
macro_player_t *input_pipeline_macros(input_pipeline_t *p);

/* buffer: `seq` is atomic_uint in C, but that is the same layout as unsigned int and
 * we never touch it from Python (the buffer is opaque -- only passed by pointer). */
typedef struct { buttons_t buttons; uint8_t axis[6]; } input_frame_t;
typedef struct { unsigned int seq; input_frame_t slot; } buffer_t;
void buffer_init(buffer_t *b);
void buffer_publish(buffer_t *b, const input_frame_t *f);
bool buffer_snapshot(const buffer_t *b, input_frame_t *out);
void buffer_write_begin(buffer_t *b);
void buffer_write_data(buffer_t *b, const input_frame_t *f);
void buffer_write_end(buffer_t *b);

/* hotkeys: `action` is hotkey_action_t (enum) in C -- same layout as int here. */
typedef struct { buttons_t combo; int action; uint8_t param; } hotkey_t;
typedef struct { hotkey_t hotkeys[16]; uint32_t count; buttons_t prev; } hotkeys_t;
typedef struct { int action; uint8_t param; } hotkey_fire_t;
void      hotkeys_init(hotkeys_t *h);
int32_t   hotkeys_add(hotkeys_t *h, buttons_t combo, int action, uint8_t param);
buttons_t hotkeys_step(hotkeys_t *h, buttons_t pressed, hotkey_fire_t *fired,
                       uint32_t cap, uint32_t *nfired);
"""


def _find_cc():
    """A host C compiler as an argv prefix, or None. Prefer zig cc (hermetic)."""
    for cc in (["zig", "cc"], ["cc"], ["gcc"], ["clang"]):
        if which(cc[0]):
            return cc
    return None


def _lib_path():
    if sys.platform == "win32":
        return _BUILD / "core.dll"
    if sys.platform == "darwin":
        return _BUILD / "libcore.dylib"
    return _BUILD / "libcore.so"


def _build():
    """Compile the core sources into a shared lib; return its path, or None if no
    compiler. Rebuilds when any source OR header is newer than the lib."""
    cc = _find_cc()
    if cc is None:
        return None
    srcs = [_CORE / s for s in _SRCS]
    out = _lib_path()
    _BUILD.mkdir(exist_ok=True)
    inputs = srcs + list(_CORE.glob("*.h"))          # a header change must rebuild too
    newest = max(p.stat().st_mtime for p in inputs)
    if out.exists() and out.stat().st_mtime >= newest:
        return out                                   # up to date -> reuse
    cmd = cc + ["-shared", "-O2", "-I", str(_CORE)] + [str(s) for s in srcs] + ["-o", str(out)]
    if sys.platform == "win32":
        cmd.append("-Wl,--export-all-symbols")       # MinGW/lld: export without dllexport
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("C core shared-lib build failed:\n" + r.stderr)
    return out


_cache = None


def load():
    """(ffi, lib) for the C core, or (None, None) if no host C compiler is available."""
    global _cache
    if _cache is not None:
        return _cache
    path = _build()
    if path is None:
        _cache = (None, None)
        return _cache
    ffi = cffi.FFI()
    ffi.cdef(_CDEF)
    _cache = (ffi, ffi.dlopen(str(path)))
    return _cache


def mask(names):
    """Set of button names -> u32 bitmask (firmware/core/buttons.h layout)."""
    m = 0
    for n in names:
        m |= BIT[n]
    return m


def unmask(m):
    """u32 bitmask -> set of button names."""
    return {n for n, b in BIT.items() if m & b}
