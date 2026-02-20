#!/usr/bin/env python3
"""Demo: Buffer creation, file loading, and playback with PlayerProcessor.

Shows off:
  - Creating buffers with programmatic audio (sine waves, chords)
  - Loading audio files from disk (if a path is provided)
  - PlayerProcessor playback with speed and loop control
  - BufferLibrary queries (buffer_info, buffers listing)
  - Live audio output
"""

from __future__ import annotations

import math
import sys

from squeeze import Squeeze, BufferInfo


def generate_sine(freq: float, duration: float, sample_rate: float = 44100.0,
                  amplitude: float = 0.3) -> list[float]:
    """Generate a sine wave."""
    n = int(sample_rate * duration)
    return [amplitude * math.sin(2 * math.pi * freq * i / sample_rate) for i in range(n)]


def generate_chord(freqs: list[float], duration: float,
                   sample_rate: float = 44100.0,
                   amplitude: float = 0.2) -> list[float]:
    """Generate a chord by summing sine waves."""
    n = int(sample_rate * duration)
    return [
        sum(amplitude * math.sin(2 * math.pi * f * i / sample_rate) for f in freqs)
        for i in range(n)
    ]


def main() -> None:
    sr = 44100.0
    s = Squeeze(sample_rate=sr, block_size=512, plugins=False)

    print("Squeeze engine created")
    print(f"  Version: {s.version}")
    print()

    # --- Create buffers with programmatic audio ---

    # A major chord: A4, C#5, E5
    chord_data = generate_chord([440.0, 554.37, 659.25], duration=2.0, sample_rate=sr)
    chord_buf = s.create_buffer(
        channels=2, length=len(chord_data), sample_rate=sr, name="A major chord"
    )
    chord_buf.write(channel=0, data=chord_data)
    chord_buf.write(channel=1, data=chord_data)

    # A simple kick-like thump (decaying sine at 60 Hz)
    kick_n = int(sr * 0.3)
    kick_data = [
        0.8 * math.sin(2 * math.pi * 60 * i / sr) * math.exp(-i / (sr * 0.05))
        for i in range(kick_n)
    ]
    kick_buf = s.create_buffer(
        channels=2, length=kick_n, sample_rate=sr, name="kick"
    )
    kick_buf.write(channel=0, data=kick_data)
    kick_buf.write(channel=1, data=kick_data)

    # --- Optionally load a file from disk ---
    file_buf_id = None
    if len(sys.argv) > 1:
        path = sys.argv[1]
        print(f"Loading audio file: {path}")
        try:
            file_buf_id = s.load_buffer(path)
            info = s.buffer_info(file_buf_id)
            print(f"  Loaded: {info.name}")
            print(f"  Channels: {info.num_channels}, Length: {info.length} samples")
            print(f"  Sample rate: {info.sample_rate} Hz, Duration: {info.length_seconds:.2f}s")
            if info.file_path:
                print(f"  File: {info.file_path}")
        except Exception as e:
            print(f"  Failed to load: {e}")
            file_buf_id = None
        print()

    # --- Show all buffers ---
    print(f"Buffers in engine ({s.buffer_count}):")
    for buf_id, name in s.buffers:
        info = s.buffer_info(buf_id)
        print(f"  [{buf_id}] {name:20s}  {info.num_channels}ch  "
              f"{info.length:>7d} samples  {info.length_seconds:.2f}s  "
              f"{info.sample_rate:.0f} Hz")
    print()

    # --- Set up playback ---
    # Play the chord (or loaded file) with looping
    play_buf_id = file_buf_id if file_buf_id else chord_buf.buffer_id

    player = s.add_source("Player", player=True)
    player.set_buffer(play_buf_id)
    player.route_to(s.master)
    player["fade_ms"] = 10.0
    player["loop_mode"] = 1.0    # forward loop
    player["speed"] = 1.0

    # Play the kick on a separate player, no loop
    kick_player = s.add_source("Kick", player=True)
    kick_player.set_buffer(kick_buf.buffer_id)
    kick_player.route_to(s.master)
    kick_player["fade_ms"] = 0.0
    kick_player["loop_mode"] = 0.0   # one-shot

    print("Starting audio playback...")
    print(f"  Main player: buffer [{play_buf_id}], looping at 1x speed")
    print(f"  Kick: buffer [{kick_buf.buffer_id}], one-shot")
    print()
    print("Playing for 5 seconds (main loop)...")
    print("  - Kick triggers at start")
    print("  - Speed changes to 0.5x at 2s, then 1.5x at 3.5s")
    print()

    # Start audio device and trigger playback
    s.start()
    player["playing"] = 1.0
    kick_player["playing"] = 1.0

    # Phase 1: normal speed (0-2s)
    s.run(seconds=2.0)

    # Phase 2: half speed (2-3.5s)
    print("  -> Speed: 0.5x")
    player["speed"] = 0.5
    s.run(seconds=1.5)

    # Phase 3: 1.5x speed (3.5-5s)
    print("  -> Speed: 1.5x")
    player["speed"] = 1.5
    s.run(seconds=1.5)

    # Done
    print()
    print("Done!")
    s.stop()
    s.close()


if __name__ == "__main__":
    main()
