#ifndef SQUEEZE_FFI_H
#define SQUEEZE_FFI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opaque handle ─────────────────────────────────────────────── */

typedef void* SqEngine;

/* ── String ownership ──────────────────────────────────────────── */

/// Free a string returned by any sq_* function.
/// Passing NULL is safe (no-op).
void sq_free_string(char* s);

/* ── Logging ───────────────────────────────────────────────────── */

/// Set log level globally. 0=off, 1=warn, 2=info, 3=debug, 4=trace.
/// Does not require an SqEngine handle.
void sq_set_log_level(int level);

/// Set a callback to receive log messages. Pass NULL to revert to stderr.
/// The callback receives: level (int), message (const char*), userData.
/// Callback is invoked from the control thread only.
void sq_set_log_callback(void (*callback)(int level, const char* message, void* user_data),
                         void* user_data);

/* ── Engine lifecycle ──────────────────────────────────────────── */

/// Create a new engine. Returns NULL on failure (sets *error).
/// Caller owns the returned handle; free with sq_engine_destroy().
/// Initializes JUCE MessageManager on first call (static, process-wide).
SqEngine sq_engine_create(char** error);

/// Destroy the engine and free all resources.
/// Passing NULL is safe (no-op).
void sq_engine_destroy(SqEngine engine);

/* ── Version ───────────────────────────────────────────────────── */

/// Returns the engine version string. Caller must sq_free_string() the result.
char* sq_version(SqEngine engine);

/* ── Port / Parameter C structs ────────────────────────────────── */

typedef struct {
    char* name;
    int   direction;    /* 0 = input, 1 = output */
    int   signal_type;  /* 0 = audio, 1 = midi   */
    int   channels;
} SqPortDescriptor;

typedef struct {
    SqPortDescriptor* ports;
    int               count;
} SqPortList;

typedef struct {
    char* name;
    float default_value;
    int   num_steps;
    bool  automatable;
    bool  boolean_param;
    char* label;
    char* group;
} SqParamDescriptor;

typedef struct {
    SqParamDescriptor* descriptors;
    int                count;
} SqParamDescriptorList;

/* ── Node management ───────────────────────────────────────────── */

/// Add a GainNode. Returns node id (>0).
int sq_add_gain(SqEngine engine);

/// Remove a node by id. Returns false if not found (or if node is the output).
bool sq_remove_node(SqEngine engine, int node_id);

/// Returns the built-in output node id.
int sq_output_node(SqEngine engine);

/// Returns the total number of nodes (including the output node).
int sq_node_count(SqEngine engine);

/// Returns the node's type name. Caller must sq_free_string().
/// Returns NULL if node_id is invalid.
char* sq_node_name(SqEngine engine, int node_id);

/// Returns all ports for a node. Free with sq_free_port_list().
/// Returns empty list if node_id is invalid.
SqPortList sq_get_ports(SqEngine engine, int node_id);

/// Free a port list returned by sq_get_ports().
void sq_free_port_list(SqPortList list);

/// Returns parameter descriptors. Free with sq_free_param_descriptor_list().
/// Returns empty list if node_id is invalid.
SqParamDescriptorList sq_param_descriptors(SqEngine engine, int node_id);

/// Free a parameter descriptor list.
void sq_free_param_descriptor_list(SqParamDescriptorList list);

/// Get a parameter value by name. Returns 0.0f if node_id or name is invalid.
float sq_get_param(SqEngine engine, int node_id, const char* name);

/// Set a parameter value by name. Returns false if node_id is invalid.
bool sq_set_param(SqEngine engine, int node_id, const char* name, float value);

/// Get parameter display text. Caller must sq_free_string().
/// Returns NULL if node_id or name is invalid.
char* sq_param_text(SqEngine engine, int node_id, const char* name);

/* ── Connection C structs ─────────────────────────────────────────── */

typedef struct {
    int   id;
    int   src_node;
    char* src_port;
    int   dst_node;
    char* dst_port;
} SqConnection;

typedef struct {
    SqConnection* connections;
    int           count;
} SqConnectionList;

/* ── Connection management ────────────────────────────────────────── */

/// Connect two ports. Returns connection id (>= 0) on success, -1 on failure.
/// On failure, *error is set (caller must sq_free_string it). On success, *error is NULL.
int sq_connect(SqEngine engine, int src_node, const char* src_port,
               int dst_node, const char* dst_port, char** error);

/// Disconnect by connection id. Returns false if not found.
bool sq_disconnect(SqEngine engine, int conn_id);

/// Get all connections. Free with sq_free_connection_list().
SqConnectionList sq_connections(SqEngine engine);

/// Free a connection list returned by sq_connections().
void sq_free_connection_list(SqConnectionList list);

/* ── Transport ────────────────────────────────────────────────── */

void   sq_transport_play(SqEngine engine);
void   sq_transport_stop(SqEngine engine);
void   sq_transport_pause(SqEngine engine);
void   sq_transport_set_tempo(SqEngine engine, double bpm);
void   sq_transport_set_time_signature(SqEngine engine, int numerator, int denominator);
void   sq_transport_seek_samples(SqEngine engine, int64_t samples);
void   sq_transport_seek_beats(SqEngine engine, double beats);
void   sq_transport_set_loop_points(SqEngine engine, double start_beats, double end_beats);
void   sq_transport_set_looping(SqEngine engine, bool enabled);
double sq_transport_position(SqEngine engine);
double sq_transport_tempo(SqEngine engine);
bool   sq_transport_is_playing(SqEngine engine);

/* ── Event scheduling ─────────────────────────────────────────── */

bool sq_schedule_note_on(SqEngine engine, int node_id, double beat_time,
                         int channel, int note, float velocity);
bool sq_schedule_note_off(SqEngine engine, int node_id, double beat_time,
                          int channel, int note);
bool sq_schedule_cc(SqEngine engine, int node_id, double beat_time,
                    int channel, int cc_num, int cc_val);
bool sq_schedule_param_change(SqEngine engine, int node_id, double beat_time,
                              const char* param_name, float value);

/* ── String list ──────────────────────────────────────────────── */

typedef struct {
    char** items;
    int    count;
} SqStringList;

/// Free a string list returned by sq_available_plugins().
void sq_free_string_list(SqStringList list);

/* ── Plugin nodes ─────────────────────────────────────────────── */

/// Add a built-in test synth (PluginNode wrapping TestProcessor).
/// 0 audio inputs, 2 audio outputs, accepts MIDI. Has "Gain" and "Mix" parameters.
/// Returns node id (>0).
int sq_add_test_synth(SqEngine engine);

/* ── Plugin manager ──────────────────────────────────────────── */

/// Load a plugin cache XML file. Returns false on failure (sets *error).
bool sq_load_plugin_cache(SqEngine engine, const char* path, char** error);

/// Add a plugin by name. Returns node id (>0) on success, -1 on failure (sets *error).
/// The engine must be prepared (prepareForTesting or audio device) before calling this.
int sq_add_plugin(SqEngine engine, const char* name, char** error);

/// Return the list of available plugin names (sorted). Free with sq_free_string_list().
SqStringList sq_available_plugins(SqEngine engine);

/// Return the number of plugins in the loaded cache.
int sq_num_plugins(SqEngine engine);

/* ── MIDI device C structs ────────────────────────────────────── */

typedef struct {
    int   id;
    char* device;
    int   node_id;
    int   channel_filter;
    int   note_filter;
} SqMidiRoute;

typedef struct {
    SqMidiRoute* routes;
    int          count;
} SqMidiRouteList;

/// Free a MIDI route list returned by sq_midi_routes().
void sq_free_midi_route_list(SqMidiRouteList list);

/* ── MIDI device management ──────────────────────────────────── */

/// Return available MIDI input devices. Free with sq_free_string_list().
SqStringList sq_midi_devices(SqEngine engine);

/// Open a MIDI input device by name. Returns false on failure (sets *error).
bool sq_midi_open(SqEngine engine, const char* name, char** error);

/// Close a MIDI input device by name. No-op if not open.
void sq_midi_close(SqEngine engine, const char* name);

/// Return currently open MIDI devices. Free with sq_free_string_list().
SqStringList sq_midi_open_devices(SqEngine engine);

/* ── MIDI routing ─────────────────────────────────────────────── */

/// Route a MIDI device to a node. Returns route id (>= 0), -1 on failure (sets *error).
/// channel_filter: 0=all, 1-16=specific. note_filter: -1=all, 0-127=specific.
int sq_midi_route(SqEngine engine, const char* device, int node_id,
                  int channel_filter, int note_filter, char** error);

/// Remove a MIDI route by id. Returns false if not found.
bool sq_midi_unroute(SqEngine engine, int route_id);

/// Get all MIDI routes. Free with sq_free_midi_route_list().
SqMidiRouteList sq_midi_routes(SqEngine engine);

/* ── Audio device ─────────────────────────────────────────────── */

/// Start the audio device with the given sample rate and block size hints.
/// Returns false on failure (sets *error). The actual device SR/BS may differ
/// from the hints — use sq_sample_rate() / sq_block_size() to query actual values.
bool sq_start(SqEngine engine, double sample_rate, int block_size, char** error);

/// Stop the audio device. No-op if not running.
void sq_stop(SqEngine engine);

/// Returns true if the audio device is currently running.
bool sq_is_running(SqEngine engine);

/// Returns the actual device sample rate (0.0 if not running).
double sq_sample_rate(SqEngine engine);

/// Returns the actual device block size (0 if not running).
int sq_block_size(SqEngine engine);

/* ── Plugin editor ────────────────────────────────────────────── */

/// Open the native editor window for a plugin node.
/// Returns false on failure (sets *error). Caller must sq_free_string the error.
/// Fails if: node not found, not a plugin, no editor, already open, GUI timeout.
bool sq_open_editor(SqEngine engine, int node_id, char** error);

/// Close the editor window for a plugin node.
/// Returns false on failure (sets *error). Caller must sq_free_string the error.
bool sq_close_editor(SqEngine engine, int node_id, char** error);

/// Returns true if an editor window is currently open for this node.
bool sq_has_editor(SqEngine engine, int node_id);

/// Process pending JUCE GUI/message events.
/// With timeout_ms=0, processes pending events and returns immediately (non-blocking).
/// With timeout_ms>0, processes events for up to that many milliseconds (blocking).
/// Call from the main thread. Does not require an SqEngine handle.
void sq_process_events(int timeout_ms);

/* ── Testing ──────────────────────────────────────────────────── */

/// Prepare engine for headless testing. Must be called before sq_render().
void sq_prepare_for_testing(SqEngine engine, double sample_rate, int block_size);

/// Render one block in test mode (allocates output buffer internally).
void sq_render(SqEngine engine, int num_samples);

#ifdef __cplusplus
}
#endif

#endif /* SQUEEZE_FFI_H */
