// Tests for the Linux installer's logic (tools/installer/Installation.cpp):
// which parts there are, where each one goes, and that install, update and
// remove touch exactly the files they should.
//
// Everything happens in a scratch "home folder" under the system's temp
// folder, with a fake payload (small stand-in files shaped like the real
// package), so the tests never touch the real home folder and run in well
// under a second.

#include "../tools/installer/Installation.h"

#include <juce_core/juce_core.h>

#include <filesystem>

namespace lowchug::installer
{

class InstallerTests final : public juce::UnitTest
{
public:
    InstallerTests() : UnitTest ("Installer", "LowChug") {}

    void runTest() override
    {
        // expect() needs a test to report to, so this comes first.
        beginTest ("A scratch home folder and a fake payload");

        // A fresh scratch folder for this run, deleted again at the end.
        const auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getNonexistentChildFile ("lowchug-installer-tests", {}, false);
        scratch.createDirectory();
        const auto payload = scratch.getChildFile ("payload");
        const auto home = scratch.getChildFile ("home");
        makePayload (payload);
        home.createDirectory();

        const auto where = Locations::under (home);
        const auto vst3 = home.getChildFile (".vst3/Lithium LowChug.vst3");
        const auto lv2 = home.getChildFile (".lv2/Lithium LowChug.lv2");
        const auto program = home.getChildFile (".local/lib/lithium-lowchug");
        const auto app = program.getChildFile ("Lithium LowChug");
        const auto desktop = home.getChildFile (".local/share/applications/io.github.thecalamityjoe87.lithiumlowchug.desktop");
        const auto icon48 = home.getChildFile (".local/share/icons/hicolor/48x48/apps/io.github.thecalamityjoe87.lithiumlowchug.png");
        const auto command = home.getChildFile (".local/bin/lithium-lowchug");
        const auto quiet = [] (const juce::String&, float) {};

        beginTest ("The parts on offer are the standalone app, VST3 and LV2, all ticked by default");
        {
            juce::StringArray ids;
            for (const auto& c : components())
                ids.add (c.id);

            expectEquals (ids.joinIntoString (","), juce::String ("standalone,vst3,lv2"));
            expectEquals (defaultComponents().joinIntoString (","), juce::String ("standalone,vst3,lv2"));
        }

        beginTest ("Locations are in the home folder");
        expect (where.programFolder() == program);
        expect (where.manifestFile() == program.getChildFile ("install.json"));
        expect (where.binFolder() == home.getChildFile (".local/bin"));
        expect (where.dataHome == home.getChildFile (".local/share"));

        beginTest ("An incomplete payload is refused, and nothing is installed");
        {
            Installation incomplete (scratch.getChildFile ("no-such-payload"), "0.5.0", where);
            expect (incomplete.checkPayload().contains ("incomplete"));
            expect (incomplete.install (defaultComponents(), quiet).isNotEmpty());
            expect (! where.manifestFile().exists());
            expect (! incomplete.existing().installed);
        }

        Installation installation (payload, "0.5.0", where);
        expectEquals (installation.checkPayload(), juce::String());

        beginTest ("Unknown or no parts are refused");
        expect (installation.install ({ "vst3", "clap" }, quiet).contains ("Unknown part \"clap\""));
        expect (installation.install ({}, quiet).isNotEmpty());
        expect (! vst3.exists());
        expect (! where.manifestFile().exists());

        // Files that belong to the user, next to ours: never touched.
        const auto otherPlugin = home.getChildFile (".vst3/Some Other Plugin.vst3/Contents/x86_64-linux/other.so");
        const auto settings = home.getChildFile (".config/Lithium LowChug.settings");
        otherPlugin.getParentDirectory().createDirectory();
        expect (otherPlugin.replaceWithText ("not ours"));
        settings.getParentDirectory().createDirectory();
        expect (settings.replaceWithText ("<settings/>"));

        beginTest ("Install puts every part in its place");
        {
            float lastProgress = 0.0f;
            const auto error = installation.install (defaultComponents(), [&] (const juce::String&, float p) { lastProgress = p; });
            expectEquals (error, juce::String());
            expectEquals (lastProgress, 1.0f);

            expect (vst3.getChildFile ("Contents/x86_64-linux/Lithium LowChug.so").existsAsFile());
            expect (lv2.getChildFile ("manifest.ttl").existsAsFile());
            expect (app.existsAsFile());
            expect (isExecutable (app));
            expect (program.getChildFile ("LICENSE").existsAsFile());
            expect (program.getChildFile ("THIRD-PARTY-NOTICES.txt").existsAsFile());
            expect (icon48.existsAsFile());
            expect (command.isSymbolicLink());
            expect (command.getLinkedTarget() == app);

            const auto entry = desktop.loadFileAsString();
            expect (entry.contains ("Exec=\"" + app.getFullPathName() + "\"\n"));
            expect (entry.contains ("Icon=io.github.thecalamityjoe87.lithiumlowchug\n"));
            expect (entry.contains ("StartupWMClass=Lithium LowChug\n"));
        }

        beginTest ("The manifest records the version and the parts");
        {
            const auto existing = installation.existing();
            expect (existing.installed);
            expectEquals (existing.version, juce::String ("0.5.0"));
            expectEquals (existing.components.joinIntoString (","), juce::String ("standalone,vst3,lv2"));
            expect (installation.standaloneApp() == app);
        }

        beginTest ("An update installs only what's ticked now, and clears out the rest");
        {
            Installation update (payload, "0.5.1", where);
            expectEquals (update.install ({ "vst3" }, quiet), juce::String());

            expect (vst3.isDirectory());
            expect (! lv2.exists());
            expect (! app.exists());
            expect (! desktop.exists());
            expect (! icon48.exists());
            expect (! command.exists() && ! command.isSymbolicLink());
            expect (update.standaloneApp() == juce::File());

            const auto existing = update.existing();
            expectEquals (existing.version, juce::String ("0.5.1"));
            expectEquals (existing.components.joinIntoString (","), juce::String ("vst3"));
        }

        beginTest ("Remove deletes everything it installed, and nothing else");
        {
            expectEquals (installation.install (defaultComponents(), quiet), juce::String());
            expectEquals (installation.remove (quiet), juce::String());

            for (const auto& f : { vst3, lv2, app, desktop, icon48, program })
                expect (! f.exists(), f.getFullPathName() + " is still there");

            expect (! command.isSymbolicLink());
            expect (! installation.existing().installed);
            expect (otherPlugin.existsAsFile());
            expect (settings.existsAsFile());
        }

        beginTest ("Remove skips paths in a damaged manifest that aren't ours");
        {
            const auto outside = scratch.getChildFile ("outside.txt");          // outside the home folder
            const auto notOurs = home.getChildFile ("Documents/song.txt");      // in it, but no "lithium" in the path
            expect (outside.replaceWithText ("keep"));
            notOurs.getParentDirectory().createDirectory();
            expect (notOurs.replaceWithText ("keep"));

            program.createDirectory();
            juce::var paths (juce::StringArray { outside.getFullPathName(), notOurs.getFullPathName(), "relative/path" });
            auto manifest = std::make_unique<juce::DynamicObject>();
            manifest->setProperty ("version", "0.5.0");
            manifest->setProperty ("paths", paths);
            expect (where.manifestFile().replaceWithText (juce::JSON::toString (juce::var (manifest.release()))));

            expectEquals (installation.remove (quiet), juce::String());
            expect (outside.existsAsFile());
            expect (notOurs.existsAsFile());
            expect (! where.manifestFile().exists());
        }

        beginTest ("Remove with nothing installed does nothing");
        expectEquals (installation.remove (quiet), juce::String());

        scratch.deleteRecursively();
    }

private:
    // juce::File can set the execute bit but not read it; std::filesystem can.
    static bool isExecutable (const juce::File& f)
    {
        const auto perms = std::filesystem::status (f.getFullPathName().toStdString()).permissions();
        return (perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
    }

    // Stand-ins for what scripts/package.sh puts in payload/.
    void makePayload (const juce::File& payload)
    {
        const auto write = [this, &payload] (const char* path, const char* text)
        {
            const auto f = payload.getChildFile (path);
            f.getParentDirectory().createDirectory();
            expect (f.replaceWithText (text));
        };

        write ("plugins/Lithium LowChug.vst3/Contents/x86_64-linux/Lithium LowChug.so", "vst3");
        write ("plugins/Lithium LowChug.lv2/manifest.ttl", "lv2");
        write ("plugins/Lithium LowChug.lv2/Lithium LowChug.so", "lv2");
        write ("standalone/Lithium LowChug", "app");
        write ("icons/icon-16.png", "png");
        write ("icons/icon-48.png", "png");
        write ("icons/icon-256.png", "png");
        write ("LICENSE", "AGPLv3");
        write ("THIRD-PARTY-NOTICES.txt", "notices");
        write ("README.md", "readme");
    }
};

static InstallerTests installerTests;

} // namespace lowchug::installer

int main()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("LowChug");

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;
    return failures == 0 ? 0 : 1;
}
