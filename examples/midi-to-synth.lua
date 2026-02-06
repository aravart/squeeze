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
local midi, err = sq.add_midi_input(midi_name)
if not midi then
    print("Error: " .. err)
    return
end
print("MIDI node id=" .. midi.id)

-- Find and load the first instrument plugin
local plugins = sq.list_plugins()
local synth = nil

for _, name in ipairs(plugins) do
    local info = sq.plugin_info(name)
    if info and info.instrument then
        local node, serr = sq.add_plugin(name)
        if node then
            synth = node
            break
        end
    end
end

if not synth then
    print("No loadable instrument plugins found.")
    return
end

print("Loaded synth: " .. synth.name .. " (id=" .. synth.id .. ")")

-- Print ports
local ports = synth:ports()
print("  Inputs:")
for _, p in ipairs(ports.inputs) do
    print("    " .. p.name .. " (" .. p.type .. ", " .. p.channels .. "ch)")
end
print("  Outputs:")
for _, p in ipairs(ports.outputs) do
    print("    " .. p.name .. " (" .. p.type .. ", " .. p.channels .. "ch)")
end

-- Connect MIDI output to synth MIDI input
local conn, cerr = sq.connect(midi, "midi_out", synth, "midi_in")
if not conn then
    print("Connect error: " .. cerr)
    return
end
print("\nConnected: " .. midi.name .. " -> " .. synth.name)

-- Push graph and start audio
sq.update()
sq.start(44100, 512)
print("Playing! Use the REPL to adjust, or Ctrl+C to quit.")
