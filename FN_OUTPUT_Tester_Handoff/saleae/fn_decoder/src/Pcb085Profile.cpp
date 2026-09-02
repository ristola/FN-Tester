#include "Pcb085Profile.h"
#include <cstdio>

namespace
{
	// PCB-085's 6 recurring FN addresses, as plain integers (5-bit value, A1=MSB),
	// per docs/PCB085_ANALYSIS.md section 4. Named here so the switch below reads
	// like the docs rather than a wall of magic numbers.
	const U8 kAddrOutputs1to4 = 0x11; // 10001
	const U8 kAddrOutputs5to8 = 0x12; // 10010
	const U8 kAddrAnalogValue = 0x16; // 10110
	const U8 kAddrAnalogCompanion = 0x14; // 10100
	const U8 kAddrUnmappedBank1 = 0x10; // 10000 -- candidate outputs 9-12/13-16, unassigned
	const U8 kAddrUnmappedBank2 = 0x13; // 10011 -- candidate outputs 9-12/13-16, unassigned

	std::string BitString( U8 value, U32 num_bits )
	{
		std::string out;
		for( U32 i = 0; i < num_bits; i++ )
		{
			U32 shift = num_bits - 1 - i;
			out += ( ( value >> shift ) & 1 ) ? '1' : '0';
		}
		return out;
	}

	// Renders only the outputs that are ON, one name per `separator`, e.g. "Valve 1 ON"
	// on its own line -- dropping the OFF ones keeps the decoded-frame text readable at a
	// glance instead of listing all 4 outputs in every bank every time.
	std::string FormatOnList( const bool bits[ 4 ], const char* const names[ 4 ], const char* separator,
		const char* none_text )
	{
		std::string out;
		bool first = true;
		for( U32 i = 0; i < 4; i++ )
		{
			if( !bits[ i ] )
				continue;
			if( !first )
				out += separator;
			out += names[ i ];
			out += " ON";
			first = false;
		}
		if( out.empty() )
			out = none_text;
		return out;
	}
}

namespace Pcb085Profile
{
	std::string AddressBitString( U8 address5 )
	{
		return BitString( address5, 5 );
	}

	std::string DataBitString( U8 data4 )
	{
		return BitString( data4, 4 );
	}

	Result Interpret( U8 address5, U8 data4 )
	{
		Result r;
		r.known = false;

		bool d1 = ( data4 >> 3 ) & 1;
		bool d2 = ( data4 >> 2 ) & 1;
		bool d3 = ( data4 >> 1 ) & 1;
		bool d4 = ( data4 >> 0 ) & 1;

		char buf[ 256 ];

		switch( address5 )
		{
		case kAddrOutputs1to4:
		{
			// docs/PCB085_ANALYSIS.md section 5. CONFIRMED, including a blind-validation
			// pass (section 10/23) where this mapping alone predicted machine state
			// exactly before the setup was revealed.
			r.known = true;
			r.confidence = "CONFIRMED";
			bool bits[ 4 ] = { d1, d2, d3, d4 };
			static const char* const kNames[ 4 ] = { "Alarm", "Valve 1", "Valve 2", "Process Blower" };
			r.summary = FormatOnList( bits, kNames, ", ", "(Alarm/Valve 1/Valve 2/Process Blower all OFF)" );
			r.detail = FormatOnList( bits, kNames, "\n", "(Alarm/Valve 1/Valve 2/Process Blower all OFF)" );
			break;
		}

		case kAddrOutputs5to8:
		{
			// docs/PCB085_ANALYSIS.md section 6-9. CONFIRMED (each of D2/D3/D4 was
			// individually isolated against a machine-commanded change).
			r.known = true;
			r.confidence = "CONFIRMED";
			bool bits[ 4 ] = { d1, d2, d3, d4 };
			static const char* const kNames[ 4 ] = { "Regen Blower", "Regen Heater", "Isolation Valve", "Process Heater" };
			r.summary = FormatOnList( bits, kNames, ", ", "(Regen Blower/Regen Heater/Isolation Valve/Process Heater all OFF)" );
			r.detail = FormatOnList( bits, kNames, "\n", "(Regen Blower/Regen Heater/Isolation Valve/Process Heater all OFF)" );
			break;
		}

		case kAddrAnalogValue:
		{
			// docs/PCB085_ANALYSIS.md section 12-13. Data is transmitted LSB-first:
			// D1=bit0 .. D4=bit3 -- the reverse convention from the address/output
			// fields above, so do not reuse the same bit-order helper for this.
			U8 code = ( d1 ? 1 : 0 ) | ( d2 ? 2 : 0 ) | ( d3 ? 4 : 0 ) | ( d4 ? 8 : 0 );
			double approx_percent = ( double( code ) * 100.0 ) / 15.0;
			double approx_ma = 4.0 + double( code ) * ( 16.0 / 15.0 );

			r.known = true;
			r.confidence = "CONFIRMED / STRONG EVIDENCE";
			snprintf( buf, sizeof( buf ), "Analog code %u (~%.0f%%, ~%.2fmA)", code, approx_percent, approx_ma );
			r.summary = buf;
			snprintf( buf, sizeof( buf ),
				"Analog code %u | Approx. requested ~%.0f%% | Approx. 4-20mA ~%.2f mA "
				"(working model -- code = floor(percent*15/100); current not independently measured against real output, PCB085_ANALYSIS.md #13)",
				code, approx_percent, approx_ma );
			r.detail = buf;
			break;
		}

		case kAddrAnalogCompanion:
			// docs/PCB085_ANALYSIS.md section 14. STRONG EVIDENCE / NOT FULLY CONFIRMED --
			// an even analog code capture is still needed to properly challenge this rule.
			// Deliberately not decoded into a value here so this experimental status can't
			// be silently promoted to a confident-looking number in the UI.
			r.known = true;
			r.confidence = "STRONG EVIDENCE / NOT FULLY CONFIRMED";
			r.summary = "Analog companion (experimental, see PCB085_ANALYSIS.md #14)";
			r.detail = r.summary;
			break;

		case kAddrUnmappedBank1:
		case kAddrUnmappedBank2:
			// docs/PCB085_ANALYSIS.md section 15. Recognized as a recurring PCB-085
			// address, but CLAUDE.md forbids assigning outputs 9-16 without a capture --
			// report the raw data only, no output guess.
			r.known = true;
			r.confidence = "UNKNOWN";
			r.summary = "Recognized PCB-085 address, output bank not yet mapped (candidate: outputs 9-16)";
			r.detail = r.summary;
			break;

		default:
			r.known = false;
			r.confidence = "UNKNOWN";
			r.summary = "Address not recognized by the PCB-085 profile";
			r.detail = r.summary;
			break;
		}

		return r;
	}
}
