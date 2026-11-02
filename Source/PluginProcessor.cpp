// The engine headers go first: they use short names that Windows headers (pulled in by JUCE) could otherwise redefine.
#include "engine/WingEngine.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::StringArray kCreatures { "Hummingbird", "Honeybee", "Dragonfly", "Owl", "Albatross", "Crested pigeon", "Little brown bat", "Ostrich",
                                         "Bumblebee", "Hawkmoth", "Monarch", "Locust", "Firefly", "Simurgh", "Phoenix", "Thunderbird", "Fenghuang", "Garuda", "Icarus" };
    const juce::StringArray kModes { "Working", "Attacking", "Escaping", "Defending", "Nesting" };
    const juce::StringArray kSeasons { "None", "Swarm", "Migrate", "Pillage", "Mating", "Bloom" };
    const juce::StringArray kWeather { "Clear", "Headwind", "Tailwind", "Rain", "Storm", "Fog", "Cold", "Heat" };
    const juce::StringArray kStretch { "Tracker", "Sampler", "Modern", "Smear", "Tape" };
    const char* kEvents[9] = { "Wildfire", "Ash fall", "Tremor", "Oil spill", "Pesticide", "Turbines", "City lights", "Magnetic storm", "Radio noise" };
}

juce::AudioProcessorValueTreeState::ParameterLayout WingMachineProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto choice = [&] (const char* id, const char* name, const StringArray& items, int def) { p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };
    auto flt = [&] (const char* id, const char* name, float lo, float hi, float def, float step = 0.001f) { p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, step), def)); };
    choice ("creature", "Creature", kCreatures, 0);
    choice ("mode", "Behavior", kModes, 0);
    choice ("season", "Season", kSeasons, 0);
    choice ("weather", "Weather", kWeather, 0);
    for (int i = 0; i < 9; ++i) p.push_back (std::make_unique<AudioParameterBool> (ParameterID { String ("ev") + String (i), 1 }, String ("Event: ") + kEvents[i], false));
    flt ("sizefx", "Size effect", 0.f, 2.f, 1.3f);
    flt ("seek", "Seek (swarm fusion)", 0.f, 1.5f, 0.8f);
    flt ("slevel", "Season level", 0.f, 1.f, 0.7f);
    flt ("elevel", "Environment level", 0.f, 1.f, 0.8f);
    flt ("alt", "Altitude (Icarus)", 0.f, 1.f, 0.5f);
    flt ("merge", "Merge", 0.f, 1.f, 0.45f);
    flt ("flange", "Flange", 0.f, 1.f, 0.4f);
    flt ("mod", "Feather modulation", 0.f, 1.f, 0.7f);
    flt ("stretch", "Stretch", 0.f, 1.f, 0.35f);
    flt ("sspeed", "Stretch speed", 0.f, 1.f, 0.5f);
    choice ("smode", "Stretch mode", kStretch, 4);
    flt ("dubTrim", "Dub trim", -1.f, 1.f, 0.f);
    flt ("dfbTrim", "Dub feedback trim", -1.f, 1.f, 0.f);
    flt ("echoTrim", "Echo trim", -1.f, 1.f, 0.f);
    flt ("airTrim", "Air trim", -1.f, 1.f, 0.f);
    flt ("roomTrim", "Room trim", -1.f, 1.f, 0.f);
    flt ("tame", "Tame (0 = off)", 0.f, 1.f, 0.6f);
    flt ("quant", "Quantize to host grid", 0.f, 1.f, 0.8f);
    flt ("swing", "Grid swing", 0.5f, 0.68f, 0.5f);
    flt ("block", "Beat lock", 0.f, 1.f, 0.6f);
    flt ("coupling", "Wing coupling", 0.3f, 1.7f, 1.f);
    flt ("out", "Output", 0.f, 1.f, 0.7f);
    return { p.begin(), p.end() };
}

WingMachineProcessor::WingMachineProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout()),
      engine (new wm::WingEngine())
{
}

WingMachineProcessor::~WingMachineProcessor() = default;

bool WingMachineProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void WingMachineProcessor::prepareToPlay (double sampleRate, int)
{
    auto fresh = std::make_unique<wm::WingEngine>();
    fresh->prepare (sampleRate);
    { const juce::SpinLock::ScopedLockType sl (engineLock); engine.swap (fresh); } // the editor reads the status text from another thread
}

void WingMachineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (buffer.getNumChannels() < 2) return;

    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    wm::Settings s;
    s.creature = (int) get ("creature"); s.mode = (int) get ("mode"); s.season = (int) get ("season"); s.weather = (int) get ("weather"); s.smode = (int) get ("smode");
    for (int i = 0; i < 9; ++i) s.events[i] = get ((juce::String ("ev") + juce::String (i)).toRawUTF8()) > 0.5f;
    s.sizefx = get ("sizefx"); s.seek = get ("seek"); s.slevel = get ("slevel"); s.elevel = get ("elevel"); s.alt = get ("alt");
    s.merge = get ("merge"); s.flange = get ("flange"); s.mod = get ("mod"); s.stretch = get ("stretch"); s.sspeed = get ("sspeed");
    s.dubTrim = get ("dubTrim"); s.dfbTrim = get ("dfbTrim"); s.echoTrim = get ("echoTrim"); s.airTrim = get ("airTrim"); s.roomTrim = get ("roomTrim");
    s.tame = get ("tame"); s.quant = get ("quant"); s.swing = get ("swing"); s.block = get ("block"); s.coupling = get ("coupling"); s.outGain = get ("out");

    wm::HostInfo h; h.bpm = lastBpm;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) { h.bpm = *b; lastBpm = *b; }
            if (auto q = pos->getPpqPosition()) { h.ppq = *q; h.hasPpq = true; }
            h.playing = pos->getIsPlaying();
        }
    engine->process (s, h, buffer.getWritePointer (0), buffer.getWritePointer (1), n);
}

juce::String WingMachineProcessor::getStatusText() const
{
    char buf[320] = {};
    { const juce::SpinLock::ScopedLockType sl (engineLock); if (engine) engine->getStatus (buf, sizeof buf); }
    return juce::String::fromUTF8 (buf);
}

juce::AudioProcessorEditor* WingMachineProcessor::createEditor() { return new WingMachineEditor (*this); }

void WingMachineProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void WingMachineProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new WingMachineProcessor(); }
