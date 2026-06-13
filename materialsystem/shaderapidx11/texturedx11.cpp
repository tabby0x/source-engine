//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX11 texture system (M3). Textures are created TYPELESS where an
// sRGB pairing exists; the SRV (linear vs sRGB) is chosen at bind time from
// the snapshot's per-sampler sRGB-read state, mirroring D3DSAMP_SRGBTEXTURE.
//
//===========================================================================//

#include <d3d11.h>

#include "texturedx11.h"
#include "shaderapidx11_global.h"
#include "bitmap/imageformat.h"
#include "pixelwriter.h"
#include "vtf/vtf.h"
#include "tier1/utlvector.h"
#include "tier1/strtools.h"

// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// ImageFormat -> DXGI mapping. m_UploadFormat is what we CPU-convert into.
//-----------------------------------------------------------------------------
struct FormatMapDx11_t
{
	DXGI_FORMAT m_Resource;		// typeless where an sRGB pair exists
	DXGI_FORMAT m_SrvLinear;
	DXGI_FORMAT m_SrvSRGB;		// == linear when no sRGB variant
	ImageFormat m_UploadFormat;	// CPU-side layout we convert source data into
	bool m_bCompressed;
};

static bool GetFormatMap( ImageFormat fmt, FormatMapDx11_t &out )
{
	switch ( fmt )
	{
	case IMAGE_FORMAT_RGBA8888:
		out = { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, IMAGE_FORMAT_RGBA8888, false };
		return true;
	case IMAGE_FORMAT_BGRA8888:
	case IMAGE_FORMAT_BGRX8888:
	case IMAGE_FORMAT_BGR888:
	case IMAGE_FORMAT_RGB888:
	case IMAGE_FORMAT_BGR888_BLUESCREEN:
	case IMAGE_FORMAT_RGB888_BLUESCREEN:
	// ARGB8888/ABGR8888 have no DXGI layout — CPU uploads convert to BGRA.
	// ARGB8888 matters as a RENDER TARGET: the _rt_Shadows atlas
	// (clientshadowmgr) requests it, and without a map entry the atlas fell
	// to the 1x1 white stand-in (shadows built into nowhere).
	case IMAGE_FORMAT_ARGB8888:
	case IMAGE_FORMAT_ABGR8888:
		out = { DXGI_FORMAT_B8G8R8A8_TYPELESS, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, IMAGE_FORMAT_BGRA8888, false };
		return true;
	case IMAGE_FORMAT_DXT1:
	case IMAGE_FORMAT_DXT1_ONEBITALPHA:
		out = { DXGI_FORMAT_BC1_TYPELESS, DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM_SRGB, fmt, true };
		return true;
	case IMAGE_FORMAT_DXT3:
		out = { DXGI_FORMAT_BC2_TYPELESS, DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC2_UNORM_SRGB, fmt, true };
		return true;
	case IMAGE_FORMAT_DXT5:
		out = { DXGI_FORMAT_BC3_TYPELESS, DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM_SRGB, fmt, true };
		return true;
	case IMAGE_FORMAT_A8:
		out = { DXGI_FORMAT_A8_UNORM, DXGI_FORMAT_A8_UNORM, DXGI_FORMAT_A8_UNORM, IMAGE_FORMAT_A8, false };
		return true;
	case IMAGE_FORMAT_I8:
		out = { DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, IMAGE_FORMAT_I8, false };
		return true;
	case IMAGE_FORMAT_IA88:
		out = { DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM, IMAGE_FORMAT_IA88, false };
		return true;
	case IMAGE_FORMAT_RGBA16161616F:
		out = { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, IMAGE_FORMAT_RGBA16161616F, false };
		return true;
	case IMAGE_FORMAT_RGBA16161616:
		out = { DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_UNORM, IMAGE_FORMAT_RGBA16161616, false };
		return true;
	default:
		return false;
	}
}


//-----------------------------------------------------------------------------
// Texture record
//-----------------------------------------------------------------------------
struct TextureDx11_t
{
	ID3D11Texture2D *m_pTexture;
	// Volume textures (color-correction LUTs): the 3D resource lives here and
	// m_pTexture stays NULL. SRVs below are TEXTURE3D-dimension views.
	ID3D11Texture3D *m_pTexture3D;
	int m_nDepth;
	ID3D11ShaderResourceView *m_pSrvLinear;
	ID3D11ShaderResourceView *m_pSrvSRGB;
	ID3D11RenderTargetView *m_pRtvLinear;	// lazy; only for render-target textures
	ID3D11RenderTargetView *m_pRtvSRGB;
	ID3D11DepthStencilView *m_pDsv;			// shadow depth-map textures only
	ID3D11SamplerState *m_pSampler;
	D3D11_SAMPLER_DESC m_SamplerDesc;
	FormatMapDx11_t m_Format;
	ImageFormat m_CreateFormat;
	int m_nWidth, m_nHeight, m_nMipLevels;
	bool m_bCube;
	bool m_bRenderTarget;
	bool m_bDepth;		// shadow depth map (DSV target + raw-depth SRV)
	bool m_bInUse;
	bool m_bSamplerDirty;
	// CPU mirror of mip0/face0 for TexLock textures (lightmap pages): dx9
	// LockRect writes in place, so texels the caller doesn't touch must keep
	// their previous contents — a transient staging buffer hands back garbage.
	CUtlVector<unsigned char> *m_pShadowMip0;
};

static CUtlVector<TextureDx11_t> s_Textures;
static ShaderAPITextureHandle_t s_hModify = INVALID_SHADERAPI_TEXTURE_HANDLE;
static ID3D11Texture2D *s_pWhiteTexture;
static ID3D11ShaderResourceView *s_pWhiteSRV;
static ID3D11Texture2D *s_pWhiteCubeTexture;
static ID3D11ShaderResourceView *s_pWhiteCubeSRV;
static ID3D11Texture2D *s_pBlackTexture;
static ID3D11ShaderResourceView *s_pBlackSRV;
static ID3D11SamplerState *s_pDefaultSampler;

static TextureDx11_t *GetTexture( ShaderAPITextureHandle_t h )
{
	if ( h < 0 || h >= s_Textures.Count() || !s_Textures[h].m_bInUse )
		return NULL;
	return &s_Textures[h];
}


//-----------------------------------------------------------------------------
// Standard textures (1x1 solid colors) in a reserved negative handle range,
// so BindStandardTexture flows through the same bind path as regular handles.
//-----------------------------------------------------------------------------
const ShaderAPITextureHandle_t DX11_STD_TEXTURE_HANDLE_BASE = -100;

static ID3D11Texture2D *s_pStdTexture[TEXTURE_MAX_STD_TEXTURES];
static ID3D11ShaderResourceView *s_pStdSRV[TEXTURE_MAX_STD_TEXTURES];

static bool IsStandardHandle( ShaderAPITextureHandle_t h )
{
	return h <= DX11_STD_TEXTURE_HANDLE_BASE && h > DX11_STD_TEXTURE_HANDLE_BASE - TEXTURE_MAX_STD_TEXTURES;
}

static int StandardIdFromHandle( ShaderAPITextureHandle_t h )
{
	return (int)( DX11_STD_TEXTURE_HANDLE_BASE - h );
}

ShaderAPITextureHandle_t TextureDx11_GetStandardHandle( int nStdTextureId )
{
	if ( nStdTextureId < 0 || nStdTextureId >= TEXTURE_MAX_STD_TEXTURES )
		nStdTextureId = TEXTURE_WHITE;
	return DX11_STD_TEXTURE_HANDLE_BASE - nStdTextureId;
}

// BGRA8 texel for each standard texture id. Lightmaps are fullbright white
// until the real lightmap pages arrive (M4); unhandled ids fall back to white.
static uint32 StandardTexel( int id )
{
	switch ( id )
	{
	case TEXTURE_BLACK:				return 0xFF000000;
	case TEXTURE_GREY:				return 0xFF7F7F7F;
	case TEXTURE_GREY_ALPHA_ZERO:	return 0x007F7F7F;
	case TEXTURE_NORMALMAP_FLAT:	return 0xFF8080FF;	// (128,128,255): +Z tangent normal
	default:						return 0xFFFFFFFF;
	}
}

static ID3D11ShaderResourceView *GetStandardSRV( int id )
{
	if ( id < 0 || id >= TEXTURE_MAX_STD_TEXTURES )
		return NULL;
	if ( !s_pStdSRV[id] && D3D11Device() )
	{
		const uint32 nTexel = StandardTexel( id );
		D3D11_TEXTURE2D_DESC desc = { 1, 1, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 }, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
		D3D11_SUBRESOURCE_DATA data = { &nTexel, 4, 4 };
		if ( SUCCEEDED( D3D11Device()->CreateTexture2D( &desc, &data, &s_pStdTexture[id] ) ) )
		{
			D3D11Device()->CreateShaderResourceView( s_pStdTexture[id], NULL, &s_pStdSRV[id] );
		}
	}
	return s_pStdSRV[id];
}

bool TextureDx11_IsValid( ShaderAPITextureHandle_t h )
{
	if ( IsStandardHandle( h ) )
		return true;
	return GetTexture( h ) != NULL;
}


//-----------------------------------------------------------------------------
// Creation
//-----------------------------------------------------------------------------

// Slot 0 is permanently reserved: handle 0 IS INVALID_SHADERAPI_TEXTURE_HANDLE
// (ishaderapi.h), so the first-created texture was indistinguishable from "no
// texture" — HL2's _rt_Camera (created first by InitClientRenderTargets) got
// handle 0 and every CTexture::SetRenderTarget push for it fell through to the
// backbuffer: all func_monitor screens sampled an all-black, never-rendered RT.
static int AllocTextureSlot()
{
	if ( !s_Textures.Count() )
	{
		int nReserved = s_Textures.AddToTail();
		memset( &s_Textures[nReserved], 0, sizeof( TextureDx11_t ) );
	}
	for ( int i = 1; i < s_Textures.Count(); ++i )
	{
		if ( !s_Textures[i].m_bInUse )
			return i;
	}
	return s_Textures.AddToTail();
}

// The dx9 vendor shadow depth-map formats (clientshadowmgr's
// _rt_ShadowDepthTexture_N arrives with GetShadowDepthTextureFormat = NV_DST24
// through the NORMAL CreateTextures path — MATERIAL_RT_DEPTH_NONE means no
// separate zbuffer handle, the texture itself IS the depth target).
static bool IsDepthImageFormat( ImageFormat fmt )
{
	return fmt == IMAGE_FORMAT_NV_DST16 || fmt == IMAGE_FORMAT_NV_DST24 ||
		fmt == IMAGE_FORMAT_NV_INTZ || fmt == IMAGE_FORMAT_NV_RAWZ ||
		fmt == IMAGE_FORMAT_ATI_DST16 || fmt == IMAGE_FORMAT_ATI_DST24;
}

// Shadow depth-map texture: R24G8_TYPELESS bound DEPTH_STENCIL|SHADER_RESOURCE.
// DSV writes D24S8 during the caster pass; the SRV reads raw 0..1 depth in .r
// (R24_UNORM_X8_TYPELESS) for the manual compare in the flashlight families
// (no comparison sampler — register layouts collide across the dx9 families).
// Sampler is forced POINT/CLAMP and stays that way (filtering raw depth then
// comparing is wrong, and CTexture's filter pushes must not undo it).
static ShaderAPITextureHandle_t CreateDepthOne( int nWidth, int nHeight, const char *pDebugName )
{
	if ( !D3D11Device() )
		return INVALID_SHADERAPI_TEXTURE_HANDLE;

	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory( &desc, sizeof( desc ) );
	desc.Width = MAX( 1, nWidth );
	desc.Height = MAX( 1, nHeight );
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	ID3D11Texture2D *pTexture = NULL;
	HRESULT hr = D3D11Device()->CreateTexture2D( &desc, NULL, &pTexture );
	if ( FAILED( hr ) )
	{
		Warning( "shaderapidx11: depth texture CreateTexture2D failed (hr=0x%08x, %dx%d, %s)\n",
			hr, nWidth, nHeight, pDebugName ? pDebugName : "?" );
		return INVALID_SHADERAPI_TEXTURE_HANDLE;
	}
	Dx11_SetDebugName( pTexture, pDebugName );

	D3D11_DEPTH_STENCIL_VIEW_DESC dsv;
	ZeroMemory( &dsv, sizeof( dsv ) );
	dsv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	ID3D11DepthStencilView *pDSV = NULL;
	D3D11Device()->CreateDepthStencilView( pTexture, &dsv, &pDSV );

	D3D11_SHADER_RESOURCE_VIEW_DESC srv;
	ZeroMemory( &srv, sizeof( srv ) );
	srv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MipLevels = 1;
	ID3D11ShaderResourceView *pSRV = NULL;
	D3D11Device()->CreateShaderResourceView( pTexture, &srv, &pSRV );

	int h = AllocTextureSlot();
	TextureDx11_t &tex = s_Textures[h];
	memset( &tex, 0, sizeof( tex ) );
	tex.m_pTexture = pTexture;
	tex.m_nDepth = 1;
	tex.m_pSrvLinear = pSRV;
	tex.m_pSrvSRGB = pSRV;
	tex.m_pDsv = pDSV;
	tex.m_CreateFormat = IMAGE_FORMAT_NV_DST24;
	tex.m_nWidth = MAX( 1, nWidth );
	tex.m_nHeight = MAX( 1, nHeight );
	tex.m_nMipLevels = 1;
	tex.m_bDepth = true;
	tex.m_bInUse = true;
	tex.m_bSamplerDirty = true;

	D3D11_SAMPLER_DESC &samp = tex.m_SamplerDesc;
	ZeroMemory( &samp, sizeof( samp ) );
	samp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samp.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samp.MaxLOD = D3D11_FLOAT32_MAX;
	return h;
}

static ShaderAPITextureHandle_t CreateOne( int nWidth, int nHeight, int nDepth, ImageFormat dstFormat,
	int nMipLevels, int nFlags, const char *pDebugName )
{
	if ( IsDepthImageFormat( dstFormat ) )
		return CreateDepthOne( nWidth, nHeight, pDebugName );

	FormatMapDx11_t map;
	if ( !GetFormatMap( dstFormat, map ) )
	{
		static bool s_bWarned[NUM_IMAGE_FORMATS];
		if ( dstFormat >= 0 && dstFormat < NUM_IMAGE_FORMATS && !s_bWarned[dstFormat] )
		{
			s_bWarned[dstFormat] = true;
			Warning( "shaderapidx11: unsupported texture format %d (%s); using white fallback\n", dstFormat, pDebugName ? pDebugName : "?" );
		}
		// Hand out a valid handle that renders white rather than failing the caller.
		GetFormatMap( IMAGE_FORMAT_BGRA8888, map );
		nWidth = nHeight = 1;
		nDepth = 1;
		nMipLevels = 1;
	}

	bool bCube = ( nFlags & TEXTURE_CREATE_CUBEMAP ) != 0;
	bool bRenderTarget = ( nFlags & TEXTURE_CREATE_RENDERTARGET ) != 0;
	bool bVolume = nDepth > 1;

	if ( nMipLevels <= 0 )
		nMipLevels = 1;

	ID3D11Texture2D *pTexture = NULL;
	ID3D11Texture3D *pTexture3D = NULL;
	if ( bVolume )
	{
		// Volume texture (color-correction LUTs: 32x32x32). SRV-only.
		D3D11_TEXTURE3D_DESC desc3;
		ZeroMemory( &desc3, sizeof( desc3 ) );
		desc3.Width = MAX( 1, nWidth );
		desc3.Height = MAX( 1, nHeight );
		desc3.Depth = MAX( 1, nDepth );
		desc3.MipLevels = nMipLevels;
		desc3.Format = map.m_Resource;
		desc3.Usage = D3D11_USAGE_DEFAULT;
		desc3.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		HRESULT hr = D3D11Device()->CreateTexture3D( &desc3, NULL, &pTexture3D );
		if ( FAILED( hr ) )
		{
			Warning( "shaderapidx11: CreateTexture3D failed (hr=0x%08x, %dx%dx%d fmt %d, %s)\n", hr, nWidth, nHeight, nDepth, dstFormat, pDebugName ? pDebugName : "?" );
			return INVALID_SHADERAPI_TEXTURE_HANDLE;
		}
		Dx11_SetDebugName( pTexture3D, pDebugName );
	}
	else
	{
		D3D11_TEXTURE2D_DESC desc;
		ZeroMemory( &desc, sizeof( desc ) );
		desc.Width = MAX( 1, nWidth );
		desc.Height = MAX( 1, nHeight );
		desc.MipLevels = nMipLevels;
		desc.ArraySize = bCube ? 6 : 1;
		desc.Format = map.m_Resource;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | ( bRenderTarget ? D3D11_BIND_RENDER_TARGET : 0 );
		desc.MiscFlags = bCube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;

		HRESULT hr = D3D11Device()->CreateTexture2D( &desc, NULL, &pTexture );
		if ( FAILED( hr ) )
		{
			Warning( "shaderapidx11: CreateTexture2D failed (hr=0x%08x, %dx%d fmt %d, %s)\n", hr, nWidth, nHeight, dstFormat, pDebugName ? pDebugName : "?" );
			return INVALID_SHADERAPI_TEXTURE_HANDLE;
		}
		Dx11_SetDebugName( pTexture, pDebugName );
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srv;
	ZeroMemory( &srv, sizeof( srv ) );
	srv.ViewDimension = bVolume ? D3D11_SRV_DIMENSION_TEXTURE3D
		: ( bCube ? D3D11_SRV_DIMENSION_TEXTURECUBE : D3D11_SRV_DIMENSION_TEXTURE2D );
	if ( bVolume )
		srv.Texture3D.MipLevels = nMipLevels;
	else if ( bCube )
		srv.TextureCube.MipLevels = nMipLevels;
	else
		srv.Texture2D.MipLevels = nMipLevels;

	ID3D11Resource *pResource = bVolume ? (ID3D11Resource *)pTexture3D : (ID3D11Resource *)pTexture;
	ID3D11ShaderResourceView *pSrvLinear = NULL, *pSrvSRGB = NULL;
	srv.Format = map.m_SrvLinear;
	D3D11Device()->CreateShaderResourceView( pResource, &srv, &pSrvLinear );
	if ( map.m_SrvSRGB != map.m_SrvLinear )
	{
		srv.Format = map.m_SrvSRGB;
		D3D11Device()->CreateShaderResourceView( pResource, &srv, &pSrvSRGB );
	}

	int h = AllocTextureSlot();
	TextureDx11_t &tex = s_Textures[h];
	memset( &tex, 0, sizeof( tex ) );
	tex.m_pTexture = pTexture;
	tex.m_pTexture3D = pTexture3D;
	tex.m_nDepth = bVolume ? nDepth : 1;
	tex.m_pSrvLinear = pSrvLinear;
	tex.m_pSrvSRGB = pSrvSRGB ? pSrvSRGB : pSrvLinear;
	tex.m_Format = map;
	tex.m_CreateFormat = dstFormat;
	tex.m_nWidth = MAX( 1, nWidth );
	tex.m_nHeight = MAX( 1, nHeight );
	tex.m_nMipLevels = nMipLevels;
	tex.m_bCube = bCube;
	tex.m_bRenderTarget = bRenderTarget && !bVolume;
	tex.m_bInUse = true;
	tex.m_bSamplerDirty = true;

	D3D11_SAMPLER_DESC &samp = tex.m_SamplerDesc;
	ZeroMemory( &samp, sizeof( samp ) );
	samp.Filter = nMipLevels > 1 ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samp.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samp.MaxLOD = D3D11_FLOAT32_MAX;
	return h;
}

void TextureDx11_CreateTextures( ShaderAPITextureHandle_t *pHandles, int nCount, int nWidth, int nHeight,
	int nDepth, ImageFormat dstFormat, int nMipLevels, int nCopies, int nFlags, const char *pDebugName )
{
	for ( int i = 0; i < nCount; ++i )
	{
		pHandles[i] = CreateOne( nWidth, nHeight, nDepth, dstFormat, nMipLevels, nFlags, pDebugName );
	}
}

void TextureDx11_Delete( ShaderAPITextureHandle_t h )
{
	TextureDx11_t *pTex = GetTexture( h );
	if ( !pTex )
		return;
	if ( pTex->m_pRtvSRGB && pTex->m_pRtvSRGB != pTex->m_pRtvLinear )
		pTex->m_pRtvSRGB->Release();
	if ( pTex->m_pRtvLinear )
		pTex->m_pRtvLinear->Release();
	if ( pTex->m_pDsv )
		pTex->m_pDsv->Release();
	if ( pTex->m_pSrvSRGB && pTex->m_pSrvSRGB != pTex->m_pSrvLinear )
		pTex->m_pSrvSRGB->Release();
	if ( pTex->m_pSrvLinear )
		pTex->m_pSrvLinear->Release();
	if ( pTex->m_pSampler )
		pTex->m_pSampler->Release();
	if ( pTex->m_pTexture )
		pTex->m_pTexture->Release();
	if ( pTex->m_pTexture3D )
		pTex->m_pTexture3D->Release();
	delete pTex->m_pShadowMip0;
	memset( pTex, 0, sizeof( *pTex ) );
}


//-----------------------------------------------------------------------------
// Upload
//-----------------------------------------------------------------------------
void TextureDx11_Modify( ShaderAPITextureHandle_t h )
{
	s_hModify = h;
}

static void UploadRect( TextureDx11_t *pTex, int nLevel, int nCubeFace, int nX, int nY,
	int nWidth, int nHeight, ImageFormat srcFormat, int nSrcStride, void *pData, int nZ = 0 )
{
	if ( !pTex || !pData || nWidth <= 0 || nHeight <= 0 )
		return;
	if ( nLevel >= pTex->m_nMipLevels || nCubeFace >= ( pTex->m_bCube ? 6 : 1 ) )
		return;
	// Volume textures upload one z-slice per call (the dx9 TexImage2D zOffset
	// convention — CTexture::DownloadTexture iterates slices)
	if ( nZ < 0 || nZ >= MAX( 1, pTex->m_nDepth >> nLevel ) )
		return;

	// An out-of-bounds UpdateSubresource box hangs the GPU; reject instead.
	int nMipW = MAX( 1, pTex->m_nWidth >> nLevel );
	int nMipH = MAX( 1, pTex->m_nHeight >> nLevel );
	if ( nX + nWidth > nMipW || nY + nHeight > nMipH )
	{
		static bool s_bWarnedBounds = false;
		if ( !s_bWarnedBounds )
		{
			s_bWarnedBounds = true;
			Warning( "shaderapidx11: rejected out-of-bounds texture upload (%dx%d at %d,%d into mip %d of %dx%d)\n",
				nWidth, nHeight, nX, nY, nLevel, nMipW, nMipH );
		}
		return;
	}

	const FormatMapDx11_t &map = pTex->m_Format;
	int nSubresource = nLevel + ( pTex->m_bCube ? nCubeFace * pTex->m_nMipLevels : 0 );

	if ( map.m_bCompressed )
	{
		// Source data must already be the matching DXT format
		if ( srcFormat != map.m_UploadFormat && !( srcFormat == IMAGE_FORMAT_DXT1 && map.m_UploadFormat == IMAGE_FORMAT_DXT1_ONEBITALPHA ) )
			return;
		int nBlockBytes = ( srcFormat == IMAGE_FORMAT_DXT1 || srcFormat == IMAGE_FORMAT_DXT1_ONEBITALPHA ) ? 8 : 16;
		int nBlocksWide = ( nWidth + 3 ) / 4;
		int nBlocksHigh = ( nHeight + 3 ) / 4;
		if ( nX == 0 && nY == 0 && nWidth == nMipW && nHeight == nMipH )
		{
			// Full-mip update: a NULL box sidesteps the BC block-alignment rule
			// on tail mips smaller than a block (2x2, 1x1).
			D3D11Context()->UpdateSubresource( pTex->m_pTexture, nSubresource, NULL, pData, nBlocksWide * nBlockBytes, nBlocksWide * nBlockBytes * nBlocksHigh );
			return;
		}
		if ( ( nX | nY | nWidth | nHeight ) & 3 )
		{
			static bool s_bWarnedAlign = false;
			if ( !s_bWarnedAlign )
			{
				s_bWarnedAlign = true;
				Warning( "shaderapidx11: rejected non-block-aligned partial DXT upload (%dx%d at %d,%d)\n", nWidth, nHeight, nX, nY );
			}
			return;
		}
		D3D11_BOX box = { (UINT)nX, (UINT)nY, 0, (UINT)( nX + nWidth ), (UINT)( nY + nHeight ), 1 };
		D3D11Context()->UpdateSubresource( pTex->m_pTexture, nSubresource, &box, pData, nBlocksWide * nBlockBytes, nBlocksWide * nBlockBytes * nBlocksHigh );
		return;
	}

	int nDstPixelBytes = ImageLoader::SizeInBytes( map.m_UploadFormat );
	int nDstStride = nWidth * nDstPixelBytes;

	CUtlVector<byte> converted;
	const void *pUpload = pData;
	int nUploadStride = nSrcStride ? nSrcStride : nWidth * ImageLoader::SizeInBytes( srcFormat );
	if ( srcFormat != map.m_UploadFormat )
	{
		converted.SetCount( nDstStride * nHeight );
		if ( !ImageLoader::ConvertImageFormat( (const unsigned char *)pData, srcFormat,
			converted.Base(), map.m_UploadFormat, nWidth, nHeight, nSrcStride, 0 ) )
		{
			static bool s_bWarnedConvert = false;
			if ( !s_bWarnedConvert )
			{
				s_bWarnedConvert = true;
				Warning( "shaderapidx11: ConvertImageFormat %d -> %d failed\n", srcFormat, map.m_UploadFormat );
			}
			return;
		}
		pUpload = converted.Base();
		nUploadStride = nDstStride;
	}

	// Keep the TexLock CPU mirror coherent with direct uploads
	if ( pTex->m_pShadowMip0 && nLevel == 0 && nCubeFace == 0 && nZ == 0 )
	{
		int nShadowPitch = pTex->m_nWidth * nDstPixelBytes;
		unsigned char *pShadowRect = pTex->m_pShadowMip0->Base() + nY * nShadowPitch + nX * nDstPixelBytes;
		if ( (const void *)pShadowRect != pUpload )
		{
			for ( int row = 0; row < nHeight; ++row )
			{
				memcpy( pShadowRect + row * nShadowPitch, (const byte *)pUpload + row * nUploadStride, nDstStride );
			}
		}
	}

	ID3D11Resource *pResource = pTex->m_pTexture3D
		? (ID3D11Resource *)pTex->m_pTexture3D : (ID3D11Resource *)pTex->m_pTexture;
	D3D11_BOX box = { (UINT)nX, (UINT)nY, (UINT)nZ, (UINT)( nX + nWidth ), (UINT)( nY + nHeight ), (UINT)( nZ + 1 ) };
	D3D11Context()->UpdateSubresource( pResource, nSubresource, &box, pUpload, nUploadStride, nUploadStride * nHeight );
}

//-----------------------------------------------------------------------------
// TexLock/TexUnlock: CPU staging handed out through a CPixelWriter (the
// lightmap page update path), uploaded on unlock.
//-----------------------------------------------------------------------------
static CUtlVector<unsigned char> s_TexLockStaging;
static ShaderAPITextureHandle_t s_hTexLock = INVALID_SHADERAPI_TEXTURE_HANDLE;
static int s_nTexLockLevel, s_nTexLockFace, s_nTexLockX, s_nTexLockY, s_nTexLockW, s_nTexLockH;

bool TextureDx11_TexLock( int nLevel, int nCubeFace, int nXOffset, int nYOffset,
	int nWidth, int nHeight, CPixelWriter &writer )
{
	TextureDx11_t *pTex = GetTexture( s_hModify );
	if ( !pTex || nWidth <= 0 || nHeight <= 0 || s_hTexLock != INVALID_SHADERAPI_TEXTURE_HANDLE )
		return false;
	if ( pTex->m_Format.m_bCompressed || nLevel != 0 || nCubeFace != 0 )
		return false;
	if ( nXOffset < 0 || nYOffset < 0 || nXOffset + nWidth > pTex->m_nWidth || nYOffset + nHeight > pTex->m_nHeight )
		return false;

	// The writer gets a window into a persistent CPU mirror of the whole mip:
	// dx9 LockRect semantics — texels the caller doesn't write keep their
	// previous values (dlights re-light only part of a lightmap page rect).
	int nPixelBytes = ImageLoader::SizeInBytes( pTex->m_Format.m_UploadFormat );
	int nPagePitch = pTex->m_nWidth * nPixelBytes;
	if ( !pTex->m_pShadowMip0 )
	{
		pTex->m_pShadowMip0 = new CUtlVector<unsigned char>;
		pTex->m_pShadowMip0->SetCount( nPagePitch * pTex->m_nHeight );
		memset( pTex->m_pShadowMip0->Base(), 0, nPagePitch * pTex->m_nHeight );
	}
	writer.SetPixelMemory( pTex->m_Format.m_UploadFormat,
		pTex->m_pShadowMip0->Base() + nYOffset * nPagePitch + nXOffset * nPixelBytes, nPagePitch );

	s_hTexLock = s_hModify;
	s_nTexLockLevel = nLevel;
	s_nTexLockFace = nCubeFace;
	s_nTexLockX = nXOffset;
	s_nTexLockY = nYOffset;
	s_nTexLockW = nWidth;
	s_nTexLockH = nHeight;
	return true;
}

void TextureDx11_TexUnlock()
{
	TextureDx11_t *pTex = GetTexture( s_hTexLock );
	s_hTexLock = INVALID_SHADERAPI_TEXTURE_HANDLE;
	if ( !pTex || !pTex->m_pShadowMip0 )
		return;
	int nPixelBytes = ImageLoader::SizeInBytes( pTex->m_Format.m_UploadFormat );
	int nPagePitch = pTex->m_nWidth * nPixelBytes;
	UploadRect( pTex, s_nTexLockLevel, s_nTexLockFace, s_nTexLockX, s_nTexLockY,
		s_nTexLockW, s_nTexLockH, pTex->m_Format.m_UploadFormat, nPagePitch,
		pTex->m_pShadowMip0->Base() + s_nTexLockY * nPagePitch + s_nTexLockX * nPixelBytes );
}

void TextureDx11_Image2D( int nLevel, int nCubeFace, ImageFormat dstFormat, int nWidth, int nHeight,
	ImageFormat srcFormat, void *pData, int nZOffset )
{
	UploadRect( GetTexture( s_hModify ), nLevel, nCubeFace, 0, 0, nWidth, nHeight, srcFormat, 0, pData, nZOffset );
}

void TextureDx11_SubImage2D( int nLevel, int nCubeFace, int nXOffset, int nYOffset, int nWidth, int nHeight,
	ImageFormat srcFormat, int nSrcStride, void *pData )
{
	UploadRect( GetTexture( s_hModify ), nLevel, nCubeFace, nXOffset, nYOffset, nWidth, nHeight, srcFormat, nSrcStride, pData );
}

void TextureDx11_ImageFromVTF( IVTFTexture *pVTF, int nFrame )
{
	TextureDx11_t *pTex = GetTexture( s_hModify );
	if ( !pTex || !pVTF )
		return;

	// The texture may have been created with its top mips dropped (picmip /
	// LOD skipping): find the VTF mip whose dimensions match level 0.
	int nSkip = 0;
	while ( nSkip < pVTF->MipCount() - 1 &&
		( MAX( 1, pVTF->Width() >> nSkip ) > pTex->m_nWidth || MAX( 1, pVTF->Height() >> nSkip ) > pTex->m_nHeight ) )
	{
		++nSkip;
	}

	int nFaces = MIN( pVTF->FaceCount(), pTex->m_bCube ? 6 : 1 );
	for ( int nFace = 0; nFace < nFaces; ++nFace )
	{
		for ( int nLevel = 0; nLevel < pTex->m_nMipLevels; ++nLevel )
		{
			int nVTFMip = nSkip + nLevel;
			if ( nVTFMip >= pVTF->MipCount() )
				break;
			int nWidth = MAX( 1, pVTF->Width() >> nVTFMip );
			int nHeight = MAX( 1, pVTF->Height() >> nVTFMip );
			// Volume VTFs (e.g. the 32^3 color-correction LUTs, procedural
			// download path) carry every z-slice in the mip; UploadRect writes
			// one slice per call. Without this loop only the first slice's
			// bytes ever landed — slices 1..N stayed uninitialized VRAM.
			int nDepth = MAX( 1, pVTF->Depth() >> nVTFMip );
			for ( int nZ = 0; nZ < nDepth; ++nZ )
			{
				UploadRect( pTex, nLevel, nFace, 0, 0, nWidth, nHeight, pVTF->Format(), 0,
					pVTF->ImageData( nFrame, nFace, nVTFMip, 0, 0, nZ ), nZ );
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Sampler state
//-----------------------------------------------------------------------------
static D3D11_TEXTURE_ADDRESS_MODE TranslateWrap( ShaderTexWrapMode_t mode )
{
	switch ( mode )
	{
	case SHADER_TEXWRAPMODE_CLAMP: return D3D11_TEXTURE_ADDRESS_CLAMP;
	case SHADER_TEXWRAPMODE_REPEAT: return D3D11_TEXTURE_ADDRESS_WRAP;
	case SHADER_TEXWRAPMODE_BORDER: return D3D11_TEXTURE_ADDRESS_BORDER;
	default: return D3D11_TEXTURE_ADDRESS_CLAMP;
	}
}

// Min/mag/mip are packed into one D3D11 filter; track the pieces.
static void UpdateFilter( TextureDx11_t *pTex, bool bMin, ShaderTexFilterMode_t mode )
{
	// Depth maps stay POINT/CLAMP — raw depth must not be filtered before the
	// manual shadow compare, whatever CTexture's filter pushes ask for.
	if ( pTex->m_bDepth )
		return;
	bool bAniso = ( mode == SHADER_TEXFILTERMODE_ANISOTROPIC );
	bool bLinear = ( mode != SHADER_TEXFILTERMODE_NEAREST && mode != SHADER_TEXFILTERMODE_NEAREST_MIPMAP_NEAREST && mode != SHADER_TEXFILTERMODE_NEAREST_MIPMAP_LINEAR );
	bool bMipLinear = ( mode == SHADER_TEXFILTERMODE_LINEAR_MIPMAP_LINEAR || mode == SHADER_TEXFILTERMODE_NEAREST_MIPMAP_LINEAR || bAniso );

	D3D11_SAMPLER_DESC &s = pTex->m_SamplerDesc;
	if ( bAniso )
	{
		s.Filter = D3D11_FILTER_ANISOTROPIC;
		s.MaxAnisotropy = 8;
	}
	else
	{
		// Collapse to the common cases; per-axis min/mag splits are rare in vgui
		s.Filter = bLinear
			? ( bMipLinear ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT )
			: ( bMipLinear ? D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT );
	}
	pTex->m_bSamplerDirty = true;
}

void TextureDx11_MinFilter( ShaderTexFilterMode_t mode )
{
	TextureDx11_t *pTex = GetTexture( s_hModify );
	if ( pTex ) UpdateFilter( pTex, true, mode );
}

void TextureDx11_MagFilter( ShaderTexFilterMode_t mode )
{
	TextureDx11_t *pTex = GetTexture( s_hModify );
	if ( pTex ) UpdateFilter( pTex, false, mode );
}

void TextureDx11_Wrap( ShaderTexCoordComponent_t coord, ShaderTexWrapMode_t mode )
{
	TextureDx11_t *pTex = GetTexture( s_hModify );
	if ( !pTex || pTex->m_bDepth )
		return;
	D3D11_TEXTURE_ADDRESS_MODE addr = TranslateWrap( mode );
	if ( coord == SHADER_TEXCOORD_S ) pTex->m_SamplerDesc.AddressU = addr;
	else if ( coord == SHADER_TEXCOORD_T ) pTex->m_SamplerDesc.AddressV = addr;
	else pTex->m_SamplerDesc.AddressW = addr;
	pTex->m_bSamplerDirty = true;
}

ID3D11SamplerState *TextureDx11_GetSamplerState( ShaderAPITextureHandle_t h )
{
	if ( IsStandardHandle( h ) )
		return TextureDx11_GetDefaultSampler();
	TextureDx11_t *pTex = GetTexture( h );
	if ( !pTex )
		return TextureDx11_GetDefaultSampler();
	if ( pTex->m_bSamplerDirty || !pTex->m_pSampler )
	{
		if ( pTex->m_pSampler )
			pTex->m_pSampler->Release();
		pTex->m_pSampler = NULL;
		D3D11Device()->CreateSamplerState( &pTex->m_SamplerDesc, &pTex->m_pSampler );
		pTex->m_bSamplerDirty = false;
	}
	return pTex->m_pSampler ? pTex->m_pSampler : TextureDx11_GetDefaultSampler();
}

ID3D11ShaderResourceView *TextureDx11_GetSRV( ShaderAPITextureHandle_t h, bool bSRGB, bool bAllowCube )
{
	if ( IsStandardHandle( h ) )
	{
		ID3D11ShaderResourceView *pSRV = GetStandardSRV( StandardIdFromHandle( h ) );
		return pSRV ? pSRV : TextureDx11_GetWhiteSRV();
	}
	TextureDx11_t *pTex = GetTexture( h );
	if ( !pTex || !pTex->m_pSrvLinear )
		return TextureDx11_GetWhiteSRV();
	// Most bring-up shader slots declare Texture2D; binding a cube SRV there
	// is a debug-layer error. Slots whose shader declares TextureCube (the
	// phong envmap at s8) opt in via bAllowCube.
	if ( pTex->m_bCube && !bAllowCube )
		return TextureDx11_GetWhiteSRV();
	return bSRGB ? pTex->m_pSrvSRGB : pTex->m_pSrvLinear;
}


//-----------------------------------------------------------------------------
// Fallbacks
//-----------------------------------------------------------------------------
ID3D11ShaderResourceView *TextureDx11_GetWhiteSRV()
{
	if ( !s_pWhiteSRV && D3D11Device() )
	{
		const uint32 nWhite = 0xFFFFFFFF;
		D3D11_TEXTURE2D_DESC desc = { 1, 1, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 }, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
		D3D11_SUBRESOURCE_DATA data = { &nWhite, 4, 4 };
		if ( SUCCEEDED( D3D11Device()->CreateTexture2D( &desc, &data, &s_pWhiteTexture ) ) )
		{
			D3D11Device()->CreateShaderResourceView( s_pWhiteTexture, NULL, &s_pWhiteSRV );
		}
	}
	return s_pWhiteSRV;
}

// Opaque black (e.g. the eyes glint slot while RT textures are suppressed —
// the glint term is additive, so black = no glint, same as the LOD fallback).
ID3D11ShaderResourceView *TextureDx11_GetBlackSRV()
{
	if ( !s_pBlackSRV && D3D11Device() )
	{
		const uint32 nBlack = 0xFF000000;
		D3D11_TEXTURE2D_DESC desc = { 1, 1, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 }, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
		D3D11_SUBRESOURCE_DATA data = { &nBlack, 4, 4 };
		if ( SUCCEEDED( D3D11Device()->CreateTexture2D( &desc, &data, &s_pBlackTexture ) ) )
		{
			D3D11Device()->CreateShaderResourceView( s_pBlackTexture, NULL, &s_pBlackSRV );
		}
	}
	return s_pBlackSRV;
}

// Fallback for slots the shader declares as TextureCube (phong envmap s8):
// a 2D white there would trip the debug layer's view-dimension check.
ID3D11ShaderResourceView *TextureDx11_GetWhiteCubeSRV()
{
	if ( !s_pWhiteCubeSRV && D3D11Device() )
	{
		const uint32 nWhite = 0xFFFFFFFF;
		D3D11_TEXTURE2D_DESC desc = { 1, 1, 1, 6, DXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 },
			D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_TEXTURECUBE };
		D3D11_SUBRESOURCE_DATA data[6];
		for ( int i = 0; i < 6; ++i )
		{
			data[i].pSysMem = &nWhite;
			data[i].SysMemPitch = 4;
			data[i].SysMemSlicePitch = 4;
		}
		if ( SUCCEEDED( D3D11Device()->CreateTexture2D( &desc, data, &s_pWhiteCubeTexture ) ) )
		{
			D3D11Device()->CreateShaderResourceView( s_pWhiteCubeTexture, NULL, &s_pWhiteCubeSRV );
		}
	}
	return s_pWhiteCubeSRV;
}

ID3D11SamplerState *TextureDx11_GetDefaultSampler()
{
	if ( !s_pDefaultSampler && D3D11Device() )
	{
		D3D11_SAMPLER_DESC samp;
		ZeroMemory( &samp, sizeof( samp ) );
		samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samp.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samp.MaxLOD = D3D11_FLOAT32_MAX;
		D3D11Device()->CreateSamplerState( &samp, &s_pDefaultSampler );
	}
	return s_pDefaultSampler;
}

//-----------------------------------------------------------------------------
// Render-target views (lazy; textures created with TEXTURE_CREATE_RENDERTARGET)
//-----------------------------------------------------------------------------
ID3D11RenderTargetView *TextureDx11_GetRTV( ShaderAPITextureHandle_t h, bool bSRGB )
{
	TextureDx11_t *pTex = GetTexture( h );
	if ( !pTex || !pTex->m_bRenderTarget || pTex->m_bCube || !D3D11Device() )
		return NULL;

	if ( !pTex->m_pRtvLinear )
	{
		D3D11_RENDER_TARGET_VIEW_DESC rtv;
		ZeroMemory( &rtv, sizeof( rtv ) );
		rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtv.Format = pTex->m_Format.m_SrvLinear;
		D3D11Device()->CreateRenderTargetView( pTex->m_pTexture, &rtv, &pTex->m_pRtvLinear );
		if ( pTex->m_Format.m_SrvSRGB != pTex->m_Format.m_SrvLinear )
		{
			rtv.Format = pTex->m_Format.m_SrvSRGB;
			D3D11Device()->CreateRenderTargetView( pTex->m_pTexture, &rtv, &pTex->m_pRtvSRGB );
		}
		if ( !pTex->m_pRtvSRGB )
			pTex->m_pRtvSRGB = pTex->m_pRtvLinear;
	}
	return bSRGB ? pTex->m_pRtvSRGB : pTex->m_pRtvLinear;
}

bool TextureDx11_GetDims( ShaderAPITextureHandle_t h, int &nWidth, int &nHeight )
{
	TextureDx11_t *pTex = GetTexture( h );
	if ( !pTex )
		return false;
	nWidth = pTex->m_nWidth;
	nHeight = pTex->m_nHeight;
	return true;
}

bool TextureDx11_IsRenderTarget( ShaderAPITextureHandle_t h )
{
	TextureDx11_t *pTex = GetTexture( h );
	return pTex && pTex->m_bRenderTarget;
}

bool TextureDx11_IsVolume( ShaderAPITextureHandle_t h )
{
	TextureDx11_t *pTex = GetTexture( h );
	return pTex && pTex->m_pTexture3D != NULL;
}

//-----------------------------------------------------------------------------
// Shadow depth-map textures
//-----------------------------------------------------------------------------
ShaderAPITextureHandle_t TextureDx11_CreateDepth( int nWidth, int nHeight, const char *pDebugName )
{
	return CreateDepthOne( nWidth, nHeight, pDebugName );
}

bool TextureDx11_IsDepth( ShaderAPITextureHandle_t h )
{
	TextureDx11_t *pTex = GetTexture( h );
	return pTex && pTex->m_bDepth;
}

ID3D11DepthStencilView *TextureDx11_GetDSV( ShaderAPITextureHandle_t h )
{
	TextureDx11_t *pTex = GetTexture( h );
	return pTex ? pTex->m_pDsv : NULL;
}

void TextureDx11_ReleaseDevice()
{
	for ( int i = 0; i < s_Textures.Count(); ++i )
	{
		if ( s_Textures[i].m_bInUse )
			TextureDx11_Delete( i );
	}
	s_Textures.RemoveAll();
	if ( s_pWhiteSRV ) { s_pWhiteSRV->Release(); s_pWhiteSRV = NULL; }
	if ( s_pWhiteTexture ) { s_pWhiteTexture->Release(); s_pWhiteTexture = NULL; }
	if ( s_pWhiteCubeSRV ) { s_pWhiteCubeSRV->Release(); s_pWhiteCubeSRV = NULL; }
	if ( s_pWhiteCubeTexture ) { s_pWhiteCubeTexture->Release(); s_pWhiteCubeTexture = NULL; }
	if ( s_pBlackSRV ) { s_pBlackSRV->Release(); s_pBlackSRV = NULL; }
	if ( s_pBlackTexture ) { s_pBlackTexture->Release(); s_pBlackTexture = NULL; }
	if ( s_pDefaultSampler ) { s_pDefaultSampler->Release(); s_pDefaultSampler = NULL; }
	for ( int i = 0; i < TEXTURE_MAX_STD_TEXTURES; ++i )
	{
		if ( s_pStdSRV[i] ) { s_pStdSRV[i]->Release(); s_pStdSRV[i] = NULL; }
		if ( s_pStdTexture[i] ) { s_pStdTexture[i]->Release(); s_pStdTexture[i] = NULL; }
	}
	s_hModify = INVALID_SHADERAPI_TEXTURE_HANDLE;
}
