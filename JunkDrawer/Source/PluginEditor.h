#pragma once
#include "PluginProcessor.h"
#include "Ui.h"

class JunkDrawerEditor : public juce::AudioProcessorEditor, private juce::ChangeListener
{
public:
    explicit JunkDrawerEditor (JunkDrawerProcessor&);
    ~JunkDrawerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    enum Page { Drawer, Lfos, Master, Matrix, NumPages };
    void showPage (int page);
    int  currentPage() const noexcept { return page; }

    static constexpr int kW = 1280, kH = 900;

private:
    struct Header;
    struct DrawerPage;
    struct LfoPage;
    struct MasterPage;
    struct MatrixPage;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { modCtx.changed.sendChangeMessage(); }
    void buildModContext();

    JunkDrawerProcessor& proc;
    ui::LookAndFeel lnf;
    ui::ModContext modCtx;                                  // before the pages, so it outlives every knob
    std::unique_ptr<Header> header;
    std::unique_ptr<DrawerPage> drawerPage;
    std::unique_ptr<LfoPage> lfoPage;
    std::unique_ptr<MasterPage> masterPage;
    std::unique_ptr<MatrixPage> matrixPage;
    juce::TooltipWindow tooltip { this, 600 };
    int page = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JunkDrawerEditor)
};
