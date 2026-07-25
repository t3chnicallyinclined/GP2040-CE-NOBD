"""
Configurable input-processing pipeline — the ordered source side of the buffer.

Chains the input stages in the ONE correct order (arch review), each stage
optional and tunable — GP2040-CE-webconfig-inspired, but a plain config object
here (the schema a web UI would later expose):

    raw pins → remap → [sync window] → SOCD → [turbo] → [macros] → buffer

*Bounce rejection is HARDWARE on V2 (timer IC-filter / CMP hysteresis) and is
near-zero latency; there is no software debounce stage in this pipeline. And the
sync window already absorbs intra-window press-bounce (proven in dst.py::bounce),
so debounce is redundant whenever the window is on.

**Latency is the priority.** Defaults are LOW-LATENCY-FIRST: window OFF, only SOCD
on (tournaments require it). Turn reliability features on only when you want them.
For the absolute floor, use Hall-effect buttons (no bounce) with everything off.

TigerStyle: static config, bounded stages, asserts, named presets.
"""
from dataclasses import dataclass

from sync_window import SyncWindow, DEFAULT_WINDOW, MAX_WINDOW
from socd import SOCDCleaner, MODES as SOCD_MODES
from turbo import Turbo, TurboConfig
from macros import MacroPlayer
from remap import Remap
import reverse
import focus


@dataclass
class InputConfig:
    sync_window: bool = False           # NOBD co-registration (adds up to its window of latency)
    sync_window_ticks: int = DEFAULT_WINDOW
    sync_release_debounce: bool = False
    socd_mode: str = "neutral"          # "bypass" disables SOCD (raw dual-press)
    turbo: bool = False                 # auto-fire held buttons (post-SOCD)
    turbo_buttons: frozenset = frozenset()
    turbo_on_ticks: int = 1
    turbo_off_ticks: int = 1
    macros: tuple = ()                  # Macro sequences (see macros.py); () = disabled
    remap: tuple = ()                   # (physical_name, logical_name) pairs; () = identity
    reverse: bool = False               # swap d-pad axes (after remap, before SOCD)
    reverse_ud: bool = False
    reverse_lr: bool = False
    reverse_trigger: frozenset = frozenset()   # empty = always; else hold-to-reverse modifier
    focus: bool = False                 # silence buttons while a modifier is held (last stage)
    focus_trigger: frozenset = frozenset()
    focus_disabled: frozenset = frozenset()

    def __post_init__(self):
        assert 1 <= self.sync_window_ticks <= MAX_WINDOW, "sync window out of range"
        assert self.socd_mode in SOCD_MODES, f"unknown SOCD mode {self.socd_mode!r}"

    # --- presets ---
    @staticmethod
    def low_latency():
        """Lowest latency: no windowing, SOCD neutral (still tournament-legal)."""
        return InputConfig(sync_window=False, socd_mode="neutral")

    @staticmethod
    def hall():
        """Hall-effect buttons: no bounce, no window — everything off but SOCD."""
        return InputConfig(sync_window=False, socd_mode="neutral")

    @staticmethod
    def reliable():
        """Mechanical switches, reliability-first: sync window absorbs bounce."""
        return InputConfig(sync_window=True, sync_window_ticks=DEFAULT_WINDOW,
                           sync_release_debounce=True, socd_mode="neutral")


class InputPipeline:
    def __init__(self, cfg=None):
        self.cfg = cfg or InputConfig()
        self.remap = Remap(self.cfg.remap)
        self.sync = (SyncWindow(self.cfg.sync_window_ticks, self.cfg.sync_release_debounce)
                     if self.cfg.sync_window else None)
        self.socd = SOCDCleaner(self.cfg.socd_mode)
        self.turbo = (Turbo(TurboConfig(self.cfg.turbo_buttons,
                                        self.cfg.turbo_on_ticks, self.cfg.turbo_off_ticks))
                      if self.cfg.turbo else None)
        self.macros = MacroPlayer(self.cfg.macros) if self.cfg.macros else None

    def step(self, now, raw):
        """raw physical pins at time `now` → the cleaned logical buffer state."""
        p = set(self.remap.apply(raw))          # physical pins -> logical buttons (first)
        if self.cfg.reverse:                    # swap d-pad axes -- before SOCD cleans them
            p = reverse.apply(p, self.cfg.reverse_ud, self.cfg.reverse_lr, self.cfg.reverse_trigger)
        if self.sync is not None:               # co-registration (optional)
            p = set(self.sync.step(now, p))
        p = set(self.socd.clean(p))              # SOCD (once, before the buffer)
        # negative space: after SOCD, opposing directions never both survive. Checked
        # HERE on SOCD's own output — a later macro is free to emit any button frame.
        assert self.cfg.socd_mode == "bypass" or not {"LEFT", "RIGHT"} <= p, "SOCD leaked L+R"
        assert self.cfg.socd_mode == "bypass" or not {"UP", "DOWN"} <= p, "SOCD leaked U+D"
        if self.turbo is not None:              # auto-fire (gates held buttons off/on)
            p = set(self.turbo.step(now, p))
        if self.macros is not None:             # macro playback (overlays its frames)
            p = set(self.macros.step(now, p))
        if self.cfg.focus:                      # focus: silence buttons while held (LAST)
            p = focus.apply(p, self.cfg.focus_trigger, self.cfg.focus_disabled)
        return p
