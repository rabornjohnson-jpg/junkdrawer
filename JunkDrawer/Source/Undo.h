#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// Undo and redo for every control.  It remembers the value of every parameter and, once a change has settled for half a second
// (so one drag is one step), keeps the picture from just before it.  Nothing runs unless something changed.
// Sounds that were loaded or recorded are not part of it; only the settings, regions and steps are.
class UndoHistory : private juce::Timer, private juce::AudioProcessorListener
{
public:
    explicit UndoHistory (juce::AudioProcessor& p, juce::StringArray ignore = {}) : proc (p)
    {
        const auto& ps = proc.getParameters();
        skip.assign ((size_t) ps.size(), false);
        for (int i = 0; i < ps.size(); ++i)
            if (auto* wid = dynamic_cast<juce::AudioProcessorParameterWithID*> (ps[i]))
                skip[(size_t) i] = ignore.contains (wid->paramID);
        base = capture();
        proc.addListener (this);
        startTimerHz (5);
    }
    ~UndoHistory() override { proc.removeListener (this); }

    bool canUndo() const { return ! undoStack.empty() || dirty.load(); }
    bool canRedo() const { return ! redoStack.empty() && ! dirty.load(); }

    void undo()
    {
        commit();
        if (undoStack.empty()) return;
        auto target = std::move (undoStack.back()); undoStack.pop_back();
        redoStack.push_back (base);
        apply (target);
    }
    void redo()
    {
        commit();
        if (redoStack.empty()) return;
        auto target = std::move (redoStack.back()); redoStack.pop_back();
        undoStack.push_back (base);
        apply (target);
    }
    /** After a whole new state was loaded (a project, a station): start the history over. */
    void reset() { undoStack.clear(); redoStack.clear(); base = capture(); dirty = false; }

    std::function<void()> onChange;                       // the editor refreshes its buttons

private:
    using Snap = std::vector<float>;
    Snap capture() const
    {
        const auto& ps = proc.getParameters();
        Snap s ((size_t) ps.size());
        for (int i = 0; i < ps.size(); ++i) s[(size_t) i] = ps[i]->getValue();
        return s;
    }
    void apply (const Snap& target)
    {
        restoring = true;
        const auto& ps = proc.getParameters();
        for (int i = 0; i < ps.size() && (size_t) i < target.size(); ++i)
        {
            if (skip[(size_t) i] || ps[i]->getValue() == target[(size_t) i]) continue;
            ps[i]->beginChangeGesture(); ps[i]->setValueNotifyingHost (target[(size_t) i]); ps[i]->endChangeGesture();
        }
        restoring = false;
        base = capture();
        dirty = false;
        if (auto* cb = dynamic_cast<juce::ChangeBroadcaster*> (&proc)) cb->sendChangeMessage();
        if (onChange) onChange();
    }
    void commit()
    {
        if (! dirty.exchange (false)) return;
        auto cur = capture();
        if (cur != base)
        {
            undoStack.push_back (std::move (base));
            if (undoStack.size() > 60) undoStack.erase (undoStack.begin());
            redoStack.clear();
            base = std::move (cur);
            if (onChange) onChange();
        }
    }

    void timerCallback() override { if (dirty.load() && juce::Time::getMillisecondCounter() - lastChange.load() >= 500) commit(); }
    void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override
    {
        if (restoring || (size_t) index >= skip.size() || skip[(size_t) index]) return;
        lastChange.store (juce::Time::getMillisecondCounter());
        if (! dirty.exchange (true) && onChange) juce::MessageManager::callAsync ([this, alive = alive] { if (alive.lock()) if (onChange) onChange(); });
    }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}

    juce::AudioProcessor& proc;
    std::vector<bool> skip;
    std::vector<Snap> undoStack, redoStack;
    Snap base;
    std::atomic<bool> dirty { false };
    std::atomic<juce::uint32> lastChange { 0 };
    bool restoring = false;
    std::shared_ptr<int> aliveTok = std::make_shared<int> (0);
    std::weak_ptr<int> alive = aliveTok;
};
