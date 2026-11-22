#pragma once

namespace lowchug
{

/**
    Makes the Standalone app usable with a guitar interface out of the box.
    Does nothing when LowChug runs as a plugin in a host.

    1. Linux only: opening a direct ALSA device (e.g. "MiniFuse 2, USB Audio")
       waits for PipeWire to let go of it instead of failing with "no
       channels". PipeWire keeps a card open for a few seconds after anything
       last used it. JACK is offered too (PipeWire provides it via pw-jack).
       The same fix as Lithium Drums' StandaloneAudioFix.

    2. JUCE's standalone wrapper mutes the audio input by default to avoid
       feedback (a laptop mic next to its speakers). For a guitar effect the
       input is the whole point, so the first time LowChug's Standalone runs
       the input is unmuted. After that, the "Mute audio input" box in
       Options > Audio/MIDI Settings is respected (JUCE saves it).

    Call from the processor's constructor, on the message thread.
*/
void setUpStandaloneAudio();

} // namespace lowchug
