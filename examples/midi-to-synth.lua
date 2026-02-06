-- midi-to-synth.lua
-- Connect a MIDI input device to the first available instrument plugin.
-- Usage: ./Squeeze -d examples/midi-to-synth.lua -i

-- List MIDI devices
local devices = sq.list_midi_inputs()
print("MIDI inputs: " .. #devices)
for i, name in ipairs(devices) do
    print("  " .. i .. ": " .. name)
end

if #devices == 0 then
    print("No MIDI devices found.")
    return
end

-- Open the first MIDI device
local midi_name = devices[1]
print("\nOpening: " .. midi_name)
local midi_id, err = sq.add_midi_input(midi_name)
if not midi_id then
    print("Error: " .. err)
    return
end
print("MIDI node id=" .. midi_id)

-- Find and load the first instrument plugin
local plugins = sq.list_plugins()
local synth_id = nil
local synth_name = nil

for _, name in ipairs(plugins) do
    local info = sq.plugin_info(name)
    if info and info.instrument then
        local id, serr = sq.add_plugin(name)
        if id then
            synth_id = id
            synth_name = name
            break
        end
    end
end

if not synth_id then
    print("No loadable instrument plugins found.")
    return
end

print("Loaded synth: " .. synth_name .. " (id=" .. synth_id .. ")")

-- Print ports
local ports = sq.ports(synth_id)
print("  Inputs:")
for _, p in ipairs(ports.inputs) do
    print("    " .. p.name .. " (" .. p.type .. ", " .. p.channels .. "ch)")
end
print("  Outputs:")
for _, p in ipairs(ports.outputs) do
    print("    " .. p.name .. " (" .. p.type .. ", " .. p.channels .. "ch)")
end

-- Connect MIDI output to synth MIDI input
local conn, cerr = sq.connect(midi_id, "midi_out", synth_id, "midi_in")
if not conn then
    print("Connect error: " .. cerr)
    return
end
print("\nConnected: " .. midi_name .. " -> " .. synth_name)

-- Push graph and start audio
sq.update()
sq.start(44100, 512)
print("Playing! Use the REPL to adjust, or Ctrl+C to quit.")
