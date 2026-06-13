//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Module globals for the DX11 shaderapi backend.
//
//===========================================================================//

#ifndef SHADERAPIDX11_GLOBAL_H
#define SHADERAPIDX11_GLOBAL_H

#ifdef _WIN32
#pragma once
#endif

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;

class CShaderAPIBase;
class IShaderShadow;
class IMesh;
class IVertexBuffer;
class IIndexBuffer;

//-----------------------------------------------------------------------------
// D3D11 device globals, owned by CShaderDeviceDx11. Valid between
// InitDevice/ShutdownDevice; DX11 implementation files only.
//-----------------------------------------------------------------------------
extern ID3D11Device *g_pD3D11Device;
extern ID3D11DeviceContext *g_pD3D11Context;
extern IDXGISwapChain *g_pD3D11SwapChain;
extern ID3D11RenderTargetView *g_pD3D11RTV;
extern ID3D11RenderTargetView *g_pD3D11RTV_SRGB;
extern ID3D11DepthStencilView *g_pD3D11DSV;

inline ID3D11Device* D3D11Device() { return g_pD3D11Device; }
inline ID3D11DeviceContext* D3D11Context() { return g_pD3D11Context; }

//-----------------------------------------------------------------------------
// Accessors into the (still stubbed) shader API objects, used by the device
// manager when wiring up the module globals at SetMode time. Implemented in
// shaderapidx11_stub.cpp until the real implementations land (M3+).
//-----------------------------------------------------------------------------
CShaderAPIBase *ShaderApiDx11_GetShaderAPI();
IShaderShadow *ShaderApiDx11_GetShaderShadow();
IMesh *ShaderApiDx11_GetStubMesh( bool bDynamic );
IVertexBuffer *ShaderApiDx11_GetStubVertexBuffer();
IIndexBuffer *ShaderApiDx11_GetStubIndexBuffer( bool bDynamic );

// dx8-parity lightmap scale by HDR type (LDR = GammaToLinearFullRange(2))
float ShaderApiDx11_LightMapScaleFactor();

// RenderDoc-facing debug helpers (shaderdevicedx11.cpp): object names in the
// resource browser + ID3DUserDefinedAnnotation for pass markers.
struct ID3D11DeviceChild;
struct ID3DUserDefinedAnnotation;
void Dx11_SetDebugName( ID3D11DeviceChild *pObject, const char *pName );
ID3DUserDefinedAnnotation *Dx11_Annotation();

#endif // SHADERAPIDX11_GLOBAL_H
