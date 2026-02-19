# Squeeze

Python client for the Squeeze audio engine — mixer-centric VST3/AU plugin hosting with beat-accurate MIDI scheduling.

## Install

From source (requires building `libsqueeze_ffi` first — see the [root README](../README.md)):

```bash
cd python && pip install -e .
```

## Quick Start

```python
from squeeze import Squeeze

with Squeeze() as s:
    synth = s.add_source("Lead", plugin="Diva")
    synth.route_to(s.master)

    s.transport.tempo = 120.0
    synth.note_on(0.0, channel=1, note=60, velocity=0.8)
    synth.note_off(1.0, channel=1, note=60)

    s.transport.play()
    s.render(512)
```

## API at a Glance

| Class | Role |
|-------|------|
| `Squeeze` | Engine entry point. Creates sources, buses; controls transport and audio device. |
| `Source` | Sound generator (plugin synth or gain). Has insert chain, gain/pan, routing, MIDI. |
| `Bus` | Summing point with insert chain and routing. Master bus always exists. |
| `Chain` | Ordered list of insert processors on a Source or Bus. |
| `Processor` | Single effect/instrument. Parameter access by name. |
| `Transport` | Play, stop, tempo, seek, loop. |
| `Midi` | MIDI device listing, routing, and management. |
| `Clock` | Beat-driven callbacks for generative music. |

See [INTEGRATION.md](squeeze/INTEGRATION.md) for the full API reference.

## Requirements

- Python >= 3.10
- `libsqueeze_ffi` shared library (built from the Squeeze C++ engine)

## License

GPLv3
