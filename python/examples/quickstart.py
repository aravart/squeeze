"""
quickstart.py — Squeeze in 60 seconds.

Sets up a small mixer (two sources, one aux bus, master),
schedules a 4-bar phrase, and renders it headless.

No audio device or plugins required — uses the built-in
GainProcessor as a stand-in so you can run this anywhere.
"""

from squeeze import Squeeze

# ── create engine (44.1 kHz, 512-sample blocks) ──────────────
with Squeeze(44100.0, 512) as s:

    # ── sources ───────────────────────────────────────────────
    keys = s.add_source("Keys")
    bass = s.add_source("Bass")

    # ── aux bus for a shared effect return ────────────────────
    reverb = s.add_bus("Reverb")

    # ── routing ───────────────────────────────────────────────
    #   keys ──► master
    #        └─► reverb (pre-fader send @ -6 dB)
    #   bass ──► master
    #   reverb ──► master

    keys.route_to(s.master)
    keys.send(reverb, level=-6.0, tap="pre")

    bass.route_to(s.master)

    reverb.route_to(s.master)

    # ── mix ───────────────────────────────────────────────────
    keys.gain = 0.8
    keys.pan  = -0.3          # slightly left

    bass.gain = 0.9
    bass.pan  = 0.0           # center

    reverb.gain = 0.5         # return level
    s.master.gain = 0.85

    # ── generator parameters ──────────────────────────────────
    #  (GainProcessor exposes a single "gain" param)
    keys.generator.set_param("gain", 0.7)
    print("keys generator gain:", keys.generator.get_param("gain"))

    # ── insert chain ──────────────────────────────────────────
    #  append a processor to the keys insert chain
    eq = keys.chain.append()
    eq.set_param("gain", 0.9)
    print(f"keys chain length: {len(keys.chain)}")

    # ── transport & sequencing ────────────────────────────────
    t = s.transport
    t.tempo = 120.0
    t.set_time_signature(4, 4)
    t.set_loop(0.0, 16.0)     # loop 4 bars
    t.looping = True

    # schedule a short phrase on keys (channel 1)
    melody = [
        (0.0,  60), (1.0,  64), (2.0,  67), (3.0,  72),
        (4.0,  71), (5.0,  67), (6.0,  64), (7.0,  60),
    ]
    for beat, note in melody:
        keys.note_on(beat,  channel=1, note=note, velocity=0.8)
        keys.note_off(beat + 0.9, channel=1, note=note)

    # bass: root notes
    for bar in range(4):
        beat = bar * 4.0
        bass.note_on(beat,       channel=1, note=36, velocity=0.9)
        bass.note_off(beat + 3.5, channel=1, note=36)

    # ── render ────────────────────────────────────────────────
    blocks = 200               # ~2.3 s at 44.1 kHz / 512
    t.play()
    for _ in range(blocks):
        s.render(512)
    t.stop()

    # ── read meters ───────────────────────────────────────────
    print(f"master peak: {s.master.peak:.4f}")
    print(f"master rms:  {s.master.rms:.4f}")

    # ── summary ───────────────────────────────────────────────
    print(f"\ndone — rendered {blocks * 512} samples")
    print(f"  sources: {s.source_count}")
    print(f"  buses:   {s.bus_count}")
    print(f"  version: {s.version}")
