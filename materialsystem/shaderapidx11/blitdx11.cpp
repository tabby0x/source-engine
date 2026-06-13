//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX11 stretch-blit (StretchRect replacement). The backbuffer can't
// be sampled (no SHADER_RESOURCE bind on the swapchain), so backbuffer reads
// go through a same-format intermediate copy; texture RTs sample directly.
// The draw itself is a layout-less fullscreen triangle with a UV remap.
//
//===========================================================================//

#include <d3d11.h>
#include <d3dcompiler.h>

#include "blitdx11.h"
#include "statedx11.h"
#include "texturedx11.h"
#include "shaderapidx11_global.h"
#include "shaderdevicedx11.h"

// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"

static const char s_szBlitHlsl[] =
	"cbuffer BlitParams : register(b0) { float4 g_UvScaleBias; }\n"
	"Texture2D g_Src : register(t0);\n"
	"SamplerState g_Samp : register(s0);\n"
	"struct VsOut { float4 vPos : SV_Position; float2 vUv : TEXCOORD0; };\n"
	"VsOut MainVs( uint id : SV_VertexID )\n"
	"{\n"
	"	VsOut o;\n"
	"	float2 uv = float2( (id << 1) & 2, id & 2 );\n"
	"	o.vPos = float4( uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0 );\n"
	"	o.vUv = uv * g_UvScaleBias.xy + g_UvScaleBias.zw;\n"
	"	return o;\n"
	"}\n"
	"float4 MainPs( VsOut i ) : SV_Target\n"
	"{\n"
	"	return g_Src.Sample( g_Samp, i.vUv );\n"
	"}\n";

static struct BlitStateDx11_t
{
	bool m_bInitAttempted;
	bool m_bReady;
	ID3D11VertexShader *m_pVS;
	ID3D11PixelShader *m_pPS;
	ID3D11Buffer *m_pParamsCB;
	ID3D11SamplerState *m_pSampler;
	ID3D11DepthStencilState *m_pNoDepth;
	// Intermediate copy of the backbuffer (it has no SRV bind)
	ID3D11Texture2D *m_pBackbufferCopy;
	ID3D11ShaderResourceView *m_pBackbufferCopySRV;
	int m_nCopyWidth, m_nCopyHeight;
	DXGI_FORMAT m_CopyFormat;
} s_Blit;

static bool Blit_CompileStage( const char *pEntry, const char *pTarget, ID3DBlob **ppCode )
{
	ID3DBlob *pErrors = NULL;
	HRESULT hr = D3DCompile( s_szBlitHlsl, sizeof( s_szBlitHlsl ) - 1, "blitdx11", NULL, NULL,
		pEntry, pTarget, D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, ppCode, &pErrors );
	if ( pErrors )
	{
		if ( FAILED( hr ) )
			Warning( "shaderapidx11: blit shader compile failed [%s]:\n%s\n", pEntry, (const char *)pErrors->GetBufferPointer() );
		pErrors->Release();
	}
	return SUCCEEDED( hr );
}

static bool Blit_Init()
{
	ID3DBlob *pVSCode = NULL, *pPSCode = NULL;
	if ( !Blit_CompileStage( "MainVs", "vs_5_0", &pVSCode ) )
		return false;
	if ( !Blit_CompileStage( "MainPs", "ps_5_0", &pPSCode ) )
	{
		pVSCode->Release();
		return false;
	}

	bool bOk =
		SUCCEEDED( D3D11Device()->CreateVertexShader( pVSCode->GetBufferPointer(), pVSCode->GetBufferSize(), NULL, &s_Blit.m_pVS ) ) &&
		SUCCEEDED( D3D11Device()->CreatePixelShader( pPSCode->GetBufferPointer(), pPSCode->GetBufferSize(), NULL, &s_Blit.m_pPS ) );
	pVSCode->Release();
	pPSCode->Release();

	D3D11_BUFFER_DESC cb = { 16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
	bOk = bOk && SUCCEEDED( D3D11Device()->CreateBuffer( &cb, NULL, &s_Blit.m_pParamsCB ) );

	D3D11_SAMPLER_DESC samp;
	ZeroMemory( &samp, sizeof( samp ) );
	samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samp.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samp.MaxLOD = D3D11_FLOAT32_MAX;
	bOk = bOk && SUCCEEDED( D3D11Device()->CreateSamplerState( &samp, &s_Blit.m_pSampler ) );

	D3D11_DEPTH_STENCIL_DESC ds;
	ZeroMemory( &ds, sizeof( ds ) );
	ds.DepthEnable = FALSE;
	ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	ds.DepthFunc = D3D11_COMPARISON_ALWAYS;
	bOk = bOk && SUCCEEDED( D3D11Device()->CreateDepthStencilState( &ds, &s_Blit.m_pNoDepth ) );

	if ( !bOk )
		Warning( "shaderapidx11: blit pipeline creation failed\n" );
	return bOk;
}

static bool Blit_Ready()
{
	if ( !D3D11Device() || !D3D11Context() )
		return false;
	if ( !s_Blit.m_bInitAttempted )
	{
		s_Blit.m_bInitAttempted = true;
		s_Blit.m_bReady = Blit_Init();
	}
	return s_Blit.m_bReady;
}

// Snapshot the backbuffer into the intermediate (recreated on size change)
static ID3D11ShaderResourceView *Blit_CopyBackbuffer( int &nWidth, int &nHeight )
{
	if ( !g_pD3D11RTV )
		return NULL;

	ID3D11Resource *pResource = NULL;
	g_pD3D11RTV->GetResource( &pResource );
	ID3D11Texture2D *pBackbuffer = NULL;
	if ( FAILED( pResource->QueryInterface( __uuidof( ID3D11Texture2D ), (void **)&pBackbuffer ) ) )
	{
		pResource->Release();
		return NULL;
	}
	pResource->Release();

	D3D11_TEXTURE2D_DESC desc;
	pBackbuffer->GetDesc( &desc );
	nWidth = (int)desc.Width;
	nHeight = (int)desc.Height;

	if ( s_Blit.m_pBackbufferCopy &&
		 ( s_Blit.m_nCopyWidth != nWidth || s_Blit.m_nCopyHeight != nHeight || s_Blit.m_CopyFormat != desc.Format ) )
	{
		s_Blit.m_pBackbufferCopySRV->Release();
		s_Blit.m_pBackbufferCopy->Release();
		s_Blit.m_pBackbufferCopySRV = NULL;
		s_Blit.m_pBackbufferCopy = NULL;
	}

	if ( !s_Blit.m_pBackbufferCopy )
	{
		D3D11_TEXTURE2D_DESC copyDesc = desc;
		copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		copyDesc.Usage = D3D11_USAGE_DEFAULT;
		copyDesc.CPUAccessFlags = 0;
		copyDesc.MiscFlags = 0;
		copyDesc.MipLevels = 1;
		// Always single-sampled: with mat_antialias the backbuffer is MS and
		// the copy is the RESOLVE destination (an MS copy could not be
		// sampled by the non-MS blit shader anyway).
		copyDesc.SampleDesc.Count = 1;
		copyDesc.SampleDesc.Quality = 0;
		if ( FAILED( D3D11Device()->CreateTexture2D( &copyDesc, NULL, &s_Blit.m_pBackbufferCopy ) ) ||
			 FAILED( D3D11Device()->CreateShaderResourceView( s_Blit.m_pBackbufferCopy, NULL, &s_Blit.m_pBackbufferCopySRV ) ) )
		{
			Warning( "shaderapidx11: backbuffer copy creation failed (%dx%d)\n", nWidth, nHeight );
			if ( s_Blit.m_pBackbufferCopy )
			{
				s_Blit.m_pBackbufferCopy->Release();
				s_Blit.m_pBackbufferCopy = NULL;
			}
			pBackbuffer->Release();
			return NULL;
		}
		Dx11_SetDebugName( s_Blit.m_pBackbufferCopy, "blit_backbuffer_copy" );
		s_Blit.m_nCopyWidth = nWidth;
		s_Blit.m_nCopyHeight = nHeight;
		s_Blit.m_CopyFormat = desc.Format;
	}

	if ( desc.SampleDesc.Count > 1 )
		D3D11Context()->ResolveSubresource( s_Blit.m_pBackbufferCopy, 0, pBackbuffer, 0, desc.Format );
	else
		D3D11Context()->CopyResource( s_Blit.m_pBackbufferCopy, pBackbuffer );
	pBackbuffer->Release();
	return s_Blit.m_pBackbufferCopySRV;
}

// Resolve the active RT as a sample source. Texture RTs sample directly
// (they're unbound from the OM stage during the blit draw).
static ID3D11ShaderResourceView *Blit_GetSourceSRV( int &nWidth, int &nHeight )
{
	ShaderAPITextureHandle_t hRT = StateDx11_GetRenderTexture();
	if ( hRT != INVALID_SHADERAPI_TEXTURE_HANDLE )
	{
		if ( !TextureDx11_GetDims( hRT, nWidth, nHeight ) )
			return NULL;
		return TextureDx11_GetSRV( hRT, false );
	}
	return Blit_CopyBackbuffer( nWidth, nHeight );
}

static void Blit_Draw( ID3D11ShaderResourceView *pSrcSRV, int nSrcW, int nSrcH, const Rect_t *pSrcRect,
	ID3D11RenderTargetView *pDstRTV, int nDstW, int nDstH, const Rect_t *pDstRect )
{
	ID3D11DeviceContext *pCtx = D3D11Context();

	float flUvParams[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
	if ( pSrcRect && nSrcW > 0 && nSrcH > 0 )
	{
		flUvParams[0] = (float)pSrcRect->width / nSrcW;
		flUvParams[1] = (float)pSrcRect->height / nSrcH;
		flUvParams[2] = (float)pSrcRect->x / nSrcW;
		flUvParams[3] = (float)pSrcRect->y / nSrcH;
	}
	D3D11_MAPPED_SUBRESOURCE map;
	if ( SUCCEEDED( pCtx->Map( s_Blit.m_pParamsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &map ) ) )
	{
		memcpy( map.pData, flUvParams, sizeof( flUvParams ) );
		pCtx->Unmap( s_Blit.m_pParamsCB, 0 );
	}

	D3D11_VIEWPORT vp;
	vp.TopLeftX = pDstRect ? (float)pDstRect->x : 0.0f;
	vp.TopLeftY = pDstRect ? (float)pDstRect->y : 0.0f;
	vp.Width = pDstRect ? (float)pDstRect->width : (float)nDstW;
	vp.Height = pDstRect ? (float)pDstRect->height : (float)nDstH;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;

	pCtx->OMSetRenderTargets( 1, &pDstRTV, NULL );
	pCtx->OMSetBlendState( NULL, NULL, 0xFFFFFFFF );
	pCtx->OMSetDepthStencilState( s_Blit.m_pNoDepth, 0 );
	pCtx->RSSetState( NULL );
	pCtx->RSSetViewports( 1, &vp );
	pCtx->IASetInputLayout( NULL );
	pCtx->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	pCtx->VSSetShader( s_Blit.m_pVS, NULL, 0 );
	pCtx->VSSetConstantBuffers( 0, 1, &s_Blit.m_pParamsCB );
	pCtx->GSSetShader( NULL, NULL, 0 );
	pCtx->PSSetShader( s_Blit.m_pPS, NULL, 0 );
	pCtx->PSSetShaderResources( 0, 1, &pSrcSRV );
	pCtx->PSSetSamplers( 0, 1, &s_Blit.m_pSampler );
	pCtx->Draw( 3, 0 );

	// Unbind the source SRV (it may be the next draw's render target), then
	// restore the engine's viewport; the per-draw commit rebinds the rest.
	ID3D11ShaderResourceView *pNullSRV = NULL;
	pCtx->PSSetShaderResources( 0, 1, &pNullSRV );
	ID3D11RenderTargetView *pNullRTV = NULL;
	pCtx->OMSetRenderTargets( 1, &pNullRTV, NULL );
	StateDx11_ReapplyViewport();
}

void BlitDx11_RTToTexture( ShaderAPITextureHandle_t hDst, const Rect_t *pSrcRect, const Rect_t *pDstRect )
{
	// Gated with texture-RT support: filling FB-copy textures (alpha=1)
	// feeds the not-yet-ported refract/post overlays — see dx11_rt_textures.
	if ( !StateDx11_RTTexturesEnabled() || !Blit_Ready() )
		return;

	ID3D11RenderTargetView *pDstRTV = TextureDx11_GetRTV( hDst, false );
	int nDstW = 0, nDstH = 0;
	if ( !pDstRTV || !TextureDx11_GetDims( hDst, nDstW, nDstH ) )
	{
		static bool s_bWarnedNoRTV = false;
		if ( !s_bWarnedNoRTV )
		{
			s_bWarnedNoRTV = true;
			Warning( "shaderapidx11: CopyRenderTargetToTexture dest %d has no RTV; skipped\n", (int)hDst );
		}
		return;
	}

	int nSrcW = 0, nSrcH = 0;
	ID3D11ShaderResourceView *pSrcSRV = Blit_GetSourceSRV( nSrcW, nSrcH );
	if ( !pSrcSRV )
		return;

	Blit_Draw( pSrcSRV, nSrcW, nSrcH, pSrcRect, pDstRTV, nDstW, nDstH, pDstRect );
}

void BlitDx11_TextureToRT( ShaderAPITextureHandle_t hSrc, const Rect_t *pSrcRect, const Rect_t *pDstRect )
{
	if ( !StateDx11_RTTexturesEnabled() || !Blit_Ready() )
		return;

	int nSrcW = 0, nSrcH = 0;
	ID3D11ShaderResourceView *pSrcSRV = TextureDx11_GetSRV( hSrc, false );
	if ( !pSrcSRV || !TextureDx11_GetDims( hSrc, nSrcW, nSrcH ) )
		return;

	ID3D11RenderTargetView *pDstRTV = NULL;
	int nDstW = 0, nDstH = 0;
	ShaderAPITextureHandle_t hRT = StateDx11_GetRenderTexture();
	if ( hRT != INVALID_SHADERAPI_TEXTURE_HANDLE )
	{
		pDstRTV = TextureDx11_GetRTV( hRT, false );
		TextureDx11_GetDims( hRT, nDstW, nDstH );
	}
	else
	{
		pDstRTV = g_pD3D11RTV;
		if ( g_pShaderDeviceDx11 )
			g_pShaderDeviceDx11->GetBackBufferDimensions( nDstW, nDstH );
	}
	if ( !pDstRTV )
		return;

	Blit_Draw( pSrcSRV, nSrcW, nSrcH, pSrcRect, pDstRTV, nDstW, nDstH, pDstRect );
}

void BlitDx11_ReleaseDevice()
{
	if ( s_Blit.m_pBackbufferCopySRV ) { s_Blit.m_pBackbufferCopySRV->Release(); s_Blit.m_pBackbufferCopySRV = NULL; }
	if ( s_Blit.m_pBackbufferCopy ) { s_Blit.m_pBackbufferCopy->Release(); s_Blit.m_pBackbufferCopy = NULL; }
	if ( s_Blit.m_pNoDepth ) { s_Blit.m_pNoDepth->Release(); s_Blit.m_pNoDepth = NULL; }
	if ( s_Blit.m_pSampler ) { s_Blit.m_pSampler->Release(); s_Blit.m_pSampler = NULL; }
	if ( s_Blit.m_pParamsCB ) { s_Blit.m_pParamsCB->Release(); s_Blit.m_pParamsCB = NULL; }
	if ( s_Blit.m_pPS ) { s_Blit.m_pPS->Release(); s_Blit.m_pPS = NULL; }
	if ( s_Blit.m_pVS ) { s_Blit.m_pVS->Release(); s_Blit.m_pVS = NULL; }
	s_Blit.m_bInitAttempted = false;
	s_Blit.m_bReady = false;
}
