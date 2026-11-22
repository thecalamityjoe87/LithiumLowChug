#include "HeaderBar.h"

#include "Theme.h"
#include "plugin/Parameters.h"

namespace lowchug
{

HeaderBar::HeaderBar (LowChugProcessor& p)
    : processor (p),
      presetBar (p),
      bypassAttachment (p.getState(), params::bypass, bypassButton)
{
    bypassButton.setClickingTogglesState (true);
    bypassButton.getProperties().set ("danger", true); // lights red when on
    bypassButton.setTooltip ("Bypass: hear the guitar untouched. It stays delayed by the latency, so it stays in time.");
    addAndMakeVisible (bypassButton);

    infoButton.onClick = [this] { if (onShowAbout) onShowAbout(); };
    addAndMakeVisible (infoButton);
    addAndMakeVisible (presetBar);

    latencyText = makeLatencyText();
    startTimerHz (4);
}

juce::String HeaderBar::makeLatencyText() const
{
    const auto rate = processor.getSampleRate();
    if (rate <= 0.0)
        return "--";
    return juce::String (1000.0 * processor.getLatencySamples() / rate, 1) + " ms";
}

void HeaderBar::timerCallback()
{
    if (auto text = makeLatencyText(); text != latencyText)
    {
        latencyText = text;
        repaint();
    }
}

namespace
{
juce::Font productFont()
{
    return theme::label (theme::wordmark::productSize, theme::Weight::medium).withExtraKerningFactor (theme::wordmark::tracking);
}
juce::Font subtitleFont()
{
    return theme::label (theme::wordmark::subtitleSize, theme::Weight::medium).withExtraKerningFactor (theme::wordmark::tracking);
}
} // namespace

float HeaderBar::getWordmarkRight() const
{
    return (float) theme::space::page + juce::GlyphArrangement::getStringWidth (productFont(), "LOWCHUG") + 10.0f
           + juce::GlyphArrangement::getStringWidth (subtitleFont(), "GUITAR DETUNER");
}

void HeaderBar::paint (juce::Graphics& g)
{
    using namespace theme;

    g.fillAll (colors::panel);
    g.setColour (colors::border);
    g.fillRect (getLocalBounds().removeFromBottom (1));

    // The family wordmark, all capitals: LITHIUM (bold, white) over LOWCHUG
    // (medium, gray) and GUITAR DETUNER. drawSingleLineText places text on
    // a baseline, so the two sizes on the second row line up exactly.
    const auto x = (float) space::page;
    const auto brand = label (wordmark::brandSize, Weight::bold).withExtraKerningFactor (wordmark::tracking);
    const auto product = productFont();
    const auto subtitle = subtitleFont();

    g.setColour (colors::text);
    g.setFont (brand);
    g.drawSingleLineText ("LITHIUM", juce::roundToInt (x), 25);

    g.setColour (colors::textMuted);
    g.setFont (product);
    g.drawSingleLineText ("LOWCHUG", juce::roundToInt (x), 50);
    const auto productWidth = juce::GlyphArrangement::getStringWidth (product, "LOWCHUG");
    g.setFont (subtitle);
    g.drawSingleLineText ("GUITAR DETUNER", juce::roundToInt (x + productWidth + 10.0f), 50);

    // LATENCY over its value, right-aligned, left of Bypass.
    auto readout = latencyArea;
    drawCaption (g, "LATENCY", readout.removeFromTop (14.0f), juce::Justification::centredRight);
    g.setColour (colors::text);
    g.setFont (mono (type::body, Weight::medium));
    g.drawText (latencyText, readout, juce::Justification::centredRight, false);
}

void HeaderBar::resized()
{
    // Right to left: the "i" button at the edge (as in Lithium Drums), then Bypass.
    constexpr int iconSize = 30;
    infoButton.setBounds (getWidth() - theme::space::page - iconSize, (getHeight() - iconSize) / 2, iconSize, iconSize);
    bypassButton.setBounds (infoButton.getX() - theme::space::md - 88, (getHeight() - 32) / 2, 88, 32);

    // The latency readout left of Bypass ("21.3 ms" needs about 64 px)...
    latencyArea = { (float) bypassButton.getX() - theme::space::lg - 64.0f, 14.0f, 64.0f, 36.0f };

    // ...and the presets centered in the space between the wordmark and it.
    const auto left = juce::roundToInt (getWordmarkRight()) + theme::space::lg;
    const auto right = juce::roundToInt (latencyArea.getX()) - theme::space::lg;
    const auto width = std::min (PresetBar::idealWidth, right - left);
    presetBar.setBounds (left + (right - left - width) / 2, (getHeight() - 32) / 2, width, 32);
}

} // namespace lowchug
