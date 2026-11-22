#include "TunerView.h"

#include "Theme.h"

namespace lowchug
{

namespace
{
using namespace theme;

constexpr const char* noteNames[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
constexpr int standardStrings[] = { 40, 45, 50, 55, 59, 64 }; // E2 A2 D3 G3 B3 E4
constexpr int dropDStrings[] = { 38, 45, 50, 55, 59, 64 };

// How long a note stays on screen after the string stops (updates at 30 Hz).
constexpr int holdUpdates = 20;

juce::Colour colorForCents (float cents)
{
    const auto size = std::abs (cents);
    return size <= 3.0f ? colors::accent : (size <= 15.0f ? colors::caution : colors::warning);
}
} // namespace

TunerView::TunerView (TuningController& c)
    : controller (c)
{
    setInterceptsMouseClicks (false, false); // nothing to click
}

void TunerView::prepare (double rate)
{
    sampleRate = rate;
    detector.prepare (rate);
    ring.assign ((size_t) detector.getWindowSize(), 0.0f);
    window.assign ((size_t) detector.getWindowSize(), 0.0f);
    writePos = 0;
    received = 0;
    recentCount = 0;
    hasPitch = false;
}

void TunerView::push (const float* samples, int numSamples)
{
    if (ring.empty())
        return;
    for (int i = 0; i < numSamples; ++i)
    {
        ring[(size_t) writePos] = samples[i];
        writePos = (writePos + 1) % (int) ring.size();
    }
    received += numSamples;
}

void TunerView::update()
{
    if (ring.empty() || received < (juce::int64) ring.size())
        return;

    // Oldest sample first, as the detector expects.
    const auto size = ring.size();
    for (size_t i = 0; i < size; ++i)
        window[i] = ring[((size_t) writePos + i) % size];

    const auto reading = detector.detect (window.data());

    if (reading.frequency > 0.0f)
    {
        const auto note = 69.0f + 12.0f * std::log2 (reading.frequency / 440.0f);

        // The median of the last five readings throws out the odd wild one
        // (the pick attack, a harmonic jumping out); easing toward it keeps
        // the needle calm without lagging behind a real change.
        if (recentCount > 0 && std::abs (note - recent[(size_t) (recentCount - 1) % recent.size()]) > 0.5f)
            recentCount = 0; // a different note: start afresh

        recent[(size_t) recentCount % recent.size()] = note;
        ++recentCount;
        auto sorted = recent;
        const auto n = std::min<int> (recentCount, (int) recent.size());
        std::sort (sorted.begin(), sorted.begin() + n);
        const auto median = sorted[(size_t) n / 2];

        shownNote = hasPitch && std::abs (median - shownNote) < 0.5f ? shownNote + 0.35f * (median - shownNote) : median;
        hasPitch = true;
        silentUpdates = 0;
    }
    else if (hasPitch && ++silentUpdates > holdUpdates)
    {
        hasPitch = false;
        recentCount = 0;
    }

    repaint();
}

void TunerView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    const auto nearest = juce::roundToInt (shownNote);
    const auto cents = (shownNote - (float) nearest) * 100.0f;
    const auto meterColor = hasPitch ? colorForCents (cents) : colors::textMuted;
    const auto inTune = hasPitch && std::abs (cents) <= 3.0f;

    // The note, big, with its octave.
    auto noteRow = area.removeFromTop (66.0f);
    if (hasPitch)
    {
        const auto name = juce::String (noteNames[((nearest % 12) + 12) % 12]);
        const auto nameFont = label (64.0f, Weight::semibold);
        const auto width = juce::GlyphArrangement::getStringWidth (nameFont, name);
        auto box = noteRow.withSizeKeepingCentre (width + 20.0f, noteRow.getHeight());
        g.setColour (inTune ? colors::accent : colors::text);
        g.setFont (nameFont);
        g.drawText (name, box.removeFromLeft (width), juce::Justification::centredLeft, false);
        g.setColour (colors::textMuted);
        g.setFont (mono (14.0f));
        g.drawText (juce::String (nearest / 12 - 1), box.withTrimmedTop (30.0f), juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (colors::textMuted);
        g.setFont (body (15.0f));
        g.drawText ("Play one string", noteRow, juce::Justification::centred, false);
    }

    // The meter: -50 .. +50 cents, ticks every 10, the needle a dot.
    area.removeFromTop (6.0f);
    auto meter = area.removeFromTop (30.0f).reduced (8.0f, 0.0f);
    const auto y = meter.getCentreY() + 2.0f;
    const auto xFor = [&] (float c) { return meter.getCentreX() + c / 50.0f * meter.getWidth() * 0.5f; };

    g.setColour (colors::border);
    g.drawLine (meter.getX(), y, meter.getRight(), y, 2.0f);
    for (int c = -50; c <= 50; c += 10)
    {
        g.setColour (c == 0 ? colors::textMuted : colors::knobTicks);
        g.drawLine (xFor ((float) c), y - (c == 0 ? 10.0f : 5.0f), xFor ((float) c), y + 5.0f, c == 0 ? 2.0f : 1.5f);
    }
    if (hasPitch)
    {
        const auto x = xFor (juce::jlimit (-50.0f, 50.0f, cents));
        g.setColour (meterColor.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ x, y }));
        g.setColour (meterColor);
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ x, y }));
    }

    // -50   +4 ct · 82.6 Hz   +50
    area.removeFromTop (4.0f);
    auto readout = area.removeFromTop (16.0f);
    g.setFont (mono (11.5f));
    g.setColour (colors::textMuted);
    g.drawText ("-50", readout, juce::Justification::centredLeft, false);
    g.drawText ("+50", readout, juce::Justification::centredRight, false);
    if (hasPitch)
    {
        const auto hz = 440.0f * std::exp2 ((shownNote - 69.0f) / 12.0f);
        const auto rounded = juce::roundToInt (cents);
        g.setColour (meterColor);
        g.drawText ((rounded > 0 ? "+" : "") + juce::String (rounded) + " ct" + juce::String::fromUTF8 (" \xc2\xb7 ")
                        + juce::String (hz, 1) + " Hz",
                    readout, juce::Justification::centred, false);
    }

    // The strings of your tuning; the one being played lights up.
    auto strings = area.removeFromBottom (30.0f);
    const auto source = controller.getSource();
    if (source == tunings::Source::custom)
    {
        g.setColour (colors::textMuted);
        g.setFont (body (type::small));
        g.drawText ("Chromatic", strings, juce::Justification::centred, false);
        return;
    }

    const auto& midis = source == tunings::Source::dropD ? dropDStrings : standardStrings;
    const auto gap = 6.0f;
    const auto chipWidth = (strings.getWidth() - 5.0f * gap) / 6.0f;
    for (auto midi : midis)
    {
        const auto chip = strings.removeFromLeft (chipWidth);
        strings.removeFromLeft (gap);
        const auto playing = hasPitch && nearest == midi;

        g.setColour (playing && inTune ? colors::accent : (playing ? colors::raisedHigh : colors::raised));
        g.fillRoundedRectangle (chip, 5.0f);
        g.setColour (playing ? meterColor : colors::border);
        g.drawRoundedRectangle (chip.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (playing ? (inTune ? colors::background : meterColor) : colors::textMuted);
        g.setFont (label (15.0f, Weight::semibold));
        g.drawText (noteNames[midi % 12], chip, juce::Justification::centred, false);
    }
}

} // namespace lowchug
