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
void sq_set_log_level(int level);

/// Set a callback to receive log messages. Pass NULL to revert to stderr.
void sq_set_log_callback(void (*callback)(int level, const char* message, void* user_data),
                         void* user_data);

/* ── Engine lifecycle ──────────────────────────────────────────── */

/// Create a new engine with the given sample rate and block size.
/// Returns NULL on failure (sets *error).
SqEngine sq_engine_create(double sample_rate, int block_size, char** error);

/// Destroy the engine and free all resources.
void sq_engine_destroy(SqEngine engine);

/* ── Version / Info ────────────────────────────────────────────── */

/// Returns the engine version string. Caller must sq_free_string().
char* sq_version(SqEngine engine);

/// Returns the engine sample rate.
double sq_engine_sample_rate(SqEngine engine);

/// Returns the engine block size.
int sq_engine_block_size(SqEngine engine);

/* ── Source management ─────────────────────────────────────────── */

/// Add a source with a GainProcessor generator (for testing).
/// Returns the source handle (>0), or -1 on failure.
int sq_add_source(SqEngine engine, const char* name);

/// Add a source with a custom generator (used internally by sq_add_source_plugin).
/// Returns the source handle, or -1 on failure.
int sq_add_source_with_generator(SqEngine engine, const char* name, int gen_proc_handle);

/// Remove a source by handle. Returns false if not found.
bool sq_remove_source(SqEngine engine, int source_handle);

/// Returns the number of sources.
int sq_source_count(SqEngine engine);

/// Returns the generator processor handle for a source, or -1 if not found.
int sq_source_generator(SqEngine engine, int source_handle);

/// Returns the source name. Caller must sq_free_string().
char* sq_source_name(SqEngine engine, int source_handle);

/// Returns the source gain (linear).
float sq_source_gain(SqEngine engine, int source_handle);

/// Set the source gain (linear).
void sq_source_set_gain(SqEngine engine, int source_handle, float gain);

/// Returns the source pan (-1.0 to 1.0).
float sq_source_pan(SqEngine engine, int source_handle);

/// Set the source pan (-1.0 to 1.0).
void sq_source_set_pan(SqEngine engine, int source_handle, float pan);

/// Returns true if the source is bypassed.
bool sq_source_bypassed(SqEngine engine, int source_handle);

/// Set the source bypass state.
void sq_source_set_bypassed(SqEngine engine, int source_handle, bool bypassed);

/// Assign MIDI input to a source.
void sq_source_midi_assign(SqEngine engine, int source_handle,
                           const char* device, int channel,
                           int note_low, int note_high);

/* ── Bus management ────────────────────────────────────────────── */

/// Add a bus. Returns the bus handle (>0).
int sq_add_bus(SqEngine engine, const char* name);

/// Remove a bus by handle. Returns false if Master or not found.
bool sq_remove_bus(SqEngine engine, int bus_handle);

/// Returns the number of buses (including Master).
int sq_bus_count(SqEngine engine);

/// Returns the Master bus handle.
int sq_master(SqEngine engine);

/// Returns the bus name. Caller must sq_free_string().
char* sq_bus_name(SqEngine engine, int bus_handle);

/// Returns the bus gain (linear).
float sq_bus_gain(SqEngine engine, int bus_handle);

/// Set the bus gain (linear).
void sq_bus_set_gain(SqEngine engine, int bus_handle, float gain);

/// Returns the bus pan (-1.0 to 1.0).
float sq_bus_pan(SqEngine engine, int bus_handle);

/// Set the bus pan (-1.0 to 1.0).
void sq_bus_set_pan(SqEngine engine, int bus_handle, float pan);

/// Returns true if the bus is bypassed.
bool sq_bus_bypassed(SqEngine engine, int bus_handle);

/// Set the bus bypass state.
void sq_bus_set_bypassed(SqEngine engine, int bus_handle, bool bypassed);

/* ── Routing ───────────────────────────────────────────────────── */

/// Route a source to a bus.
void sq_route(SqEngine engine, int source_handle, int bus_handle);

/// Add a send from a source to a bus. Returns send ID, or -1 on failure.
/// pre_fader: 1 = pre-fader, 0 = post-fader.
int sq_send(SqEngine engine, int source_handle, int bus_handle, float level_db, int pre_fader);

/// Remove a send from a source.
void sq_remove_send(SqEngine engine, int source_handle, int send_id);

/// Set send level.
void sq_set_send_level(SqEngine engine, int source_handle, int send_id, float level_db);

/// Set send tap point. pre_fader: 1 = pre-fader, 0 = post-fader.
void sq_set_send_tap(SqEngine engine, int source_handle, int send_id, int pre_fader);

/// Route a bus to another bus. Returns false if would create cycle.
bool sq_bus_route(SqEngine engine, int from_handle, int to_handle);

/// Add a send from one bus to another. Returns send ID, or -1 on failure/cycle.
/// pre_fader: 1 = pre-fader, 0 = post-fader.
int sq_bus_send(SqEngine engine, int from_handle, int to_handle, float level_db, int pre_fader);

/// Remove a send from a bus.
void sq_bus_remove_send(SqEngine engine, int bus_handle, int send_id);

/// Set bus send level.
void sq_bus_set_send_level(SqEngine engine, int bus_handle, int send_id, float level_db);

/// Set bus send tap point. pre_fader: 1 = pre-fader, 0 = post-fader.
void sq_bus_set_send_tap(SqEngine engine, int bus_handle, int send_id, int pre_fader);

/* ── Source chain ──────────────────────────────────────────────── */

/// Append a GainProcessor to source's chain. Returns proc handle.
int sq_source_append_proc(SqEngine engine, int source_handle);

/// Insert a GainProcessor at index. Returns proc handle.
int sq_source_insert_proc(SqEngine engine, int source_handle, int index);

/// Remove processor at index from source's chain.
void sq_source_remove_proc(SqEngine engine, int source_handle, int index);

/// Returns the number of processors in source's chain.
int sq_source_chain_size(SqEngine engine, int source_handle);

/* ── Bus chain ─────────────────────────────────────────────────── */

/// Append a GainProcessor to bus's chain. Returns proc handle.
int sq_bus_append_proc(SqEngine engine, int bus_handle);

/// Insert a GainProcessor at index. Returns proc handle.
int sq_bus_insert_proc(SqEngine engine, int bus_handle, int index);

/// Remove processor at index from bus's chain.
void sq_bus_remove_proc(SqEngine engine, int bus_handle, int index);

/// Returns the number of processors in bus's chain.
int sq_bus_chain_size(SqEngine engine, int bus_handle);

/* ── Parameters (by processor handle) ─────────────────────────── */

typedef struct {
    char* name;
    float default_value;
    float min_value;
    float max_value;
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

/// Get a parameter value by name. Returns 0.0f if invalid.
float sq_get_param(SqEngine engine, int proc_handle, const char* name);

/// Set a parameter value by name. Returns false if invalid.
bool sq_set_param(SqEngine engine, int proc_handle, const char* name, float value);

/// Get parameter display text. Caller must sq_free_string().
char* sq_param_text(SqEngine engine, int proc_handle, const char* name);

/// Returns parameter descriptors. Free with sq_free_param_descriptor_list().
SqParamDescriptorList sq_param_descriptors(SqEngine engine, int proc_handle);

/// Free a parameter descriptor list.
void sq_free_param_descriptor_list(SqParamDescriptorList list);

/* ── Metering ──────────────────────────────────────────────────── */

/// Returns peak level for a bus.
float sq_bus_peak(SqEngine engine, int bus_handle);

/// Returns RMS level for a bus.
float sq_bus_rms(SqEngine engine, int bus_handle);

/* ── Batching ──────────────────────────────────────────────────── */

/// Begin batching — defers snapshot rebuilds until sq_batch_commit().
void sq_batch_begin(SqEngine engine);

/// Commit batch — rebuilds snapshot if dirty.
void sq_batch_commit(SqEngine engine);

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
bool   sq_transport_is_looping(SqEngine engine);

/* ── Event scheduling ─────────────────────────────────────────── */

bool sq_schedule_note_on(SqEngine engine, int source_handle, double beat_time,
                         int channel, int note, float velocity);
bool sq_schedule_note_off(SqEngine engine, int source_handle, double beat_time,
                          int channel, int note);
bool sq_schedule_cc(SqEngine engine, int source_handle, double beat_time,
                    int channel, int cc_num, int cc_val);
bool sq_schedule_pitch_bend(SqEngine engine, int source_handle, double beat_time,
                            int channel, int value);
bool sq_schedule_param_change(SqEngine engine, int proc_handle, double beat_time,
                              const char* param_name, float value);

/* ── String list ──────────────────────────────────────────────── */

typedef struct {
    char** items;
    int    count;
} SqStringList;

void sq_free_string_list(SqStringList list);

/* ── Plugin manager ──────────────────────────────────────────── */

/// Load a plugin cache XML file. Returns false on failure (sets *error).
bool sq_load_plugin_cache(SqEngine engine, const char* path, char** error);

/// Add a plugin as a source generator. Returns source handle, or -1 on failure (sets *error).
int sq_add_plugin(SqEngine engine, const char* name, char** error);

/// Return the list of available plugin names (sorted). Free with sq_free_string_list().
SqStringList sq_available_plugins(SqEngine engine);

/// Return the number of plugins in the loaded cache.
int sq_num_plugins(SqEngine engine);

/* ── MIDI device C structs ────────────────────────────────────── */

typedef struct {
    int   id;
    char* device;
    int   target_handle;    /* source handle */
    int   channel_filter;
    int   note_filter;
} SqMidiRoute;

typedef struct {
    SqMidiRoute* routes;
    int          count;
} SqMidiRouteList;

void sq_free_midi_route_list(SqMidiRouteList list);

/* ── MIDI device management ──────────────────────────────────── */

SqStringList sq_midi_devices(SqEngine engine);
bool sq_midi_open(SqEngine engine, const char* name, char** error);
void sq_midi_close(SqEngine engine, const char* name);
SqStringList sq_midi_open_devices(SqEngine engine);

/* ── MIDI routing ─────────────────────────────────────────────── */

int sq_midi_route(SqEngine engine, const char* device, int source_handle,
                  int channel_filter, int note_filter, char** error);
bool sq_midi_unroute(SqEngine engine, int route_id);
SqMidiRouteList sq_midi_routes(SqEngine engine);

/* ── Audio device ─────────────────────────────────────────────── */

bool sq_start(SqEngine engine, double sample_rate, int block_size, char** error);
void sq_stop(SqEngine engine);
bool sq_is_running(SqEngine engine);
double sq_sample_rate(SqEngine engine);
int sq_block_size(SqEngine engine);

/* ── Plugin editor ────────────────────────────────────────────── */

/// Open editor for a plugin processor (by handle).
bool sq_open_editor(SqEngine engine, int proc_handle, char** error);

/// Close editor for a plugin processor (by handle).
bool sq_close_editor(SqEngine engine, int proc_handle, char** error);

/// Returns true if editor is open for this processor.
bool sq_has_editor(SqEngine engine, int proc_handle);

/// Process JUCE GUI events.
void sq_process_events(int timeout_ms);

/* ── Testing ──────────────────────────────────────────────────── */

void sq_render(SqEngine engine, int num_samples);

#ifdef __cplusplus
}
#endif

#endif /* SQUEEZE_FFI_H */
