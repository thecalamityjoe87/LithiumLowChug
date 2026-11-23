#include "Installation.h"

namespace lowchug::installer
{

namespace
{
    // Used for the menu entry's and the icons' file names. Matches the
    // plugin's bundle ID (cmake/PluginIdentity.cmake).
    const juce::String appId = "io.github.thecalamityjoe87.lithiumlowchug";

    // The name JUCE gives the plugin bundles and the standalone app (the
    // PRODUCT_NAME in cmake/PluginIdentity.cmake).
    const juce::String productName = "Lithium LowChug";

    // The menu entry. Exec is quoted because the path has a space in it;
    // StartupWMClass is JUCE's X11 window class (the app name), which lets
    // the desktop match the open window to this entry and its icon.
    juce::String desktopEntry (const juce::File& app)
    {
        return "[Desktop Entry]\n"
               "Type=Application\n"
               "Name=Lithium LowChug\n"
               "GenericName=Guitar Detuner\n"
               "Comment=Play in standard and hear your guitar tuned lower (standalone; also installed as a plugin for your DAW)\n"
               "Exec=\"" + app.getFullPathName() + "\"\n"
               "Icon=" + appId + "\n"
               "Terminal=false\n"
               "Categories=AudioVideo;Audio;Music;\n"
               "Keywords=guitar;detune;pitch;shifter;drop tuning;\n"
               "StartupWMClass=" + productName + "\n";
    }

    bool isKnownComponent (const juce::String& id)
    {
        for (const auto& c : components())
            if (c.id == id)
                return true;

        return false;
    }
}

//==============================================================================
const std::vector<Part>& components()
{
    static const std::vector<Part> list {
        { "standalone", "Standalone app", "Runs on its own, no DAW needed. Adds it to your app menu.", "~/.local + app menu", true },
        { "vst3", "VST3 plugin", "For most Linux DAWs: Reaper, Bitwig, Ardour, Lithium Studio...", "~/.vst3", true },
        { "lv2", "LV2 plugin", "For Ardour, Carla, Qtractor and other LV2 hosts.", "~/.lv2", true },
    };
    return list;
}

juce::StringArray defaultComponents()
{
    juce::StringArray ids;

    for (const auto& c : components())
        if (c.selectedByDefault)
            ids.add (c.id);

    return ids;
}

//==============================================================================
Locations Locations::forCurrentUser()
{
    // On Linux, JUCE's userHomeDirectory is $HOME (or the password database
    // when HOME isn't set).
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto xdg = juce::SystemStats::getEnvironmentVariable ("XDG_DATA_HOME", {});
    return { home, xdg.isNotEmpty() && juce::File::isAbsolutePath (xdg) ? juce::File (xdg) : home.getChildFile (".local/share") };
}

Locations Locations::under (const juce::File& home)
{
    return { home, home.getChildFile (".local/share") };
}

// The program's own files: the standalone app, the license, and the
// manifest. In ~/.local/lib, like /usr/lib/<app> for a system install
// (Lithium Drums uses the same layout).
juce::File Locations::programFolder() const { return home.getChildFile (".local/lib/lithium-lowchug"); }
juce::File Locations::manifestFile() const  { return programFolder().getChildFile ("install.json"); }
juce::File Locations::binFolder() const     { return home.getChildFile (".local/bin"); }

//==============================================================================
Installation::Installation (juce::File p, juce::String v, Locations l)
    : payload (std::move (p)), version (std::move (v)), where (std::move (l))
{
}

juce::File Installation::standaloneApp() const
{
    const auto app = where.programFolder().getChildFile (productName);
    return app.existsAsFile() ? app : juce::File();
}

// Remove only deletes paths that look like ours: inside the home or data
// folder, and with "lithium" somewhere in them. The manifest is a file the
// user could edit; this keeps a damaged one from deleting anything else.
bool Installation::safeToDelete (const juce::File& f) const
{
    return (f.isAChildOf (where.home) || f.isAChildOf (where.dataHome)) && f.getFullPathName().containsIgnoreCase ("lithium");
}

// Nudges the desktop to re-read menu entries and icons. Desktops watch these
// folders' modification times, so touching them is enough. (Not
// gtk-update-icon-cache: a cache file left in the user's icon folder goes
// stale and hides icons other apps add later.)
void Installation::refreshDesktop() const
{
    for (const auto* folder : { "applications", "icons/hicolor" })
        if (const auto f = where.dataHome.getChildFile (folder); f.isDirectory())
            f.setLastModificationTime (juce::Time::getCurrentTime());
}

juce::String Installation::checkPayload() const
{
    for (const auto* path : { "plugins/Lithium LowChug.vst3", "plugins/Lithium LowChug.lv2", "standalone/Lithium LowChug", "icons", "LICENSE" })
        if (! payload.getChildFile (path).exists())
            return "The installer's files are incomplete (no " + juce::String (path) + " in " + payload.getFullPathName()
                   + "). Unpack the whole download again and run the installer from there.";

    return {};
}

Existing Installation::existing() const
{
    Existing result;

    // JSON::parse (File) gives a void var when the file is missing or
    // isn't valid JSON; getDynamicObject() is then nullptr.
    const auto json = juce::JSON::parse (where.manifestFile());

    if (auto* object = json.getDynamicObject())
    {
        result.installed = true;
        result.version = object->getProperty ("version").toString();

        if (auto* list = object->getProperty ("components").getArray())
            for (const auto& c : *list)
                result.components.add (c.toString());
    }

    return result;
}

juce::String Installation::remove (const Progress& progress)
{
    const auto json = juce::JSON::parse (where.manifestFile());
    juce::StringArray paths;

    if (auto* object = json.getDynamicObject())
        if (auto* list = object->getProperty ("paths").getArray())
            for (const auto& p : *list)
                paths.add (p.toString());

    juce::StringArray failed;

    for (int i = 0; i < paths.size(); ++i)
    {
        // A relative path in a damaged manifest would make juce::File
        // assert; skip it like any other path that isn't ours.
        if (! juce::File::isAbsolutePath (paths[i]))
            continue;

        const juce::File f (paths[i]);
        progress ("Removing " + f.getFileName(), (float) i / (float) std::max (1, paths.size()));

        if (! safeToDelete (f))
            continue;

        // A symlink is deleted as a link, never followed.
        const auto ok = f.isSymbolicLink() || ! f.isDirectory() ? f.deleteFile() : f.deleteRecursively();

        if (! ok && f.exists())
            failed.add (f.getFullPathName());
    }

    where.manifestFile().deleteFile();

    // The program folder, if nothing else is left in it.
    if (const auto folder = where.programFolder();
        folder.isDirectory() && folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        folder.deleteFile();

    refreshDesktop();
    progress ("Removed", 1.0f);
    return failed.isEmpty() ? juce::String() : "Couldn't remove:\n" + failed.joinIntoString ("\n");
}

juce::String Installation::install (const juce::StringArray& ids, const Progress& progress)
{
    if (ids.isEmpty())
        return "Nothing to install: pick at least one of " + defaultComponents().joinIntoString (", ") + ".";

    for (const auto& id : ids)
        if (! isKnownComponent (id))
            return "Unknown part \"" + id + "\": pick from " + defaultComponents().joinIntoString (", ") + ".";

    if (const auto error = checkPayload(); error.isNotEmpty())
        return error;

    // A fresh start: an update shouldn't leave behind a format the user has
    // now unticked, or files an older version had and this one doesn't.
    if (where.manifestFile().existsAsFile())
        if (const auto error = remove ([&] (const juce::String& s, float p) { progress (s, p * 0.1f); }); error.isNotEmpty())
            return error;

    // Each step: what to copy from the payload, and where to.
    struct Step { juce::String status; juce::File from, to; bool executable = false; };
    std::vector<Step> steps;
    const auto& home = where.home;
    const auto program = where.programFolder();

    if (ids.contains ("vst3"))
        steps.push_back ({ "Copying the VST3 plugin", payload.getChildFile ("plugins/Lithium LowChug.vst3"), home.getChildFile (".vst3/Lithium LowChug.vst3") });

    if (ids.contains ("lv2"))
        steps.push_back ({ "Copying the LV2 plugin", payload.getChildFile ("plugins/Lithium LowChug.lv2"), home.getChildFile (".lv2/Lithium LowChug.lv2") });

    if (ids.contains ("standalone"))
    {
        steps.push_back ({ "Copying the standalone app", payload.getChildFile ("standalone/Lithium LowChug"), program.getChildFile (productName), true });

        // icon-48.png goes to icons/hicolor/48x48/apps/<appId>.png, and so on.
        for (const auto& icon : payload.getChildFile ("icons").findChildFiles (juce::File::findFiles, false, "icon-*.png"))
        {
            const auto size = icon.getFileNameWithoutExtension().fromFirstOccurrenceOf ("-", false, false);
            steps.push_back ({ "Adding the icons", icon, where.dataHome.getChildFile ("icons/hicolor/" + size + "x" + size + "/apps/" + appId + ".png") });
        }
    }

    for (const auto* doc : { "LICENSE", "THIRD-PARTY-NOTICES.txt", "README.md" })
        if (payload.getChildFile (doc).existsAsFile())
            steps.push_back ({ "Copying the license", payload.getChildFile (doc), program.getChildFile (doc) });

    juce::StringArray installed;

    for (size_t i = 0; i < steps.size(); ++i)
    {
        const auto& step = steps[i];
        progress (step.status + "...", 0.1f + 0.85f * (float) i / (float) steps.size());

        if (! step.from.exists())
            return "Missing from the installer: " + step.from.getFullPathName();

        step.to.getParentDirectory().createDirectory();

        // Delete first, then write a new file: a DAW that has the old plugin
        // loaded keeps its (now unlinked) copy instead of having it
        // overwritten under it, which could crash it.
        step.to.isDirectory() ? step.to.deleteRecursively() : step.to.deleteFile();
        const auto ok = step.from.isDirectory() ? step.from.copyDirectoryTo (step.to) : step.from.copyFileTo (step.to);

        if (! ok)
            return "Couldn't write " + step.to.getFullPathName() + ". Is the disk full, or the folder read-only?";

        if (step.executable)
            step.to.setExecutePermission (true);

        installed.add (step.to.getFullPathName());
    }

    if (ids.contains ("standalone"))
    {
        progress ("Adding Lithium LowChug to the app menu...", 0.96f);
        const auto app = program.getChildFile (productName);
        const auto entry = where.dataHome.getChildFile ("applications/" + appId + ".desktop");
        entry.getParentDirectory().createDirectory();

        // "\n": replaceWithText writes Windows line endings (\r\n) by
        // default, and a menu entry must have Unix ones.
        if (! entry.replaceWithText (desktopEntry (app), false, false, "\n"))
            return "Couldn't write " + entry.getFullPathName();

        installed.add (entry.getFullPathName());

        // A command for the terminal too. ~/.local/bin is on the PATH on
        // most distributions; if it isn't, the menu entry still works.
        const auto bin = where.binFolder();
        const auto link = bin.getChildFile ("lithium-lowchug");
        bin.createDirectory();

        if (app.createSymbolicLink (link, true))
            installed.add (link.getFullPathName());
    }

    // The manifest last, so a failed install doesn't claim to be one.
    // DynamicObject is JUCE's JSON object; juce::var takes ownership of it.
    auto manifest = std::make_unique<juce::DynamicObject>();
    manifest->setProperty ("version", version);
    manifest->setProperty ("components", juce::var (ids));
    manifest->setProperty ("paths", juce::var (installed));
    program.createDirectory();

    if (! where.manifestFile().replaceWithText (juce::JSON::toString (juce::var (manifest.release())), false, false, "\n"))
        return "Couldn't write " + where.manifestFile().getFullPathName();

    refreshDesktop();
    progress ("Installed", 1.0f);
    return {};
}

} // namespace lowchug::installer
