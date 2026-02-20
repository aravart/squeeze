"""
perf_monitor.py — Performance monitoring dashboard.

Builds a small mixer, renders audio, and prints a live-ish
performance report showing callback timing, CPU load,
xrun detection, and per-slot profiling.

No audio device or plugins required — runs entirely headless.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from squeeze import Squeeze

SAMPLE_RATE = 44100.0
BLOCK_SIZE = 512
BLOCKS_PER_PASS = 100           # render in bursts, then snapshot


def fmt_us(us):
    """Format microseconds with sensible units."""
    if us >= 1000:
        return f"{us / 1000:.2f} ms"
    return f"{us:.1f} us"


def print_snapshot(snap, slots, pass_num):
    budget = snap["buffer_duration_us"]
    avg = snap["callback_avg_us"]
    peak = snap["callback_peak_us"]
    cpu = snap["cpu_load_percent"]
    xruns = snap["xrun_count"]
    callbacks = snap["callback_count"]

    print(f"\n{'=' * 56}")
    print(f"  Pass {pass_num}  |  {callbacks} callbacks  |  "
          f"{snap['sample_rate']:.0f} Hz / {snap['block_size']} samples")
    print(f"{'=' * 56}")
    print(f"  Budget:    {fmt_us(budget)}")
    print(f"  Avg:       {fmt_us(avg):>10}   ({avg / budget * 100:.1f}% of budget)" if budget > 0 else "")
    print(f"  Peak:      {fmt_us(peak):>10}   ({peak / budget * 100:.1f}% of budget)" if budget > 0 else "")
    print(f"  CPU load:  {cpu:.2f}%")
    print(f"  Xruns:     {xruns}")

    if slots:
        print(f"\n  {'Slot':>6}  {'Handle':>8}  {'Avg':>12}  {'Peak':>12}")
        print(f"  {'-' * 6}  {'-' * 8}  {'-' * 12}  {'-' * 12}")
        for slot in slots:
            print(f"  {slots.index(slot):>6}  {slot['handle']:>8}  "
                  f"{fmt_us(slot['avg_us']):>12}  {fmt_us(slot['peak_us']):>12}")


def main():
    with Squeeze(SAMPLE_RATE, BLOCK_SIZE) as s:

        # ── build a mixer ────────────────────────────────────────
        keys = s.add_source("Keys")
        bass = s.add_source("Bass")
        drums = s.add_source("Drums")
        pad = s.add_source("Pad")

        reverb = s.add_bus("Reverb")
        delay = s.add_bus("Delay")

        with s.batch():
            keys.route_to(s.master)
            keys.send(reverb, level=-6.0, tap="pre")

            bass.route_to(s.master)

            drums.route_to(s.master)
            drums.send(delay, level=-9.0)

            pad.route_to(s.master)
            pad.send(reverb, level=-3.0)

            reverb.route_to(s.master)
            delay.route_to(s.master)

        # add some insert processors to make the graph meatier
        for src in [keys, bass, drums, pad]:
            src.chain.append()   # "eq"
            src.chain.append()   # "comp"

        s.master.chain.append()  # master limiter

        print(f"Mixer: {s.source_count} sources, {s.bus_count} buses")
        print(f"Engine: {SAMPLE_RATE:.0f} Hz, {BLOCK_SIZE} samples/block")

        # ── enable monitoring ────────────────────────────────────
        s.perf.enabled = True
        s.perf.slot_profiling = True

        # schedule some notes so the sources are doing work
        t = s.transport
        t.tempo = 128.0
        t.set_time_signature(4, 4)
        t.play()

        for beat in range(64):
            keys.note_on(float(beat), 1, 60 + (beat % 12), 0.7)
            keys.note_off(beat + 0.8, 1, 60 + (beat % 12))

            if beat % 2 == 0:
                bass.note_on(float(beat), 1, 36, 0.9)
                bass.note_off(beat + 1.8, 1, 36)

            if beat % 4 == 0:
                drums.note_on(float(beat), 10, 36, 1.0)      # kick
            if beat % 2 == 0:
                drums.note_on(float(beat), 10, 42, 0.7)      # hat

            if beat % 8 == 0:
                pad.note_on(float(beat), 1, 48, 0.5)
                pad.note_off(beat + 7.5, 1, 48)

        # ── render passes ────────────────────────────────────────
        for pass_num in range(1, 4):
            for _ in range(BLOCKS_PER_PASS):
                s.render(BLOCK_SIZE)

            snap = s.perf.snapshot()
            slots = s.perf.slots()
            print_snapshot(snap, slots, pass_num)

        # ── threshold experiment ─────────────────────────────────
        print(f"\n{'─' * 56}")
        print(f"  Lowering xrun threshold to 0.1 (10% of budget)...")
        s.perf.reset()
        s.perf.xrun_threshold = 0.1

        for _ in range(BLOCKS_PER_PASS):
            s.render(BLOCK_SIZE)

        snap = s.perf.snapshot()
        threshold_us = s.perf.xrun_threshold * snap["buffer_duration_us"]
        print(f"  Threshold:  {fmt_us(threshold_us)} "
              f"({s.perf.xrun_threshold:.1f}x of {fmt_us(snap['buffer_duration_us'])} budget)")
        print(f"  Avg:        {fmt_us(snap['callback_avg_us'])}")
        print(f"  Xruns:      {snap['xrun_count']}"
              f"{'  (callbacks too fast to exceed even 10% of budget!)' if snap['xrun_count'] == 0 else ''}")

        # ── final summary ────────────────────────────────────────
        t.stop()
        print(f"\n  Done — rendered {4 * BLOCKS_PER_PASS * BLOCK_SIZE:,} samples")


if __name__ == "__main__":
    main()
