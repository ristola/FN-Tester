#include "FnSymbolAnalyzer.h"
#include "FnSymbolAnalyzerSettings.h"
#include "FnSymbolAnalyzerResults.h"
#include <AnalyzerChannelData.h>

namespace
{
	// Finalizes the current field once 4 short/long intervals have been collected, or
	// fewer if a sync interval interrupted it (see WorkerThreadDecodeSymbols). Appends
	// the decoded bit to word_bits, emits a kFnFrameError/kFnErrorMalformedSymbol frame
	// if the collected intervals didn't match the expected SLSL/LSLS (or truncated
	// SLS/LSL) shape, and resets field_count for the next field.
	void FinishFnField( FnSymbolAnalyzerResults* results, const bool field_is_long[ 4 ], U32& field_count,
		U64 field_start_sample, U64 field_end_sample,
		bool word_bits[ 9 ], U32& word_bit_count, U64& word_start_sample, bool& word_has_symbol_warning )
	{
		if( field_count == 0 )
			return; // nothing pending -- e.g. two sync intervals arrived back to back

		bool bit_value = field_is_long[ 0 ]; // interval 0 alone distinguishes SLSL (0) from LSLS (1)

		bool pattern_ok = ( field_count == 4 || field_count == 3 );
		for( U32 i = 1; i < field_count && pattern_ok; i++ )
		{
			bool expected_long = bit_value ? ( i % 2 == 0 ) : ( i % 2 != 0 );
			if( field_is_long[ i ] != expected_long )
				pattern_ok = false;
		}
		if( !pattern_ok )
			word_has_symbol_warning = true;

		if( word_bit_count == 0 )
			word_start_sample = field_start_sample;
		if( word_bit_count < 9 )
			word_bits[ word_bit_count ] = bit_value;
		word_bit_count++;

		if( !pattern_ok )
		{
			Frame frame;
			frame.mStartingSampleInclusive = field_start_sample;
			frame.mEndingSampleInclusive = field_end_sample;
			frame.mData1 = field_count;
			frame.mData2 = kFnErrorMalformedSymbol;
			frame.mType = kFnFrameError;
			frame.mFlags = 0;
			results->AddFrame( frame );
			results->CommitResults();
		}

		field_count = 0;
	}

	// Finalizes the current word once 9 bits have been collected via FinishFnField, or
	// emits a kFnFrameError/kFnErrorIncompleteWord frame and drops pairing state if a
	// sync boundary arrived first. On success, emits a kFnFrameWord frame.
	//
	// Pairing is keyed by ADDRESS, not by alternating parity: FN-MAIN cycles through
	// several different addresses (10000, 10001, 10010, ...) before returning to any one
	// of them, so two temporally-adjacent words are not necessarily a copy pair just
	// because they're adjacent. A real capture caught this: naive "every consecutive word
	// alternates copy1/copy2" pairing produced a nonsensical MISMATCH between address
	// 10000 and an unrelated address 10100 that merely happened to follow it. The correct
	// pair FN_PROTOCOL_FINDINGS.md section 5 means is two consecutive words sharing the
	// SAME address -- so a word only gets compared against the immediately preceding word
	// if that preceding word's address (top 5 bits) matches this one's.
	void FinishFnWord( FnSymbolAnalyzerResults* results, bool word_bits[ 9 ], U32& word_bit_count,
		U64 word_start_sample, U64 word_end_sample, bool& word_has_symbol_warning,
		bool& have_previous_word, bool previous_word_bits[ 9 ] )
	{
		if( word_bit_count == 0 )
			return;

		if( word_bit_count != 9 )
		{
			Frame frame;
			frame.mStartingSampleInclusive = word_start_sample;
			frame.mEndingSampleInclusive = word_end_sample;
			frame.mData1 = word_bit_count;
			frame.mData2 = kFnErrorIncompleteWord;
			frame.mType = kFnFrameError;
			frame.mFlags = 0;
			results->AddFrame( frame );
			results->CommitResults();

			have_previous_word = false; // don't pair a broken word against the next one
			word_bit_count = 0;
			word_has_symbol_warning = false;
			return;
		}

		U32 packed = 0;
		for( U32 i = 0; i < 9; i++ )
			if( word_bits[ i ] )
				packed |= ( 1u << ( 8 - i ) );

		Frame frame;
		frame.mStartingSampleInclusive = word_start_sample;
		frame.mEndingSampleInclusive = word_end_sample;
		frame.mData1 = packed;
		frame.mType = kFnFrameWord;
		frame.mFlags = word_has_symbol_warning ? kFrameFlagSymbolWarning : 0;

		U32 previous_packed = 0;
		bool is_repeat_of_previous = false;
		if( have_previous_word )
		{
			for( U32 i = 0; i < 9; i++ )
				if( previous_word_bits[ i ] )
					previous_packed |= ( 1u << ( 8 - i ) );

			is_repeat_of_previous = ( ( previous_packed >> 4 ) == ( packed >> 4 ) ); // same 5-bit address
		}

		if( is_repeat_of_previous )
		{
			frame.mFlags |= kFrameFlagSecondCopy;
			if( previous_packed == packed )
				frame.mFlags |= kFrameFlagCopiesMatch;
			frame.mData2 = previous_packed;

			have_previous_word = false; // pair consumed -- the next word starts fresh
		}
		else
		{
			frame.mData2 = 0;
			for( U32 i = 0; i < 9; i++ )
				previous_word_bits[ i ] = word_bits[ i ];
			have_previous_word = true;
		}

		results->AddFrame( frame );
		results->CommitResults();

		word_bit_count = 0;
		word_has_symbol_warning = false;
	}
}

FnSymbolAnalyzer::FnSymbolAnalyzer()
:	Analyzer2(),
	mSettings(),
	mSimulationInitilized( false )
{
	SetAnalyzerSettings( &mSettings );
}

FnSymbolAnalyzer::~FnSymbolAnalyzer()
{
	KillThread();
}

void FnSymbolAnalyzer::SetupResults()
{
	// SetupResults is called each time the analyzer is run. Because the same instance can be used for multiple runs, we need to clear the results each time.
	mResults.reset(new FnSymbolAnalyzerResults( this, &mSettings ));
	SetAnalyzerResults( mResults.get() );
	mResults->AddChannelBubblesWillAppearOn( mSettings.mInputChannel );
}

void FnSymbolAnalyzer::WorkerThread()
{
	U32 sample_rate_hz = GetSampleRate();

	mSerial = GetAnalyzerChannelData( mSettings.mInputChannel );

	if( mSettings.mDecodeMode == kFnDecodeSymbolsAndFrames )
		WorkerThreadDecodeSymbols( sample_rate_hz );
	else
		WorkerThreadRawDwell( sample_rate_hz );
}

void FnSymbolAnalyzer::WorkerThreadRawDwell( U32 sample_rate_hz )
{
	// Layer 2 of docs/SALEAE_DECODER_NOTES.md's roadmap: "Edge/pulse analyzer --
	// timestamps, high/low duration, period, classification". Every dwell (the time the
	// line holds one level between two edges) becomes one Frame, classified into a
	// duration bin per FnSymbolAnalyzerSettings' thresholds. Deliberately no framing,
	// addressing, or symbol semantics -- see FnSymbolAnalyzerSettings.h and
	// FN_OUTPUT_Tester_Handoff/docs/FN_PROTOCOL_FINDINGS.md for why this mode exists
	// separately from WorkerThreadDecodeSymbols() below.
	for( ; ; )
	{
		BitState level = mSerial->GetBitState();
		U64 starting_sample = mSerial->GetSampleNumber();

		mSerial->AdvanceToNextEdge();

		// AdvanceToNextEdge() leaves us positioned at the first sample of the new level,
		// so the prior dwell's last sample is one before that -- Frames may not share a
		// sample (per docs/Analyzer_API.md's Frame member notes).
		U64 ending_sample = mSerial->GetSampleNumber() - 1;
		U64 duration_samples = ( ending_sample - starting_sample ) + 1;
		double duration_us = ( double( duration_samples ) * 1000000.0 ) / double( sample_rate_hz );

		FnSymbolClass symbol_class;
		if( duration_us < double( mSettings.mNoiseThresholdUs ) )
			symbol_class = kFnSymbolNoise;
		else if( duration_us < double( mSettings.mShortThresholdUs ) )
			symbol_class = kFnSymbolShort;
		else if( duration_us < double( mSettings.mMediumThresholdUs ) )
			symbol_class = kFnSymbolMedium;
		else if( duration_us < double( mSettings.mLongThresholdUs ) )
			symbol_class = kFnSymbolWide;
		else
			symbol_class = kFnSymbolLong;

		Frame frame;
		frame.mStartingSampleInclusive = starting_sample;
		frame.mEndingSampleInclusive = ending_sample;
		frame.mData1 = duration_samples; // raw dwell width, in samples -- convert with the sample rate for us/ms
		frame.mData2 = ( level == BIT_HIGH ) ? 1 : 0; // level held during this dwell
		frame.mType = ( U8 )symbol_class;
		frame.mFlags = 0;

		mResults->AddFrame( frame );
		mResults->CommitResults();
		ReportProgress( frame.mEndingSampleInclusive );
	}
}

void FnSymbolAnalyzer::WorkerThreadDecodeSymbols( U32 sample_rate_hz )
{
	// Layers 3-5 of docs/SALEAE_DECODER_NOTES.md's roadmap, built on the evidence in
	// docs/FN_PROTOCOL_FINDINGS.md (sections 3-8) and docs/PCB085_ANALYSIS.md, which
	// resolved this mode's original blocker (no capture with a known commanded output
	// change) via a blind-validated PCB-085 decode -- see
	// docs/SALEAE_DECODER_NOTES.md's 2026-08-31 status update.
	//
	// Symbol decoder: each interval classifies as SHORT (~25-27us) or LONG (~180-182us),
	// split at mSettings.mFnShortLongSplitUs. A 4-interval field decodes as
	// SLSL -> bit 0 / LSLS -> bit 1 (bit value = whether interval 0 is LONG); the field
	// immediately before sync is legitimately truncated to 3 intervals (SLS/LSL) because
	// the sync interval absorbs the final transition (FN_PROTOCOL_FINDINGS.md section 7).
	//
	// Frame detector: intervals at/above mSettings.mFnSyncMinUs are word/frame
	// boundaries (section 4). A word is 5 address bits + 4 data bits = 9 fields
	// (section 8); two consecutive word copies separated by one sync are compared for
	// the repeated-word validation the docs call for (section 5).

	// Current field (up to 4 short/long intervals) being accumulated.
	bool field_is_long[ 4 ];
	U32 field_count = 0;
	U64 field_start_sample = 0;

	// Current word (9 fields) being accumulated.
	bool word_bits[ 9 ];
	U32 word_bit_count = 0;
	U64 word_start_sample = 0;
	bool word_has_symbol_warning = false;

	// The previous word, held to compare against the next one IF it turns out to share
	// the same address (see FinishFnWord's comment for why that check matters).
	bool have_previous_word = false;
	bool previous_word_bits[ 9 ] = { false };

	for( ; ; )
	{
		mSerial->GetBitState(); // level doesn't carry symbol identity here -- FN_PROTOCOL_FINDINGS.md section 7
		U64 starting_sample = mSerial->GetSampleNumber();

		mSerial->AdvanceToNextEdge();

		U64 ending_sample = mSerial->GetSampleNumber() - 1;
		U64 duration_samples = ( ending_sample - starting_sample ) + 1;
		double duration_us = ( double( duration_samples ) * 1000000.0 ) / double( sample_rate_hz );

		if( duration_us < double( mSettings.mNoiseThresholdUs ) )
		{
			// Sub-noise-floor glitch/ringing -- not a real S/L interval, so it's ignored
			// outright rather than counted as a field interval (same reasoning as
			// extract_reference_frame.py's noise filter, docs/EXPERIMENT_LOG.md
			// Experiment 002 addendum). The raw-dwell mode above still shows every edge
			// if this needs auditing.
			ReportProgress( ending_sample );
			continue;
		}

		if( duration_us >= double( mSettings.mFnSyncMinUs ) )
		{
			// Word/frame boundary: finalize whatever field/word was in progress, then
			// emit the sync frame itself.
			FinishFnField( mResults.get(), field_is_long, field_count, field_start_sample, ending_sample,
				word_bits, word_bit_count, word_start_sample, word_has_symbol_warning );
			FinishFnWord( mResults.get(), word_bits, word_bit_count, word_start_sample, ending_sample,
				word_has_symbol_warning, have_previous_word, previous_word_bits );

			bool known_band = ( duration_us >= 1150.0 && duration_us <= 1500.0 );
			// ~1150-1500us covers both observed sync bands (~1.2645-1.2647ms and
			// ~1.4195-1.4198ms, FN_PROTOCOL_FINDINGS.md section 4) with margin. This is
			// informational only -- anything at/above mFnSyncMinUs still structurally
			// ends the word either way, per that section's "recognize a valid sync range
			// rather than a single exact value" rule.

			Frame frame;
			frame.mStartingSampleInclusive = starting_sample;
			frame.mEndingSampleInclusive = ending_sample;
			frame.mData1 = duration_samples;
			frame.mData2 = 0;
			frame.mType = kFnFrameSync;
			frame.mFlags = known_band ? kFrameFlagSyncKnownBand : 0;
			mResults->AddFrame( frame );
			mResults->CommitResults();

			ReportProgress( ending_sample );
			continue;
		}

		bool is_long = ( duration_us >= double( mSettings.mFnShortLongSplitUs ) );

		if( field_count == 0 )
			field_start_sample = starting_sample;
		if( field_count < 4 )
			field_is_long[ field_count ] = is_long;
		field_count++;

		if( field_count >= 4 )
			FinishFnField( mResults.get(), field_is_long, field_count, field_start_sample, ending_sample,
				word_bits, word_bit_count, word_start_sample, word_has_symbol_warning );

		if( word_bit_count == 9 )
			FinishFnWord( mResults.get(), word_bits, word_bit_count, word_start_sample, ending_sample,
				word_has_symbol_warning, have_previous_word, previous_word_bits );

		ReportProgress( ending_sample );
	}
}

bool FnSymbolAnalyzer::NeedsRerun()
{
	return false;
}

U32 FnSymbolAnalyzer::GenerateSimulationData( U64 minimum_sample_index, U32 device_sample_rate, SimulationChannelDescriptor** simulation_channels )
{
	if( mSimulationInitilized == false )
	{
		mSimulationDataGenerator.Initialize( GetSimulationSampleRate(), &mSettings );
		mSimulationInitilized = true;
	}

	return mSimulationDataGenerator.GenerateSimulationData( minimum_sample_index, device_sample_rate, simulation_channels );
}

U32 FnSymbolAnalyzer::GetMinimumSampleRateHz()
{
	// This analyzer has no single fixed "bit rate" to derive a rate from -- it classifies
	// raw dwell widths (see FnSymbolAnalyzerSettings.h). captures/digital.csv (see
	// docs/FN_PROTOCOL_FINDINGS.md) shows real sub-3us edges, well below the default 3us
	// noise threshold, so require enough oversampling to resolve those reliably rather
	// than falling back to the SDK's generic 25kHz floor.
	return 4000000; // 4 MSa/s
}

const char* FnSymbolAnalyzer::GetAnalyzerName() const
{
	return "FN Symbol (raw pulses)";
}

const char* GetAnalyzerName()
{
	return "FN Symbol (raw pulses)";
}

Analyzer* CreateAnalyzer()
{
	return new FnSymbolAnalyzer();
}

void DestroyAnalyzer( Analyzer* analyzer )
{
	delete analyzer;
}
