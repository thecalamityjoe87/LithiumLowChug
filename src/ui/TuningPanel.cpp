#include "TuningPanel.h"

namespace lowchug
{

namespace
{
constexpr int padding = theme::space::lg;
constexpr int columns = 5;
constexpr int chipHeight = 52;
constexpr const char* sourceLabels[] = { "Standard", "Drop D", "Custom" };

juce::String formatSemitones (int st)
{
    return (st > 0 ? "+" : "") + juce::String (st) + " st";
}
} // namespace

TuningPanel::TuningPanel (TuningController& c)
    : controller (c)
{
    for (int i = 0; i < 3; ++i)
    {
        auto& b = sourceButtons[(size_t) i];
        b.setButtonText (sourceLabels[i]);
        b.getProperties().set ("segment", true);
        b.setTitle (juce::String ("Playing in ") + sourceLabels[i]);
        b.onClick = [this, i] { controller.pickSource (static_cast<tunings::Source> (i)); };
        addAndMakeVisible (b);
    }
    sourceButtons[2].setTooltip ("Your guitar is in some other tuning: set the shift with the Shift knob.");

    for (int i = 0; i < (int) tunings::targets.size(); ++i)
    {
        const auto& t = tunings::targets[(size_t) i];
        const auto name = juce::String (std::string (t.name));

        // "Eb Standard" -> "Eb", "Drop C" -> "C": the card says which family.
        auto big = name.replace (" Standard", "").replace ("Drop ", "");
        auto small = formatSemitones (t.offset);
        if (i == tunings::octaveDownIndex) { big = "Octave"; small = "-12 st"; }
        if (i == tunings::customTargetIndex) { big = "Custom"; small = "by knob"; }

        auto* chip = chips.add (new TuningChip (name, big, small));
        chip->setTooltip (i == tunings::customTargetIndex ? "Set the shift yourself with the Shift knob" : "Hear " + name);
        chip->onClick = [this, i] { controller.pickTarget (i); };
        addChildComponent (chip); // refresh() decides which are visible
    }

    refresh();
}

void TuningPanel::refresh()
{
    const auto source = controller.getSource();
    for (int i = 0; i < 3; ++i)
        sourceButtons[(size_t) i].setToggleState (static_cast<int> (source) == i, juce::dontSendNotification);

    const auto lit = controller.getDisplayedTarget();
    for (int i = 0; i < chips.size(); ++i)
        chips[i]->setToggleState (i == lit, juce::dontSendNotification);

    resized(); // which chips show depends on the source
    repaint();
}

juce::Rectangle<int> TuningPanel::getSlotBounds() const
{
    return { padding, padding + 21, getWidth() - 2 * padding, 36 };
}

juce::Rectangle<int> TuningPanel::getChipArea() const
{
    return { padding, padding + 92, getWidth() - 2 * padding, 2 * chipHeight + theme::space::sm };
}

void TuningPanel::resized()
{
    auto slot = getSlotBounds().reduced (3);
    const auto segmentWidth = (slot.getWidth() - 2 * theme::space::xs) / 3;
    for (auto& b : sourceButtons)
    {
        b.setBounds (slot.removeFromLeft (segmentWidth));
        slot.removeFromLeft (theme::space::xs);
    }

    // The reachable targets, in list order, flowed into a 5-column grid.
    const auto source = controller.getSource();
    const auto area = getChipArea();
    const auto gap = theme::space::sm;
    const auto chipWidth = (area.getWidth() - (columns - 1) * gap) / columns;
    int slotIndex = 0;

    for (int i = 0; i < chips.size(); ++i)
    {
        // Playing in Custom: there is no named tuning to move from, so only
        // Custom is offered.
        const bool show = source == tunings::Source::custom ? i == tunings::customTargetIndex
                                                            : tunings::isReachable (source, i);
        chips[i]->setVisible (show);
        if (! show)
            continue;

        const auto col = slotIndex % columns, row = slotIndex / columns;
        chips[i]->setBounds (area.getX() + col * (chipWidth + gap), area.getY() + row * (chipHeight + gap), chipWidth, chipHeight);
        ++slotIndex;
    }
}

void TuningPanel::paint (juce::Graphics& g)
{
    using namespace theme;

    drawCard (g, getLocalBounds().toFloat(), colors::panel);

    drawCaption (g, "PLAYING IN", { (float) padding, (float) padding, 200.0f, 13.0f });
    drawSegmentSlot (g, getSlotBounds().toFloat());

    const auto hearRow = juce::Rectangle<float> ((float) padding, (float) padding + 71.0f, (float) getWidth() - 2.0f * padding, 13.0f);
    drawCaption (g, "HEAR", hearRow);

    const auto source = controller.getSource();
    g.setColour (colors::textMuted);
    g.setFont (body (type::small));
    g.drawText (source == tunings::Source::standard ? "Standard tunings"
                : source == tunings::Source::dropD  ? "Drop tunings"
                                                    : "Manual shift",
                hearRow.expanded (0.0f, 2.0f), juce::Justification::centredRight, false);

    if (source == tunings::Source::custom)
    {
        // Next to the lone Custom chip.
        const auto chip = chips[tunings::customTargetIndex]->getBounds();
        const auto text = juce::Rectangle<int> (chip.getRight() + space::md, chip.getY() - 4,
                                                getWidth() - padding - chip.getRight() - space::md, chipHeight + 8);
        g.setFont (body (13.0f));
        g.drawFittedText ("Your guitar is in a tuning of its own, so LowChug can't name a target. "
                          "Set the amount with the Shift knob.",
                          text, juce::Justification::centredLeft, 3, 1.0f);
    }
}

} // namespace lowchug
