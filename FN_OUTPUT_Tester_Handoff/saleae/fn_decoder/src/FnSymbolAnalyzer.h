#ifndef FNSYMBOL_ANALYZER_H
#define FNSYMBOL_ANALYZER_H

#include <Analyzer.h>
#include "FnSymbolAnalyzerSettings.h"
#include "FnSymbolAnalyzerResults.h"
#include "FnSymbolSimulationDataGenerator.h"
#include <memory>

class ANALYZER_EXPORT FnSymbolAnalyzer : public Analyzer2
{
public:
	FnSymbolAnalyzer();
	virtual ~FnSymbolAnalyzer();

	virtual void SetupResults();
	virtual void WorkerThread();

	virtual U32 GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channels );
	virtual U32 GetMinimumSampleRateHz();

	virtual const char* GetAnalyzerName() const;
	virtual bool NeedsRerun();

protected: //functions
	// Layer 2 (docs/SALEAE_DECODER_NOTES.md): classify every raw dwell into a duration
	// bin. No framing, addressing, or symbol semantics.
	void WorkerThreadRawDwell( U32 sample_rate_hz );

	// Layers 3-5: S/L symbol decode, sync/word framing, repeated-word validation, and
	// FN address+data (with PCB-085 board-profile interpretation where recognized).
	void WorkerThreadDecodeSymbols( U32 sample_rate_hz );

protected: //vars
	FnSymbolAnalyzerSettings mSettings;
	std::unique_ptr<FnSymbolAnalyzerResults> mResults;
	AnalyzerChannelData* mSerial;

	FnSymbolSimulationDataGenerator mSimulationDataGenerator;
	bool mSimulationInitilized;
};

extern "C" ANALYZER_EXPORT const char* __cdecl GetAnalyzerName();
extern "C" ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer( );
extern "C" ANALYZER_EXPORT void __cdecl DestroyAnalyzer( Analyzer* analyzer );

#endif //FNSYMBOL_ANALYZER_H
