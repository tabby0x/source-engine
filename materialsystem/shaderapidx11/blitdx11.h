//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX11 stretch-blit between the active render target and textures —
// the StretchRect replacement behind CopyRenderTargetToTexture(Ex) and
// CopyTextureToRenderTargetEx (screen-effect/_rt_FullFrameFB updates).
//
//===========================================================================//

#ifndef BLITDX11_H
#define BLITDX11_H

#ifdef _WIN32
#pragma once
#endif

#include "shaderapi/ishaderapi.h"

// Active render target (backbuffer or texture RT) -> dest texture.
// NULL rects = full surface. Scales + converts via a sampling draw.
void BlitDx11_RTToTexture( ShaderAPITextureHandle_t hDst, const Rect_t *pSrcRect, const Rect_t *pDstRect );

// Texture -> active render target (the reverse path).
void BlitDx11_TextureToRT( ShaderAPITextureHandle_t hSrc, const Rect_t *pSrcRect, const Rect_t *pDstRect );

void BlitDx11_ReleaseDevice();

#endif // BLITDX11_H
