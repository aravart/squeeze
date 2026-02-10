-- midi-to-synth.lua
-- Connect all MIDI input devices to the first available instrument plugin.
-- Usage: ./Squeeze -d examples/midi-to-synth.lua -i

-- Find and load the first instrument plugin
local plugins = sq.list_plugins()
synth = nil

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

-- Route all MIDI devices to the synth
local devices = sq.list_midi_devices()
if #devices == 0 then
    print("\nNo MIDI devices found.")
else
    print("\nMIDI routing:")
    for _, dev in ipairs(devices) do
        local route_id, rerr = sq.midi_route(dev, synth)
        if route_id then
            print("  " .. dev .. " -> " .. synth.name)
        else
            print("  " .. dev .. " FAILED: " .. rerr)
        end
    end
end

-- Push graph and start audio
sq.update()
sq.start(44100, 512)
print("\nPlaying! Use the REPL to adjust, or Ctrl+C to quit.")
