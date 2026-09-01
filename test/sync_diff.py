"""
test/sync_diff.py -- differential fuzz: the SHIPPING C sync_window_step() vs the Python
reference in test/reference/input/sync_window.py, across every policy knob.

The two must agree step for step. This is the same discipline as DST tier I10, run over the
new surface (synced_mask / attack_mask / commit_at / preserve_width), which I10's own fuzz
ranges do not reach. Both sides drive an IDENTICAL LCG so the input script is shared without
having to serialise it.

Build the harness first:
  zig cc test/sync_diff_harness.c src/input/core/sync_window.c -I src/input/core -O2 -o dh.exe
Then:  python test/sync_diff.py
"""
import subprocess, sys, itertools, random
sys.path.insert(0, 'test/reference')
from input.sync_window import SyncWindow
BITS = [1<<i for i in range(8)]
def py_run(seed, window, rd, commit_at, pw, sm, am):
    st = seed
    def nxt():
        nonlocal st
        st = (st*1664525 + 1013904223) & 0xFFFFFFFF
        return (st >> 16) & 0xFFFF
    tobits = lambda m: {b for b in BITS if m & b}
    sw = SyncWindow(window=window, release_debounce=bool(rd),
                    synced_mask=tobits(sm) if sm else None,
                    attack_mask=tobits(am) if am else None,
                    commit_at=commit_at, preserve_width=bool(pw))
    raw = 0; out = []
    for t in range(400):
        r = nxt()
        if r & 1: raw ^= 1 << ((r >> 1) & 7)
        c = sw.step(t, tobits(raw & 0xFF))
        out.append(sum(c))
    return out

random.seed(7); bad = 0; n = 0
for trial in range(160):
    window = random.randint(1, 12)
    rd     = random.randint(0, 1)
    pw     = 0 if rd else random.randint(0, 1)
    ca     = random.choice([0, 2, 2, 3])
    sm     = random.choice([0, 0xF0, 0xFF, 0xCC])
    am     = random.choice([0, 0xF0, 0x30])
    seed   = random.randint(1, 10**6)
    args = [str(x) for x in (seed, window, rd, ca, pw, sm, am)]
    c = subprocess.run(["./dh.exe", *args], capture_output=True, text=True).stdout.split()
    p = [str(x) for x in py_run(seed, window, rd, ca, pw, sm, am)]
    n += 1
    if c != p:
        bad += 1
        i = next(k for k in range(min(len(c), len(p))) if c[k] != p[k])
        print(f"  DIVERGE trial={trial} args={args} at step {i}: C={c[i]} PY={p[i]}")
        if bad > 2: break
print(f"\n{n} random trials, {bad} divergences" + ("  -- C and Python agree" if not bad else ""))
