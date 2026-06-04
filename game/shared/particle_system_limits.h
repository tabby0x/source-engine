//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Shared particle system network limits.
//
//=============================================================================//

#ifndef PARTICLE_SYSTEM_LIMITS_H
#define PARTICLE_SYSTEM_LIMITS_H
#ifdef _WIN32
#pragma once
#endif

// Modern TF2 content exceeds the old particle string tables once retail PCFs,
// item effects, unusuals, taunts, muzzle flashes, and impact effects are loaded.
// Keep this in sync with any network field that serializes particle indices.
#define MAX_PARTICLESYSTEMS_STRING_BITS		15
#define MAX_PARTICLESYSTEMS_STRINGS			( 1 << MAX_PARTICLESYSTEMS_STRING_BITS )
#define PARTICLESYSTEMS_INVALID_STRING		( MAX_PARTICLESYSTEMS_STRINGS - 1 )

#endif // PARTICLE_SYSTEM_LIMITS_H
