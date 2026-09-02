#include "FnSymbolAnalyzerSettings.h"
#include <AnalyzerHelpers.h>

FnSymbolAnalyzerSettings::FnSymbolAnalyzerSettings()
    : mInputChannel( UNDEFINED_CHANNEL ),
      mDecodeMode( kFnDecodeRawDwell ),
      mNoiseThresholdUs( 3 ),
      mShortThresholdUs( 20 ),
      mMediumThresholdUs( 300 ),
      mLongThresholdUs( 1000 ),
      mFnShortLongSplitUs( 100 ),
      mFnSyncMinUs( 1000 )
{
    mInputChannelInterface.SetTitleAndTooltip( "FN 2-Wire", "FN two-wire field signal (raw, pre-decode)" );
    mInputChannelInterface.SetChannel( mInputChannel );

    mDecodeModeInterface.SetTitleAndTooltip( "Decode layer",
        "Raw dwell bins = layer 2 only (every edge, no protocol semantics). FN symbols + frames = "
        "layers 3-5 (S/L symbol decode, sync/word framing, address+data, PCB-085 interpretation) -- "
        "see docs/SALEAE_DECODER_NOTES.md." );
    mDecodeModeInterface.AddNumber( kFnDecodeRawDwell, "Raw dwell bins (layer 2)",
        "Classify every dwell into a duration bin. No framing or addressing." );
    mDecodeModeInterface.AddNumber( kFnDecodeSymbolsAndFrames, "FN symbols + frames (layers 3-5, PCB-085)",
        "Decode S/L symbols, sync/word boundaries, FN address+data, and PCB-085 output/analog interpretation." );
    mDecodeModeInterface.SetNumber( mDecodeMode );

    mNoiseThresholdInterface.SetTitleAndTooltip( "Noise threshold (us)",
        "Edges shorter than this are flagged as chirp/noise-candidates (not dropped) - default 3us matches "
        "extract_reference_frame.py's noise-filter threshold." );
    mNoiseThresholdInterface.SetMin( 0 );
    mNoiseThresholdInterface.SetMax( 100000 );
    mNoiseThresholdInterface.SetInteger( mNoiseThresholdUs );

    mShortThresholdInterface.SetTitleAndTooltip( "Short threshold (us)",
        "Upper bound for 'short' pulses - default 20us covers the ~15-17us reference pulse and most chirp-region edges." );
    mShortThresholdInterface.SetMin( 0 );
    mShortThresholdInterface.SetMax( 100000 );
    mShortThresholdInterface.SetInteger( mShortThresholdUs );

    mMediumThresholdInterface.SetTitleAndTooltip( "Medium threshold (us)",
        "Upper bound for 'medium' pulses - default 300us covers the observed ~50-300us 'data' pulse region." );
    mMediumThresholdInterface.SetMin( 0 );
    mMediumThresholdInterface.SetMax( 1000000 );
    mMediumThresholdInterface.SetInteger( mMediumThresholdUs );

    mLongThresholdInterface.SetTitleAndTooltip( "Long/dwell threshold (us)",
        "Edges at or above this are 'long dwells' - default 1000us (1ms) separates wide pulses from the "
        "multi-millisecond LOW/HIGH dwells that dominate each ~16.7ms sub-cycle." );
    mLongThresholdInterface.SetMin( 0 );
    mLongThresholdInterface.SetMax( 10000000 );
    mLongThresholdInterface.SetInteger( mLongThresholdUs );

    mFnShortLongSplitInterface.SetTitleAndTooltip( "FN symbol S/L split (us)",
        "Symbol/frame mode only. Observed SHORT intervals are ~25-27us, LONG are ~180-182us -- default 100us "
        "splits between them per FN_PROTOCOL_FINDINGS.md section 3." );
    mFnShortLongSplitInterface.SetMin( 0 );
    mFnShortLongSplitInterface.SetMax( 100000 );
    mFnShortLongSplitInterface.SetInteger( mFnShortLongSplitUs );

    mFnSyncMinInterface.SetTitleAndTooltip( "FN sync/word-boundary minimum (us)",
        "Symbol/frame mode only. Observed sync gaps are ~1.2645-1.2647ms or ~1.4195-1.4198ms -- default 1000us "
        "(1ms) treats anything at/above this as a word boundary rather than a data interval, per "
        "FN_PROTOCOL_FINDINGS.md section 4." );
    mFnSyncMinInterface.SetMin( 0 );
    mFnSyncMinInterface.SetMax( 10000000 );
    mFnSyncMinInterface.SetInteger( mFnSyncMinUs );

    AddInterface( &mInputChannelInterface );
    AddInterface( &mDecodeModeInterface );
    AddInterface( &mNoiseThresholdInterface );
    AddInterface( &mShortThresholdInterface );
    AddInterface( &mMediumThresholdInterface );
    AddInterface( &mLongThresholdInterface );
    AddInterface( &mFnShortLongSplitInterface );
    AddInterface( &mFnSyncMinInterface );

    AddExportOption( 0, "Export as text/csv file" );
    AddExportExtension( 0, "text", "txt" );
    AddExportExtension( 0, "csv", "csv" );

    ClearChannels();
    AddChannel( mInputChannel, "FN 2-Wire", false );
}

FnSymbolAnalyzerSettings::~FnSymbolAnalyzerSettings()
{
}

bool FnSymbolAnalyzerSettings::SetSettingsFromInterfaces()
{
    mInputChannel = mInputChannelInterface.GetChannel();
    mDecodeMode = ( FnDecodeMode )( U32 )mDecodeModeInterface.GetNumber();
    mNoiseThresholdUs = mNoiseThresholdInterface.GetInteger();
    mShortThresholdUs = mShortThresholdInterface.GetInteger();
    mMediumThresholdUs = mMediumThresholdInterface.GetInteger();
    mLongThresholdUs = mLongThresholdInterface.GetInteger();
    mFnShortLongSplitUs = mFnShortLongSplitInterface.GetInteger();
    mFnSyncMinUs = mFnSyncMinInterface.GetInteger();

    if( !( mNoiseThresholdUs < mShortThresholdUs && mShortThresholdUs < mMediumThresholdUs &&
           mMediumThresholdUs < mLongThresholdUs ) )
    {
        SetErrorText( "Thresholds must be strictly increasing: noise < short < medium < long." );
        return false;
    }

    if( !( mNoiseThresholdUs < mFnShortLongSplitUs && mFnShortLongSplitUs < mFnSyncMinUs ) )
    {
        SetErrorText( "FN symbol thresholds must be strictly increasing: noise < S/L split < sync minimum." );
        return false;
    }

    ClearChannels();
    AddChannel( mInputChannel, "FN 2-Wire", true );

    return true;
}

void FnSymbolAnalyzerSettings::UpdateInterfacesFromSettings()
{
    mInputChannelInterface.SetChannel( mInputChannel );
    mDecodeModeInterface.SetNumber( mDecodeMode );
    mNoiseThresholdInterface.SetInteger( mNoiseThresholdUs );
    mShortThresholdInterface.SetInteger( mShortThresholdUs );
    mMediumThresholdInterface.SetInteger( mMediumThresholdUs );
    mLongThresholdInterface.SetInteger( mLongThresholdUs );
    mFnShortLongSplitInterface.SetInteger( mFnShortLongSplitUs );
    mFnSyncMinInterface.SetInteger( mFnSyncMinUs );
}

void FnSymbolAnalyzerSettings::LoadSettings( const char* settings )
{
    SimpleArchive text_archive;
    text_archive.SetString( settings );

    text_archive >> mInputChannel;
    text_archive >> mNoiseThresholdUs;
    text_archive >> mShortThresholdUs;
    text_archive >> mMediumThresholdUs;
    text_archive >> mLongThresholdUs;

    // Added after the fields above, for the FN symbol/frame decode layer. This tool has
    // no external users with old saved settings strings to stay compatible with, so no
    // attempt is made to handle a short/older archive here.
    U32 decode_mode = mDecodeMode;
    text_archive >> decode_mode;
    mDecodeMode = ( FnDecodeMode )decode_mode;
    text_archive >> mFnShortLongSplitUs;
    text_archive >> mFnSyncMinUs;

    ClearChannels();
    AddChannel( mInputChannel, "FN 2-Wire", true );

    UpdateInterfacesFromSettings();
}

const char* FnSymbolAnalyzerSettings::SaveSettings()
{
    SimpleArchive text_archive;

    text_archive << mInputChannel;
    text_archive << mNoiseThresholdUs;
    text_archive << mShortThresholdUs;
    text_archive << mMediumThresholdUs;
    text_archive << mLongThresholdUs;
    text_archive << ( U32 )mDecodeMode;
    text_archive << mFnShortLongSplitUs;
    text_archive << mFnSyncMinUs;

    return SetReturnString( text_archive.GetString() );
}
