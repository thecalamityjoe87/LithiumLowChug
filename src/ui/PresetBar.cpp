#include "PresetBar.h"

#include "Controls.h"
#include "LithiumLookAndFeel.h"
#include "Theme.h"
#include "plugin/Presets.h"

namespace lowchug
{

using namespace theme;

namespace
{
juce::String modeName (bool studio) { return studio ? "Studio" : "Live"; }

/** "play in Standard": the tuning a preset is played from. */
juce::String playedFrom (int program)
{
    switch (presets::tuningOf (program).source)
    {
        case tunings::Source::dropD:  return "play in Drop D";
        case tunings::Source::custom: return "play in Custom";
        case tunings::Source::standard: break;
    }
    return "play in Standard";
}
} // namespace

//==============================================================================
/** A small button with a chevron, for previous / next. */
class PresetBar::Arrow final : public juce::Button
{
public:
    Arrow (const juce::String& name, bool pointsLeft) : juce::Button (name), left (pointsLeft)
    {
        setTooltip (name);
    }

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isDown) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (isDown ? colors::raisedHigh.brighter (0.1f) : (isMouseOver ? colors::raisedHigh : colors::raised));
        g.fillRoundedRectangle (bounds, cornerRadius - 1.0f);
        g.setColour (isMouseOver ? colors::borderStrong : colors::border);
        g.drawRoundedRectangle (bounds, cornerRadius - 1.0f, 1.0f);

        const auto c = bounds.getCentre();
        const auto dx = left ? 2.0f : -2.0f;
        juce::Path chevron;
        chevron.startNewSubPath (c.x + dx, c.y - 4.0f);
        chevron.lineTo (c.x - dx, c.y);
        chevron.lineTo (c.x + dx, c.y + 4.0f);
        g.setColour (isMouseOver ? colors::text : colors::textMuted);
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    bool left;
};

//==============================================================================
/** The box: the preset's name (an accent "*" when edited) over its mode and
    the tuning it's played from, and a chevron. Drawn as Lithium Drums'
    preset box. */
class PresetBar::Box final : public juce::Button
{
public:
    Box() : juce::Button ("Preset")
    {
        setTooltip ("Factory presets");
    }

    juce::String title, detail;
    bool edited = false;
    bool panelOpen = false;

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isDown) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (isDown ? colors::raisedHigh.brighter (0.05f) : (isMouseOver ? colors::raisedHigh : colors::raised));
        g.fillRoundedRectangle (bounds, cornerRadius);
        g.setColour (panelOpen ? colors::accent : (isMouseOver ? colors::borderStrong : colors::border));
        g.drawRoundedRectangle (bounds, cornerRadius, 1.0f);

        if (hasKeyboardFocus (false))
            LithiumLookAndFeel::drawFocusRing (g, bounds, cornerRadius);

        // The chevron at the right.
        const auto cx = bounds.getRight() - 14.0f, cy = bounds.getCentreY();
        juce::Path chevron;
        chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
        chevron.lineTo (cx, cy + 2.0f);
        chevron.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (colors::textMuted);
        g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Two lines; a long name ends in an ellipsis.
        auto lines = bounds.reduced (10.0f, 3.0f).withTrimmedRight (18.0f);
        auto top = lines.removeFromTop (lines.getHeight() * 0.55f);
        const auto titleFont = body (13.5f, Weight::semibold);
        const auto mark = edited ? juce::String (" *") : juce::String();
        const auto markWidth = juce::GlyphArrangement::getStringWidth (titleFont, mark);
        const auto titleWidth = std::min (juce::GlyphArrangement::getStringWidth (titleFont, title) + 1.0f, top.getWidth() - markWidth);

        g.setFont (titleFont);
        g.setColour (colors::text);
        g.drawText (title, top.removeFromLeft (titleWidth), juce::Justification::bottomLeft, true);
        g.setColour (colors::accent);
        g.drawText (mark, top, juce::Justification::bottomLeft, false);

        g.setColour (colors::textMuted);
        g.setFont (body (11.5f));
        g.drawText (detail, lines, juce::Justification::topLeft, true);
    }
};

//==============================================================================
/** The preset panel, in a call-out under the box (as Lithium Drums' preset
    panel): two columns, Live and Studio, headed with each mode's latency,
    with a rule between them. The loaded preset is raised, with a dot.
    Choosing one loads it and closes the call-out. */
class PresetBar::Panel final : public juce::Component
{
public:
    static constexpr int rowHeight = 26, rowGap = 2, headingHeight = 22, padding = 12, columnGap = 24;

    Panel (PresetBar& o) : owner (o)
    {
        const auto current = owner.processor.getCurrentProgram();
        const auto edited = ! presets::matches (owner.processor.getState(), current);

        for (int program = 0; program < presets::numPresets; ++program)
        {
            auto row = std::make_unique<Row> (presets::getName (program), presets::tuningOf (program).name,
                                              program == current && ! edited);
            row->onClick = [this, program] { choose (program); };
            addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }

        headings[0] = owner.getMenuHeader (params::Mode::live);
        headings[1] = owner.getMenuHeader (params::Mode::studio);

        const auto perColumn = (int) presets::factoryTunings.size();
        setSize (2 * columnWidth + columnGap + 2 * padding,
                 8 + headingHeight + 4 + perColumn * (rowHeight + rowGap) + padding);
        setTitle ("Factory presets");
    }

    ~Panel() override
    {
        if (onClosed)
            onClosed();
    }

    std::function<void()> onClosed;

    void paint (juce::Graphics& g) override
    {
        for (int column = 0; column < 2; ++column)
        {
            g.setColour (colors::textMuted);
            drawTrackedText (g, headings[column], label (12.5f, Weight::bold), 0.1f,
                             juce::Rectangle<float> ((float) (padding + column * (columnWidth + columnGap) + 6), 8.0f,
                                                     (float) columnWidth, (float) headingHeight),
                             juce::Justification::centredLeft);
        }

        // The rule between the columns.
        g.setColour (colors::border);
        g.fillRect (padding + columnWidth + columnGap / 2, 8, 1, getHeight() - 8 - padding);
    }

    void resized() override
    {
        const auto perColumn = (int) presets::factoryTunings.size();
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const auto column = i / perColumn, row = i % perColumn;
            rows[(size_t) i]->setBounds (padding + column * (columnWidth + columnGap), 8 + headingHeight + 4 + row * (rowHeight + rowGap),
                                         columnWidth, rowHeight);
        }
    }

    /** The row for a program (for tests). */
    juce::Button& getRow (int program) { return *rows[(size_t) program]; }

private:
    static constexpr int columnWidth = 150;

    /** One preset: its name, raised with a dot when it's the one loaded. */
    class Row final : public juce::Button
    {
    public:
        Row (const juce::String& fullName, const juce::String& shown, bool isCurrent)
            : juce::Button (fullName), text (shown), current (isCurrent)
        {
            setTitle (fullName);
            setToggleable (true);
            setToggleState (isCurrent, juce::dontSendNotification);
        }

        void paintButton (juce::Graphics& g, bool isMouseOver, bool) override
        {
            auto area = getLocalBounds().toFloat();
            if (current || isMouseOver)
            {
                g.setColour (current ? colors::raisedHigh : colors::raised);
                g.fillRoundedRectangle (area, cornerRadius - 2.0f);
            }

            auto inside = area.reduced (10.0f, 0.0f);
            const auto dot = inside.removeFromLeft (7.0f).withSizeKeepingCentre (7.0f, 7.0f);
            if (current)
            {
                g.setColour (colors::accent);
                g.fillEllipse (dot);
            }
            inside.removeFromLeft (8.0f);

            g.setColour (current ? colors::text : colors::text.withAlpha (0.88f));
            g.setFont (body (13.5f, current ? Weight::semibold : Weight::regular));
            g.drawText (text, inside, juce::Justification::centredLeft, true);

            if (hasKeyboardFocus (false))
                LithiumLookAndFeel::drawFocusRing (g, area, cornerRadius - 2.0f);
        }

    private:
        juce::String text;
        bool current;
    };

    void choose (int program)
    {
        owner.processor.setCurrentProgram (program);
        owner.refresh();

        // Close the call-out. dismiss() deletes it (and this panel) later,
        // on the message loop, so nothing here runs after it.
        if (auto* callOut = findParentComponentOfClass<juce::CallOutBox>())
            callOut->dismiss();
    }

    PresetBar& owner;
    std::vector<std::unique_ptr<Row>> rows;
    juce::String headings[2];
};

//==============================================================================
PresetBar::PresetBar (LowChugProcessor& p)
    : processor (p),
      previous (std::make_unique<Arrow> ("Previous preset", true)),
      next (std::make_unique<Arrow> ("Next preset", false)),
      box (std::make_unique<Box>())
{
    previous->onClick = [this] { step (-1); };
    next->onClick = [this] { step (1); };
    box->onClick = [this] { showPanel(); };
    addAndMakeVisible (*previous);
    addAndMakeVisible (*next);
    addAndMakeVisible (*box);

    refresh();
    startTimerHz (10); // cheap: compares nine values
}

PresetBar::~PresetBar() = default;

void PresetBar::step (int steps)
{
    const auto count = presets::numPresets;
    processor.setCurrentProgram (((processor.getCurrentProgram() + steps) % count + count) % count);
    refresh();
}

void PresetBar::refresh()
{
    const auto program = processor.getCurrentProgram();
    const auto edited = ! presets::matches (processor.getState(), program);
    const auto title = juce::String (presets::tuningOf (program).name);
    const auto detail = modeName (presets::isStudio (program)) + juce::String::fromUTF8 (" \xc2\xb7 ") + playedFrom (program);
    const auto name = title + (edited ? " *" : "");

    if (name != shownName || detail != shownDetail)
    {
        shownName = name;
        shownDetail = detail;
        box->title = title;
        box->edited = edited;
        box->detail = detail;
        box->setTitle ("Preset: " + presets::getName (program) + (edited ? ", edited" : ""));
        box->repaint();
    }
}

juce::String PresetBar::getMenuHeader (params::Mode mode) const
{
    // What that mode costs in delay at the current sample rate ("LIVE · 21
    // ms"). Before the host has prepared the plugin there is no rate yet:
    // just the mode's name.
    const juce::String name = mode == params::Mode::studio ? "STUDIO" : "LIVE";
    const auto rate = processor.getSampleRate();
    const auto latency = processor.getLatencySamplesFor (mode);
    if (rate <= 0.0 || latency <= 0)
        return name;
    return name + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (juce::roundToInt (1000.0 * latency / rate)) + " ms";
}

std::unique_ptr<juce::Component> PresetBar::createPanel()
{
    return std::make_unique<Panel> (*this);
}

void PresetBar::showPanel()
{
    auto* parent = findOverlayParent (*box);
    if (parent == nullptr)
        return;

    auto panel = std::make_unique<Panel> (*this);

    // The box's outline lights while the panel is open. The panel is owned
    // by the call-out (deleted when it closes: a click outside, Escape, or a
    // choice), so it reports its own closing; the SafePointer guards
    // against this bar having gone first (the editor closed).
    box->panelOpen = true;
    box->repaint();
    panel->onClosed = [safe = juce::Component::SafePointer<PresetBar> (this)]
    {
        if (safe != nullptr)
        {
            safe->box->panelOpen = false;
            safe->box->repaint();
        }
    };

    juce::CallOutBox::launchAsynchronously (std::move (panel), parent->getLocalArea (box.get(), box->getLocalBounds()), parent);
}

void PresetBar::resized()
{
    auto area = getLocalBounds();
    previous->setBounds (area.removeFromLeft (28));
    next->setBounds (area.removeFromRight (28));
    box->setBounds (area.reduced (4, 0));
}

} // namespace lowchug
