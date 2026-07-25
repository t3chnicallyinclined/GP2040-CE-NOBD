"""
Remap -- physical GPIO pin -> logical button, the FIRST pipeline stage.

The switches report PHYSICAL pins; SOCD and everything downstream work on LOGICAL
buttons (UP/DOWN/.../P1.../START...). A pin with no mapping passes through unchanged,
so a pipeline fed logical names is pure identity (which is why the sim has worked
without remap so far). Multiple physical pins may map to one logical button -- they
OR together (e.g. two UP switches). Stateless: a pure per-tick mapping.

Remap goes FIRST (before the sync window) so co-registration and SOCD both operate on
logical buttons; the only hard constraint is remap-before-SOCD (SOCD resolves logical
directions -- two physical pins can map to one logical UP).
"""


class Remap:
    def __init__(self, mapping=()):
        # mapping: iterable of (physical_name, logical_name). Unlisted names pass through.
        self.mapping = dict(mapping)

    def apply(self, physical):
        """Set of physical button names -> set of logical button names."""
        return {self.mapping.get(p, p) for p in physical}
