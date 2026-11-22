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

// Within this many cents counts as in tune (the shaded zone, cyan).
constexpr float inTuneCents = 3.0f;

// How long a note stays on screen after the string stops (updates at 30 Hz).
constexpr int holdUpdates = 20;

juce::Colour colorForCents (float cents)
{
    const auto size = std::abs (cents);
    return size <= inTuneCents ? colors::accent : (size <= 15.0f ? colors::caution : colors::warning);
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
    const auto inTune = hasPitch && std::abs (cents) <= inTuneCents;

    // The note, big, with its octave.
    auto noteRow = area.removeFromTop (66.0f);
    if (hasPitch)
    {
        const auto name = juce::String (noteNames[((nearest % 12) + 12) % 12]);
        const auto nameFont = label (64.0f, Weight::semibold);
        // The letter is centered over the scale's 0; the octave hangs off
        // its right side, so it doesn't pull the letter off center.
        const auto width = juce::GlyphArrangement::getStringWidth (nameFont, name);
        const auto letter = noteRow.withSizeKeepingCentre (width, noteRow.getHeight());
        g.setColour (inTune ? colors::accent : colors::text);
        g.setFont (nameFont);
        g.drawText (name, letter, juce::Justification::centred, false);
        g.setColour (colors::textMuted);
        g.setFont (mono (14.0f));
        g.drawText (juce::String (nearest / 12 - 1), letter.withX (letter.getRight() + 1.0f).withWidth (20.0f).withTrimmedTop (30.0f),
                    juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (colors::textMuted);
        g.setFont (body (15.0f));
        g.drawText ("Play one string", noteRow, juce::Justification::centred, false);
    }

    // The scale, as on Lithium Studio's tuner: 50 cents flat (left) to 50
    // sharp (right), tall ticks at the ends and short ones every 10 cents,
    // the in-tune zone shaded in the middle, and the needle a bar with a
    // soft glow.
    area.removeFromTop (6.0f);
    auto scale = area.removeFromTop (30.0f).reduced (8.0f, 0.0f);
    const auto midY = scale.getCentreY();
    const auto xFor = [&] (float c) { return scale.getCentreX() + juce::jlimit (-50.0f, 50.0f, c) / 50.0f * scale.getWidth() * 0.5f; };

    for (int c = -50; c <= 50; c += 10)
    {
        if (c == 0)
            continue; // the in-tune zone marks the middle
        const auto end = std::abs (c) == 50;
        g.setColour (colors::textMuted.withAlpha (end ? 0.8f : 0.45f));
        g.fillRect (juce::Rectangle<float> (end ? 1.5f : 1.0f, end ? 13.0f : 6.0f).withCentre ({ xFor ((float) c), midY }));
    }

    const auto zone = juce::Rectangle<float> (xFor (inTuneCents) - xFor (-inTuneCents) + 4.0f, 19.0f).withCentre ({ scale.getCentreX(), midY });
    g.setColour (colors::accent.withAlpha (0.2f));
    g.fillRoundedRectangle (zone, 2.0f);
    g.setColour (colors::accent.withAlpha (0.35f));
    g.fillRect (juce::Rectangle<float> (1.0f, zone.getHeight() - 4.0f).withCentre (zone.getCentre()));

    if (hasPitch)
    {
        const auto needle = juce::Rectangle<float> (3.0f, 21.0f).withCentre ({ xFor (cents), midY });
        g.setColour (meterColor.withAlpha (0.25f));
        g.fillRoundedRectangle (needle.expanded (2.0f, 1.0f), 2.5f);
        g.setColour (meterColor);
        g.fillRoundedRectangle (needle, 1.5f);
    }

    // Under the scale: flat, 0, sharp. The side the note is off lights up.
    // (The signs are drawn: the fonts haven't got them.)
    area.removeFromTop (2.0f);
    const auto captions = area.removeFromTop (16.0f).reduced (8.0f, 0.0f);
    const auto signY = captions.getCentreY();
    const auto flat = hasPitch && ! inTune && cents < 0.0f;
    const auto sharp = hasPitch && ! inTune && cents > 0.0f;

    g.setColour (colors::textMuted);
    g.setFont (label (type::caption, Weight::semibold));
    g.drawText ("0", juce::Rectangle<float> (20.0f, captions.getHeight()).withCentre ({ scale.getCentreX() + 0.5f, signY }),
                juce::Justification::centred, false);

    {
        // A flat: a stem with a rounded bowl at its foot.
        juce::Path p;
        const auto x = captions.getX() + 1.0f, top = signY - 6.5f, bottom = signY + 5.0f;
        p.startNewSubPath (x, top);
        p.lineTo (x, bottom);
        p.cubicTo (x + 6.5f, bottom - 2.0f, x + 6.5f, bottom - 7.0f, x, bottom - 4.0f);
        g.setColour (flat ? colors::text : colors::textMuted);
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    {
        // A sharp: two upright strokes crossed by two rising bars.
        juce::Path p;
        const auto x = captions.getRight() - 9.0f;
        p.startNewSubPath (x + 2.5f, signY - 5.5f);  p.lineTo (x + 2.5f, signY + 6.0f);
        p.startNewSubPath (x + 6.5f, signY - 6.5f);  p.lineTo (x + 6.5f, signY + 5.0f);
        p.startNewSubPath (x, signY - 1.0f);         p.lineTo (x + 9.0f, signY - 3.0f);
        p.startNewSubPath (x, signY + 3.0f);         p.lineTo (x + 9.0f, signY + 1.0f);
        g.setColour (sharp ? colors::text : colors::textMuted);
        g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
