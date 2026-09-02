#include "FnSymbolSimulationDataGenerator.h"
#include "FnSymbolAnalyzerSettings.h"

#include <AnalyzerHelpers.h>

FnSymbolSimulationDataGenerator::FnSymbolSimulationDataGenerator()
:	mPatternIndex( 0 )
{
}

FnSymbolSimulationDataGenerator::~FnSymbolSimulationDataGenerator()
{
}

void FnSymbolSimulationDataGenerator::Initialize( U32 simulation_sample_rate, FnSymbolAnalyzerSettings* settings )
{
	mSimulationSampleRateHz = simulation_sample_rate;
	mSettings = settings;
	mPatternIndex = 0;

	mFnSimulationData.SetChannel( mSettings->mInputChannel );
	mFnSimulationData.SetSampleRate( simulation_sample_rate );
	mFnSimulationData.SetInitialBitState( BIT_LOW ); // both channels idle low in captures/digital.csv, per docs/FN_PROTOCOL_FINDINGS.md's "idle level" finding
}

U32 FnSymbolSimulationDataGenerator::GenerateSimulationData( U64 largest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channel )
{
	U64 adjusted_largest_sample_requested = AnalyzerHelpers::AdjustSimulationTargetSample( largest_sample_requested, sample_rate, mSimulationSampleRateHz );

	while( mFnSimulationData.GetCurrentSampleNumber() < adjusted_largest_sample_requested )
	{
		CreateFnPulse();
	}

	*simulation_channel = &mFnSimulationData;
	return 1;
}

void FnSymbolSimulationDataGenerator::CreateFnPulse()
{
	// Steps through kFnSymbolNoise..kFnSymbolLong in turn, picking a duration midway
	// between adjacent thresholds (and double the long threshold for the last bin) so
	// each dwell lands unambiguously in its bin regardless of the configured thresholds.
	// Level simply alternates each dwell via Transition().
	U32 noise = mSettings->mNoiseThresholdUs;
	U32 shortT = mSettings->mShortThresholdUs;
	U32 medT = mSettings->mMediumThresholdUs;
	U32 longT = mSettings->mLongThresholdUs;

	U32 durations_us[ 5 ] =
	{
		noise / 2,             // kFnSymbolNoise
		( noise + shortT ) / 2,  // kFnSymbolShort
		( shortT + medT ) / 2,   // kFnSymbolMedium
		( medT + longT ) / 2,    // kFnSymbolWide
		longT * 2               // kFnSymbolLong
	};

	U32 us = mSimulationSampleRateHz / 1000000;
	if( us == 0 )
		us = 1;

	mFnSimulationData.Transition();
	mFnSimulationData.Advance( durations_us[ mPatternIndex ] * us );

	mPatternIndex = ( mPatternIndex + 1 ) % 5;
}
