#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"

// Offline export, same selections and rules as DriftLooper's: the pattern is played through a second copy of the engine (the same
// processor code that runs live, so LFOs, effects and Rummage results are all in the file) and written as stereo files.
namespace jdx
{

constexpr double kMaxExportSeconds = 1800.0;            // 30 minutes of music per file (a ring-out tail comes on top)

enum class ExportMode : int { Master = 0, Stems };
enum class NormMode : int { Off = 0, Relative, Maximum };
enum class LengthKind : int { Passes = 0, Time, Bars };
enum class FileKind : int { Wav = 0, Aiff, Flac };
enum class EndingKind : int { Cut = 0, Fade, RingOut };

struct ExportOptions
{
    ExportMode mode = ExportMode::Master;
    NormMode   norm = NormMode::Relative;
    LengthKind lengthKind = LengthKind::Passes;
    int        passes = 4;
    double     timeSeconds = 120.0;
    int        bars = 16;
    FileKind   file = FileKind::Wav;
    int        bits = 24;
    double     rate = 96000.0;
    EndingKind ending = EndingKind::Cut;
    double     endingSeconds = 0.0;
    juce::File folder;
    juce::String baseName = "JunkDrawer";
    juce::int64 freeBytesOverride = -1;                  // tests only
};

inline const char* fileExtension (FileKind k) noexcept { return k == FileKind::Aiff ? ".aif" : (k == FileKind::Flac ? ".flac" : ".wav"); }
inline const char* fileKindName (FileKind k) noexcept { return k == FileKind::Aiff ? "AIFF" : (k == FileKind::Flac ? "FLAC" : "WAV"); }

inline double exportMainSeconds (const ExportOptions& o, double longestPassSeconds, double bpm, double beatsPerBar)
{
    double s;
    if (o.lengthKind == LengthKind::Time)      s = o.timeSeconds;
    else if (o.lengthKind == LengthKind::Bars) s = (double) juce::jmax (1, o.bars) * juce::jmax (1.0, beatsPerBar) * 60.0 / juce::jmax (20.0, bpm);
    else                                       s = longestPassSeconds * (double) juce::jmax (1, o.passes);
    return juce::jlimit (0.5, kMaxExportSeconds, s);
}
inline double exportTailSeconds (const ExportOptions& o) noexcept { return o.ending == EndingKind::RingOut ? juce::jlimit (0.0, 30.0, o.endingSeconds) : 0.0; }
inline double exportBytesPerFile (const ExportOptions& o, double totalSeconds) noexcept
{
    const double raw = totalSeconds * o.rate * 2.0 * (double) (o.bits / 8) + 128.0;
    return o.file == FileKind::Flac ? raw * 0.75 : raw;
}

inline juce::File appDataFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Fading Fractal Audio").getChildFile ("Junk Drawer");
}

// Where exports are written, kept per computer (not in the project).  mode: 0 = not asked yet, 1 = automatic (a temporary folder), 2 = a folder you chose.
struct ExportSettings
{
    int mode = 0;
    juce::File folder;

    static juce::File file() { return appDataFolder().getChildFile ("settings.xml"); }

    static void loadOptions (ExportOptions& o)
    {
        if (auto xml = juce::XmlDocument::parse (file()))
        {
            o.mode = xml->getIntAttribute ("expWhat", 0) == 1 ? ExportMode::Stems : ExportMode::Master;
            o.norm = (NormMode) juce::jlimit (0, 2, xml->getIntAttribute ("expNorm", 1));
            o.lengthKind = (LengthKind) juce::jlimit (0, 2, xml->getIntAttribute ("expLenKind", 0));
            o.passes = juce::jlimit (1, 16, xml->getIntAttribute ("expPasses", 4));
            o.timeSeconds = juce::jlimit (1.0, kMaxExportSeconds, xml->getDoubleAttribute ("expTime", 120.0));
            o.bars = juce::jlimit (1, 999, xml->getIntAttribute ("expBars", 16));
            o.file = (FileKind) juce::jlimit (0, 2, xml->getIntAttribute ("expFile", 0));
            o.bits = xml->getIntAttribute ("expBits", 24) == 16 ? 16 : 24;
            const int r = xml->getIntAttribute ("expRate", 96000);
            o.rate = r == 44100 ? 44100.0 : (r == 48000 ? 48000.0 : 96000.0);
            o.ending = (EndingKind) juce::jlimit (0, 2, xml->getIntAttribute ("expEnding", 0));
            o.endingSeconds = o.ending == EndingKind::Cut ? 0.0 : juce::jlimit (1.0, 30.0, xml->getDoubleAttribute ("expEndSec", 5.0));
        }
    }
    static void saveOptions (const ExportOptions& o)
    {
        std::unique_ptr<juce::XmlElement> xml = juce::XmlDocument::parse (file());
        if (xml == nullptr) xml = std::make_unique<juce::XmlElement> ("SETTINGS");
        xml->setAttribute ("expWhat", o.mode == ExportMode::Stems ? 1 : 0);
        xml->setAttribute ("expNorm", (int) o.norm);
        xml->setAttribute ("expLenKind", (int) o.lengthKind);
        xml->setAttribute ("expPasses", o.passes);
        xml->setAttribute ("expTime", o.timeSeconds);
        xml->setAttribute ("expBars", o.bars);
        xml->setAttribute ("expFile", (int) o.file);
        xml->setAttribute ("expBits", o.bits);
        xml->setAttribute ("expRate", (int) o.rate);
        xml->setAttribute ("expEnding", (int) o.ending);
        xml->setAttribute ("expEndSec", o.endingSeconds);
        file().getParentDirectory().createDirectory();
        xml->writeTo (file());
    }
    static ExportSettings load()
    {
        ExportSettings s;
        if (auto xml = juce::XmlDocument::parse (file()))
        {
            s.mode = xml->getIntAttribute ("exportMode", 0);
            const auto path = xml->getStringAttribute ("exportFolder");
            if (path.isNotEmpty()) s.folder = juce::File (path);
            if (s.mode < 0 || s.mode > 2 || (s.mode == 2 && s.folder == juce::File())) s.mode = 0;
        }
        return s;
    }
    void save() const
    {
        std::unique_ptr<juce::XmlElement> xml = juce::XmlDocument::parse (file());
        if (xml == nullptr) xml = std::make_unique<juce::XmlElement> ("SETTINGS");
        xml->setAttribute ("exportMode", mode);
        xml->setAttribute ("exportFolder", folder.getFullPathName());
        file().getParentDirectory().createDirectory();
        xml->writeTo (file());
    }
    juce::File root() const { return mode == 2 ? folder : juce::File::getSpecialLocation (juce::File::tempDirectory); }
};

struct ExportResult
{
    bool ok = false;
    juce::String error;
    juce::Array<juce::File> files;
    double seconds = 0.0;
    juce::int64 bytes = 0;
};

// Everything an export needs, taken on the message thread so the render never touches the live plug-in.
struct ExportSnapshot
{
    juce::ValueTree settings;                                              // every parameter, the LFO links and the pattern
    std::array<std::shared_ptr<const jd::TrackData>, jd::kTracks> tracks;  // at the session sample rate
    double bpm = 120.0, beatsPerBar = 4.0;
};

inline std::unique_ptr<ExportSnapshot> takeSnapshot (JunkDrawerProcessor& p)
{
    auto s = std::make_unique<ExportSnapshot>();
    p.flushPatterns();
    s->settings = p.state.copyState();
    for (int t = 0; t < jd::kTracks; ++t) s->tracks[(size_t) t] = p.getTrack (t);
    s->bpm = juce::jlimit (40.0, 240.0, p.lastBpm());
    s->beatsPerBar = juce::jlimit (1.0, 16.0, p.lastBeatsPerBar());
    return s;
}

class Exporter
{
public:
    static float getP (const ExportSnapshot& s, const juce::String& id, float def)
    {
        for (int i = 0; i < s.settings.getNumChildren(); ++i)
        {
            auto c = s.settings.getChild (i);
            if (c.getProperty ("id").toString() == id) return (float) (double) c.getProperty ("value", (double) def);
        }
        return def;
    }

    // Which tracks play in the export (a sound is loaded, and the track is not muted or silenced by another track's solo).
    static std::vector<int> audibleTracks (const ExportSnapshot& s)
    {
        bool anySolo = false;
        for (int t = 0; t < jd::kTracks; ++t) anySolo = anySolo || getP (s, jd::trackId (t, "solo"), 0.f) > 0.5f;
        std::vector<int> out;
        for (int t = 0; t < jd::kTracks; ++t)
        {
            const bool muted = getP (s, jd::trackId (t, "mute"), 0.f) > 0.5f || (anySolo && getP (s, jd::trackId (t, "solo"), 0.f) < 0.5f);
            if (s.tracks[(size_t) t] != nullptr && ! muted) out.push_back (t);
        }
        return out;
    }

    // One pass is one run of the longest lane on any audible track.
    static double passSeconds (const ExportSnapshot& s, const std::vector<int>& audible)
    {
        int steps = 1;
        for (int t : audible)
            for (int ln = 0; ln < jd::kLanes; ++ln)
                steps = juce::jmax (steps, juce::roundToInt (getP (s, jd::laneLenId (t, ln), (float) jd::kSteps)));
        double rate = getP (s, "m_rate", 1.f);
        if (getP (s, "m_ratemode", 0.f) > 0.5f) rate = jd::snapRate (rate);
        const double beats = jd::stepBeats (juce::roundToInt (getP (s, "seq_div", 2.f)), juce::roundToInt (getP (s, "seq_feel", 0.f)));
        double seconds = (double) steps * beats / juce::jmax (0.05, rate) * 60.0 / s.bpm;
        if (getP (s, "chain_on", 0.f) > 0.5f)                                   // with the chain on, one pass is one trip through the chain
        {
            int bars = 0;
            auto ch = s.settings.getChildWithName ("chain");
            for (int i = 0; i < ch.getNumChildren(); ++i) bars += juce::jmax (1, (int) ch.getChild (i).getProperty ("b", 1));
            if (bars > 0) seconds = (double) bars * s.beatsPerBar * 60.0 / s.bpm;
        }
        return seconds;
    }

    static ExportResult run (const ExportSnapshot& s, const ExportOptions& opt, std::atomic<float>& progress, std::atomic<bool>& cancel)
    {
        ExportResult res;
        const auto audible = audibleTracks (s);
        if (audible.empty()) { res.error = "Nothing to export - load a sound into a track first (muted tracks are skipped)."; return res; }

        const double rate = opt.rate;
        const double mainSeconds = exportMainSeconds (opt, passSeconds (s, audible), s.bpm, s.beatsPerBar);
        const double tailSeconds = exportTailSeconds (opt);
        const juce::int64 mainSamples = (juce::int64) std::ceil (mainSeconds * rate);
        const juce::int64 totalSamples = mainSamples + (juce::int64) std::ceil (tailSeconds * rate);
        res.seconds = (double) totalSamples / rate;

        if (! opt.folder.createDirectory().wasOk()) { res.error = "Could not create the export folder."; return res; }

        struct Out { juce::String name; int track = -1; double peak = 0.0; float gain = 1.f; juce::File file; };
        std::vector<Out> outs;
        if (opt.mode == ExportMode::Master) outs.push_back ({ opt.baseName + "_Master", -1 });
        else for (int t : audible) outs.push_back ({ opt.baseName + "_Track" + juce::String (t + 1), t });

        {
            const double need = exportBytesPerFile (opt, res.seconds) * (double) outs.size();
            const juce::int64 freeBytes = opt.freeBytesOverride >= 0 ? opt.freeBytesOverride : opt.folder.getBytesFreeOnVolume();
            if (freeBytes > 0 && (double) freeBytes < need + 100.0 * 1024.0 * 1024.0)
            {
                opt.folder.deleteRecursively();
                res.error = "Not enough free disk space: this export needs about " + juce::File::descriptionOfSizeInBytes ((juce::int64) need)
                          + " and the drive has " + juce::File::descriptionOfSizeInBytes (freeBytes) + " free.";
                return res;
            }
        }

        // sounds at the export rate, resampled once
        std::array<std::shared_ptr<const jd::TrackData>, jd::kTracks> audio;
        for (int t : audible) audio[(size_t) t] = jd::resampledCopy (*s.tracks[(size_t) t], rate);

        const juce::int64 fadeLen = opt.ending == EndingKind::Fade ? (juce::int64) (juce::jmin (opt.endingSeconds, mainSeconds * 0.5) * rate) : 0;
        const juce::int64 ringFade = opt.ending == EndingKind::RingOut ? (juce::int64) (juce::jmin (1.0, tailSeconds * 0.25) * rate) : 0;
        auto endGain = [&] (juce::int64 n) -> float
        {
            juce::int64 from = -1, len = 0;
            if (fadeLen > 0)       { from = mainSamples - fadeLen; len = fadeLen; }
            else if (ringFade > 0) { from = totalSamples - ringFade; len = ringFade; }
            if (from < 0 || n < from) return 1.f;
            const float x = juce::jlimit (0.f, 1.f, (float) (n - from) / (float) len);
            return 0.5f + 0.5f * std::cos (x * juce::MathConstants<float>::pi);
        };

        auto setParam = [] (JunkDrawerProcessor& p, const juce::String& id, float v)
        {
            if (auto* q = p.state.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (v));
        };

        // One rendering pass of one output.  writer == nullptr: only measure its peak.
        auto render = [&] (Out& out, juce::AudioFormatWriter* writer, float base, float span) -> bool
        {
            auto eng = std::make_unique<JunkDrawerProcessor>();
            eng->prepareToPlay (rate, 512);
            eng->importSettings (s.settings);
            setParam (*eng, "seq_bpm", (float) s.bpm);
            setParam (*eng, "seq_run", 1.f);
            for (int t = 0; t < jd::kTracks; ++t)
            {
                setParam (*eng, jd::trackId (t, "solo"), 0.f);
                setParam (*eng, jd::trackId (t, "mute"), (out.track >= 0 && t != out.track) ? 1.f : (getP (s, jd::trackId (t, "mute"), 0.f) > 0.5f ? 1.f : 0.f));
                if (audio[(size_t) t] != nullptr) eng->setTrackAudio (t, audio[(size_t) t]);
            }
            if (out.track >= 0) setParam (*eng, jd::trackId (out.track, "mute"), 0.f);
            else
                for (int t = 0; t < jd::kTracks; ++t)                           // solo state was folded into who is audible
                    if (std::find (audible.begin(), audible.end(), t) == audible.end()) setParam (*eng, jd::trackId (t, "mute"), 1.f);

            const int blockSize = 512;
            juce::AudioBuffer<float> buf (2, blockSize);
            juce::MidiBuffer midi;
            juce::Random dither (4242);
            juce::int64 pos = 0;
            bool stopped = false;
            while (pos < totalSamples)
            {
                if (cancel.load()) return false;
                if (! stopped && pos >= mainSamples) { setParam (*eng, "seq_run", 0.f); stopped = true; }      // ring-out: the pattern stops, the effects keep ringing
                int n = (int) juce::jmin ((juce::int64) blockSize, totalSamples - pos);
                if (pos < mainSamples) n = (int) juce::jmin ((juce::int64) n, mainSamples - pos);
                juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
                for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::clear (view.getWritePointer (c), n);
                eng->processBlock (view, midi);
                float* l = view.getWritePointer (0);
                float* r = view.getWritePointer (1);
                for (int i = 0; i < n; ++i)
                {
                    if (! std::isfinite (l[i])) l[i] = 0.f;
                    if (! std::isfinite (r[i])) r[i] = 0.f;
                    out.peak = juce::jmax (out.peak, (double) std::abs (l[i]), (double) std::abs (r[i]));
                }
                if (writer != nullptr)
                {
                    for (int i = 0; i < n; ++i)
                    {
                        const float gg = (opt.ending == EndingKind::Cut ? 1.f : endGain (pos + i)) * out.gain;
                        float a = l[i] * gg, b = r[i] * gg;
                        if (opt.bits == 16)
                        {
                            const float lsb = 1.f / 32768.f;
                            a += (dither.nextFloat() - dither.nextFloat()) * lsb;
                            b += (dither.nextFloat() - dither.nextFloat()) * lsb;
                        }
                        l[i] = juce::jlimit (-1.f, 1.f, a); r[i] = juce::jlimit (-1.f, 1.f, b);
                    }
                    const float* chans[2] = { l, r };
                    if (! writer->writeFromFloatArrays (chans, 2, n)) return false;
                }
                pos += n;
                progress.store (base + span * (float) pos / (float) totalSamples);
            }
            return true;
        };

        progress.store (0.f);
        for (size_t o = 0; o < outs.size(); ++o)
            if (! render (outs[o], nullptr, 0.45f * (float) o / (float) outs.size(), 0.45f / (float) outs.size())) { res.error = cancel.load() ? "Export canceled." : "Render failed."; opt.folder.deleteRecursively(); return res; }

        const double target = juce::Decibels::decibelsToGain (-0.3);
        double globalPeak = 0.0;
        for (auto& o : outs) globalPeak = juce::jmax (globalPeak, o.peak);
        for (auto& o : outs)
        {
            if (opt.norm == NormMode::Off || o.peak < 1e-6) o.gain = 1.f;
            else if (opt.norm == NormMode::Relative) o.gain = (float) (target / globalPeak);
            else o.gain = (float) (target / o.peak);
        }

        juce::WavAudioFormat wav; juce::AiffAudioFormat aiff; juce::FlacAudioFormat flac;
        juce::AudioFormat& format = opt.file == FileKind::Aiff ? (juce::AudioFormat&) aiff : (opt.file == FileKind::Flac ? (juce::AudioFormat&) flac : (juce::AudioFormat&) wav);
        for (size_t o = 0; o < outs.size(); ++o)
        {
            auto& out = outs[o];
            out.file = opt.folder.getChildFile (out.name + fileExtension (opt.file));
            out.file.deleteFile();
            std::unique_ptr<juce::FileOutputStream> fs (out.file.createOutputStream());
            if (fs == nullptr) { res.error = "Could not write " + out.file.getFileName(); return res; }
            std::unique_ptr<juce::AudioFormatWriter> w (format.createWriterFor (fs.get(), rate, 2, opt.bits, {}, opt.file == FileKind::Flac ? 5 : 0));
            if (w == nullptr) { res.error = juce::String ("Could not create a ") + juce::String (opt.bits) + "-bit " + fileKindName (opt.file) + " writer at " + juce::String (rate / 1000.0, 1) + " kHz."; return res; }
            fs.release();
            out.peak = 0.0;
            const bool ok = render (out, w.get(), 0.45f + 0.55f * (float) o / (float) outs.size(), 0.55f / (float) outs.size());
            w.reset();
            if (! ok) { for (auto& q : outs) q.file.deleteFile(); res.error = cancel.load() ? "Export canceled." : "Writing failed."; return res; }
        }
        for (auto& o : outs) { res.files.add (o.file); res.bytes += o.file.getSize(); }
        res.ok = true;
        progress.store (1.f);
        return res;
    }
};

class ExportThread : public juce::Thread
{
public:
    ExportThread (std::unique_ptr<ExportSnapshot> snap, ExportOptions o, std::function<void (ExportResult)> done)
        : juce::Thread ("Junk Drawer export"), snapshot (std::move (snap)), opt (std::move (o)), onDone (std::move (done)) {}
    ~ExportThread() override { cancel.store (true); stopThread (10000); }

    std::atomic<float> progress { 0.f };
    std::atomic<bool> cancel { false };
    std::atomic<bool> finished { false };
    ExportResult result;

    void run() override
    {
        result = Exporter::run (*snapshot, opt, progress, cancel);
        finished.store (true);
        auto cb = onDone; auto r = result;
        juce::MessageManager::callAsync ([cb, r] { if (cb) cb (r); });
    }

private:
    std::unique_ptr<ExportSnapshot> snapshot;
    ExportOptions opt;
    std::function<void (ExportResult)> onDone;
};

} // namespace jdx
