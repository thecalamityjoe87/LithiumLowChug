#include "ReadoutPanel.h"

#include "Controls.h"
#include "plugin/Parameters.h"

namespace lowchug
{

namespace
{
constexpr int padding = theme::space::lg;

// The open strings, as MIDI notes (E2 = 40), low to high.
constexpr int standardStrings[] = { 40, 45, 50, 55, 59, 64 };
constexpr int dropDStrings[] = { 38, 45, 50, 55, 59, 64 };
constexpr const char* noteNames[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

juce::String formatShift (int semitones, float fineCents)
{
    if (std::abs (fineCents) < 0.05f)
        return (semitones > 0 ? "+" : "") + juce::String (semitones) + " st";
    const auto total = (float) semitones + fineCents / 100.0f;
    return (total > 0.0f ? "+" : "") + juce::String (total, 2) + " st";
}
} // namespace

ReadoutPanel::ReadoutPanel (LowChugProcessor& p, TuningController& c)
    : processor (p), controller (c),
      help ("You hear", {
          "The tuning you are hearing, and the shift in semitones (st).",
          "The graph: your guitar in gray and what comes out in cyan, from low notes on the left to high on the right. "
          "The dashed lines mark the open strings of the tuning you hear; play your open strings and the cyan peaks "
          "should sit on them.",
          "The line below: the shift LowChug actually measures, and how far it is from what you asked for, in cents "
          "(hundredths of a semitone). Within 5 cents is On target." })
{
    addAndMakeVisible (help);

    bypassValue = p.getState().getRawParameterValue (params::bypass);
    scratchIn.resize (4096);
    scratchOut.resize (4096);

    // Start the tap clean (it may hold audio from an earlier editor), then on.
    auto& tap = processor.getAnalysisTap();
    tap.discardPending();
    tap.setEnabled (true);

    startTimerHz (30);
}

ReadoutPanel::~ReadoutPanel()
{
    processor.getAnalysisTap().setEnabled (false);
}

bool ReadoutPanel::isBypassed() const
{
    return bypassValue->load() >= 0.5f;
}

void ReadoutPanel::analyzeNow()
{
    // The host prepares the processor before playback; until then there is
    // no sample rate and nothing to analyze.
    const auto rate = processor.getSampleRate();
    if (rate <= 0.0)
        return;
    if (! juce::exactlyEqual (rate, analyzer.getSampleRate()))
        analyzer.prepare (rate);

    auto& tap = processor.getAnalysisTap();
    for (int n; (n = tap.read (scratchIn.data(), scratchOut.data(), (int) scratchIn.size())) > 0;)
        analyzer.push (scratchIn.data(), scratchOut.data(), n);

    result = analyzer.update (processor.getLatencySamples(), processor.getEffectiveShift());
    repaint();
}

void ReadoutPanel::resized()
{
    // The "?" in the top-right corner, level with the caption.
    help.setBounds (getWidth() - 10 - HelpButton::size, 11, HelpButton::size, HelpButton::size);
}

void ReadoutPanel::paint (juce::Graphics& g)
{
    using namespace theme;

    drawCard (g, getLocalBounds().toFloat(), colors::readoutBox);
    auto area = getLocalBounds().toFloat().reduced ((float) padding, 14.0f);

    // Caption row.
    auto captionRow = area.removeFromTop (14.0f);
    drawCaption (g, "YOU HEAR", captionRow);
    if (isBypassed())
    {
        g.setColour (colors::warning);
        drawTrackedText (g, "BYPASSED", label (type::caption, Weight::bold), 0.12f,
                         captionRow.withRight ((float) help.getX() - 4.0f), juce::Justification::centredRight);
    }
    area.removeFromTop (4.0f);

    // "Drop C"                         [-2 st]
    auto nameRow = area.removeFromTop (40.0f);
    const auto shiftText = formatShift (controller.getShiftSemitones(), controller.getFineCents());
    const auto pillFont = mono (type::body, Weight::medium);
    const auto pillWidth = juce::GlyphArrangement::getStringWidth (pillFont, shiftText) + 16.0f;
    const auto pill = nameRow.removeFromRight (pillWidth).withSizeKeepingCentre (pillWidth, 24.0f);
    g.setColour (colors::raised);
    g.fillRoundedRectangle (pill, 4.0f);
    g.setColour (colors::border);
    g.drawRoundedRectangle (pill.reduced (0.5f), 4.0f, 1.0f);
    g.setColour (colors::accent);
    g.setFont (pillFont);
    g.drawText (shiftText, pill, juce::Justification::centred, false);

    const auto heard = juce::String (tunings::describe (controller.getSource(), controller.getShiftSemitones()));
    nameRow.removeFromRight (8.0f);
    auto nameFont = label (36.0f, Weight::semibold);
    for (auto size : { 30.0f, 24.0f, 20.0f }) // long names ("Drop D (Octave Down)") get smaller
        if (juce::GlyphArrangement::getStringWidth (nameFont, heard) > nameRow.getWidth())
            nameFont = label (size, Weight::semibold);
    g.setColour (isBypassed() ? colors::textMuted : colors::text);
    g.setFont (nameFont);
    g.drawText (heard, nameRow, juce::Justification::centredLeft, true);

    area.removeFromTop (8.0f);
    paintMatchLine (g, area.removeFromBottom (16.0f));
    area.removeFromBottom (6.0f);
    paintSpectrum (g, area);
}

void ReadoutPanel::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> plot)
{
    using namespace theme;

    auto labels = plot.removeFromBottom (12.0f);
    const auto xFor = [&] (float position) { return plot.getX() + position * plot.getWidth(); };

    g.setColour (colors::border);
    g.fillRect (plot.withTop (plot.getBottom() - 1.0f));

    // The open strings of the tuning you hear (not known when playing in Custom).
    const auto source = controller.getSource();
    if (source != tunings::Source::custom)
    {
        const auto semitones = controller.getShiftSemitones();
        const auto shift = (float) semitones + controller.getFineCents() / 100.0f;
        const auto& strings = source == tunings::Source::dropD ? dropDStrings : standardStrings;

        g.setFont (mono (8.5f));
        for (auto midi : strings)
        {
            const auto hz = 440.0f * std::exp2 (((float) midi + shift - 69.0f) / 12.0f);
            const auto x = xFor (ShiftAnalyzer::displayPosition (hz));
            if (x < plot.getX() || x > plot.getRight())
                continue;

            const float dashes[] = { 2.0f, 2.0f };
            g.setColour (colors::borderStrong);
            g.drawDashedLine ({ x, plot.getY() + 4.0f, x, plot.getBottom() }, dashes, 2, 1.0f);
            g.setColour (colors::textMuted);
            g.drawText (noteNames[((midi + semitones) % 12 + 12) % 12],
                        juce::Rectangle<float> (24.0f, labels.getHeight()).withCentre ({ x, labels.getCentreY() + 1.0f }),
                        juce::Justification::centred, false);
        }
    }

    // A spectrum as a line across the plot (0..1 levels, bottom to top).
    const auto curve = [&] (const std::array<float, ShiftAnalyzer::displayPoints>& levels)
    {
        juce::Path p;
        for (int i = 0; i < ShiftAnalyzer::displayPoints; ++i)
        {
            const auto x = xFor ((float) i / (float) (ShiftAnalyzer::displayPoints - 1));
            const auto y = plot.getBottom() - 1.0f - levels[(size_t) i] * (plot.getHeight() - 4.0f);
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        return p;
    };

    g.setColour (colors::textMuted.withAlpha (0.55f));
    g.strokePath (curve (analyzer.getInputSpectrum()), juce::PathStrokeType (1.0f));

    const auto outColor = isBypassed() ? colors::textMuted : colors::accent;
    auto out = curve (analyzer.getOutputSpectrum());
    auto fill = out;
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.lineTo (plot.getX(), plot.getBottom());
    fill.closeSubPath();
    g.setColour (outColor.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (outColor);
    g.strokePath (out, juce::PathStrokeType (1.3f));
}

void ReadoutPanel::paintMatchLine (juce::Graphics& g, juce::Rectangle<float> line)
{
    using namespace theme;

    // One line: a colored dot, a verdict, then the numbers.
    juce::Colour dot = colors::textMuted;
    juce::String verdict, details;

    if (isBypassed())
    {
        verdict = "Bypassed";
    }
    else if (result.state == ShiftAnalyzer::Result::State::silent)
    {
        verdict = "Play to check the shift";
    }
    else if (result.state == ShiftAnalyzer::Result::State::searching)
    {
        verdict = "Listening...";
    }
    else
    {
        const auto errorCents = juce::roundToInt ((result.measuredSemitones - processor.getEffectiveShift()) * 100.0f);
        const auto size = std::abs (errorCents);
        dot = size <= 5 ? colors::accent : (size <= 15 ? colors::caution : colors::warning);
        verdict = size <= 5 ? "On target" : (size <= 15 ? "Close" : "Off target");
        details = juce::String::fromUTF8 (" \xc2\xb7 ") + (result.measuredSemitones > 0.0f ? "+" : "")
                  + juce::String (result.measuredSemitones, 2) + " st" + juce::String::fromUTF8 (" \xc2\xb7 ")
                  + (errorCents > 0 ? "+" : "") + juce::String (errorCents) + " ct";
    }

    g.setColour (dot);
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ line.getX() + 4.0f, line.getCentreY() }));
    line.removeFromLeft (14.0f);

    const auto font = mono (11.5f);
    g.setFont (font);
    g.setColour (colors::text);
    const auto verdictWidth = juce::GlyphArrangement::getStringWidth (font, verdict);
    g.drawText (verdict, line, juce::Justification::centredLeft, false);
    g.setColour (colors::textMuted);
    g.drawText (details, line.withTrimmedLeft (verdictWidth), juce::Justification::centredLeft, true);
}

} // namespace lowchug
