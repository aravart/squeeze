#include "core/SamplerNode.h"
#include "core/Logger.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace squeeze {

namespace {

enum ParamIndex {
    kSampleStart = 0, kSampleEnd, kRootNote,
    kLoopStart, kLoopEnd, kLoopMode, kLoopCrossfade,
    kDirection, kPitchCoarse, kPitchFine,
    kVolume, kPan, kVelSensitivity,
    kAmpAttack, kAmpHold, kAmpDecay, kAmpSustain, kAmpRelease,
    kAmpAttackCurve, kAmpDecayCurve, kAmpReleaseCurve,
    kFilterType, kFilterCutoff, kFilterResonance, kFilterEnvAmount,
    kFilterAttack, kFilterDecay, kFilterSustain, kFilterRelease,
    kFilterAttackCurve, kFilterDecayCurve, kFilterReleaseCurve,
    kNumParams  // = 32
};

// --- Mapping functions ---

float normalizedToTime(float v) {
    return 0.001f * std::pow(10000.0f, v);
}

float timeToNormalized(float s) {
    if (s <= 0.001f) return 0.0f;
    if (s >= 10.0f) return 1.0f;
    return std::log10(s / 0.001f) / std::log10(10000.0f);
}

float normalizedToFreq(float v) {
    return 20.0f * std::pow(1000.0f, v);
}

float freqToNormalized(float hz) {
    if (hz <= 20.0f) return 0.0f;
    if (hz >= 20000.0f) return 1.0f;
    return std::log10(hz / 20.0f) / std::log10(1000.0f);
}

float normalizedToHold(float v) {
    return v * 10.0f;
}

float holdToNormalized(float s) {
    return std::clamp(s / 10.0f, 0.0f, 1.0f);
}

float normalizedToCrossfade(float v) {
    return v * v * 0.5f;
}

float crossfadeToNormalized(float s) {
    if (s <= 0.0f) return 0.0f;
    if (s >= 0.5f) return 1.0f;
    return std::sqrt(s / 0.5f);
}

int discreteIndex(float v, int numSteps) {
    return std::clamp(static_cast<int>(std::round(v * (numSteps - 1))), 0, numSteps - 1);
}

float discreteNormalized(int index, int numSteps) {
    return static_cast<float>(index) / static_cast<float>(numSteps - 1);
}

std::string formatTime(float seconds) {
    std::ostringstream oss;
    if (seconds < 1.0f) {
        float ms = seconds * 1000.0f;
        oss << std::fixed << std::setprecision(1) << ms << " ms";
    } else {
        oss << std::fixed << std::setprecision(1) << seconds << " s";
    }
    return oss.str();
}

std::string formatFreq(float hz) {
    std::ostringstream oss;
    if (hz >= 1000.0f) {
        oss << std::fixed << std::setprecision(1) << (hz / 1000.0f) << " kHz";
    } else {
        oss << std::fixed << std::setprecision(0) << hz << " Hz";
    }
    return oss.str();
}

std::string formatVolume(float gain) {
    if (gain <= 0.0f) return "-inf dB";
    float db = 20.0f * std::log10(gain);
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << db << " dB";
    return oss.str();
}

std::string formatPan(float pan) {
    // pan is -1.0 to +1.0
    if (std::abs(pan) < 0.005f) return "C";
    int val = static_cast<int>(std::round(std::abs(pan) * 100.0f));
    if (pan < 0.0f)
        return "L" + std::to_string(val);
    else
        return "R" + std::to_string(val);
}

const char* loopModeNames[] = { "Off", "Forward", "Reverse", "PingPong" };
const char* directionNames[] = { "Forward", "Reverse" };
const char* filterTypeNames[] = { "Off", "LP", "HP", "BP", "Notch" };
const char* curveNames[] = { "Linear", "Exponential", "Logarithmic" };

struct ParamDef {
    const char* name;
    float defaultValue;
    int numSteps;
    const char* label;
    const char* group;
};

const ParamDef paramDefs[kNumParams] = {
    { "sample_start",       0.0f,   0, "",   "Playback" },
    { "sample_end",         1.0f,   0, "",   "Playback" },
    { "root_note",          0.472440944f, 128, "", "Playback" },  // 60/127
    { "loop_start",         0.0f,   0, "",   "Loop" },
    { "loop_end",           1.0f,   0, "",   "Loop" },
    { "loop_mode",          0.0f,   4, "",   "Loop" },
    { "loop_crossfade",     0.0f,   0, "s",  "Loop" },
    { "direction",          0.0f,   2, "",   "Playback" },
    { "pitch_coarse",       0.5f,   97, "st", "Pitch" },
    { "pitch_fine",         0.5f,   0, "ct", "Pitch" },
    { "volume",             0.8f,   0, "dB", "Amp" },
    { "pan",                0.5f,   0, "",   "Amp" },
    { "vel_sensitivity",    1.0f,   0, "",   "Amp" },
    { "amp_attack",         0.0f,   0, "s",  "Amp Env" },
    { "amp_hold",           0.0f,   0, "s",  "Amp Env" },
    { "amp_decay",          0.15f,  0, "s",  "Amp Env" },
    { "amp_sustain",        1.0f,   0, "",   "Amp Env" },
    { "amp_release",        0.05f,  0, "s",  "Amp Env" },
    { "amp_attack_curve",   0.0f,   3, "",   "Amp Env" },
    { "amp_decay_curve",    0.333f, 3, "",   "Amp Env" },
    { "amp_release_curve",  0.333f, 3, "",   "Amp Env" },
    { "filter_type",        0.0f,   5, "",   "Filter" },
    { "filter_cutoff",      1.0f,   0, "Hz", "Filter" },
    { "filter_resonance",   0.0f,   0, "",   "Filter" },
    { "filter_env_amount",  0.5f,   0, "",   "Filter" },
    { "filter_attack",      0.0f,   0, "s",  "Filter Env" },
    { "filter_decay",       0.15f,  0, "s",  "Filter Env" },
    { "filter_sustain",     1.0f,   0, "",   "Filter Env" },
    { "filter_release",     0.05f,  0, "s",  "Filter Env" },
    { "filter_attack_curve",  0.0f,   3, "",  "Filter Env" },
    { "filter_decay_curve",   0.333f, 3, "",  "Filter Env" },
    { "filter_release_curve", 0.333f, 3, "",  "Filter Env" },
};

} // anonymous namespace

// --- Constructor ---

SamplerNode::SamplerNode(int maxVoices)
    : params_()
    , allocator_(maxVoices, params_)
{
    // Build name→index map and set defaults
    for (int i = 0; i < kNumParams; ++i) {
        paramNameToIndex_[paramDefs[i].name] = i;
        normalizedParams_[i] = paramDefs[i].defaultValue;
    }

    // Apply default mappings to params_
    for (int i = 0; i < kNumParams; ++i)
        setParameter(i, paramDefs[i].defaultValue);
}

// --- Node interface ---

void SamplerNode::prepare(double sampleRate, int blockSize) {
    allocator_.prepare(sampleRate, blockSize);
}

void SamplerNode::process(ProcessContext& ctx) {
    ctx.outputAudio.clear(0, ctx.numSamples);
    int currentSample = 0;

    for (const auto metadata : ctx.inputMidi) {
        const auto msg = metadata.getMessage();
        const int eventSample = metadata.samplePosition;

        if (eventSample > currentSample) {
            allocator_.renderBlock(ctx.outputAudio, currentSample,
                                   eventSample - currentSample);
            currentSample = eventSample;
        }

        if (msg.isNoteOn() && msg.getVelocity() > 0) {
            allocator_.noteOn(buffer_, msg.getNoteNumber(),
                              static_cast<float>(msg.getVelocity()));
        } else if (msg.isNoteOff() ||
                   (msg.isNoteOn() && msg.getVelocity() == 0)) {
            allocator_.noteOff(msg.getNoteNumber());
        }
    }

    if (currentSample < ctx.numSamples) {
        allocator_.renderBlock(ctx.outputAudio, currentSample,
                               ctx.numSamples - currentSample);
    }
}

void SamplerNode::release() {
    allocator_.release();
}

std::vector<PortDescriptor> SamplerNode::getInputPorts() const {
    return {
        { "midi_in", PortDirection::input, SignalType::midi, 1 }
    };
}

std::vector<PortDescriptor> SamplerNode::getOutputPorts() const {
    return {
        { "out", PortDirection::output, SignalType::audio, 2 }
    };
}

// --- Parameters ---

std::vector<ParameterDescriptor> SamplerNode::getParameterDescriptors() const {
    std::vector<ParameterDescriptor> descs;
    descs.reserve(kNumParams);
    for (int i = 0; i < kNumParams; ++i) {
        descs.push_back({
            paramDefs[i].name,
            i,
            paramDefs[i].defaultValue,
            paramDefs[i].numSteps,
            true,   // automatable
            false,  // boolean
            paramDefs[i].label,
            paramDefs[i].group
        });
    }
    return descs;
}

float SamplerNode::getParameter(int index) const {
    if (index < 0 || index >= kNumParams) return 0.0f;
    return normalizedParams_[index];
}

void SamplerNode::setParameter(int index, float value) {
    if (index < 0 || index >= kNumParams) return;
    value = std::clamp(value, 0.0f, 1.0f);
    normalizedParams_[index] = value;

    switch (index) {
        case kSampleStart:    params_.sampleStart = value; break;
        case kSampleEnd:      params_.sampleEnd = value; break;
        case kRootNote:        params_.rootNote = std::clamp(static_cast<int>(std::round(value * 127.0f)), 0, 127); break;
        case kLoopStart:      params_.loopStart = value; break;
        case kLoopEnd:        params_.loopEnd = value; break;
        case kLoopMode: {
            int idx = discreteIndex(value, 4);
            params_.loopMode = static_cast<LoopMode>(idx);
            break;
        }
        case kLoopCrossfade:  params_.loopCrossfadeSec = normalizedToCrossfade(value); break;
        case kDirection: {
            int idx = discreteIndex(value, 2);
            params_.direction = static_cast<PlayDirection>(idx);
            break;
        }
        case kPitchCoarse: {
            params_.pitchSemitones = static_cast<int>(std::round(value * 96.0f)) - 48;
            break;
        }
        case kPitchFine:      params_.pitchCents = (value - 0.5f) * 200.0f; break;
        case kVolume:         params_.volume = value * value; break;
        case kPan:            params_.pan = (value - 0.5f) * 2.0f; break;
        case kVelSensitivity: params_.velSensitivity = value; break;
        case kAmpAttack:      params_.ampAttack = normalizedToTime(value); break;
        case kAmpHold:        params_.ampHold = normalizedToHold(value); break;
        case kAmpDecay:       params_.ampDecay = normalizedToTime(value); break;
        case kAmpSustain:     params_.ampSustain = value; break;
        case kAmpRelease:     params_.ampRelease = normalizedToTime(value); break;
        case kAmpAttackCurve: {
            int idx = discreteIndex(value, 3);
            params_.ampAttackCurve = static_cast<EnvCurve>(idx);
            break;
        }
        case kAmpDecayCurve: {
            int idx = discreteIndex(value, 3);
            params_.ampDecayCurve = static_cast<EnvCurve>(idx);
            break;
        }
        case kAmpReleaseCurve: {
            int idx = discreteIndex(value, 3);
            params_.ampReleaseCurve = static_cast<EnvCurve>(idx);
            break;
        }
        case kFilterType: {
            int idx = discreteIndex(value, 5);
            params_.filterType = static_cast<FilterType>(idx);
            break;
        }
        case kFilterCutoff:    params_.filterCutoffHz = normalizedToFreq(value); break;
        case kFilterResonance: params_.filterResonance = value; break;
        case kFilterEnvAmount: params_.filterEnvAmount = (value - 0.5f) * 2.0f; break;
        case kFilterAttack:    params_.filterAttack = normalizedToTime(value); break;
        case kFilterDecay:     params_.filterDecay = normalizedToTime(value); break;
        case kFilterSustain:   params_.filterSustain = value; break;
        case kFilterRelease:   params_.filterRelease = normalizedToTime(value); break;
        case kFilterAttackCurve: {
            int idx = discreteIndex(value, 3);
            params_.filterAttackCurve = static_cast<EnvCurve>(idx);
            break;
        }
        case kFilterDecayCurve: {
            int idx = discreteIndex(value, 3);
            params_.filterDecayCurve = static_cast<EnvCurve>(idx);
            break;
        }
        case kFilterReleaseCurve: {
            int idx = discreteIndex(value, 3);
            params_.filterReleaseCurve = static_cast<EnvCurve>(idx);
            break;
        }
    }
}

std::string SamplerNode::getParameterText(int index) const {
    if (index < 0 || index >= kNumParams) return "";
    float v = normalizedParams_[index];

    switch (index) {
        case kSampleStart:
        case kSampleEnd:
        case kLoopStart:
        case kLoopEnd: {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(3) << v;
            return oss.str();
        }
        case kRootNote: {
            int note = std::clamp(static_cast<int>(std::round(v * 127.0f)), 0, 127);
            return std::to_string(note);
        }
        case kLoopMode: {
            int idx = discreteIndex(v, 4);
            return loopModeNames[idx];
        }
        case kLoopCrossfade:
            return formatTime(normalizedToCrossfade(v));
        case kDirection: {
            int idx = discreteIndex(v, 2);
            return directionNames[idx];
        }
        case kPitchCoarse: {
            int st = static_cast<int>(std::round(v * 96.0f)) - 48;
            if (st > 0) return "+" + std::to_string(st) + " st";
            if (st < 0) return std::to_string(st) + " st";
            return "0 st";
        }
        case kPitchFine: {
            float ct = (v - 0.5f) * 200.0f;
            int ctRound = static_cast<int>(std::round(ct));
            if (ctRound > 0) return "+" + std::to_string(ctRound) + " ct";
            if (ctRound < 0) return std::to_string(ctRound) + " ct";
            return "0 ct";
        }
        case kVolume:
            return formatVolume(v * v);
        case kPan:
            return formatPan((v - 0.5f) * 2.0f);
        case kVelSensitivity:
        case kAmpSustain:
        case kFilterResonance:
        case kFilterSustain: {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << v;
            return oss.str();
        }
        case kAmpAttack:
        case kAmpDecay:
        case kAmpRelease:
        case kFilterAttack:
        case kFilterDecay:
        case kFilterRelease:
            return formatTime(normalizedToTime(v));
        case kAmpHold:
            return formatTime(normalizedToHold(v));
        case kAmpAttackCurve:
        case kAmpDecayCurve:
        case kAmpReleaseCurve:
        case kFilterAttackCurve:
        case kFilterDecayCurve:
        case kFilterReleaseCurve: {
            int idx = discreteIndex(v, 3);
            return curveNames[idx];
        }
        case kFilterType: {
            int idx = discreteIndex(v, 5);
            return filterTypeNames[idx];
        }
        case kFilterCutoff:
            return formatFreq(normalizedToFreq(v));
        case kFilterEnvAmount: {
            float amt = (v - 0.5f) * 2.0f;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2);
            if (amt > 0.0f) oss << "+";
            oss << amt;
            return oss.str();
        }
    }
    return "";
}

int SamplerNode::findParameterIndex(const std::string& name) const {
    auto it = paramNameToIndex_.find(name);
    if (it != paramNameToIndex_.end()) return it->second;
    return -1;
}

// --- Buffer ---

void SamplerNode::setBuffer(const Buffer* buffer) {
    buffer_ = buffer;
}

const Buffer* SamplerNode::getBuffer() const {
    return buffer_;
}

} // namespace squeeze
