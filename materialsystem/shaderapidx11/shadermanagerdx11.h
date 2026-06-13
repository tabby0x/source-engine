//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX11 shader pack loading + the M2 toolchain-spike debug triangle.
//
//===========================================================================//

#ifndef SHADERMANAGERDX11_H
#define SHADERMANAGERDX11_H

#ifdef _WIN32
#pragma once
#endif

#include "tier1/utlbuffer.h"

// Loads one permutation's raw DXBC out of <game>/shaders/dx11/<pack>.vcsx (v1).
bool ShaderPackDx11_LoadBlob( const char *pPackName, uint64 nKey, CUtlBuffer &blob );

// Compiles HLSL from an absolute/relative disk path via d3dcompiler_47.
// Optional single #define (e.g. MODE=n for the universal permutations).
bool ShaderDx11_CompileFromSource( const char *pHlslPath, const char *pEntry, const char *pTarget, CUtlBuffer &blob,
	const char *pDefineName = 0, int nDefineValue = 0 );

// M2 acceptance harness: with -dx11triangle, draws a fullscreen-ish triangle
// through pack-loaded shaders each frame; with -dx11hotreload, watches the
// HLSL source mtime and recompiles live. Called from CShaderDeviceDx11::Present.
void DebugTriangleDx11_DrawIfEnabled();
void DebugTriangleDx11_Shutdown();

#endif // SHADERMANAGERDX11_H
