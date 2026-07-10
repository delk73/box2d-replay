// SPDX-License-Identifier: MIT

#pragma once

#include <stddef.h>

enum
{
	REPLAY_MAX_NAME = 32,
	REPLAY_MAX_BOXES = 8,
};

typedef enum ReplayBoxType
{
	REPLAY_STATIC_BOX,
	REPLAY_DYNAMIC_BOX,
} ReplayBoxType;

typedef struct ReplayBox
{
	ReplayBoxType type;
	char name[REPLAY_MAX_NAME];
	float centerX;
	float centerY;
	float halfX;
	float halfY;
	float density;
} ReplayBox;

typedef struct ReplayScenario
{
	char name[REPLAY_MAX_NAME];
	int frames;
	double dt;
	int substeps;
	float gravityX;
	float gravityY;
	ReplayBox boxes[REPLAY_MAX_BOXES];
	int boxCount;
} ReplayScenario;

int ReplayParseScenario( const char* path, ReplayScenario* scenario, char* error, size_t errorSize );
