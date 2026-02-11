-- transit2-demo.lua
-- Loads a sample into a SamplerNode, chains Transit 2 VST3 FX after it,
-- opens the plugin editor, and schedules beat-synced parameter automation.
--
-- Usage:
--   squeeze scripts/transit2-demo.lua -- path/to/sample.wav
--   squeeze scripts/transit2-demo.lua -- path/to/sample.wav 0
--
-- Arguments after "--" are passed to the script:
--   arg[1] = sample file path (required)
--   arg[2] = Transit 2 parameter index to automate (default 0)
--
-- Add -i before the script path for interactive REPL after loading.

local sample_path = arg[1]
if not sample_path then
    print("Usage: squeeze scripts/transit2-demo.lua -- path/to/sample.wav [param_index]")
    return
end

-- Configuration
local BPM             = 120
local PHRASES         = 8       -- total 4-bar phrases (32 bars ~ 64s)
local BARS_PER_PHRASE = 4
local BEATS_PER_BAR  = 4
local BEATS_PER_PHRASE = BARS_PER_PHRASE * BEATS_PER_BAR  -- 16
local RAMP_BEATS      = 4       -- ramp over last bar of each phrase
local RAMP_STEPS      = 16      -- automation points per ramp
local MACRO_PARAM     = tonumber(arg[2]) or 0
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

-- Schedule note pattern: hit on beat 1 of every bar, shorter hits on beat 3
local total_beats = PHRASES * BEATS_PER_PHRASE
for phrase = 0, PHRASES - 1 do
    local phrase_start = phrase * BEATS_PER_PHRASE
    for bar = 0, BARS_PER_PHRASE - 1 do
        local bar_start = phrase_start + bar * BEATS_PER_BAR

        -- Strong hit on beat 1
        sq.schedule(bar_start, sampler.id, "note_on", CHANNEL, NOTE, VELOCITY)
        sq.schedule(bar_start + 1.5, sampler.id, "note_off", CHANNEL, NOTE)

        -- Lighter hit on beat 3
        sq.schedule(bar_start + 2, sampler.id, "note_on", CHANNEL, NOTE, math.floor(VELOCITY * 0.6))
        sq.schedule(bar_start + 3.0, sampler.id, "note_off", CHANNEL, NOTE)
    end
end

-- Schedule Transit 2 macro automation:
-- Ramp 0 -> 1 over the last RAMP_BEATS of each phrase, reset at next phrase
for phrase = 0, PHRASES - 1 do
    local phrase_start = phrase * BEATS_PER_PHRASE
    local ramp_start   = phrase_start + BEATS_PER_PHRASE - RAMP_BEATS

    -- Reset to 0 at phrase start (except first phrase, already 0)
    if phrase > 0 then
        sq.schedule(phrase_start, fx.id, "param", MACRO_PARAM, 0.0)
    end

    -- Ramp up
    for step = 0, RAMP_STEPS - 1 do
        local t = step / RAMP_STEPS
        local beat = ramp_start + t * RAMP_BEATS
        local value = t * t  -- quadratic ease-in curve
        sq.schedule(beat, fx.id, "param", MACRO_PARAM, value)
    end
end

-- Final reset so it doesn't stay at max
sq.schedule(total_beats, fx.id, "param", MACRO_PARAM, 0.0)

print(string.format(
    "\nScheduled %d bars at %d BPM (%.0fs). Automating param [%d] with quadratic ramp.",
    PHRASES * BARS_PER_PHRASE, BPM,
    total_beats / BPM * 60, MACRO_PARAM))

-- Start playback and open the plugin editor
sq.play()
fx:open_editor()

print("Playing. Press Ctrl+C to stop.")
