//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX11 texture system: handle table, creation/upload with paired
// linear/sRGB SRVs, per-texture sampler state (M3).
//
//===========================================================================//

#ifndef TEXTUREDX11_H
#define TEXTUREDX11_H

#ifdef _WIN32
#pragma once
#endif

#include "shaderapi/ishaderapi.h"
#include "bitmap/imageformat.h"

struct ID3D11ShaderResourceView;
struct ID3D11SamplerState;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;

void TextureDx11_CreateTextures( ShaderAPITextureHandle_t *pHandles, int nCount, int nWidth, int nHeight,
	int nDepth, ImageFormat dstFormat, int nMipLevels, int nCopies, int nFlags, const char *pDebugName );
void TextureDx11_Delete( ShaderAPITextureHandle_t hTexture );
bool TextureDx11_IsValid( ShaderAPITextureHandle_t hTexture );

void TextureDx11_Modify( ShaderAPITextureHandle_t hTexture );
void TextureDx11_Image2D( int nLevel, int nCubeFace, ImageFormat dstFormat, int nWidth, int nHeight,
	ImageFormat srcFormat, void *pData, int nZOffset = 0 );
class IVTFTexture;
void TextureDx11_ImageFromVTF( IVTFTexture *pVTF, int nFrame );
void TextureDx11_SubImage2D( int nLevel, int nCubeFace, int nXOffset, int nYOffset, int nWidth, int nHeight,
	ImageFormat srcFormat, int nSrcStride, void *pData );
class CPixelWriter;
bool TextureDx11_TexLock( int nLevel, int nCubeFace, int nXOffset, int nYOffset,
	int nWidth, int nHeight, CPixelWriter &writer );
void TextureDx11_TexUnlock();
void TextureDx11_MinFilter( ShaderTexFilterMode_t mode );
void TextureDx11_MagFilter( ShaderTexFilterMode_t mode );
void TextureDx11_Wrap( ShaderTexCoordComponent_t coord, ShaderTexWrapMode_t mode );

// bAllowCube: only slots whose shader declares TextureCube (phong envmap s8)
// may receive a cube SRV; everywhere else cubes fall back to white.
ID3D11ShaderResourceView *TextureDx11_GetSRV( ShaderAPITextureHandle_t hTexture, bool bSRGB, bool bAllowCube = false );
ID3D11SamplerState *TextureDx11_GetSamplerState( ShaderAPITextureHandle_t hTexture );
// NULL unless the texture was created with TEXTURE_CREATE_RENDERTARGET
ID3D11RenderTargetView *TextureDx11_GetRTV( ShaderAPITextureHandle_t hTexture, bool bSRGB );
bool TextureDx11_GetDims( ShaderAPITextureHandle_t hTexture, int &nWidth, int &nHeight );
bool TextureDx11_IsRenderTarget( ShaderAPITextureHandle_t hTexture );
// true when the texture was created with depth > 1 (Texture3D resource)
bool TextureDx11_IsVolume( ShaderAPITextureHandle_t hTexture );

// Shadow depth-map textures (flashlight depth stage): created when the image
// format is one of the dx9 vendor depth-texture formats (NV_DST24 et al) —
// R24G8_TYPELESS resource bound DEPTH_STENCIL|SHADER_RESOURCE, sampled
// through an R24-UNORM SRV (raw depth in .r; the shader compares manually).
// CreateDepth is the IShaderAPI::CreateDepthTexture entry (separate zbuffers
// for RTs that want one).
ShaderAPITextureHandle_t TextureDx11_CreateDepth( int nWidth, int nHeight, const char *pDebugName );
bool TextureDx11_IsDepth( ShaderAPITextureHandle_t hTexture );
ID3D11DepthStencilView *TextureDx11_GetDSV( ShaderAPITextureHandle_t hTexture );

// 1x1 white fallback for unbound/disabled samplers
ID3D11ShaderResourceView *TextureDx11_GetWhiteSRV();
// 1x1 opaque black (additive-term silencer, e.g. suppressed glint RT)
ID3D11ShaderResourceView *TextureDx11_GetBlackSRV();
// 1x1x6 white cube for slots the shader declares as TextureCube
ID3D11ShaderResourceView *TextureDx11_GetWhiteCubeSRV();
ID3D11SamplerState *TextureDx11_GetDefaultSampler();

// Standard textures (TEXTURE_WHITE/BLACK/GREY/NORMALMAP_FLAT...) live in a
// reserved negative handle range so they flow through the normal bind path.
ShaderAPITextureHandle_t TextureDx11_GetStandardHandle( int nStdTextureId );

void TextureDx11_ReleaseDevice();

#endif // TEXTUREDX11_H
