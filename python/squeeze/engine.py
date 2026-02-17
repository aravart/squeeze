"""Engine — high-level entry point for the Squeeze audio engine."""

from __future__ import annotations

from squeeze._low_level import Squeeze
from squeeze.midi import Midi
from squeeze.node import Node
from squeeze.transport import Transport
from squeeze.types import Connection


class Engine:
    """Squeeze audio engine — high-level Pythonic interface.

    Usage:
        with Engine() as engine:
            synth = engine.add_test_synth()
            synth >> engine.output
            engine.start()
    """

    def __init__(self):
        self._sq = Squeeze()
        self._transport: Transport | None = None
        self._midi: Midi | None = None

    def close(self) -> None:
        """Destroy the engine. Safe to call multiple times."""
        self._sq.close()

    def __enter__(self) -> Engine:
        return self

    def __exit__(self, *args) -> None:
        self.close()

    def __del__(self):
        self.close()

    @property
    def version(self) -> str:
        """Engine version string."""
        return self._sq.version

    # --- Sub-objects ---

    @property
    def transport(self) -> Transport:
        """Transport control (play, stop, tempo, seek, loop)."""
        if self._transport is None:
            self._transport = Transport(self._sq)
        return self._transport

    @property
    def midi(self) -> Midi:
        """MIDI device management and routing."""
        if self._midi is None:
            self._midi = Midi(self, self._sq)
        return self._midi

    # --- Node management ---

    def add_gain(self) -> Node:
        """Add a GainNode. Returns a Node object."""
        node_id = self._sq.add_gain()
        return Node(self, node_id)

    def add_test_synth(self) -> Node:
        """Add a test synth (sine/noise, 2ch audio out, MIDI in).
        Returns a Node object."""
        node_id = self._sq.add_test_synth()
        return Node(self, node_id)

    def add_plugin(self, name: str) -> Node:
        """Add a plugin by name. Returns a Node object.
        Raises SqueezeError if plugin not found or instantiation fails."""
        node_id = self._sq.add_plugin(name)
        return Node(self, node_id)

    @property
    def output(self) -> Node:
        """The built-in output node."""
        return Node(self, self._sq.output)

    @property
    def node_count(self) -> int:
        """Total number of nodes (including the output node)."""
        return self._sq.node_count()

    def node(self, node_id: int) -> Node:
        """Wrap an existing node ID in a Node object.
        Useful for interop with the low-level API.
        Does not validate the ID — operations on an invalid Node
        will raise SqueezeError or return empty results.
        """
        return Node(self, node_id)

    # --- Connection management ---

    def connect(self, src: Node, src_port: str,
                dst: Node, dst_port: str) -> Connection:
        """Connect two specific ports. Returns a Connection.
        Raises SqueezeError on failure."""
        conn_id = self._sq.connect(src.id, src_port, dst.id, dst_port)
        return Connection(id=conn_id, src_node=src.id, src_port=src_port,
                          dst_node=dst.id, dst_port=dst_port)

    def disconnect(self, conn) -> bool:
        """Disconnect by Connection object or connection ID.
        Returns False if not found."""
        conn_id = conn.id if isinstance(conn, Connection) else conn
        return self._sq.disconnect(conn_id)

    @property
    def connections(self) -> list[Connection]:
        """All active connections as typed Connection objects."""
        return [
            Connection(
                id=c["id"],
                src_node=c["src_node"],
                src_port=c["src_port"],
                dst_node=c["dst_node"],
                dst_port=c["dst_port"],
            )
            for c in self._sq.connections()
        ]

    # --- Plugin management ---

    def load_plugin_cache(self, path: str) -> None:
        """Load plugin cache from XML file. Raises SqueezeError on failure."""
        self._sq.load_plugin_cache(path)

    @property
    def available_plugins(self) -> list[str]:
        """Available plugin names (sorted alphabetically)."""
        return self._sq.available_plugins

    @property
    def num_plugins(self) -> int:
        """Number of plugins in the loaded cache."""
        return self._sq.num_plugins

    # --- Audio device ---

    def start(self, sample_rate: float = 44100.0,
              block_size: int = 512) -> None:
        """Start the audio device. Raises SqueezeError on failure."""
        self._sq.start(sample_rate, block_size)

    def stop(self) -> None:
        """Stop the audio device. No-op if not running."""
        self._sq.stop()

    @property
    def running(self) -> bool:
        """True if the audio device is currently running."""
        return self._sq.is_running

    @property
    def sample_rate(self) -> float:
        """Actual device sample rate (0.0 if not running)."""
        return self._sq.sample_rate

    @property
    def block_size(self) -> int:
        """Actual device block size (0 if not running)."""
        return self._sq.block_size

    # --- Testing ---

    def prepare_for_testing(self, sample_rate: float = 44100.0,
                            block_size: int = 512) -> None:
        """Prepare engine for headless testing (no audio device)."""
        self._sq.prepare_for_testing(sample_rate, block_size)

    def render(self, num_samples: int = 512) -> None:
        """Render one block in test mode."""
        self._sq.render(num_samples)

    # --- Low-level access ---

    @property
    def sq(self) -> Squeeze:
        """Access the underlying low-level Squeeze instance.
        For advanced use or interop with code that uses the low-level API.
        """
        return self._sq
