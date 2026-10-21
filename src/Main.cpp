#include "ui/MainComponent.h"

namespace bridgerack
{

class BridgeRackApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = "BridgeRack";
        options.filenameSuffix      = "settings";
        options.folderName          = "BridgeRack";
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (options);

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        mainWindow = std::make_unique<MainWindow> (properties);
    }

    void shutdown() override
    {
        if (mainWindow != nullptr)
            mainWindow->saveAutosave();

        mainWindow.reset();
        properties.closeFiles();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (juce::ApplicationProperties& settings)
            : DocumentWindow ("BridgeRack", Colours::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            content = new MainComponent (settings);
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (900, 400, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void saveAutosave() { content->saveAutosave(); }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        MainComponent* content = nullptr;
    };

    BridgeLookAndFeel lookAndFeel;
    juce::ApplicationProperties properties;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace bridgerack

START_JUCE_APPLICATION (bridgerack::BridgeRackApplication)
