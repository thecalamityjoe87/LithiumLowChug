#include "AboutPanel.h"

#include "Theme.h"

// JucePlugin_VersionString, JucePlugin_Manufacturer etc. come from
// juce_add_plugin (compile definitions); LOWCHUG_AUTHOR_STRING from
// src/plugin/CMakeLists.txt.

namespace lowchug
{

namespace
{
constexpr int cardPadding = 28;
}

//==============================================================================
OverlayPanel::OverlayPanel (int width, int height)
    : cardWidth (width), cardHeight (height)
{
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);
    setWantsKeyboardFocus (true); // for Escape
}

juce::Rectangle<int> OverlayPanel::getCardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (cardWidth, cardHeight);
}

void OverlayPanel::paint (juce::Graphics& g)
{
    using namespace theme;

    g.fillAll (colors::background.withAlpha (0.78f));

    const auto card = getCardBounds().toFloat();
    g.setColour (colors::panel);
    g.fillRoundedRectangle (card, 10.0f);
    g.setColour (colors::borderStrong);
    g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.0f);
}

void OverlayPanel::resized()
{
    closeButton.setBounds (getCardBounds().reduced (cardPadding, 18).removeFromBottom (30).removeFromRight (96));
    layoutContent();
}

void OverlayPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! getCardBounds().contains (e.getPosition()) && onClose)
        onClose();
}

bool OverlayPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && onClose)
    {
        onClose();
        return true;
    }
    return false;
}

//==============================================================================
namespace
{
constexpr const char* licenseUrl = "https://www.gnu.org/licenses/agpl-3.0.html";
}

AboutPanel::AboutPanel()
    : OverlayPanel (600, 460)
{
    websiteButton.setTooltip (JucePlugin_ManufacturerWebsite);
    websiteButton.onClick = [] { juce::URL (JucePlugin_ManufacturerWebsite).launchInDefaultBrowser(); };
    addAndMakeVisible (websiteButton);

    licenseButton.setTooltip ("GNU Affero General Public License, version 3");
    licenseButton.onClick = [] { juce::URL (licenseUrl).launchInDefaultBrowser(); };
    addAndMakeVisible (licenseButton);
}

void AboutPanel::layoutContent()
{
    const auto card = getCardBounds();
    // Top right, beside the wordmark: GitHub, and License under it.
    websiteButton.setBounds (card.getRight() - cardPadding - 112, card.getY() + cardPadding + 2, 112, 30);
    licenseButton.setBounds (websiteButton.getBounds().translated (0, 38));
}

void AboutPanel::paint (juce::Graphics& g)
{
    using namespace theme;
    OverlayPanel::paint (g);

    auto area = getCardBounds().reduced (cardPadding).toFloat();

    // The wordmark on one line, then the version and who made it.
    const auto brand = label (26.0f, Weight::bold).withExtraKerningFactor (wordmark::tracking);
    const auto product = label (26.0f, Weight::medium).withExtraKerningFactor (wordmark::tracking);
    const auto wordmarkRow = area.removeFromTop (30.0f);
    g.setColour (colors::text);
    g.setFont (brand);
    g.drawText ("LITHIUM", wordmarkRow, juce::Justification::centredLeft, false);
    g.setColour (colors::textMuted);
    g.setFont (product);
    g.drawText ("LOWCHUG", wordmarkRow.withTrimmedLeft (juce::GlyphArrangement::getStringWidth (brand, "LITHIUM") + 8.0f),
                juce::Justification::centredLeft, false);

    g.setFont (mono (13.0f));
    g.drawText ("Version " JucePlugin_VersionString, area.removeFromTop (22.0f), juce::Justification::centredLeft, false);

    // "by Isaac Joseph · Calamity Audio": the name in full color, the brand muted.
    const auto byLine = area.removeFromTop (22.0f);
    const juce::String by = "by " LOWCHUG_AUTHOR_STRING;
    const auto byWidth = juce::GlyphArrangement::getStringWidth (body (15.0f), by);
    g.setColour (colors::text);
    g.setFont (body (15.0f));
    g.drawText (by, byLine, juce::Justification::centredLeft, false);
    g.setColour (colors::textMuted);
    g.drawText (juce::String::fromUTF8 ("\xc2\xb7 ") + JucePlugin_Manufacturer, byLine.withTrimmedLeft (byWidth + 4.5f),
                juce::Justification::centredLeft, false);

    area.removeFromTop (14.0f);
    g.setColour (colors::border);
    g.fillRect (area.removeFromTop (1.0f));
    area.removeFromTop (16.0f);

    // What it is.
    g.setColour (colors::text);
    g.setFont (body (14.5f));
    const juce::String what = "LowChug is a guitar detuner: play in standard or Drop D and hear your guitar tuned lower, "
                              "chords and all. Use it as a VST3, LV2 or AU plugin, or on its own as a standalone app.";
    g.drawFittedText (what, area.removeFromTop (60.0f).toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
    area.removeFromTop (14.0f);

    drawCaption (g, "BUILT WITH", area.removeFromTop (16.0f));
    area.removeFromTop (6.0f);

    static const std::pair<const char*, const char*> parts[] {
        { "JUCE", "AGPLv3 or commercial" },
        { "VST3 SDK (Steinberg)", "MIT" },
        { "Signalsmith Linear and Stretch", "MIT" },
        { "Barlow and Barlow Condensed fonts", "SIL Open Font License" },
        { "IBM Plex Mono font", "SIL Open Font License" },
    };

    for (const auto& [name, license] : parts)
    {
        auto row = area.removeFromTop (21.0f);
        g.setColour (colors::text);
        g.setFont (body (14.0f));
        g.drawText (name, row, juce::Justification::centredLeft, true);
        g.setColour (colors::textMuted);
        g.setFont (mono (12.5f));
        g.drawText (license, row.withTrimmedLeft (row.getWidth() * 0.55f), juce::Justification::centredLeft, true);
    }

    // The GPL family asks interactive programs to show a copyright and
    // no-warranty notice; this is it (as in Lithium Drums).
    area.removeFromTop (10.0f);
    g.setColour (colors::textMuted);
    g.setFont (body (12.5f));
    g.drawFittedText ("Copyright (C) 2026 " LOWCHUG_AUTHOR_STRING ". Lithium LowChug is free software: you can redistribute "
                      "and change it under the terms of the GNU Affero General Public License, version 3. It comes with no "
                      "warranty.",
                      area.removeFromTop (36.0f).toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
}

} // namespace lowchug
