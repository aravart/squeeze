-- channel-split.lua
-- Route MIDI channel 1 to Pure LoFi and channel 10 to XO.
-- All auto-loaded MIDI input nodes are connected to both plugins.
-- Usage: ./Squeeze -d examples/channel-split.lua -i

-- Collect all MIDI input nodes (auto-loaded at startup)
local midi_nodes = {}
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
local lofi, err1 = sq.add_plugin("Pure LoFi")
if not lofi then
    print("Failed to load Pure LoFi: " .. err1)
    return
end
print("Loaded Pure LoFi (id=" .. lofi.id .. ")")

local xo, err2 = sq.add_plugin("XO")
if not xo then
    print("Failed to load XO: " .. err2)
    return
end
print("Loaded XO (id=" .. xo.id .. ")")

-- Connect each MIDI input -> Pure LoFi (ch 1) and -> XO (ch 10)
for _, n in ipairs(midi_nodes) do
    local c1, e1 = sq.connect(n.id, "midi_out", lofi, "midi_in", 1)
    if c1 then
        print(n.name .. " ch 1 -> Pure LoFi")
    else
        print(n.name .. " -> Pure LoFi failed: " .. e1)
    end

    local c2, e2 = sq.connect(n.id, "midi_out", xo, "midi_in", 10)
    if c2 then
        print(n.name .. " ch 10 -> XO")
    else
        print(n.name .. " -> XO failed: " .. e2)
    end
end

-- Start audio (opens device and rebuilds graph with correct sr/bs)
sq.start(44100, 512)

print("\nPlaying! Channel 1 -> Pure LoFi, Channel 10 -> XO")
print("Use the REPL to adjust, or Ctrl+C to quit.")
