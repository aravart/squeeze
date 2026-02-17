#!/usr/bin/env python3
"""Squeeze v2 demo — build a synth graph, route MIDI, and play.

Usage:
    cd python && pip install -e .
    python examples/demo.py [--plugin "Plugin Name"]

Requires: built libsqueeze_ffi (cmake --build build).
Works best with a MIDI controller connected.
"""

import argparse
import os
import sys
import time

# Add python/ to path so squeeze is importable without install
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from squeeze import Squeeze, SqueezeError, set_log_level

CACHE_PATH = os.path.join(os.path.dirname(__file__), "..", "..", "plugin-cache.xml")


def main():
    parser = argparse.ArgumentParser(description="Squeeze v2 demo")
    parser.add_argument("--plugin", type=str, default=None,
                        help="Plugin name to load (requires plugin-cache.xml)")
    parser.add_argument("--list-plugins", action="store_true",
                        help="List available plugins and exit")
    parser.add_argument("--log-level", type=int, default=1,
                        help="Log level: 0=off 1=warn 2=info 3=debug 4=trace")
    args = parser.parse_args()

    set_log_level(args.log_level)

    with Squeeze(44100.0, 512) as sq:
        print(f"Squeeze v{sq.version}")
        print()

        # --- Load plugin cache (if available) ---
        if os.path.exists(CACHE_PATH):
            sq.load_plugin_cache(CACHE_PATH)
            plugins = sq.available_plugins
            print(f"Plugin cache: {len(plugins)} plugins loaded")
        else:
            plugins = []

        if args.list_plugins:
            if not plugins:
                print("No plugin cache found.")
            else:
                for name in plugins:
                    print(f"  {name}")
            return

        print()

        # --- Add a synth node ---
        synth_id = None
        synth_name = None

        if args.plugin:

            try:
                synth_id = sq.add_plugin(args.plugin)
                synth_name = args.plugin
            except SqueezeError as e:
                print(f"Failed to load '{args.plugin}': {e}")
                print("Falling back to test synth.")

        if synth_id is None:
            synth_id = sq.add_test_synth()
            synth_name = sq.node_name(synth_id)

        print(f"Synth: {synth_name} (node {synth_id})")

        # Show ports
        ports = sq.get_ports(synth_id)
        for p in ports:
            print(f"  {p['direction']:>6} {p['signal_type']:>5}  {p['name']} ({p['channels']}ch)")

        # Show parameters
        params = sq.param_descriptors(synth_id)
        if params:
            print(f"  Parameters:")
            for p in params:
                val = sq.get_param(synth_id, p["name"])
                text = sq.param_text(synth_id, p["name"])
                print(f"    {p['name']}: {val:.3f} ({text})")
        print()

        # --- Connect synth -> output ---
        out_id = sq.output

        synth_out = next((p["name"] for p in ports
                          if p["direction"] == "output" and p["signal_type"] == "audio"), None)
        out_ports = sq.get_ports(out_id)
        out_in = next((p["name"] for p in out_ports
                       if p["direction"] == "input" and p["signal_type"] == "audio"), None)

        if synth_out and out_in:
            conn_id = sq.connect(synth_id, synth_out, out_id, out_in)
            print(f"Graph: {synth_name}:{synth_out} -> output:{out_in}")
        else:
            print("Could not find matching audio ports to connect.")
        print()

        # --- MIDI devices ---
        devices = sq.midi_devices
        if devices:
            print(f"MIDI devices ({len(devices)}):")
            for dev in devices:
                try:
                    sq.midi_open(dev)
                    route_id = sq.midi_route(dev, synth_id)
                    print(f"  {dev} -> {synth_name} (route {route_id})")
                except SqueezeError as e:
                    print(f"  {dev}: {e}")
            print()

        # --- Start audio ---
        try:
            sq.start(44100.0, 512)
            print(f"Audio: sr={sq.sample_rate:.0f} bs={sq.block_size}")
            print("Playing! Press Ctrl+C to stop.\n")

            while True:
                time.sleep(1)

        except SqueezeError:
            print("No audio device available — rendering offline instead.")
            print()


            # Schedule a C major arpeggio
            sq.transport_set_tempo(120.0)
            notes = [60, 64, 67, 72, 67, 64]
            for i, note in enumerate(notes):
                sq.schedule_note_on(synth_id, float(i) * 0.5, 1, note, 0.8)
                sq.schedule_note_off(synth_id, float(i) * 0.5 + 0.4, 1, note)

            sq.transport_play()
            blocks = int(44100 * 3 / 512)
            for _ in range(blocks):
                sq.render(512)
            sq.transport_stop()

            print(f"Rendered {blocks * 512 / 44100:.1f}s offline.")

        except KeyboardInterrupt:
            print("\nStopping...")
            sq.stop()

        print("Done.")


if __name__ == "__main__":
    main()
