-- sampler.lua
-- Load a WAV file into a sampler and play it from a MIDI controller.
-- Usage: ./Squeeze -d examples/sampler.lua -i -- /path/to/sample.wav
--
-- Pass the sample path as the first argument after "--".
-- If no path is given, creates a 1-second silent buffer as a placeholder.

-- ============================================================
-- Load or create a buffer
-- ============================================================

local sample_path = arg and arg[1]
local buf, err

if sample_path then
    buf, err = sq.load_buffer(sample_path)
    if not buf then
        print("Failed to load " .. sample_path .. ": " .. err)
        return
    end
    local info = sq.buffer_info(buf)
    print(string.format("Loaded: %s (%.1fs, %dch, %.0f Hz)",
        info.name, info.length_seconds, info.channels, info.sample_rate))
else
    print("No sample path given, creating silent placeholder buffer.")
    buf, err = sq.create_buffer(2, 44100, 44100, "silence")
    if not buf then
        print("Failed to create buffer: " .. err)
        return
    end
end

-- ============================================================
-- Open MIDI input
-- ============================================================

local devices = sq.list_midi_inputs()
if #devices == 0 then
    print("No MIDI devices found.")
    return
end

midi = {}
for _, midi_name in ipairs(devices) do
    local m, err = sq.add_midi_input(midi_name)
    if m then
        table.insert(midi, m)
        print("MIDI input: " .. midi_name)
    else
        print("MIDI error (" .. midi_name .. "): " .. err)
    end
end

-- ============================================================
-- Create sampler and assign buffer
-- ============================================================

sampler = sq.add_sampler("sampler", 8)
sampler:set_buffer(buf)

-- Tweak some parameters
sampler:set_param("amp_attack",  0.01)  -- 10 ms attack
sampler:set_param("amp_release", 0.3)   -- 300 ms release
sampler:set_param("volume",      0.8)

-- Print all parameters
print("\nSampler parameters:")
local info = sampler:param_info()
for _, p in ipairs(info) do
    local text = sampler:param_text(p.index)
    print(string.format("  [%2d] %-20s %s", p.index, p.name, text))
end

-- ============================================================
-- Connect and start
-- ============================================================

for _, m in ipairs(midi) do
    local c, cerr = sq.connect(m, "midi_out", sampler, "midi_in")
    if not c then
        print("Connect error (" .. tostring(m) .. "): " .. cerr)
    end
end

sq.update()
sq.start(44100, 512)

print("\nPlaying! MIDI -> " .. tostring(sampler))
print("Try in the REPL:")
print("  sampler:set_param('filter_cutoff', 0.5)")
print("  sampler:set_param('amp_release', 0.8)")
print("  sampler:set_param('loop_mode', 0.5)    -- forward loop")
