#ifndef FNSYMBOL_ANALYZER_SETTINGS
#define FNSYMBOL_ANALYZER_SETTINGS

#include <AnalyzerSettings.h>
#include <AnalyzerTypes.h>

// Decode mode: which layer(s) of docs/SALEAE_DECODER_NOTES.md's roadmap this
// analyzer run produces frames for.
enum FnDecodeMode
{
    // Layer 2 only: classify every raw dwell into a duration bin. No framing,
    // addressing, or symbol semantics. Kept as the default so existing saved
    // settings/behavior don't silently change meaning.
    kFnDecodeRawDwell = 0,

    // Layers 3-5: S/L symbol decode, sync/word framing, repeated-word
    // validation, and FN address+data (with a PCB-085 board-profile
    // interpretation where the address is recognized). See
    // docs/SALEAE_DECODER_NOTES.md's 2026-08-31 status update and
    // docs/PCB085_ANALYSIS.md for the evidence this is derived from.
    kFnDecodeSymbolsAndFrames = 1
};

// Settings for the FN two-wire decoder - see the README in this directory
// and FN_OUTPUT_Tester_Handoff/docs/FN_PROTOCOL_FINDINGS.md /
// docs/PCB085_ANALYSIS.md for what is/isn't established about this protocol.
class FnSymbolAnalyzerSettings : public AnalyzerSettings
{
public:
    FnSymbolAnalyzerSettings();
    virtual ~FnSymbolAnalyzerSettings();

    virtual bool SetSettingsFromInterfaces();
    void UpdateInterfacesFromSettings();
    virtual void LoadSettings( const char* settings );
    virtual const char* SaveSettings();

    Channel mInputChannel;

    FnDecodeMode mDecodeMode;

    // Duration-bin thresholds, in microseconds. Defaults come directly from
    // saleae/analysis/compare_mc145026.py and docs/FN_PROTOCOL_FINDINGS.md's
    // measurements against captures/digital.csv:
    // - mNoiseThresholdUs (3us): extract_reference_frame.py's own
    //   noise-filter threshold (3.2us, rounded down to an integer setting).
    //   Edges shorter than this are flagged as chirp/noise-candidates, not
    //   silently dropped in raw-dwell mode -- and are ignored outright (not
    //   even counted as a symbol interval) in symbol/frame mode, since a
    //   sub-few-microsecond glitch can't be a real S/L interval.
    // - mShortThresholdUs (20us): the "reference" ~15-17us pulse and most of
    //   the chirp region's edges fall under this. (Raw-dwell mode only.)
    // - mMediumThresholdUs (300us): the "data" pulse region (~50-300us
    //   observed) falls under this. (Raw-dwell mode only.)
    // - mLongThresholdUs (1000us / 1ms): separates the wide-pulse band from
    //   the multi-millisecond LOW/HIGH dwells that dominate each ~16.7ms
    //   sub-cycle. (Raw-dwell mode only.)
    U32 mNoiseThresholdUs;
    U32 mShortThresholdUs;
    U32 mMediumThresholdUs;
    U32 mLongThresholdUs;

    // Symbol/frame-mode (layers 3-5) thresholds, in microseconds, from the
    // PCB-085 evidence in docs/FN_PROTOCOL_FINDINGS.md sections 3-4:
    // - mFnShortLongSplitUs (100us): observed SHORT intervals are ~25-27us and
    //   LONG intervals are ~180-182us, so a threshold anywhere between them
    //   works; 100us matches the doc's stated practical threshold.
    // - mFnSyncMinUs (1000us): observed sync gaps are ~1.2645-1.2647ms or
    //   ~1.4195-1.4198ms -- both far above any real S/L interval -- so
    //   anything at/above 1ms is structurally treated as a word/frame
    //   boundary rather than a data interval. Don't require one exact sync
    //   duration (FN_PROTOCOL_FINDINGS.md section 4); the two known-good
    //   bands are checked separately, informationally, in FnSymbolAnalyzer.cpp.
    U32 mFnShortLongSplitUs;
    U32 mFnSyncMinUs;

protected:
    AnalyzerSettingInterfaceChannel mInputChannelInterface;
    AnalyzerSettingInterfaceNumberList mDecodeModeInterface;
    AnalyzerSettingInterfaceInteger mNoiseThresholdInterface;
    AnalyzerSettingInterfaceInteger mShortThresholdInterface;
    AnalyzerSettingInterfaceInteger mMediumThresholdInterface;
    AnalyzerSettingInterfaceInteger mLongThresholdInterface;
    AnalyzerSettingInterfaceInteger mFnShortLongSplitInterface;
    AnalyzerSettingInterfaceInteger mFnSyncMinInterface;
};

#endif // FNSYMBOL_ANALYZER_SETTINGS
