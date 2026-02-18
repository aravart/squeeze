#include "core/PluginProcessor.h"

namespace squeeze {

// ═══════════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════════

PluginProcessor::PluginProcessor(std::unique_ptr<juce::AudioProcessor> processor,
                                 int inputChannels, int outputChannels, bool acceptsMidi)
    : Processor(processor->getName().toStdString()),
      processor_(std::move(processor)),
      inputChannels_(inputChannels),
      outputChannels_(outputChannels),
      acceptsMidi_(acceptsMidi)
{
    pluginName_ = processor_->getName().toStdString();
    buildParameterMap();
    SQ_DEBUG("PluginProcessor: created '%s' in=%d out=%d midi=%s",
             pluginName_.c_str(), inputChannels_, outputChannels_,
             acceptsMidi_ ? "yes" : "no");
}

PluginProcessor::~PluginProcessor()
{
    SQ_DEBUG("PluginProcessor: destroyed '%s'", pluginName_.c_str());
}

// ═══════════════════════════════════════════════════════════════════
// Parameter map
// ═══════════════════════════════════════════════════════════════════

void PluginProcessor::buildParameterMap()
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
            SQ_TRACE("PluginProcessor: param[%d] = '%s'", i, name.c_str());
        }
    }
    SQ_DEBUG("PluginProcessor: built parameter map with %d entries",
             static_cast<int>(parameterMap_.size()));
}

// ═══════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════

void PluginProcessor::prepare(double sampleRate, int blockSize)
{
    SQ_DEBUG("PluginProcessor::prepare: '%s' sr=%f bs=%d",
             pluginName_.c_str(), sampleRate, blockSize);
    processor_->prepareToPlay(sampleRate, blockSize);
}

void PluginProcessor::reset()
{
    SQ_DEBUG("PluginProcessor::reset: '%s'", pluginName_.c_str());
    processor_->reset();
}

void PluginProcessor::release()
{
    SQ_DEBUG("PluginProcessor::release: '%s'", pluginName_.c_str());
    processor_->releaseResources();
}

// ═══════════════════════════════════════════════════════════════════
// Processing
// ═══════════════════════════════════════════════════════════════════

void PluginProcessor::process(juce::AudioBuffer<float>& buffer)
{
    tempMidi_.clear();
    processor_->processBlock(buffer, tempMidi_);
}

void PluginProcessor::process(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi)
{
    tempMidi_.clear();
    tempMidi_.addEvents(midi, 0, buffer.getNumSamples(), 0);
    processor_->processBlock(buffer, tempMidi_);
}

// ═══════════════════════════════════════════════════════════════════
// Parameters
// ═══════════════════════════════════════════════════════════════════

int PluginProcessor::getParameterCount() const
{
    return processor_->getParameters().size();
}

ParamDescriptor PluginProcessor::getParameterDescriptor(int index) const
{
    auto& params = processor_->getParameters();
    if (index < 0 || index >= params.size()) return {};

    auto* p = params[index];
    ParamDescriptor desc;
    desc.name = p->getName(256).toStdString();
    desc.defaultValue = p->getDefaultValue();
    desc.minValue = 0.0f;
    desc.maxValue = 1.0f;
    desc.numSteps = p->getNumSteps();
    desc.automatable = p->isAutomatable();
    desc.boolean = p->isBoolean();
    desc.label = p->getLabel().toStdString();
    desc.group = "";
    return desc;
}

std::vector<ParamDescriptor> PluginProcessor::getParameterDescriptors() const
{
    std::vector<ParamDescriptor> result;
    auto& params = processor_->getParameters();
    for (int i = 0; i < params.size(); ++i)
    {
        auto* p = params[i];
        std::string name = p->getName(256).toStdString();
        if (name.empty()) continue;

        ParamDescriptor desc;
        desc.name = name;
        desc.defaultValue = p->getDefaultValue();
        desc.minValue = 0.0f;
        desc.maxValue = 1.0f;
        desc.numSteps = p->getNumSteps();
        desc.automatable = p->isAutomatable();
        desc.boolean = p->isBoolean();
        desc.label = p->getLabel().toStdString();
        desc.group = "";
        result.push_back(std::move(desc));
    }
    return result;
}

float PluginProcessor::getParameter(const std::string& name) const
{
    auto it = parameterMap_.find(name);
    if (it == parameterMap_.end())
    {
        SQ_TRACE("PluginProcessor::getParameter: unknown param '%s'", name.c_str());
        return 0.0f;
    }
    return processor_->getParameters()[it->second]->getValue();
}

void PluginProcessor::setParameter(const std::string& name, float value)
{
    auto it = parameterMap_.find(name);
    if (it == parameterMap_.end())
    {
        SQ_TRACE("PluginProcessor::setParameter: unknown param '%s'", name.c_str());
        return;
    }
    SQ_DEBUG("PluginProcessor::setParameter: '%s' = %f", name.c_str(), value);
    processor_->getParameters()[it->second]->setValue(value);
}

std::string PluginProcessor::getParameterText(const std::string& name) const
{
    auto it = parameterMap_.find(name);
    if (it == parameterMap_.end()) return "";
    return processor_->getParameters()[it->second]->getCurrentValueAsText().toStdString();
}

// ═══════════════════════════════════════════════════════════════════
// Latency
// ═══════════════════════════════════════════════════════════════════

int PluginProcessor::getLatencySamples() const
{
    return processor_->getLatencySamples();
}

// ═══════════════════════════════════════════════════════════════════
// Accessors
// ═══════════════════════════════════════════════════════════════════

const std::string& PluginProcessor::getPluginName() const
{
    return pluginName_;
}

juce::AudioProcessor* PluginProcessor::getJuceProcessor()
{
    return processor_.get();
}

} // namespace squeeze
