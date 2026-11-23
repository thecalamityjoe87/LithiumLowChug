#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

// What the Linux installer installs, where, and how it removes it again.
// No UI here: InstallerMain.cpp drives it from a window, or from the command
// line (--install / --remove), and tests/InstallerTests.cpp drives it
// directly with a scratch home folder.
//
// Everything goes into the user's home folder, so no password is needed:
//   VST3            ~/.vst3/Lithium LowChug.vst3
//   LV2             ~/.lv2/Lithium LowChug.lv2
//   standalone app  ~/.local/lib/lithium-lowchug/Lithium LowChug, a menu
//                   entry, icons, and ~/.local/bin/lithium-lowchug (a link to it)
// (The menu entry and icons go in ~/.local/share, or $XDG_DATA_HOME when
// that's set.) The 18 factory presets are built into the plugin, so there
// is no presets folder to install.
//
// Every path it creates is listed in ~/.local/lib/lithium-lowchug/install.json,
// and Remove deletes exactly those, so nothing else is ever touched.

namespace lowchug::installer
{

struct Part
{
    juce::String id, name, description;
    juce::String where;            // shown to the user, e.g. "~/.vst3"
    bool selectedByDefault = true;
};

/** The choices offered, in the order shown. */
const std::vector<Part>& components();

/** The ids of the parts ticked by default (all of them). */
juce::StringArray defaultComponents();

/** Where things go. The installer uses forCurrentUser(); the tests use
    scratch folders, so they never touch the real home folder. */
struct Locations
{
    juce::File home;       // the user's home folder
    juce::File dataHome;   // $XDG_DATA_HOME, or ~/.local/share

    /** $HOME and $XDG_DATA_HOME (a relative XDG_DATA_HOME is ignored, as the
        XDG spec says). */
    static Locations forCurrentUser();

    /** Scratch locations under `home`: dataHome is home/.local/share. */
    static Locations under (const juce::File& home);

    juce::File programFolder() const;   // ~/.local/lib/lithium-lowchug
    juce::File manifestFile() const;    // install.json in the program folder
    juce::File binFolder() const;       // ~/.local/bin
};

/** What's installed now, read from the manifest. */
struct Existing
{
    bool installed = false;
    juce::String version;
    juce::StringArray components;
};

class Installation
{
public:
    /** `payload` is the folder of files to install (next to the installer:
        plugins/, standalone/, icons/, and the license files). `version` is
        the version being installed. */
    Installation (juce::File payload, juce::String version, Locations = Locations::forCurrentUser());

    /** Checks the payload holds everything; returns an error message, or
        an empty string. */
    juce::String checkPayload() const;

    Existing existing() const;

    /** Status lines and progress (0 to 1) as the work goes. Called on the
        thread that runs install() or remove(). */
    using Progress = std::function<void (const juce::String& status, float progress)>;

    /** Removes any earlier install, then installs `componentIds` (ids from
        components()). Runs on whatever thread calls it (the window runs it
        on a background thread so the UI stays live). Returns an error
        message, or an empty string. */
    juce::String install (const juce::StringArray& componentIds, const Progress&);

    /** Removes everything the manifest lists. Returns an error message, or
        an empty string. */
    juce::String remove (const Progress&);

    /** The installed standalone app, if it is installed. */
    juce::File standaloneApp() const;

    const Locations& locations() const { return where; }

private:
    bool safeToDelete (const juce::File&) const;
    void refreshDesktop() const;

    juce::File payload;
    juce::String version;
    Locations where;
};

} // namespace lowchug::installer
