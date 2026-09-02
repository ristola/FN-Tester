#ifndef FNSYMBOL_SIMULATION_DATA_GENERATOR
#define FNSYMBOL_SIMULATION_DATA_GENERATOR

#include <SimulationChannelDescriptor.h>
class FnSymbolAnalyzerSettings;

// Generates a repeating, illustrative waveform that steps through all five FnSymbolClass
// duration bins (see FnSymbolAnalyzerResults.h), derived from the current settings'
// thresholds so it stays correct if a user changes them. This exists purely so the
// analyzer can be previewed/tested without real hardware attached -- it is NOT a
// reproduction of any real capture. See docs/FN_PROTOCOL_FINDINGS.md for what is/isn't
// established about the actual FN two-wire waveform.
class FnSymbolSimulationDataGenerator
{
public:
	FnSymbolSimulationDataGenerator();
	~FnSymbolSimulationDataGenerator();

	void Initialize( U32 simulation_sample_rate, FnSymbolAnalyzerSettings* settings );
	U32 GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channel );

protected:
	FnSymbolAnalyzerSettings* mSettings;
	U32 mSimulationSampleRateHz;

protected:
	void CreateFnPulse();
	U32 mPatternIndex;

	SimulationChannelDescriptor mFnSimulationData;

};
#endif //FNSYMBOL_SIMULATION_DATA_GENERATOR
