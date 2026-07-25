"""
Analog / Hall-effect actuation — the premium low-latency input source.

A Hall sensor reports a continuous value (0 = key up / no field, 255 = fully
pressed) via the chip's OPA→PGA→ADC (or the CMP→DAC2 hardware fast-path). This
turns that value stream into a digital press/release with two modes:

  fixed   — press at a configurable actuation point, release with hysteresis
            (a Schmitt trigger). The "adjustable actuation point" feature.
  rapid   — direction-of-travel re-actuation (Wooting-style rapid trigger): press
            the instant the key moves DOWN by `press_sens` from its local minimum;
            release the instant it moves UP by `release_sens` from its local
            maximum. Lets you re-press WITHOUT fully releasing → fastest possible
            re-actuation. Below `floor`, always released.

Why this is the latency floor: Hall has NO mechanical bounce (no debounce needed
at all — not even the sync window), and actuation is a threshold compare, so a
press → digital in sub-µs. This is the "best fight stick" path.

TigerStyle: static per-button state, bounded (one compare per step), asserts on
the value range + the never-pressed-below-floor invariant, named limits.
"""
from dataclasses import dataclass

VALUE_MAX = 255


@dataclass
class AnalogConfig:
    mode: str = "fixed"          # "fixed" | "rapid"
    actuation: int = 128         # fixed: press threshold (0..255) — adjustable point
    hysteresis: int = 8          # fixed: release band below the actuation point
    press_sens: int = 10         # rapid: down-travel from local min to actuate
    release_sens: int = 10       # rapid: up-travel from local max to deactuate
    floor: int = 20              # rapid: below this depth, always released

    def __post_init__(self):
        assert self.mode in ("fixed", "rapid"), f"bad mode {self.mode!r}"
        assert 0 <= self.actuation <= VALUE_MAX, "actuation out of range"
        assert 1 <= self.press_sens <= VALUE_MAX and 1 <= self.release_sens <= VALUE_MAX


class Actuation:
    def __init__(self, cfg=None):
        self.cfg = cfg or AnalogConfig()
        self.pressed = False
        self._ref = 0            # local extremum: min while released, max while pressed

    def step(self, value):
        """One analog sample (0..VALUE_MAX) → the digital pressed state."""
        assert 0 <= value <= VALUE_MAX, "analog value out of range"
        c = self.cfg
        if c.mode == "fixed":
            if not self.pressed and value >= c.actuation:
                self.pressed = True
            elif self.pressed and value <= c.actuation - c.hysteresis:
                self.pressed = False
        else:                                        # rapid trigger
            if value < c.floor:
                self.pressed = False
                self._ref = value
            elif not self.pressed:
                self._ref = min(self._ref, value)    # track the local minimum
                if value - self._ref >= c.press_sens:
                    self.pressed = True
                    self._ref = value                # switch to tracking the maximum
            else:
                self._ref = max(self._ref, value)    # track the local maximum
                if self._ref - value >= c.release_sens:
                    self.pressed = False
                    self._ref = value                # switch to tracking the minimum
        # negative space: rapid trigger is never actuated below the floor
        assert not (self.pressed and c.mode == "rapid" and value < c.floor), \
            "actuated below the floor"
        return self.pressed
