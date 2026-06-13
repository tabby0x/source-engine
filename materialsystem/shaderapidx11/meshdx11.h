//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX11 dynamic mesh + ring buffers (M3).
//
//===========================================================================//

#ifndef MESHDX11_H
#define MESHDX11_H

#ifdef _WIN32
#pragma once
#endif

#include "materialsystem/imesh.h"

class IMaterial;

IMesh *MeshDx11_GetDynamic( IMaterial *pMaterial, VertexFormat_t fmtOverride );
// Index-only dynamic mesh over a static mesh's vertices (world batch path);
// returns NULL if pVertexOverride is not one of our static meshes.
IMesh *MeshDx11_GetDynamicWithVertexOverride( IMesh *pVertexOverride );
// Vertex-only dynamic mesh drawn through a static mesh's strip indices (the
// old-format studio flex/SW-skin and eyeball paths rebuild a group's vertices
// each frame); returns NULL if pIndexOverride is not one of our static meshes.
IMesh *MeshDx11_GetDynamicWithIndexOverride( IMaterial *pMaterial, VertexFormat_t fmtOverride, IMesh *pIndexOverride );
// BindBatch: retarget the vertex override while keeping the dynamic mesh's
// ring-IB window from the preceding index-only build lock; returns NULL if
// the overrides aren't the world batch pattern.
IMesh *MeshDx11_BindBatch( IMesh *pVertexOverride, IMesh *pIndexOverride );
IMesh *MeshDx11_CreateStatic( VertexFormat_t fmt, IMaterial *pMaterial );
void MeshDx11_DestroyStatic( IMesh *pMesh );
// Studio facial flex deltas (dx9 stream 2): studiorender fills this mesh per
// flexed group and attaches it to the studio mesh via IMesh::SetFlexMesh.
IMesh *MeshDx11_GetFlexMesh();
int MeshDx11_DynamicVBSize();
void MeshDx11_ReleaseDevice();

// Material pass plumbing (mirrors the dx9 flow): the bound material's pass
// loop runs inside IMesh::Draw, and each RenderPass() issues the GPU draw.
void MeshDx11_BindMaterial( IMaterial *pMaterial );
void MeshDx11_RenderPass();
bool MeshDx11_RenderMeshHasColorMesh();	// dx9 LightState_t::m_bStaticLightVertex

#endif // MESHDX11_H
