-- channel-split.lua
-- Route each MIDI channel to a different plugin.
-- All auto-loaded MIDI input nodes are connected to all plugins.
-- Usage: ./Squeeze -d examples/channel-split.lua -i

-- ============================================================
-- Configuration: plugin name -> MIDI channel
-- ============================================================

local plugins = {
    { name = "Pure LoFi",         channel = 1 },
    { name = "Wurli V3",                channel = 10 },
    { name = "Augmented STRINGS", channel = 2 },
}

-- ============================================================

-- Collect all MIDI input nodes (auto-loaded at startup)
midi_nodes = {}
for _, n in ipairs(sq.nodes()) do
    local ports = sq.ports(n.id)
    for _, p in ipairs(ports.outputs) do
        if p.type == "midi" then
            table.insert(midi_nodes, n)
            break
        end
    end
end

if #midi_nodes == 0 then
    print("No MIDI input nodes found.")
    return
end

for _, n in ipairs(midi_nodes) do
    print("MIDI source: " .. n.name .. " (id=" .. n.id .. ")")
end

-- Load plugins
nodes = {}
for _, p in ipairs(plugins) do
    local node, err = sq.add_plugin(p.name)
    if not node then
        print("Failed to load " .. p.name .. ": " .. err)
        return
    end
    print("Loaded " .. p.name .. " (id=" .. node.id .. ")")
    table.insert(nodes, { node = node, channel = p.channel, name = p.name })
end

-- Connect each MIDI input to each plugin on its channel
for _, midi in ipairs(midi_nodes) do
    for _, p in ipairs(nodes) do
        local c, e = sq.connect(midi.id, "midi_out", p.node, "midi_in", p.channel)
        if c then
            print(midi.name .. " ch " .. p.channel .. " -> " .. p.name)
        else
            print(midi.name .. " -> " .. p.name .. " failed: " .. e)
        end
    end
end

-- Start audio
sq.start(44100, 512)

print("\nPlaying!")
for _, p in ipairs(nodes) do
    print("  Channel " .. p.channel .. " -> " .. p.name)
end
print("Use the REPL to adjust, or Ctrl+C to quit.")
