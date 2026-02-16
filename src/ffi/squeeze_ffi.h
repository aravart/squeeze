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

/// Remove a node by id. Returns false if not found.
bool sq_remove_node(SqEngine engine, int node_id);

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

#ifdef __cplusplus
}
#endif

#endif /* SQUEEZE_FFI_H */
