// SPDX-License-Identifier: MIT

#include "box2d/box2d.h"
#include "box2d/math_functions.h"

#include "replay_parse.h"

#include <inttypes.h>
#include <locale.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

static FILE* witnessFile = NULL;

static void UpdateFnv1a64( uint64_t* hash, const char* text )
{
	const unsigned char* bytes = (const unsigned char*)text;
	while ( *bytes != '\0' )
	{
		*hash ^= (uint64_t)*bytes;
		*hash *= UINT64_C( 1099511628211 );
		bytes += 1;
	}
}

static int EmitWitnessLine( uint64_t* hash, const char* format, ... )
{
	char buffer[256];

	va_list args;
	va_start( args, format );
	int count = vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );

	if ( count < 0 || count >= (int)sizeof( buffer ) )
	{
		return 1;
	}

	if ( fputs( buffer, witnessFile ) == EOF )
	{
		return 1;
	}

	UpdateFnv1a64( hash, buffer );
	return 0;
}

static const char* GetShapeLabel( b2ShapeId shapeId )
{
	const char* label = (const char*)b2Shape_GetUserData( shapeId );
	return label != NULL ? label : "unknown";
}

static int EmitContactWitnessEvents( uint64_t* hash, b2WorldId worldId, int frame )
{
	b2ContactEvents events = b2World_GetContactEvents( worldId );

	for ( int i = 0; i < events.beginCount; ++i )
	{
		const b2ContactBeginTouchEvent* event = events.beginEvents + i;
		if ( EmitWitnessLine( hash, "contact_begin,%d,%s,%s\n", frame, GetShapeLabel( event->shapeIdA ),
							  GetShapeLabel( event->shapeIdB ) ) )
		{
			return 1;
		}
	}

	for ( int i = 0; i < events.endCount; ++i )
	{
		const b2ContactEndTouchEvent* event = events.endEvents + i;
		if ( EmitWitnessLine( hash, "contact_end,%d,%s,%s\n", frame, GetShapeLabel( event->shapeIdA ),
							  GetShapeLabel( event->shapeIdB ) ) )
		{
			return 1;
		}
	}

	for ( int i = 0; i < events.hitCount; ++i )
	{
		const b2ContactHitEvent* event = events.hitEvents + i;
		if ( EmitWitnessLine( hash, "contact_hit,%d,%s,%s,%.9g,%.9g,%.9g,%.9g,%.9g\n", frame,
							  GetShapeLabel( event->shapeIdA ), GetShapeLabel( event->shapeIdB ),
							  (double)event->normal.x, (double)event->normal.y, (double)event->point.x,
							  (double)event->point.y, (double)event->approachSpeed ) )
		{
			return 1;
		}
	}

	return 0;
}

static int BuildScenarioWorld( const ReplayScenario* scenario, b2WorldId* worldId, b2BodyId* witnessBodyId )
{
	b2WorldDef worldDef = b2DefaultWorldDef();
	worldDef.gravity = (b2Vec2){ scenario->gravityX, scenario->gravityY };
	worldDef.workerCount = 1;

	*worldId = b2CreateWorld( &worldDef );
	if ( b2World_IsValid( *worldId ) == false )
	{
		return 1;
	}

	*witnessBodyId = b2_nullBodyId;

	for ( int i = 0; i < scenario->boxCount; ++i )
	{
		const ReplayBox* box = scenario->boxes + i;

		b2BodyDef bodyDef = b2DefaultBodyDef();
		bodyDef.type = box->type == REPLAY_DYNAMIC_BOX ? b2_dynamicBody : b2_staticBody;
		bodyDef.position = (b2Pos){ box->centerX, box->centerY };

		b2BodyId bodyId = b2CreateBody( *worldId, &bodyDef );
		if ( b2Body_IsValid( bodyId ) == false )
		{
			return 1;
		}

		b2Polygon shape = b2MakeBox( box->halfX, box->halfY );
		b2ShapeDef shapeDef = b2DefaultShapeDef();
		shapeDef.userData = (void*)box->name;
		shapeDef.density = box->density;
		shapeDef.enableContactEvents = true;
		shapeDef.enableHitEvents = true;

		b2ShapeId shapeId = b2CreatePolygonShape( bodyId, &shapeDef, &shape );
		if ( b2Shape_IsValid( shapeId ) == false )
		{
			return 1;
		}

		if ( box->type == REPLAY_DYNAMIC_BOX && b2Body_IsValid( *witnessBodyId ) == false )
		{
			*witnessBodyId = bodyId;
		}
	}

	return b2Body_IsValid( *witnessBodyId ) ? 0 : 1;
}

int main( int argc, char** argv )
{
	setlocale( LC_ALL, "C" );

	if ( argc != 2 && argc != 3 )
	{
		fprintf( stderr, "usage: headless_replay <scenario.replay> [output.witness]\n" );
		return 1;
	}

	const char* scenarioPath = argv[1];
	witnessFile = stdout;
	if ( argc == 3 )
	{
		witnessFile = fopen( argv[2], "wb" );
		if ( witnessFile == NULL )
		{
			fprintf( stderr, "failed to open witness output '%s'\n", argv[2] );
			return 1;
		}
	}

	ReplayScenario scenario;
	char parseError[256];
	if ( ReplayParseScenario( scenarioPath, &scenario, parseError, sizeof( parseError ) ) != 0 )
	{
		fprintf( stderr, "failed to parse scenario: %s\n", parseError );
		if ( witnessFile != stdout )
		{
			fclose( witnessFile );
		}
		return 1;
	}

	b2WorldId worldId = b2_nullWorldId;
	b2BodyId boxId = b2_nullBodyId;
	if ( BuildScenarioWorld( &scenario, &worldId, &boxId ) != 0 )
	{
		fprintf( stderr, "failed to build scenario '%s'\n", scenario.name );
		if ( b2World_IsValid( worldId ) )
		{
			b2DestroyWorld( worldId );
		}
		if ( witnessFile != stdout )
		{
			fclose( witnessFile );
		}
		return 1;
	}

	uint64_t digest = UINT64_C( 14695981039346656037 );

	if ( EmitWitnessLine( &digest, "scenario=%s\n", scenario.name ) ||
		 EmitWitnessLine( &digest, "dt=%.9f\n", scenario.dt ) ||
		 EmitWitnessLine( &digest, "substeps=%d\n", scenario.substeps ) ||
		 EmitWitnessLine( &digest, "frames=%d\n", scenario.frames ) ||
		 EmitWitnessLine( &digest, "pose_columns=frame,x,y,angle,vx,vy,angular_velocity\n" ) ||
		 EmitWitnessLine( &digest, "contact_begin_columns=event,frame,shape_a,shape_b\n" ) ||
		 EmitWitnessLine( &digest, "contact_end_columns=event,frame,shape_a,shape_b\n" ) ||
		 EmitWitnessLine( &digest,
						  "contact_hit_columns=event,frame,shape_a,shape_b,normal_x,normal_y,point_x,point_y,approach_speed\n" ) )
	{
		fprintf( stderr, "failed to emit witness metadata\n" );
		b2DestroyWorld( worldId );
		if ( witnessFile != stdout )
		{
			fclose( witnessFile );
		}
		return 1;
	}

	for ( int frame = 0; frame < scenario.frames; ++frame )
	{
		b2World_Step( worldId, (float)scenario.dt, scenario.substeps );

		b2Pos position = b2Body_GetPosition( boxId );
		b2Rot rotation = b2Body_GetRotation( boxId );
		b2Vec2 linearVelocity = b2Body_GetLinearVelocity( boxId );
		float angularVelocity = b2Body_GetAngularVelocity( boxId );

		if ( EmitWitnessLine( &digest, "%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n", frame, (double)position.x, (double)position.y,
							  (double)b2Rot_GetAngle( rotation ), (double)linearVelocity.x, (double)linearVelocity.y,
							  (double)angularVelocity ) )
		{
			fprintf( stderr, "failed to emit witness row\n" );
			b2DestroyWorld( worldId );
			if ( witnessFile != stdout )
			{
				fclose( witnessFile );
			}
			return 1;
		}

		if ( EmitContactWitnessEvents( &digest, worldId, frame ) )
		{
			fprintf( stderr, "failed to emit contact witness row\n" );
			b2DestroyWorld( worldId );
			if ( witnessFile != stdout )
			{
				fclose( witnessFile );
			}
			return 1;
		}
	}

	if ( fprintf( witnessFile, "digest=fnv1a64:%016" PRIx64 "\n", digest ) < 0 )
	{
		fprintf( stderr, "failed to emit witness digest\n" );
		b2DestroyWorld( worldId );
		if ( witnessFile != stdout )
		{
			fclose( witnessFile );
		}
		return 1;
	}

	b2DestroyWorld( worldId );
	if ( witnessFile != stdout && fclose( witnessFile ) != 0 )
	{
		fprintf( stderr, "failed to close witness output\n" );
		return 1;
	}

	return 0;
}
