#include "PluginWindows.h"
#include "engine/Model.h"

namespace galpon
{

struct PluginWindows::Window : public juce::DocumentWindow
{
    Window (PluginWindows& o, juce::AudioProcessor& p, const juce::String& title)
        : juce::DocumentWindow (title, juce::Colour (0xff1e1e21), juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          owner (o), processor (p)
    {
        setUsingNativeTitleBar (true);
        juce::AudioProcessorEditor* editor = p.hasEditor() ? p.createEditorIfNeeded() : nullptr;
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (p);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override
    {
        // Deleting ourselves from inside a callback: defer it
        juce::MessageManager::callAsync ([&o = owner, ptr = &processor] { o.closeFor (*ptr); });
    }

    PluginWindows& owner;
    juce::AudioProcessor& processor;
};

PluginWindows::PluginWindows()
{
    ProcessorDeleter::hook() = [this] (juce::AudioProcessor& p) { closeFor (p); };
}

PluginWindows::~PluginWindows()
{
    ProcessorDeleter::hook() = nullptr;
    closeAll();
}

void PluginWindows::open (juce::AudioProcessor& p, const juce::String& title)
{
    for (auto& w : windows)
        if (&w->processor == &p)
        {
            w->setVisible (true);
            w->toFront (true);
            return;
        }
    windows.push_back (std::make_unique<Window> (*this, p, title));
}

void PluginWindows::closeFor (juce::AudioProcessor& p)
{
    windows.erase (std::remove_if (windows.begin(), windows.end(),
                                   [&p] (const std::unique_ptr<Window>& w) { return &w->processor == &p; }),
                   windows.end());
}

void PluginWindows::closeAll()
{
    windows.clear();
}

} // namespace galpon
