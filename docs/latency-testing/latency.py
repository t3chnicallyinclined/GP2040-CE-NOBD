#!/usr/bin/env python3
# gp2040-te edge->host-receive latency, from a passive USBPcap capture.
# New stamp layout (single-writer ISR, atomic aligned T0):
#   report byte [14] == 0x7E  marker  -> reject foreign 20-byte reports
#   report bytes [16..19]     T0 little-endian uint32 = device microseconds at the edge
# Usage:  tshark -r cap.pcap -Y "usb.data_len==20" -T fields -e frame.time_epoch -e usb.capdata > cap.txt
#         python latency.py cap.txt
import sys, statistics

edges = []      # (t0_us, first_recv_s)  -- first host-receive carrying each fresh forward edge
prev = None
mark_ok = mark_bad = 0
for line in open(sys.argv[1], encoding="ascii", errors="ignore"):
    p = line.rstrip("\n").split("\t")
    if len(p) != 2: continue
    t, h = p; h = h.replace(":", "").strip()
    if len(h) < 40: continue
    try:
        b = bytes.fromhex(h[:40]); recv = float(t)
    except Exception:
        continue
    if b[14] != 0x7E:                       # not one of ours (or a pre-stamp report)
        mark_bad += 1; continue
    mark_ok += 1
    t0 = b[16] | (b[17] << 8) | (b[18] << 16) | (b[19] << 24)   # aligned, un-torn
    if t0 == 0: continue
    if prev is None or t0 > prev:            # a NEW edge (device clock only moves forward)
        edges.append((t0, recv))
    prev = t0 if prev is None else max(prev, t0)

print("reports: %d ours (0x7E) / %d foreign-or-unstamped" % (mark_ok, mark_bad))
print("fresh forward edges:", len(edges))
if len(edges) < 20:
    print("too few edges -- press buttons DURING the capture window."); sys.exit(0)

deltas = sorted(r - t0/1e6 for t0, r in edges)   # constant clock offset + true edge->host latency
def q(s, p): return s[min(len(s)-1, int(len(s)*p))]

# With the tearing + clobber fixed, the low tail should be clean, so the absolute min IS the offset.
# Guard anyway: if min sits far below p1, a stray sample slipped through -> fall back to p1.
lo, p1 = deltas[0], q(deltas, 0.01)
off = lo if (p1 - lo) < 0.002 else p1        # 2ms guard band
if off != lo:
    print("NOTE: min was %.0fus below p1 -> using p1 as offset (a stray sample survived)"
          % ((p1 - lo) * 1e6))

lat = sorted(max(0.0, (d - off) * 1e6) for d in deltas)   # microseconds
n = len(lat)
print("\ndelta-cluster spread p01..p99: %.3f ms  (should be ~1ms = one USB poll)" %
      ((q(deltas,.99) - q(deltas,.01)) * 1000))
print("=== edge -> host-receive latency (us) ===")
print("  min=%.0f  p10=%.0f  p50=%.0f  MEAN=%.0f  p90=%.0f  p95=%.0f  p99=%.0f  max=%.0f" % (
    lat[0], q(lat,.10), q(lat,.50), statistics.mean(lat), q(lat,.90), q(lat,.95), q(lat,.99), lat[-1]))
core = [x for x in lat if x <= q(lat,.99)]
print("  MEAN (drop top 1%%): %.0f us = %.3f ms   over %d edges" % (
    statistics.mean(core), statistics.mean(core)/1000, len(core)))

# histogram, 100us buckets
print("\n=== histogram (100us buckets) ===")
bk = [0]*13
for x in lat:
    bk[min(12, int(x//100))] += 1
for i in range(12):
    print("  %4d-%4dus | %-50s %d (%.1f%%)" % (
        i*100, (i+1)*100, "#"*int(50*bk[i]/n), bk[i], 100*bk[i]/n))
print("    >1200us  | %-50s %d (%.1f%%)" % ("."*int(50*bk[12]/n), bk[12], 100*bk[12]/n))
