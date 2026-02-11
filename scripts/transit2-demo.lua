-- transit2-demo.lua
-- Loads a sample into a SamplerNode, chains Transit 2 VST3 FX after it,
-- opens the plugin editor, and schedules beat-synced parameter automation.
--
-- Usage:
--   squeeze scripts/transit2-demo.lua -- path/to/sample.wav [bpm] [param_index]
--
-- Arguments after "--" are passed to the script:
--   arg[1] = sample file path (required)
--   arg[2] = BPM (default 120)
--   arg[3] = Transit 2 parameter index to automate (default 0)
--
-- Add -i before the script path for interactive REPL after loading.

local sample_path = arg[1]
if not sample_path then
    print("Usage: squeeze scripts/transit2-demo.lua -- path/to/sample.wav [bpm] [param_index]")
    return
end

-- Configuration
local BPM             = tonumber(arg[2]) or 120
local BARS            = 4
local BEATS_PER_BAR   = 4
local TOTAL_BEATS     = BARS * BEATS_PER_BAR  -- 16
local RAMP_BEATS      = 4       -- ramp over last bar
local RAMP_STEPS      = 16      -- automation points per ramp
local MACRO_PARAM     = tonumber(arg[3]) or 0
local NOTE            = 60      -- middle C
local VELOCITY        = 100
local CHANNEL         = 1

-- Start audio device
sq.start()

-- Load sample
local buf_id, err = sq.load_buffer(sample_path)
if not buf_id then
    print("Failed to load sample: " .. err)
    return
end
print("Buffer loaded (id " .. buf_id .. "): " .. sample_path)

-- Create sampler
local sampler, err = sq.add_sampler("sampler", 8)
if not sampler then
    print("Failed to create sampler: " .. err)
    return
end
print("Created " .. tostring(sampler))

-- Assign buffer to sampler
local ok, err = sampler:set_buffer(buf_id)
if not ok then
    print("Failed to set buffer: " .. err)
    return
end

-- Load Transit 2 plugin
local fx, err = sq.add_plugin("Transit 2")
if not fx then
    print("Failed to load Transit 2: " .. err)
    print("\nAvailable plugins:")
    for _, p in ipairs(sq.list_plugins()) do
        print("  " .. p)
    end
    return
end
print("Created " .. tostring(fx))

-- Connect sampler -> Transit 2
local conn_id, err = sq.connect(sampler, "out", fx, "in")
if not conn_id then
    print("Failed to connect: " .. err)
    return
end

-- Push the graph
sq.update()

-- Show Transit 2 parameters so user knows what index to automate
print("\nTransit 2 parameters:")
local info, err = fx:param_info()
if info then
    for i, p in ipairs(info) do
        local marker = (i - 1 == MACRO_PARAM) and " <-- automating" or ""
        print(string.format("  [%d] %s (default %.2f)%s",
            i - 1, p.name, p["default"], marker))
    end
end

-- Set tempo and time signature
sq.set_tempo(BPM)
sq.set_time_sig(BEATS_PER_BAR, 4)

-- Pre-schedule 100 repeats (~10 min at 150 BPM). Events are one-shot.
local REPEATS = 100

for rep = 0, REPEATS - 1 do
    local offset = rep * TOTAL_BEATS

    -- Retrigger sample at start of each phrase
    sq.schedule(offset, sampler.id, "note_on", CHANNEL, NOTE, VELOCITY)
    sq.schedule(offset + TOTAL_BEATS, sampler.id, "note_off", CHANNEL, NOTE)

    -- Ramp macro 0 -> 1 over the last bar, reset at phrase start
    if rep > 0 then
        sq.schedule(offset, fx.id, "param", MACRO_PARAM, 0.0)
    end
    local ramp_start = offset + TOTAL_BEATS - RAMP_BEATS
    for step = 0, RAMP_STEPS - 1 do
        local t = step / RAMP_STEPS
        local beat = ramp_start + t * RAMP_BEATS
        local value = t * t  -- quadratic ease-in curve
        sq.schedule(beat, fx.id, "param", MACRO_PARAM, value)
    end
end
sq.schedule(REPEATS * TOTAL_BEATS, fx.id, "param", MACRO_PARAM, 0.0)

local duration = REPEATS * TOTAL_BEATS / BPM * 60
print(string.format(
    "\n%d bars at %d BPM, looping for ~%.0f min. Automating param [%d] with quadratic ramp on last bar.",
    BARS, BPM, duration / 60, MACRO_PARAM))

-- Start playback and open the plugin editor
sq.play()
fx:open_editor()

print("Playing. Press Ctrl+C to stop.")
