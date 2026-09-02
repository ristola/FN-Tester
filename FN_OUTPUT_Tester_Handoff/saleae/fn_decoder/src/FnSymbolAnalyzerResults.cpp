#include "FnSymbolAnalyzerResults.h"
#include <AnalyzerHelpers.h>
#include "FnSymbolAnalyzer.h"
#include "FnSymbolAnalyzerSettings.h"
#include "Pcb085Profile.h"
#include <cstdio>
#include <iostream>
#include <fstream>

namespace
{
	// Unpacks a kFnFrameWord's mData1/mData2 9-bit-word encoding: bit 8 = A1 (first
	// field decoded) down to bit 0 = D4 (last field decoded) -- see FnSymbolAnalyzer.cpp.
	void UnpackFnWord( U32 packed, U8& address5, U8& data4 )
	{
		address5 = ( U8 )( ( packed >> 4 ) & 0x1F );
		data4 = ( U8 )( packed & 0x0F );
	}

	std::string FormatFnWordRaw( U32 packed )
	{
		U8 address5, data4;
		UnpackFnWord( packed, address5, data4 );
		return Pcb085Profile::AddressBitString( address5 ) + " " + Pcb085Profile::DataBitString( data4 );
	}

	std::string FormatFnWordMedium( const Frame& frame )
	{
		U8 address5, data4;
		UnpackFnWord( ( U32 )frame.mData1, address5, data4 );

		std::string out = FormatFnWordRaw( ( U32 )frame.mData1 );

		Pcb085Profile::Result profile = Pcb085Profile::Interpret( address5, data4 );
		if( profile.known )
			out += "  " + profile.summary; // e.g. "Valve 1 ON, Process Blower ON" -- readable at a glance

		if( frame.mFlags & kFrameFlagSecondCopy )
			out += ( frame.mFlags & kFrameFlagCopiesMatch ) ? " [copy2 MATCH]" : " [copy2 MISMATCH]";
		else
			out += " [copy1]";
		if( frame.mFlags & kFrameFlagSymbolWarning )
			out += " (symbol warning)";
		return out;
	}

	std::string FormatFnWordFull( const Frame& frame )
	{
		U8 address5, data4;
		UnpackFnWord( ( U32 )frame.mData1, address5, data4 );

		std::string out = "Address " + Pcb085Profile::AddressBitString( address5 ) +
			"  Data " + Pcb085Profile::DataBitString( data4 );

		if( frame.mFlags & kFrameFlagSecondCopy )
		{
			if( frame.mFlags & kFrameFlagCopiesMatch )
			{
				out += "  [repeated-word copy 2/2: MATCH]";
			}
			else
			{
				U8 first_address5, first_data4;
				UnpackFnWord( ( U32 )frame.mData2, first_address5, first_data4 );
				out += "  [repeated-word copy 2/2: MISMATCH vs copy 1 (" +
					Pcb085Profile::AddressBitString( first_address5 ) + " " +
					Pcb085Profile::DataBitString( first_data4 ) + ")]";
			}
		}
		else
		{
			out += "  [repeated-word copy 1/2]";
		}

		if( frame.mFlags & kFrameFlagSymbolWarning )
			out += "  (one or more fields had an unexpected S/L pattern -- check raw-dwell mode)";

		Pcb085Profile::Result profile = Pcb085Profile::Interpret( address5, data4 );
		out += "  -- PCB-085 [" + profile.confidence + "]: " + profile.detail;

		return out;
	}

	std::string FormatFnError( const Frame& frame )
	{
		char buf[ 160 ];
		if( frame.mData2 == kFnErrorMalformedSymbol )
		{
			snprintf( buf, sizeof( buf ), "FIELD ERROR: unexpected S/L pattern (%u interval(s) collected)",
				( unsigned int )frame.mData1 );
		}
		else if( frame.mData2 == kFnErrorIncompleteWord )
		{
			snprintf( buf, sizeof( buf ), "FRAME ERROR: incomplete word (%u/9 bits before sync)",
				( unsigned int )frame.mData1 );
		}
		else
		{
			snprintf( buf, sizeof( buf ), "FRAME ERROR: unknown (code %u)", ( unsigned int )frame.mData2 );
		}
		return buf;
	}

	std::string CsvQuote( const std::string& s )
	{
		std::string out = "\"";
		for( size_t i = 0; i < s.size(); i++ )
		{
			if( s[ i ] == '"' )
				out += "\"\"";
			else
				out += s[ i ];
		}
		out += "\"";
		return out;
	}

	// Single-letter bin code, distinct from the "H"/"L" level letters used alongside it.
	const char* SymbolClassLabel( U8 type )
	{
		switch( ( FnSymbolClass )type )
		{
		case kFnSymbolNoise:
			return "n";
		case kFnSymbolShort:
			return "s";
		case kFnSymbolMedium:
			return "m";
		case kFnSymbolWide:
			return "w";
		case kFnSymbolLong:
			return "d"; // "dwell"
		default:
			return "?";
		}
	}

	const char* SymbolClassName( U8 type )
	{
		switch( ( FnSymbolClass )type )
		{
		case kFnSymbolNoise:
			return "noise";
		case kFnSymbolShort:
			return "short";
		case kFnSymbolMedium:
			return "medium";
		case kFnSymbolWide:
			return "wide";
		case kFnSymbolLong:
			return "dwell";
		default:
			return "unknown";
		}
	}

	void FormatDuration( double duration_us, char* out, size_t out_size )
	{
		if( duration_us < 1000.0 )
			snprintf( out, out_size, "%.2fus", duration_us );
		else
			snprintf( out, out_size, "%.3fms", duration_us / 1000.0 );
	}
}

FnSymbolAnalyzerResults::FnSymbolAnalyzerResults( FnSymbolAnalyzer* analyzer, FnSymbolAnalyzerSettings* settings )
:	AnalyzerResults(),
	mSettings( settings ),
	mAnalyzer( analyzer )
{
}

FnSymbolAnalyzerResults::~FnSymbolAnalyzerResults()
{
}

void FnSymbolAnalyzerResults::GenerateBubbleText( U64 frame_index, Channel& /*channel*/, DisplayBase /*display_base*/ )
{
	ClearResultStrings();
	Frame frame = GetFrame( frame_index );

	if( frame.mType == kFnFrameSync )
	{
		U32 sample_rate = mAnalyzer->GetSampleRate();
		double duration_us = ( double( frame.mData1 ) * 1000000.0 ) / double( sample_rate );
		char duration_str[ 64 ];
		FormatDuration( duration_us, duration_str, sizeof( duration_str ) );

		AddResultString( "SYNC" );

		char mid_str[ 80 ];
		snprintf( mid_str, sizeof( mid_str ), "SYNC %s", duration_str );
		AddResultString( mid_str );

		char full_str[ 128 ];
		snprintf( full_str, sizeof( full_str ), "SYNC %s (%s)", duration_str,
			( frame.mFlags & kFrameFlagSyncKnownBand ) ? "known band" : "outside previously observed sync bands" );
		AddResultString( full_str );
		return;
	}

	if( frame.mType == kFnFrameWord )
	{
		AddResultString( FormatFnWordRaw( ( U32 )frame.mData1 ).c_str() );
		AddResultString( FormatFnWordMedium( frame ).c_str() );
		AddResultString( FormatFnWordFull( frame ).c_str() );
		return;
	}

	if( frame.mType == kFnFrameError )
	{
		std::string err = FormatFnError( frame );
		AddResultString( "ERR" );
		AddResultString( err.c_str() );
		AddResultString( err.c_str() );
		return;
	}

	// frame.mType < 100: layer-2 raw-dwell bin (kFnDecodeRawDwell mode).
	U32 sample_rate = mAnalyzer->GetSampleRate();
	double duration_us = ( double( frame.mData1 ) * 1000000.0 ) / double( sample_rate );
	bool is_high = ( frame.mData2 != 0 );
	const char* level_str = is_high ? "H" : "L";
	const char* label = SymbolClassLabel( frame.mType );
	const char* class_name = SymbolClassName( frame.mType );

	char duration_str[ 64 ];
	FormatDuration( duration_us, duration_str, sizeof( duration_str ) );

	char short_str[ 16 ];
	snprintf( short_str, sizeof( short_str ), "%s%s", level_str, label );
	AddResultString( short_str );

	char mid_str[ 64 ];
	snprintf( mid_str, sizeof( mid_str ), "%s %s (%s)", level_str, duration_str, label );
	AddResultString( mid_str );

	char full_str[ 128 ];
	snprintf( full_str, sizeof( full_str ), "%s %s - %s", is_high ? "HIGH" : "LOW", duration_str, class_name );
	AddResultString( full_str );
}

void FnSymbolAnalyzerResults::GenerateExportFile( const char* file, DisplayBase display_base, U32 export_type_user_id )
{
	std::ofstream file_stream( file, std::ios::out );

	U64 trigger_sample = mAnalyzer->GetTriggerSample();
	U32 sample_rate = mAnalyzer->GetSampleRate();

	file_stream << "Time [s],Level,Duration (us),Class" << std::endl;

	U64 num_frames = GetNumFrames();
	for( U32 i=0; i < num_frames; i++ )
	{
		Frame frame = GetFrame( i );

		char time_str[128];
		AnalyzerHelpers::GetTimeString( frame.mStartingSampleInclusive, trigger_sample, sample_rate, time_str, 128 );

		if( frame.mType == kFnFrameSync )
		{
			double duration_us = ( double( frame.mData1 ) * 1000000.0 ) / double( sample_rate );
			std::string label = std::string( "SYNC" ) +
				( ( frame.mFlags & kFrameFlagSyncKnownBand ) ? " (known band)" : " (outside previously observed sync bands)" );
			file_stream << time_str << ",," << duration_us << "," << CsvQuote( label ) << std::endl;
		}
		else if( frame.mType == kFnFrameWord )
		{
			// No single dwell duration applies to a whole decoded word -- left blank.
			file_stream << time_str << ",,," << CsvQuote( FormatFnWordFull( frame ) ) << std::endl;
		}
		else if( frame.mType == kFnFrameError )
		{
			file_stream << time_str << ",,," << CsvQuote( FormatFnError( frame ) ) << std::endl;
		}
		else
		{
			double duration_us = ( double( frame.mData1 ) * 1000000.0 ) / double( sample_rate );
			file_stream << time_str << "," << ( frame.mData2 != 0 ? "H" : "L" ) << ","
				<< duration_us << "," << SymbolClassName( frame.mType ) << std::endl;
		}

		if( UpdateExportProgressAndCheckForCancel( i, num_frames ) == true )
		{
			file_stream.close();
			return;
		}
	}

	file_stream.close();
}

void FnSymbolAnalyzerResults::GenerateFrameTabularText( U64 frame_index, DisplayBase display_base )
{
	ClearResultStrings();
	Frame frame = GetFrame( frame_index );

	if( frame.mType == kFnFrameSync )
	{
		U32 sample_rate = mAnalyzer->GetSampleRate();
		double duration_us = ( double( frame.mData1 ) * 1000000.0 ) / double( sample_rate );
		char duration_str[ 64 ];
		FormatDuration( duration_us, duration_str, sizeof( duration_str ) );

		char str[ 128 ];
		snprintf( str, sizeof( str ), "SYNC %s (%s)", duration_str,
			( frame.mFlags & kFrameFlagSyncKnownBand ) ? "known band" : "outside known band" );
		AddResultString( str );
		return;
	}

	if( frame.mType == kFnFrameWord )
	{
		AddResultString( FormatFnWordFull( frame ).c_str() );
		return;
	}

	if( frame.mType == kFnFrameError )
	{
		AddResultString( FormatFnError( frame ).c_str() );
		return;
	}

	U32 sample_rate = mAnalyzer->GetSampleRate();
	double duration_us = ( double( frame.mData1 ) * 1000000.0 ) / double( sample_rate );

	char duration_str[ 64 ];
	FormatDuration( duration_us, duration_str, sizeof( duration_str ) );

	char str[ 128 ];
	snprintf( str, sizeof( str ), "%s %s (%s)", frame.mData2 != 0 ? "H" : "L", duration_str, SymbolClassName( frame.mType ) );
	AddResultString( str );
}

void FnSymbolAnalyzerResults::GeneratePacketTabularText( U64 packet_id, DisplayBase display_base )
{
	//not supported

}

void FnSymbolAnalyzerResults::GenerateTransactionTabularText( U64 transaction_id, DisplayBase display_base )
{
	//not supported
}
