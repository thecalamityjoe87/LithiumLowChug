#include "PresetBar.h"

#include "Theme.h"
#include "plugin/Presets.h"

namespace lowchug
{

using namespace theme;

//==============================================================================
/** A small square button with a chevron, for previous / next. */
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

/** The box itself: the preset's name, LIVE or STUDIO, and a chevron. */
class PresetBar::Box final : public juce::Button
{
public:
    Box() : juce::Button ("Preset")
    {
        setTooltip ("Factory presets");
    }

    juce::String name;
    bool studio = false;
    bool menuOpen = false;

    void paintButton (juce::Graphics& g, bool isMouseOver, bool) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (isMouseOver ? colors::raisedHigh : colors::raised);
        g.fillRoundedRectangle (bounds, cornerRadius - 1.0f);
        g.setColour (menuOpen ? colors::accent : (isMouseOver ? colors::borderStrong : colors::border));
        g.drawRoundedRectangle (bounds, cornerRadius - 1.0f, 1.0f);

        auto inside = bounds.reduced (12.0f, 0.0f);

        // The chevron, then the mode tag, from the right.
        const auto chevronArea = inside.removeFromRight (10.0f);
        const auto c = chevronArea.getCentre();
        juce::Path chevron;
        chevron.startNewSubPath (c.x - 4.0f, c.y - 2.0f);
        chevron.lineTo (c.x, c.y + 2.0f);
        chevron.lineTo (c.x + 4.0f, c.y - 2.0f);
        g.setColour (colors::textMuted);
        g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        inside.removeFromRight (8.0f);
        const auto tag = studio ? "STUDIO" : "LIVE";
        const auto tagFont = label (type::caption, Weight::semibold);
        const auto tagWidth = getTrackedTextWidth (tag, tagFont, 0.1f);
        drawTrackedText (g, tag, tagFont, 0.1f, inside.removeFromRight (tagWidth), juce::Justification::centredRight);

        inside.removeFromRight (8.0f);
        g.setColour (colors::text);
        g.setFont (body (13.5f, Weight::medium));
        g.drawText (name, inside, juce::Justification::centredLeft, true);
    }
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
    box->onClick = [this] { showMenu(); };
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
    const auto name = juce::String (presets::tuningOf (program).name) + (edited ? " *" : "");
    const auto studio = presets::isStudio (program);

    if (name != shownName || studio != shownStudio)
    {
        shownName = name;
        shownStudio = studio;
        box->name = name;
        box->studio = studio;
        box->setTitle ("Preset: " + presets::getName (program) + (edited ? ", edited" : ""));
        box->repaint();
    }
}

void PresetBar::showMenu()
{
    // Two columns: Live on the left, Studio on the right, the same tunings
    // side by side. Item IDs are program + 1 (0 means "nothing chosen").
    const auto current = processor.getCurrentProgram();
    const auto edited = ! presets::matches (processor.getState(), current);
    const auto perColumn = (int) presets::factoryTunings.size();

    juce::PopupMenu menu;
    menu.addSectionHeader ("LIVE");
    for (int i = 0; i < perColumn; ++i)
        menu.addItem (i + 1, presets::tuningOf (i).name, true, i == current && ! edited);
    menu.addColumnBreak();
    menu.addSectionHeader ("STUDIO");
    for (int i = perColumn; i < presets::numPresets; ++i)
        menu.addItem (i + 1, presets::tuningOf (i).name, true, i == current && ! edited);

    box->menuOpen = true;
    box->repaint();

    // showMenuAsync returns at once and calls back later, on the message
    // thread. By then the editor may have been closed, so the callback holds
    // a SafePointer (which becomes null if this component is deleted).
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (box.get()).withMinimumWidth (box->getWidth()),
                        [safe = juce::Component::SafePointer<PresetBar> (this)] (int result)
                        {
                            if (safe == nullptr)
                                return;
                            safe->box->menuOpen = false;
                            safe->box->repaint();
                            if (result > 0)
                            {
                                safe->processor.setCurrentProgram (result - 1);
                                safe->refresh();
                            }
                        });
}

void PresetBar::resized()
{
    auto area = getLocalBounds();
    previous->setBounds (area.removeFromLeft (28));
    next->setBounds (area.removeFromRight (28));
    box->setBounds (area.reduced (4, 0));
}

} // namespace lowchug
