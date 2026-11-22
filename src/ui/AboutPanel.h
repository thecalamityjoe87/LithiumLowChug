#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lowchug
{

/** A card shown over the whole UI, dimming what's behind it. Close it with
    its button, Escape, or a click outside the card. (As in Lithium Drums.)

    It is drawn inside the editor rather than as a separate window: windows
    a plugin opens itself behave differently in every host, while a
    component inside the editor behaves the same everywhere. */
class OverlayPanel : public juce::Component
{
public:
    OverlayPanel (int cardWidth, int cardHeight);

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

protected:
    juce::Rectangle<int> getCardBounds() const;
    virtual void layoutContent() {}

private:
    int cardWidth, cardHeight;
    juce::TextButton closeButton { "Close" };
};

/** About Lithium LowChug: the wordmark, version, who made it, a link to
    the project, what it is and what it's built with. */
class AboutPanel final : public OverlayPanel
{
public:
    AboutPanel();

    void paint (juce::Graphics&) override;

private:
    void layoutContent() override;

    juce::TextButton websiteButton { "GitHub" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutPanel)
};

} // namespace lowchug
