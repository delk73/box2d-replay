// SPDX-License-Identifier: MIT

#include "replay_parse.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	maxLineLength = 256,
	maxTokens = 12,
	maxFrames = 10000,
	maxSubsteps = 64,
};

static int SetError( char* error, size_t errorSize, const char* format, ... )
{
	if ( errorSize > 0 )
	{
		va_list args;
		va_start( args, format );
		vsnprintf( error, errorSize, format, args );
		va_end( args );
	}

	return 1;
}

static bool CopyName( char* dest, size_t destSize, const char* source )
{
	size_t length = strlen( source );
	if ( length == 0 || length >= destSize )
	{
		return false;
	}

	memcpy( dest, source, length + 1 );
	return true;
}

static bool ParseInt( const char* text, int* value )
{
	char* end = NULL;
	errno = 0;
	long parsed = strtol( text, &end, 10 );
	if ( errno != 0 || end == text || *end != '\0' || parsed < -2147483647L - 1L || parsed > 2147483647L )
	{
		return false;
	}

	*value = (int)parsed;
	return true;
}

static bool ParseDouble( const char* text, double* value )
{
	char* end = NULL;
	errno = 0;
	double parsed = strtod( text, &end );
	if ( errno != 0 || end == text || *end != '\0' || isfinite( parsed ) == false )
	{
		return false;
	}

	*value = parsed;
	return true;
}

static bool ParseFloat( const char* text, float* value )
{
	double parsed = 0.0;
	if ( ParseDouble( text, &parsed ) == false )
	{
		return false;
	}

	*value = (float)parsed;
	return isfinite( *value );
}

static bool IsReplaySpace( char c )
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int TokenizeLine( char* line, char** tokens )
{
	char* comment = strchr( line, '#' );
	if ( comment != NULL )
	{
		*comment = '\0';
	}

	int count = 0;
	char* cursor = line;
	while ( *cursor != '\0' )
	{
		while ( IsReplaySpace( *cursor ) )
		{
			cursor += 1;
		}

		if ( *cursor == '\0' )
		{
			break;
		}

		if ( count == maxTokens )
		{
			return -1;
		}

		tokens[count] = cursor;
		count += 1;

		while ( *cursor != '\0' && IsReplaySpace( *cursor ) == false )
		{
			cursor += 1;
		}

		if ( *cursor != '\0' )
		{
			*cursor = '\0';
			cursor += 1;
		}
	}

	return count;
}

static FILE* OpenScenarioFile( const char* path )
{
#if defined( _MSC_VER )
	FILE* file = NULL;
	if ( fopen_s( &file, path, "r" ) != 0 )
	{
		return NULL;
	}
	return file;
#else
	return fopen( path, "r" );
#endif
}

static int ParseBox( ReplayScenario* scenario, char** tokens, int tokenCount, ReplayBoxType type, int lineNumber, char* error,
					 size_t errorSize )
{
	int expected = type == REPLAY_DYNAMIC_BOX ? 7 : 6;
	if ( tokenCount != expected )
	{
		return SetError( error, errorSize, "line %d: malformed %s", lineNumber,
					 type == REPLAY_DYNAMIC_BOX ? "dynamic_box" : "static_box" );
	}

	if ( scenario->boxCount >= REPLAY_MAX_BOXES )
	{
		return SetError( error, errorSize, "line %d: too many box definitions", lineNumber );
	}

	ReplayBox* box = scenario->boxes + scenario->boxCount;
	*box = (ReplayBox){ 0 };
	box->type = type;

	if ( CopyName( box->name, sizeof( box->name ), tokens[1] ) == false || ParseFloat( tokens[2], &box->centerX ) == false ||
		 ParseFloat( tokens[3], &box->centerY ) == false || ParseFloat( tokens[4], &box->halfX ) == false ||
		 ParseFloat( tokens[5], &box->halfY ) == false )
	{
		return SetError( error, errorSize, "line %d: invalid %s value", lineNumber,
					 type == REPLAY_DYNAMIC_BOX ? "dynamic_box" : "static_box" );
	}

	if ( box->halfX <= 0.0f || box->halfY <= 0.0f )
	{
		return SetError( error, errorSize, "line %d: invalid box dimensions", lineNumber );
	}

	if ( type == REPLAY_DYNAMIC_BOX )
	{
		if ( ParseFloat( tokens[6], &box->density ) == false )
		{
			return SetError( error, errorSize, "line %d: invalid dynamic_box density", lineNumber );
		}

		if ( box->density <= 0.0f )
		{
			return SetError( error, errorSize, "line %d: invalid dynamic_box density", lineNumber );
		}
	}

	scenario->boxCount += 1;
	return 0;
}

int ReplayParseScenario( const char* path, ReplayScenario* scenario, char* error, size_t errorSize )
{
	FILE* file = OpenScenarioFile( path );
	if ( file == NULL )
	{
		return SetError( error, errorSize, "%s: unable to open scenario", path );
	}

	*scenario = (ReplayScenario){ 0 };

	bool sawScenario = false;
	bool sawFrames = false;
	bool sawDt = false;
	bool sawSubsteps = false;
	bool sawGravity = false;

	char line[maxLineLength];
	int lineNumber = 0;
	while ( fgets( line, sizeof( line ), file ) != NULL )
	{
		lineNumber += 1;

		if ( strchr( line, '\n' ) == NULL && feof( file ) == 0 )
		{
			fclose( file );
			return SetError( error, errorSize, "line %d: line too long", lineNumber );
		}

		char* tokens[maxTokens];
		int tokenCount = TokenizeLine( line, tokens );
		if ( tokenCount < 0 )
		{
			fclose( file );
			return SetError( error, errorSize, "line %d: too many tokens", lineNumber );
		}

		if ( tokenCount == 0 )
		{
			continue;
		}

		if ( strcmp( tokens[0], "scenario" ) == 0 )
		{
			if ( tokenCount != 2 || CopyName( scenario->name, sizeof( scenario->name ), tokens[1] ) == false )
			{
				fclose( file );
				return SetError( error, errorSize, "line %d: malformed scenario", lineNumber );
			}
			sawScenario = true;
		}
		else if ( strcmp( tokens[0], "frames" ) == 0 )
		{
			if ( tokenCount != 2 || ParseInt( tokens[1], &scenario->frames ) == false || scenario->frames <= 0 ||
				scenario->frames > maxFrames )
			{
				fclose( file );
				return SetError( error, errorSize, "line %d: malformed frames", lineNumber );
			}
			sawFrames = true;
		}
		else if ( strcmp( tokens[0], "dt" ) == 0 )
		{
			if ( tokenCount != 2 || ParseDouble( tokens[1], &scenario->dt ) == false || scenario->dt <= 0.0 )
			{
				fclose( file );
				return SetError( error, errorSize, "line %d: malformed dt", lineNumber );
			}
			sawDt = true;
		}
		else if ( strcmp( tokens[0], "substeps" ) == 0 )
		{
			if ( tokenCount != 2 || ParseInt( tokens[1], &scenario->substeps ) == false || scenario->substeps <= 0 ||
				scenario->substeps > maxSubsteps )
			{
				fclose( file );
				return SetError( error, errorSize, "line %d: malformed substeps", lineNumber );
			}
			sawSubsteps = true;
		}
		else if ( strcmp( tokens[0], "gravity" ) == 0 )
		{
			if ( tokenCount != 3 || ParseFloat( tokens[1], &scenario->gravityX ) == false ||
				 ParseFloat( tokens[2], &scenario->gravityY ) == false )
			{
				fclose( file );
				return SetError( error, errorSize, "line %d: malformed gravity", lineNumber );
			}
			sawGravity = true;
		}
		else if ( strcmp( tokens[0], "static_box" ) == 0 )
		{
			if ( ParseBox( scenario, tokens, tokenCount, REPLAY_STATIC_BOX, lineNumber, error, errorSize ) != 0 )
			{
				fclose( file );
				return 1;
			}
		}
		else if ( strcmp( tokens[0], "dynamic_box" ) == 0 )
		{
			if ( ParseBox( scenario, tokens, tokenCount, REPLAY_DYNAMIC_BOX, lineNumber, error, errorSize ) != 0 )
			{
				fclose( file );
				return 1;
			}
		}
		else
		{
			fclose( file );
			return SetError( error, errorSize, "line %d: unknown directive '%s'", lineNumber, tokens[0] );
		}
	}

	if ( fclose( file ) != 0 )
	{
		return SetError( error, errorSize, "%s: failed to close scenario", path );
	}

	if ( sawScenario == false || sawFrames == false || sawDt == false || sawSubsteps == false || sawGravity == false )
	{
		return SetError( error, errorSize, "%s: missing required scenario fields", path );
	}

	if ( scenario->boxCount == 0 )
	{
		return SetError( error, errorSize, "%s: no boxes defined", path );
	}

	return 0;
}
