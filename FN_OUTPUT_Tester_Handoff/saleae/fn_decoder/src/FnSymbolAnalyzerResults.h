#ifndef FNSYMBOL_ANALYZER_RESULTS
#define FNSYMBOL_ANALYZER_RESULTS

#include <AnalyzerResults.h>

class FnSymbolAnalyzer;
class FnSymbolAnalyzerSettings;

// Frame type (mType) values - which duration bin this edge fell into.
// Deliberately unlabeled with any protocol meaning (not "address", not
// "sync") - see FnSymbolAnalyzerSettings.h and this directory's README for
// why: no capture with a known commanded value exists yet to derive real
// symbol semantics from.
enum FnSymbolClass
{
    kFnSymbolNoise = 0,  // < mNoiseThresholdUs
    kFnSymbolShort = 1,  // [mNoiseThresholdUs, mShortThresholdUs)
    kFnSymbolMedium = 2, // [mShortThresholdUs, mMediumThresholdUs)
    kFnSymbolWide = 3,   // [mMediumThresholdUs, mLongThresholdUs)
    kFnSymbolLong = 4    // >= mLongThresholdUs
};

// Frame type (mType) values for FnDecodeMode::kFnDecodeSymbolsAndFrames (layers 3-5).
// Numbered from 100 so they can never collide with FnSymbolClass (0-4) even though
// both families share the same Frame.mType byte -- GenerateBubbleText/etc. branch on
// which range a given frame's mType falls into.
enum FnDecodeFrameType
{
    kFnFrameSync = 100,  // synchronization gap between word copies (FN_PROTOCOL_FINDINGS.md section 4)
    kFnFrameWord = 101,  // one decoded 9-bit word copy: 5-bit address + 4-bit data (section 8)
    kFnFrameError = 102  // invalid interval / malformed symbol / incomplete word (section 14)
};

// Frame.mFlags bits used by kFnFrameWord frames.
const U8 kFrameFlagSecondCopy = 0x01;   // this word is the second copy of a repeated-word pair
const U8 kFrameFlagCopiesMatch = 0x02;  // valid only if kFrameFlagSecondCopy is set: copy 2 == copy 1
const U8 kFrameFlagSymbolWarning = 0x04; // at least one field's S/L pattern didn't match the expected shape

// Frame.mFlags bits used by kFnFrameSync frames.
const U8 kFrameFlagSyncKnownBand = 0x01; // duration fell within one of the two previously-observed sync ranges

// Frame.mData2 values used by kFnFrameError frames (mData1 carries context -- see FnSymbolAnalyzer.cpp).
enum FnFrameErrorKind
{
    kFnErrorMalformedSymbol = 1, // S/L pattern didn't match the expected SLSL/LSLS (or truncated SLS/LSL) shape
    kFnErrorIncompleteWord = 2   // a sync boundary arrived before a full 9-bit word was collected
};

class FnSymbolAnalyzerResults : public AnalyzerResults
{
public:
    FnSymbolAnalyzerResults( FnSymbolAnalyzer* analyzer, FnSymbolAnalyzerSettings* settings );
    virtual ~FnSymbolAnalyzerResults();

    virtual void GenerateBubbleText( U64 frame_index, Channel& channel, DisplayBase display_base );
    virtual void GenerateExportFile( const char* file, DisplayBase display_base, U32 export_type_user_id );

    virtual void GenerateFrameTabularText( U64 frame_index, DisplayBase display_base );
    virtual void GeneratePacketTabularText( U64 packet_id, DisplayBase display_base );
    virtual void GenerateTransactionTabularText( U64 transaction_id, DisplayBase display_base );

protected: // vars
    FnSymbolAnalyzerSettings* mSettings;
    FnSymbolAnalyzer* mAnalyzer;
};

#endif // FNSYMBOL_ANALYZER_RESULTS
