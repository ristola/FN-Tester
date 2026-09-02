#ifndef PCB085_PROFILE_H
#define PCB085_PROFILE_H

#include <AnalyzerTypes.h>
#include <string>

// PCB-085 board profile: translates a raw FN address/data word into named outputs,
// analog code, and an approximate 4-20mA current, per
// FN_OUTPUT_Tester_Handoff/docs/PCB085_ANALYSIS.md.
//
// Deliberately kept separate from the common FN address/data decode in
// FnSymbolAnalyzer.cpp -- CLAUDE.md's layering rule: "Keep board interpretation
// separate from common FN decoding" / "Do not use one global FN output map."
// A PCB-110 (or other generation) profile should be a sibling file, not a branch
// bolted onto this one.
namespace Pcb085Profile
{
	struct Result
	{
		bool known;              // true if this address is recognized by the PCB-085 profile at all
		std::string confidence;  // CONFIRMED / STRONG EVIDENCE / HYPOTHESIS / UNKNOWN, per docs/PCB085_ANALYSIS.md
		std::string summary;     // one-line form, for narrow bubble text
		std::string detail;      // multi-line form (\n separated), for tabular/export text
	};

	// address5: 5-bit address, bit4=A1 (MSB) .. bit0=A5 (LSB) -- i.e. the same left-to-right
	// order the docs write addresses in (e.g. "10001").
	// data4: 4-bit data, bit3=D1 (MSB) .. bit0=D4 (LSB), same convention.
	Result Interpret( U8 address5, U8 data4 );

	// Render the 5-bit address as a "10001"-style string (A1 first).
	std::string AddressBitString( U8 address5 );

	// Render the 4-bit data as a "0101"-style string (D1 first).
	std::string DataBitString( U8 data4 );
}

#endif // PCB085_PROFILE_H
