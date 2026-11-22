#include "StandaloneAudio.h"

#include <juce_audio_utils/juce_audio_utils.h>
// The standalone wrapper's classes. Including the header in shared code is
// fine: in a plugin build StandalonePluginHolder::getInstance() is simply
// null, so everything below does nothing.
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace lowchug
{

namespace
{

#if JUCE_LINUX

// PipeWire keeps a card open for a few seconds after an app stops using it.
// Until it lets go, JUCE finds the card has no channels and refuses to open it.
constexpr int maxWaitMs = 8000;
constexpr int retryIntervalMs = 250;

/** Wraps JUCE's ALSA device type, retrying device creation until the card
    has channels (or 8 s have passed). Everything else is passed straight
    through to the real ALSA type. */
class PatientAlsaType final : public juce::AudioIODeviceType,
                              private juce::AudioIODeviceType::Listener
{
public:
    explicit PatientAlsaType (std::unique_ptr<juce::AudioIODeviceType> typeToWrap)
        : AudioIODeviceType (typeToWrap->getTypeName()), inner (std::move (typeToWrap))
    {
        inner->addListener (this);
    }

    ~PatientAlsaType() override { inner->removeListener (this); }

    void scanForDevices() override                                              { inner->scanForDevices(); }
    juce::StringArray getDeviceNames (bool wantInputNames) const override       { return inner->getDeviceNames (wantInputNames); }
    int getDefaultDeviceIndex (bool forInput) const override                    { return inner->getDefaultDeviceIndex (forInput); }
    int getIndexOfDevice (juce::AudioIODevice* d, bool asInput) const override  { return inner->getIndexOfDevice (d, asInput); }
    bool hasSeparateInputsAndOutputs() const override                           { return inner->hasSeparateInputsAndOutputs(); }

    juce::AudioIODevice* createDevice (const juce::String& outputName, const juce::String& inputName) override
    {
        const auto start = juce::Time::getMillisecondCounter();

        for (;;)
        {
            std::unique_ptr<juce::AudioIODevice> device (inner->createDevice (outputName, inputName));

            if (device == nullptr || hasRequestedChannels (*device, outputName, inputName)
                || juce::Time::getMillisecondCounter() - start >= (juce::uint32) maxWaitMs)
                return device.release();

            juce::Thread::sleep (retryIntervalMs);
        }
    }

private:
    static bool hasRequestedChannels (juce::AudioIODevice& device, const juce::String& outputName, const juce::String& inputName)
    {
        return (outputName.isEmpty() || ! device.getOutputChannelNames().isEmpty())
            && (inputName.isEmpty() || ! device.getInputChannelNames().isEmpty());
    }

    void audioDeviceListChanged() override { callDeviceChangeListeners(); }

    std::unique_ptr<juce::AudioIODeviceType> inner;
};

void installPatientAlsa (juce::AudioDeviceManager& manager)
{
    // The holder creates the plugin before it sets up its audio devices, so
    // these are the types it will use. Adding any stops JUCE adding its own.
    if (auto alsa = std::unique_ptr<juce::AudioIODeviceType> (juce::AudioIODeviceType::createAudioIODeviceType_ALSA()))
        manager.addAudioDeviceType (std::make_unique<PatientAlsaType> (std::move (alsa)));

    if (auto jack = std::unique_ptr<juce::AudioIODeviceType> (juce::AudioIODeviceType::createAudioIODeviceType_JACK()))
        manager.addAudioDeviceType (std::move (jack));
}

#endif

// Our own marker in the standalone settings file: set once we've unmuted the
// input the first time, so we never override the user's choice again.
constexpr const char* inputUnmutedKey = "lowchugInputUnmutedOnce";

} // namespace

void setUpStandaloneAudio()
{
    auto* holder = juce::StandalonePluginHolder::getInstance();

    if (holder == nullptr) // running as a plugin
        return;

   #if JUCE_LINUX
    installPatientAlsa (holder->deviceManager);
   #endif

    // The holder reads its saved "mute input" setting AFTER creating us, so
    // unmuting now would be overwritten. callAsync runs this once the holder
    // has finished starting up (we are inside its constructor right now).
    juce::MessageManager::callAsync ([]
    {
        auto* h = juce::StandalonePluginHolder::getInstance();

        if (h == nullptr || h->settings == nullptr || h->settings->containsKey (inputUnmutedKey))
            return;

        h->getMuteInputValue().setValue (false);
        h->settings->setValue (inputUnmutedKey, true);
    });
}

} // namespace lowchug
