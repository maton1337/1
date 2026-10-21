#include "PluginWindows.h"
#include "LookAndFeel.h"

namespace bridgerack
{

class PluginWindowManager::Window : public juce::DocumentWindow
{
public:
    Window (juce::AudioProcessor& p, const juce::String& title, PluginWindowManager& m)
        : DocumentWindow (title, Colours::panel, DocumentWindow::closeButton),
          processor (p), manager (m)
    {
        juce::AudioProcessorEditor* editor = p.hasEditor() ? p.createEditorAndMakeActive() : nullptr;

        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (p);

        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        // Can't delete ourselves from inside our own callback.
        juce::MessageManager::callAsync ([&m = manager, p = &processor, safe = juce::Component::SafePointer<Window> (this)]
        {
            if (safe != nullptr)
                m.closeFor (p);
        });
    }

    juce::AudioProcessor& processor;
    PluginWindowManager& manager;
};

PluginWindowManager::PluginWindowManager() = default;

PluginWindowManager::~PluginWindowManager()
{
    closeAll();
}

void PluginWindowManager::show (juce::AudioProcessor& processor, const juce::String& title)
{
    for (auto* w : windows)
    {
        if (&w->processor == &processor)
        {
            w->toFront (true);
            return;
        }
    }

    windows.add (new Window (processor, title, *this));
}

void PluginWindowManager::closeFor (const juce::AudioProcessor* processor)
{
    for (int i = windows.size(); --i >= 0;)
        if (&windows[i]->processor == processor)
            windows.remove (i);
}

void PluginWindowManager::closeAll()
{
    windows.clear();
}

} // namespace bridgerack
