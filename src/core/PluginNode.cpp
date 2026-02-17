#include "core/PluginNode.h"

namespace squeeze {

// ═══════════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════════

PluginNode::PluginNode(std::unique_ptr<juce::AudioProcessor> processor,
                       int inputChannels, int outputChannels, bool acceptsMidi)
    : processor_(std::move(processor)),
      inputChannels_(inputChannels),
      outputChannels_(outputChannels),
      acceptsMidi_(acceptsMidi)
{
    pluginName_ = processor_->getName().toStdString();
    buildParameterMap();
    SQ_DEBUG("PluginNode: created '%s' in=%d out=%d midi=%s",
             pluginName_.c_str(), inputChannels_, outputChannels_,
             acceptsMidi_ ? "yes" : "no");
}

PluginNode::~PluginNode()
{
    SQ_DEBUG("PluginNode: destroyed '%s'", pluginName_.c_str());
}

// ═══════════════════════════════════════════════════════════════════
// Parameter map
// ═══════════════════════════════════════════════════════════════════

void PluginNode::buildParameterMap()
{
    parameterMap_.clear();
    auto& params = processor_->getParameters();
    for (int i = 0; i < params.size(); ++i)
    {
        auto* p = params[i];
        std::string name = p->getName(256).toStdString();
        if (!name.empty())
        {
            parameterMap_[name] = i;
            SQ_TRACE("PluginNode: param[%d] = '%s'", i, name.c_str());
        }
    }
    SQ_DEBUG("PluginNode: built parameter map with %d entries",
             static_cast<int>(parameterMap_.size()));
}

// ═══════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════

void PluginNode::prepare(double sampleRate, int blockSize)
{
    SQ_DEBUG("PluginNode::prepare: '%s' sr=%f bs=%d",
             pluginName_.c_str(), sampleRate, blockSize);
    processor_->prepareToPlay(sampleRate, blockSize);
}

void PluginNode::release()
{
    SQ_DEBUG("PluginNode::release: '%s'", pluginName_.c_str());
    processor_->releaseResources();
}

// ═══════════════════════════════════════════════════════════════════
// Processing
// ═══════════════════════════════════════════════════════════════════

void PluginNode::process(ProcessContext& context)
{
    int numSamples = context.numSamples;
    auto& outAudio = context.outputAudio;
    int outCh = outAudio.getNumChannels();

    if (inputChannels_ > 0)
    {
        // Effect: copy input audio to output buffer, then processBlock in-place
        int copyChannels = std::min(context.inputAudio.getNumChannels(), outCh);
        int copySamples = std::min(context.inputAudio.getNumSamples(), numSamples);
        for (int ch = 0; ch < copyChannels; ++ch)
            outAudio.copyFrom(ch, 0, context.inputAudio, ch, 0, copySamples);
        // Zero any extra output channels
        for (int ch = copyChannels; ch < outCh; ++ch)
            outAudio.clear(ch, 0, numSamples);
    }
    else
    {
        // Instrument: clear output buffer, processBlock fills it
        outAudio.clear();
    }

    // Copy input MIDI to output MIDI before processBlock (JUCE processes MIDI in-place)
    context.outputMidi.addEvents(context.inputMidi, 0, numSamples, 0);

    // processBlock works in-place on the output audio buffer and output MIDI buffer
    processor_->processBlock(outAudio, context.outputMidi);
}

// ═══════════════════════════════════════════════════════════════════
// Port declaration
// ═══════════════════════════════════════════════════════════════════

std::vector<PortDescriptor> PluginNode::getInputPorts() const
{
    std::vector<PortDescriptor> ports;
    if (inputChannels_ > 0)
        ports.push_back({"in", PortDirection::input, SignalType::audio, inputChannels_});
    if (acceptsMidi_)
        ports.push_back({"midi_in", PortDirection::input, SignalType::midi, 1});
    return ports;
}

std::vector<PortDescriptor> PluginNode::getOutputPorts() const
{
    std::vector<PortDescriptor> ports;
    if (outputChannels_ > 0)
        ports.push_back({"out", PortDirection::output, SignalType::audio, outputChannels_});
    if (acceptsMidi_)
        ports.push_back({"midi_out", PortDirection::output, SignalType::midi, 1});
    return ports;
}

// ═══════════════════════════════════════════════════════════════════
// Parameters
// ═══════════════════════════════════════════════════════════════════

std::vector<ParameterDescriptor> PluginNode::getParameterDescriptors() const
{
    std::vector<ParameterDescriptor> result;
    auto& params = processor_->getParameters();
    for (int i = 0; i < params.size(); ++i)
    {
        auto* p = params[i];
        std::string name = p->getName(256).toStdString();
        if (name.empty()) continue;

        ParameterDescriptor desc;
        desc.name = name;
        desc.defaultValue = p->getDefaultValue();
        desc.numSteps = p->getNumSteps();
        desc.automatable = p->isAutomatable();
        desc.boolean = p->isBoolean();
        desc.label = p->getLabel().toStdString();
        desc.group = "";
        result.push_back(std::move(desc));
    }
    return result;
}

float PluginNode::getParameter(const std::string& name) const
{
    auto it = parameterMap_.find(name);
    if (it == parameterMap_.end())
    {
        SQ_TRACE("PluginNode::getParameter: unknown param '%s'", name.c_str());
        return 0.0f;
    }
    return processor_->getParameters()[it->second]->getValue();
}

void PluginNode::setParameter(const std::string& name, float value)
{
    auto it = parameterMap_.find(name);
    if (it == parameterMap_.end())
    {
        SQ_TRACE("PluginNode::setParameter: unknown param '%s'", name.c_str());
        return;
    }
    SQ_DEBUG("PluginNode::setParameter: '%s' = %f", name.c_str(), value);
    processor_->getParameters()[it->second]->setValue(value);
}

std::string PluginNode::getParameterText(const std::string& name) const
{
    auto it = parameterMap_.find(name);
    if (it == parameterMap_.end()) return "";
    return processor_->getParameters()[it->second]->getCurrentValueAsText().toStdString();
}

// ═══════════════════════════════════════════════════════════════════
// Accessors
// ═══════════════════════════════════════════════════════════════════

const std::string& PluginNode::getPluginName() const
{
    return pluginName_;
}

juce::AudioProcessor* PluginNode::getProcessor()
{
    return processor_.get();
}

} // namespace squeeze
