//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX11 render state (M3): snapshots -> precreated state objects,
// matrix stacks (D3DX row-vector convention, matching what materialsystem
// hands the dx9 backend), the half-pixel projection fold, and the per-draw
// commit binding the universal bring-up shaders.
//
//===========================================================================//

#include <d3d11.h>
#include <d3d11_1.h>

#include "statedx11.h"
#include "texturedx11.h"
#include "shadermanagerdx11.h"
#include "shaderapidx11_global.h"
#include "shaderapi_global.h"
#include "shaderapi/ishaderutil.h"
#include "shaderdevicedx11.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialvar.h"
#include "tier1/utlvector.h"
#include "tier1/convar.h"
#include "tier1/strtools.h"

// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"

// dx9 ps20b families address s0..s15; the skin (phong) family alone uses
// s0..s14 (base, phongwarp, lightwarp, bump, flashlight x3, phongexp, envmap,
// detail/selfillum, wrinkle x2, AO, fresnel mask — skin_dx9_helper.cpp).
#define DX11_MAX_SAMPLERS 16


//-----------------------------------------------------------------------------
// Snapshots
//-----------------------------------------------------------------------------
struct SnapshotKeyDx11_t
{
	bool m_bDepthTest;
	bool m_bDepthWrite;
	uint8 m_nDepthFunc;			// D3D11_COMPARISON_FUNC
	bool m_bBlend;
	uint8 m_nSrcBlend, m_nDstBlend;	// D3D11_BLEND
	bool m_bBlendSeparateAlpha;
	uint8 m_nSrcBlendAlpha, m_nDstBlendAlpha;
	bool m_bAlphaTest;
	uint8 m_nAlphaFunc;			// ShaderAlphaFunc_t
	float m_flAlphaRef;
	bool m_bCullEnable;
	bool m_bWireframe;
	uint8 m_nPolyOffsetMode;	// PolygonOffsetMode_t: 0 disable, 1 = dx9
								// SHADER_POLYOFFSET_DECAL (D3DRS_DEPTHBIAS),
								// 2 = SHADER_POLYOFFSET_SHADOW_BIAS (the
								// DepthWrite caster pass; factors from
								// SetShadowDepthBiasFactors)
	bool m_bColorWrite;
	bool m_bAlphaWrite;
	bool m_bSRGBWrite;
	// ShaderFogMode_t (FogToFogColor/FogToBlack/...): picks the per-pass fog
	// COLOR. 0 = SHADER_FOGMODE_DISABLED (the dx9 shadow default) = no fog.
	uint8 m_nFogMode;
	bool m_bDisableFogGammaCorrection;
	bool m_bSamplerEnabled[DX11_MAX_SAMPLERS];
	bool m_bSamplerSRGB[DX11_MAX_SAMPLERS];
	VertexFormat_t m_VertexFormat;
};

struct SnapshotDx11_t
{
	SnapshotKeyDx11_t m_Key;
	ID3D11BlendState *m_pBlend;
	ID3D11DepthStencilState *m_pDepth;
};

static SnapshotKeyDx11_t s_Building;
static CUtlVector<SnapshotDx11_t> s_Snapshots;
static int s_nCurrentSnapshot = -1;
static bool s_bSnapshotDirty = true;


static D3D11_BLEND TranslateBlend( ShaderBlendFactor_t f )
{
	switch ( f )
	{
	case SHADER_BLEND_ZERO: return D3D11_BLEND_ZERO;
	case SHADER_BLEND_ONE: return D3D11_BLEND_ONE;
	case SHADER_BLEND_DST_COLOR: return D3D11_BLEND_DEST_COLOR;
	case SHADER_BLEND_ONE_MINUS_DST_COLOR: return D3D11_BLEND_INV_DEST_COLOR;
	case SHADER_BLEND_SRC_ALPHA: return D3D11_BLEND_SRC_ALPHA;
	case SHADER_BLEND_ONE_MINUS_SRC_ALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
	case SHADER_BLEND_DST_ALPHA: return D3D11_BLEND_DEST_ALPHA;
	case SHADER_BLEND_ONE_MINUS_DST_ALPHA: return D3D11_BLEND_INV_DEST_ALPHA;
	case SHADER_BLEND_SRC_ALPHA_SATURATE: return D3D11_BLEND_SRC_ALPHA_SAT;
	case SHADER_BLEND_SRC_COLOR: return D3D11_BLEND_SRC_COLOR;
	case SHADER_BLEND_ONE_MINUS_SRC_COLOR: return D3D11_BLEND_INV_SRC_COLOR;
	default: return D3D11_BLEND_ONE;
	}
}

static D3D11_COMPARISON_FUNC TranslateDepthFunc( ShaderDepthFunc_t f )
{
	switch ( f )
	{
	case SHADER_DEPTHFUNC_NEVER: return D3D11_COMPARISON_NEVER;
	case SHADER_DEPTHFUNC_NEARER: return D3D11_COMPARISON_LESS;
	case SHADER_DEPTHFUNC_EQUAL: return D3D11_COMPARISON_EQUAL;
	case SHADER_DEPTHFUNC_NEAREROREQUAL: return D3D11_COMPARISON_LESS_EQUAL;
	case SHADER_DEPTHFUNC_FARTHER: return D3D11_COMPARISON_GREATER;
	case SHADER_DEPTHFUNC_NOTEQUAL: return D3D11_COMPARISON_NOT_EQUAL;
	case SHADER_DEPTHFUNC_FARTHEROREQUAL: return D3D11_COMPARISON_GREATER_EQUAL;
	case SHADER_DEPTHFUNC_ALWAYS: return D3D11_COMPARISON_ALWAYS;
	default: return D3D11_COMPARISON_LESS_EQUAL;
	}
}

void StateDx11_ShadowSetDefault()
{
	memset( &s_Building, 0, sizeof( s_Building ) );
	s_Building.m_bDepthTest = true;
	s_Building.m_bDepthWrite = true;
	s_Building.m_nDepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	s_Building.m_nSrcBlend = D3D11_BLEND_ONE;
	s_Building.m_nDstBlend = D3D11_BLEND_ZERO;
	s_Building.m_nSrcBlendAlpha = D3D11_BLEND_ONE;
	s_Building.m_nDstBlendAlpha = D3D11_BLEND_ZERO;
	s_Building.m_nAlphaFunc = SHADER_ALPHAFUNC_GEQUAL;
	s_Building.m_flAlphaRef = 0.5f;
	s_Building.m_bCullEnable = true;
	s_Building.m_bColorWrite = true;
	s_Building.m_bAlphaWrite = true;
}

void StateDx11_ShadowDepthFunc( ShaderDepthFunc_t f ) { s_Building.m_nDepthFunc = (uint8)TranslateDepthFunc( f ); }
void StateDx11_ShadowEnableDepthWrites( bool b ) { s_Building.m_bDepthWrite = b; }
void StateDx11_ShadowEnableDepthTest( bool b ) { s_Building.m_bDepthTest = b; }
void StateDx11_ShadowEnableColorWrites( bool b ) { s_Building.m_bColorWrite = b; }
void StateDx11_ShadowEnableAlphaWrites( bool b ) { s_Building.m_bAlphaWrite = b; }
void StateDx11_ShadowEnableBlending( bool b ) { s_Building.m_bBlend = b; }
void StateDx11_ShadowBlendFunc( ShaderBlendFactor_t s, ShaderBlendFactor_t d )
{
	s_Building.m_nSrcBlend = (uint8)TranslateBlend( s );
	s_Building.m_nDstBlend = (uint8)TranslateBlend( d );
}
void StateDx11_ShadowEnableBlendingSeparateAlpha( bool b ) { s_Building.m_bBlendSeparateAlpha = b; }
void StateDx11_ShadowBlendFuncSeparateAlpha( ShaderBlendFactor_t s, ShaderBlendFactor_t d )
{
	s_Building.m_nSrcBlendAlpha = (uint8)TranslateBlend( s );
	s_Building.m_nDstBlendAlpha = (uint8)TranslateBlend( d );
}
void StateDx11_ShadowEnableAlphaTest( bool b ) { s_Building.m_bAlphaTest = b; }
void StateDx11_ShadowAlphaFunc( ShaderAlphaFunc_t f, float flRef )
{
	s_Building.m_nAlphaFunc = (uint8)f;
	s_Building.m_flAlphaRef = flRef;
}
void StateDx11_ShadowEnableCulling( bool b ) { s_Building.m_bCullEnable = b; }
void StateDx11_ShadowPolyMode( bool bWireframe ) { s_Building.m_bWireframe = bWireframe; }
// Decals redraw clipped copies of the surface polys COPLANAR with the wall —
// without the dx9 polygon offset they z-fight (scorch marks/blood shimmer).
// Mode 2 (SHADOW_BIAS) is the DepthWrite caster snapshot: slope-scaled bias
// pushes caster depth away from the light to keep receivers acne-free.
void StateDx11_ShadowPolyOffset( int nMode ) { s_Building.m_nPolyOffsetMode = (uint8)nMode; }
void StateDx11_ShadowEnableSRGBWrite( bool b ) { s_Building.m_bSRGBWrite = b; }
void StateDx11_ShadowEnableSRGBRead( int nSampler, bool b )
{
	if ( nSampler >= 0 && nSampler < DX11_MAX_SAMPLERS )
		s_Building.m_bSamplerSRGB[nSampler] = b;
}
void StateDx11_ShadowEnableTexture( int nSampler, bool b )
{
	if ( nSampler >= 0 && nSampler < DX11_MAX_SAMPLERS )
		s_Building.m_bSamplerEnabled[nSampler] = b;
}
void StateDx11_ShadowVertexFormat( VertexFormat_t fmt ) { s_Building.m_VertexFormat = fmt; }
void StateDx11_ShadowFogMode( int nShaderFogMode ) { s_Building.m_nFogMode = (uint8)nShaderFogMode; }
void StateDx11_ShadowDisableFogGammaCorrection( bool bDisable ) { s_Building.m_bDisableFogGammaCorrection = bDisable; }

StateSnapshot_t StateDx11_TakeSnapshot()
{
	for ( int i = 0; i < s_Snapshots.Count(); ++i )
	{
		if ( memcmp( &s_Snapshots[i].m_Key, &s_Building, sizeof( s_Building ) ) == 0 )
			return i;
	}

	int i = s_Snapshots.AddToTail();
	SnapshotDx11_t &snap = s_Snapshots[i];
	snap.m_Key = s_Building;
	snap.m_pBlend = NULL;
	snap.m_pDepth = NULL;

	if ( D3D11Device() )
	{
		D3D11_BLEND_DESC blend;
		ZeroMemory( &blend, sizeof( blend ) );
		blend.RenderTarget[0].BlendEnable = s_Building.m_bBlend;
		blend.RenderTarget[0].SrcBlend = (D3D11_BLEND)s_Building.m_nSrcBlend;
		blend.RenderTarget[0].DestBlend = (D3D11_BLEND)s_Building.m_nDstBlend;
		blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		if ( s_Building.m_bBlendSeparateAlpha )
		{
			blend.RenderTarget[0].SrcBlendAlpha = (D3D11_BLEND)s_Building.m_nSrcBlendAlpha;
			blend.RenderTarget[0].DestBlendAlpha = (D3D11_BLEND)s_Building.m_nDstBlendAlpha;
		}
		else
		{
			// Color factors reduced to their alpha-legal equivalents
			D3D11_BLEND sa = (D3D11_BLEND)s_Building.m_nSrcBlend;
			D3D11_BLEND da = (D3D11_BLEND)s_Building.m_nDstBlend;
			if ( sa == D3D11_BLEND_DEST_COLOR ) sa = D3D11_BLEND_DEST_ALPHA;
			if ( sa == D3D11_BLEND_INV_DEST_COLOR ) sa = D3D11_BLEND_INV_DEST_ALPHA;
			if ( sa == D3D11_BLEND_SRC_COLOR ) sa = D3D11_BLEND_SRC_ALPHA;
			if ( sa == D3D11_BLEND_INV_SRC_COLOR ) sa = D3D11_BLEND_INV_SRC_ALPHA;
			if ( da == D3D11_BLEND_DEST_COLOR ) da = D3D11_BLEND_DEST_ALPHA;
			if ( da == D3D11_BLEND_INV_DEST_COLOR ) da = D3D11_BLEND_INV_DEST_ALPHA;
			if ( da == D3D11_BLEND_SRC_COLOR ) da = D3D11_BLEND_SRC_ALPHA;
			if ( da == D3D11_BLEND_INV_SRC_COLOR ) da = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].SrcBlendAlpha = sa;
			blend.RenderTarget[0].DestBlendAlpha = da;
		}
		blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		UINT nMask = 0;
		if ( s_Building.m_bColorWrite )
			nMask |= D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
		if ( s_Building.m_bAlphaWrite )
			nMask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
		blend.RenderTarget[0].RenderTargetWriteMask = (UINT8)nMask;
		D3D11Device()->CreateBlendState( &blend, &snap.m_pBlend );

		D3D11_DEPTH_STENCIL_DESC depth;
		ZeroMemory( &depth, sizeof( depth ) );
		depth.DepthEnable = s_Building.m_bDepthTest;
		depth.DepthWriteMask = s_Building.m_bDepthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
		depth.DepthFunc = (D3D11_COMPARISON_FUNC)s_Building.m_nDepthFunc;
		D3D11Device()->CreateDepthStencilState( &depth, &snap.m_pDepth );
	}

	return i;
}

// Modulation ($color, dx9 ps c1) is only meaningful if the CURRENT pass wrote
// it — c1 carries different semantics per shader family, so a stale value
// from another material must not bleed into draws that never set it (that
// flickered/blacked UI panels and props).
static bool s_bModulationValid = false;

// dx9 PS float-constant mirror (c0..c31, shader_constant_register_map.h):
// the dx9 family dynamic blocks upload their per-pass constants through
// SetPixelShaderConstant / the command buffer — captured here and published
// to the per-pixel shading permutations as cbuffer b2. Register semantics
// differ per shader family, so validity is PER-PASS (reset at UseSnapshot)
// exactly like the c1 modulation latch; never-written registers upload as 0.
// EXCEPTION: c30 (cLightScale, SetToneMappingScaleLinear) is a dx9 STANDARD
// constant the API maintains across material changes — it survives the
// per-pass reset or every envmap/tonemap read after the first material
// change would see zeros (HDR cubemaps store /16: australiums went 16x dark).
#define DX11_PS_MIRROR_CONSTANTS 32
#define DX11_PS_MIRROR_PERSISTENT_MASK ( 1u << 30 )
static float s_flPSMirror[DX11_PS_MIRROR_CONSTANTS * 4];
static unsigned int s_nPSMirrorValid = 0;	// bit n = c<n> written this pass
static bool s_bPSMirrorDirty = false;

// dx9 VS float-constant mirror (c0..c63) — same per-pass model as the PS one.
// The family dynamic blocks write texture transforms here
// (SetVertexShaderTextureTransform → c48/c49 base, c52/c53 detail —
// VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 = 48). Published as the VS-stage b2.
#define DX11_VS_MIRROR_CONSTANTS 64
static float s_flVSMirror[DX11_VS_MIRROR_CONSTANTS * 4];
static uint64 s_nVSMirrorValid = 0;
static bool s_bVSMirrorDirty = false;

void StateDx11_SetVSConstants( int nFirst, const float *pValues, int nCount )
{
	if ( nFirst < 0 || !pValues || nCount <= 0 || nFirst >= DX11_VS_MIRROR_CONSTANTS )
		return;
	if ( nFirst + nCount > DX11_VS_MIRROR_CONSTANTS )
		nCount = DX11_VS_MIRROR_CONSTANTS - nFirst;
	memcpy( s_flVSMirror + nFirst * 4, pValues, nCount * 4 * sizeof( float ) );
	for ( int i = 0; i < nCount; ++i )
		s_nVSMirrorValid |= 1ull << ( nFirst + i );
	s_bVSMirrorDirty = true;
}

void StateDx11_UseSnapshot( StateSnapshot_t id )
{
	s_nCurrentSnapshot = id;
	s_bSnapshotDirty = true;
	s_bModulationValid = false;
	s_nPSMirrorValid &= DX11_PS_MIRROR_PERSISTENT_MASK;	// c30 persists (see above)
	s_bPSMirrorDirty = true;
	s_nVSMirrorValid = 0;
	s_bVSMirrorDirty = true;
}

static const SnapshotDx11_t *CurrentSnapshot()
{
	if ( s_nCurrentSnapshot < 0 || s_nCurrentSnapshot >= s_Snapshots.Count() )
		return NULL;
	return &s_Snapshots[s_nCurrentSnapshot];
}

bool StateDx11_IsTranslucent( StateSnapshot_t id )
{
	return ( id >= 0 && id < s_Snapshots.Count() ) ? s_Snapshots[id].m_Key.m_bBlend : false;
}
bool StateDx11_IsAlphaTested( StateSnapshot_t id )
{
	return ( id >= 0 && id < s_Snapshots.Count() ) ? s_Snapshots[id].m_Key.m_bAlphaTest : false;
}
bool StateDx11_IsDepthWriteEnabled( StateSnapshot_t id )
{
	return ( id >= 0 && id < s_Snapshots.Count() ) ? s_Snapshots[id].m_Key.m_bDepthWrite : true;
}
VertexFormat_t StateDx11_ComputeVertexFormat( int nCount, StateSnapshot_t *pIds )
{
	// Identical snapshots dominate in practice; a bitwise OR is a sufficient
	// merge until the real per-element union lands with the mesh system.
	VertexFormat_t fmt = 0;
	for ( int i = 0; i < nCount; ++i )
	{
		if ( pIds[i] >= 0 && pIds[i] < s_Snapshots.Count() )
			fmt |= s_Snapshots[pIds[i]].m_Key.m_VertexFormat;
	}
	return fmt;
}


//-----------------------------------------------------------------------------
// Matrix stacks (D3DX row-vector convention; v' = v * M * V * P)
//-----------------------------------------------------------------------------
typedef float Matrix44Dx11_t[4][4];
#define DX11_MATRIX_STACK_DEPTH 16

static Matrix44Dx11_t s_MatrixStacks[NUM_MATRIX_MODES][DX11_MATRIX_STACK_DEPTH];
static int s_nStackTop[NUM_MATRIX_MODES];
static MaterialMatrixMode_t s_nMatrixMode = MATERIAL_VIEW;
static bool s_bMatricesDirty = true;
// Model-path bone state (defined with the universal-shader section below):
// explicit LoadBoneMatrix data wins until the MODEL stack is driven again
static bool s_bBonesExplicit = false;

static inline void OnMatrixStackWrite()
{
	s_bMatricesDirty = true;
	if ( s_nMatrixMode == MATERIAL_MODEL )
		s_bBonesExplicit = false;
}

static void Mat44Identity( Matrix44Dx11_t m )
{
	memset( m, 0, sizeof( Matrix44Dx11_t ) );
	m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
}

// C = A * B (row-vector convention chain: apply A, then B)
static void Mat44Multiply( const Matrix44Dx11_t a, const Matrix44Dx11_t b, Matrix44Dx11_t out )
{
	Matrix44Dx11_t tmp;
	for ( int i = 0; i < 4; ++i )
	{
		for ( int j = 0; j < 4; ++j )
		{
			tmp[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
		}
	}
	memcpy( out, tmp, sizeof( tmp ) );
}

static float *TopMatrix( MaterialMatrixMode_t mode )
{
	return &s_MatrixStacks[mode][s_nStackTop[mode]][0][0];
}

static void EnsureMatrixInit()
{
	static bool s_bInit = false;
	if ( s_bInit )
		return;
	s_bInit = true;
	for ( int i = 0; i < NUM_MATRIX_MODES; ++i )
	{
		s_nStackTop[i] = 0;
		Mat44Identity( s_MatrixStacks[i][0] );
	}
}

void StateDx11_MatrixMode( MaterialMatrixMode_t mode )
{
	EnsureMatrixInit();
	if ( mode >= 0 && mode < NUM_MATRIX_MODES )
		s_nMatrixMode = mode;
}

void StateDx11_PushMatrix()
{
	EnsureMatrixInit();
	int &top = s_nStackTop[s_nMatrixMode];
	if ( top + 1 < DX11_MATRIX_STACK_DEPTH )
	{
		memcpy( s_MatrixStacks[s_nMatrixMode][top + 1], s_MatrixStacks[s_nMatrixMode][top], sizeof( Matrix44Dx11_t ) );
		++top;
	}
}

void StateDx11_PopMatrix()
{
	EnsureMatrixInit();
	int &top = s_nStackTop[s_nMatrixMode];
	if ( top > 0 )
		--top;
	OnMatrixStackWrite();
}

void StateDx11_LoadMatrix( const float *pM )
{
	EnsureMatrixInit();
	memcpy( TopMatrix( s_nMatrixMode ), pM, 16 * sizeof( float ) );
	OnMatrixStackWrite();
}

void StateDx11_MultMatrix( const float *pM )
{
	EnsureMatrixInit();
	Matrix44Dx11_t *pTop = &s_MatrixStacks[s_nMatrixMode][s_nStackTop[s_nMatrixMode]];
	Mat44Multiply( *pTop, *(const Matrix44Dx11_t *)pM, *pTop );
	OnMatrixStackWrite();
}

void StateDx11_MultMatrixLocal( const float *pM )
{
	EnsureMatrixInit();
	Matrix44Dx11_t *pTop = &s_MatrixStacks[s_nMatrixMode][s_nStackTop[s_nMatrixMode]];
	Mat44Multiply( *(const Matrix44Dx11_t *)pM, *pTop, *pTop );
	OnMatrixStackWrite();
}

void StateDx11_GetMatrix( MaterialMatrixMode_t mode, float *pDst )
{
	EnsureMatrixInit();
	if ( mode >= 0 && mode < NUM_MATRIX_MODES )
		memcpy( pDst, TopMatrix( mode ), 16 * sizeof( float ) );
}

void StateDx11_LoadIdentity()
{
	EnsureMatrixInit();
	Mat44Identity( s_MatrixStacks[s_nMatrixMode][s_nStackTop[s_nMatrixMode]] );
	OnMatrixStackWrite();
}

void StateDx11_Ortho( double l, double t, double r, double b, double zn, double zf )
{
	EnsureMatrixInit();
	// D3DXMatrixOrthoOffCenterRH, row-vector convention (matches the dx9 path)
	Matrix44Dx11_t m;
	memset( m, 0, sizeof( m ) );
	m[0][0] = (float)( 2.0 / ( r - l ) );
	m[1][1] = (float)( 2.0 / ( t - b ) );
	m[2][2] = (float)( 1.0 / ( zn - zf ) );
	m[3][0] = (float)( ( l + r ) / ( l - r ) );
	m[3][1] = (float)( ( t + b ) / ( b - t ) );
	m[3][2] = (float)( zn / ( zn - zf ) );
	m[3][3] = 1.0f;

	Matrix44Dx11_t *pTop = &s_MatrixStacks[s_nMatrixMode][s_nStackTop[s_nMatrixMode]];
	Mat44Multiply( m, *pTop, *pTop );
	s_bMatricesDirty = true;
}

// D3DXMatrixPerspectiveRH equivalent (row-vector), premultiplied like dx8's
// MultMatrixLocal. w/h derive from fovx exactly as CShaderAPIDx8 does.
void StateDx11_PerspectiveX( double fovx, double aspect, double zn, double zf )
{
	EnsureMatrixInit();
	double w = 2.0 * zn * tan( fovx * M_PI / 360.0 );
	double h = w / aspect;

	Matrix44Dx11_t m;
	memset( m, 0, sizeof( m ) );
	m[0][0] = (float)( 2.0 * zn / w );
	m[1][1] = (float)( 2.0 * zn / h );
	m[2][2] = (float)( zf / ( zn - zf ) );
	m[2][3] = -1.0f;
	m[3][2] = (float)( zn * zf / ( zn - zf ) );

	Matrix44Dx11_t *pTop = &s_MatrixStacks[s_nMatrixMode][s_nStackTop[s_nMatrixMode]];
	Mat44Multiply( m, *pTop, *pTop );
	s_bMatricesDirty = true;
}

void StateDx11_PerspectiveOffCenterX( double fovx, double aspect, double zn, double zf,
	double bottom, double top, double left, double right )
{
	EnsureMatrixInit();
	double w = 2.0 * zn * tan( fovx * M_PI / 360.0 );
	double h = w / aspect;

	// bottom/top/left/right arrive 0..1; remap to front-plane extents (dx8 math)
	float l = (float)( -( w / 2.0 ) * ( 1.0 - left ) + left * ( w / 2.0 ) );
	float r = (float)( -( w / 2.0 ) * ( 1.0 - right ) + right * ( w / 2.0 ) );
	float b = (float)( -( h / 2.0 ) * ( 1.0 - bottom ) + bottom * ( h / 2.0 ) );
	float t = (float)( -( h / 2.0 ) * ( 1.0 - top ) + top * ( h / 2.0 ) );

	// D3DXMatrixPerspectiveOffCenterRH (row-vector)
	Matrix44Dx11_t m;
	memset( m, 0, sizeof( m ) );
	m[0][0] = (float)( 2.0 * zn / ( r - l ) );
	m[1][1] = (float)( 2.0 * zn / ( t - b ) );
	m[2][0] = ( l + r ) / ( r - l );
	m[2][1] = ( t + b ) / ( t - b );
	m[2][2] = (float)( zf / ( zn - zf ) );
	m[2][3] = -1.0f;
	m[3][2] = (float)( zn * zf / ( zn - zf ) );

	Matrix44Dx11_t *pTop = &s_MatrixStacks[s_nMatrixMode][s_nStackTop[s_nMatrixMode]];
	Mat44Multiply( m, *pTop, *pTop );
	s_bMatricesDirty = true;
}

void StateDx11_Rotate( float flAngleDegrees, float x, float y, float z )
{
	EnsureMatrixInit();
	float flLen = sqrtf( x * x + y * y + z * z );
	if ( flLen == 0.0f )
		return;
	x /= flLen; y /= flLen; z /= flLen;
	float a = (float)( M_PI * flAngleDegrees / 180.0f );
	float c = cosf( a ), s = sinf( a ), ic = 1.0f - c;

	// D3DXMatrixRotationAxis layout (row-vector convention)
	Matrix44Dx11_t m;
	Mat44Identity( m );
	m[0][0] = c + x * x * ic;     m[0][1] = x * y * ic + z * s; m[0][2] = x * z * ic - y * s;
	m[1][0] = x * y * ic - z * s; m[1][1] = c + y * y * ic;     m[1][2] = y * z * ic + x * s;
	m[2][0] = x * z * ic + y * s; m[2][1] = y * z * ic - x * s; m[2][2] = c + z * z * ic;

	Matrix44Dx11_t *pTop = &s_MatrixStacks[s_nMatrixMode][s_nStackTop[s_nMatrixMode]];
	Mat44Multiply( m, *pTop, *pTop );
	OnMatrixStackWrite();
}

void StateDx11_Translate( float x, float y, float z )
{
	EnsureMatrixInit();
	Matrix44Dx11_t m;
	Mat44Identity( m );
	m[3][0] = x; m[3][1] = y; m[3][2] = z;

	Matrix44Dx11_t *pTop = &s_MatrixStacks[s_nMatrixMode][s_nStackTop[s_nMatrixMode]];
	Mat44Multiply( m, *pTop, *pTop );
	OnMatrixStackWrite();
}

void StateDx11_Scale( float x, float y, float z )
{
	EnsureMatrixInit();
	Matrix44Dx11_t m;
	Mat44Identity( m );
	m[0][0] = x; m[1][1] = y; m[2][2] = z;

	Matrix44Dx11_t *pTop = &s_MatrixStacks[s_nMatrixMode][s_nStackTop[s_nMatrixMode]];
	Mat44Multiply( m, *pTop, *pTop );
	OnMatrixStackWrite();
}


//-----------------------------------------------------------------------------
// Viewports, scissor, dynamic cull, bound textures
//-----------------------------------------------------------------------------
static ShaderViewport_t s_Viewport;
static bool s_bScissorEnabled = false;
static D3D11_RECT s_ScissorRect = { 0, 0, 0, 0 };
static MaterialCullMode_t s_nDynamicCullMode = MATERIAL_CULLMODE_CCW;
static ShaderAPITextureHandle_t s_BoundTextures[DX11_MAX_SAMPLERS] =
	{ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

void StateDx11_SetViewports( int nCount, const ShaderViewport_t *pViewports )
{
	if ( nCount < 1 || !pViewports )
		return;
	s_Viewport = pViewports[0];
	if ( D3D11Context() )
	{
		D3D11_VIEWPORT vp;
		vp.TopLeftX = (float)s_Viewport.m_nTopLeftX;
		vp.TopLeftY = (float)s_Viewport.m_nTopLeftY;
		vp.Width = (float)s_Viewport.m_nWidth;
		vp.Height = (float)s_Viewport.m_nHeight;
		vp.MinDepth = s_Viewport.m_flMinZ;
		vp.MaxDepth = s_Viewport.m_flMaxZ;
		D3D11Context()->RSSetViewports( 1, &vp );
	}
	s_bMatricesDirty = true;
}

int StateDx11_GetViewports( ShaderViewport_t *pViewports, int nMax )
{
	if ( pViewports && nMax >= 1 )
		pViewports[0] = s_Viewport;
	return 1;
}

// $color/$alpha tint (dx9 PSREG_DIFFUSE_MODULATION), latched per pass — see
// s_bModulationValid at UseSnapshot.
static float s_flModulation[4] = { 1, 1, 1, 1 };
static bool s_bModulationDirty = false;

// dx9 STATIC combos die with the ignored dx9 shader index, but the material
// params survive: resolve the structural ones at Bind.
// Tint: [0] = $blendtintbybasealpha (base alpha = paint mask), [1] =
// $tintreplacesbasecolor, [2] = $selfillum (the MATERIAL_VAR flag — base
// alpha masks the emissive). Phong flags: x = $halflambert, y = has
// $lightwarptexture, z = has $bumpmap, w = has $phongexponenttexture; $phong
// itself routes the draw to the per-pixel skin perms (5/6).
// FindVarFast = token-cached lookup (engine hot-loop pattern, beamdraw.cpp:74).
static float s_flTintControl[3] = { 0, 0, 0 };
static float s_flPhongFlags[4] = { 0, 0, 0, 0 };
static float s_flSelfIllumFresnel = 0.0f;	// $selfillumfresnel (badge glow)
// VS register holding the $basetexturetransform rows for THIS material's
// family, or -1 when the material has no transform param. The register
// differs per family (the c1 lesson again): most families use
// SHADER_SPECIFIC_CONST_0 = c48, but Sky keeps texture-size info there and
// its transform at c49 (sky_vs20.fxc:3-4) — applying c48 as a matrix
// mangled the whole skybox.
static float s_flVSTransformReg = -1.0f;
// $detail blend mode (TCOMBINE_*, common_ps_fxc.h:666) or -1 when the
// material has no detail texture. The detail sampler/constants are family-
// fixed and hardcoded per shader perm (vertexlit s2/c10/c4.w, lightmapped
// s12/c8, skin s13/c0.w).
static float s_flDetailBlendMode = -1.0f;
static bool s_bMaterialPhong = false;
// MATERIAL_VAR2_LIGHTING_VERTEX_LIT from $flags2: only vertex-lit shader
// families may take the lit model perms. Routing every static mesh with
// normals there painted UNLIT materials black wherever the ambient cube was
// zero — the 2fort sun is an unlit 3D-skybox prop model.
static bool s_bMaterialVertexLit = false;
// Cable family (ropes/wires): s0 is the NORMAL map and s1 the base texture —
// the inverse of every other family's slot use, so it gets its own perm
// instead of the lightmap heuristic (which painted the wires normal-map pink).
static bool s_bMaterialCable = false;
// Eyes family: sphere-normal lighting + planar iris/glint projections (perm 8)
static bool s_bMaterialEyes = false;
// WindowImposter family: areaportal window glass — eye ray into a cube (perm 9)
static bool s_bMaterialWindowImposter = false;
// EyeRefract family: the TF2 player eyeball (perm 10)
static bool s_bMaterialEyeRefract = false;
// Teeth family: vertexlit lighting dimmed by $illumfactor x dot(N, $forward) (perm 11)
static bool s_bMaterialTeeth = false;
// pyro_vision family (pyroland replacements): perm 12 world / 13 VERTEX_LIT
static bool s_bMaterialPyro = false;
// x = $effect (0/1), y = flag bits (1 vertexcolor, 2 fullbright,
// 4 basetexture2, 8 fancyblending, 16 colorbar, 32 stripes)
static float s_flPyroControl[2] = { 0, 0 };
// SpriteCard family (particles): perm 14. x = flag bits (1 addbasetexture2,
// 2 addself, 4 animblend, 8 dualsequence, 16/32 maxlumframeblend1/2,
// 64 colorramp, 128 extractgreenalpha, 256 depthblend), y = $orientation,
// z = $sequence_blend_mode
static bool s_bMaterialSpriteCard = false;
static float s_flSpriteControl[3] = { 0, 0, 0 };
// ps c12 carries MODULATION only for the lightmappedgeneric helper families
// (LightmappedGeneric/WorldVertexTransition, helper:687). pyro_vision keeps
// {writeDepthToAlpha, TIME, ...} there and vertexlitgeneric its shader
// controls — reading those as a color multiplied the pyroland world by TIME
// (solid green). The c1 lesson, third edition: registers are per-family.
static bool s_bMaterialLMModC12 = false;
// EyeRefract static combos: x = $raytracesphere, y = $spheretexkillcombo
static float s_flEyeControl[2] = { 0, 0 };
// dx9 scene-fog state (SceneFogMode/FogStart/End/SetFogZ/SceneFogColor3ub).
// Mode 2 (MATERIAL_FOG_LINEAR_BELOW_FOG_Z) = the water-refraction/underwater
// views: opaque draws write the water-fog factor to dest alpha (the dx9
// WRITEWATERFOGTODESTALPHA combo — the water surface samples it back as
// per-pixel fog depth) and fog-enabled passes height-lerp their rgb. Mode 1
// (MATERIAL_FOG_LINEAR) = world range fog: dx9 does it with fixed-function
// vertex fog (no such thing in D3D11), so the universal PS applies the same
// UpdateVertexShaderFogParams math per pixel.
static int s_nSceneFogMode = 0;	// MaterialFogMode_t: 0 none, 1 linear, 2 below-fog-z
static float s_flFogWaterZ = 0.0f;
static float s_flFogOORange = 0.0f;
static float s_flFogEndOverRange = 0.0f;
static float s_flFogMaxDensityFloor = 0.0f;	// dx9 cFogMaxDensity = 1 - $fogmaxdensity
static float s_flSceneFogColor[3] = { 0, 0, 0 };

void StateDx11_SetSceneFogState( int nSceneFogMode, float flWaterZ, float flOORange,
	float flFogEndOverRange, float flMaxDensityFloor, const float pFogColor3[3] )
{
	if ( nSceneFogMode != s_nSceneFogMode || flWaterZ != s_flFogWaterZ ||
		 flOORange != s_flFogOORange || flFogEndOverRange != s_flFogEndOverRange ||
		 flMaxDensityFloor != s_flFogMaxDensityFloor ||
		 memcmp( pFogColor3, s_flSceneFogColor, sizeof( s_flSceneFogColor ) ) != 0 )
	{
		s_nSceneFogMode = nSceneFogMode;
		s_flFogWaterZ = flWaterZ;
		s_flFogOORange = flOORange;
		s_flFogEndOverRange = flFogEndOverRange;
		s_flFogMaxDensityFloor = flMaxDensityFloor;
		memcpy( s_flSceneFogColor, pFogColor3, sizeof( s_flSceneFogColor ) );
		s_bModulationDirty = true;	// reaches the PerDraw upload gate
	}
}

// Integer-HDR tonemap scale (SetToneMappingScaleLinear .x): with the shaders
// tonemapping inline, the fog colors computed at PerDraw fill must scale to
// match (dx9 UpdatePixelFogColorConstant). 1.0 in LDR.
static float s_flToneMapScale = 1.0f;

void StateDx11_SetToneMapScale( float flScale )
{
	if ( flScale != s_flToneMapScale )
	{
		s_flToneMapScale = flScale;
		s_bModulationDirty = true;
	}
}

//-----------------------------------------------------------------------------
// Dynamic stencil state (IShaderAPI::SetStencil*): dx9 overlays these render
// states on whatever pass is active; D3D11 bakes stencil into the immutable
// depth-stencil object, so when the dynamic block is enabled the commit swaps
// in a COMPOSED object — the snapshot's depth fields + the dynamic stencil
// fields — from a small cache. StencilOperation_t/StencilComparisonFunction_t
// share D3D9's numeric values, which D3D11 kept; they cast straight through.
// Driver: the integer-HDR autoexposure histogram (lumcompare stencil marking
// + the stencil-gated occlusion-query count draws).
//-----------------------------------------------------------------------------
struct DynStencilDx11_t
{
	bool m_bEnable;
	uint8 m_nFailOp, m_nZFailOp, m_nPassOp;	// D3D11_STENCIL_OP
	uint8 m_nFunc;							// D3D11_COMPARISON_FUNC
	uint8 m_nRef;
	uint8 m_nTestMask, m_nWriteMask;
};
static DynStencilDx11_t s_DynStencil = { false, 1, 1, 1, 8, 0, 0xFF, 0xFF };	// KEEP/KEEP/KEEP, ALWAYS

void StateDx11_SetStencilEnable( bool bEnable )			{ s_DynStencil.m_bEnable = bEnable; }
void StateDx11_SetStencilFailOp( int nOp )				{ s_DynStencil.m_nFailOp = (uint8)nOp; }
void StateDx11_SetStencilZFailOp( int nOp )				{ s_DynStencil.m_nZFailOp = (uint8)nOp; }
void StateDx11_SetStencilPassOp( int nOp )				{ s_DynStencil.m_nPassOp = (uint8)nOp; }
void StateDx11_SetStencilCompareFunc( int nFunc )		{ s_DynStencil.m_nFunc = (uint8)nFunc; }
void StateDx11_SetStencilReference( int nRef )			{ s_DynStencil.m_nRef = (uint8)nRef; }
void StateDx11_SetStencilTestMask( unsigned int nMask )	{ s_DynStencil.m_nTestMask = (uint8)nMask; }
void StateDx11_SetStencilWriteMask( unsigned int nMask ){ s_DynStencil.m_nWriteMask = (uint8)nMask; }

struct ComposedDepthStencilDx11_t
{
	uint64 m_nKey;
	ID3D11DepthStencilState *m_pState;
};
static CUtlVector<ComposedDepthStencilDx11_t> s_ComposedDepthStates;

// Snapshot depth fields + dynamic stencil fields -> cached state object
static ID3D11DepthStencilState *GetComposedDepthStencil( bool bDepthTest, bool bDepthWrite, int nDepthFunc )
{
	uint64 nKey = ( bDepthTest ? 1ull : 0ull ) | ( bDepthWrite ? 2ull : 0ull ) |
		( (uint64)( nDepthFunc & 0xF ) << 2 ) |
		( (uint64)s_DynStencil.m_nFailOp << 8 ) | ( (uint64)s_DynStencil.m_nZFailOp << 12 ) |
		( (uint64)s_DynStencil.m_nPassOp << 16 ) | ( (uint64)s_DynStencil.m_nFunc << 20 ) |
		( (uint64)s_DynStencil.m_nTestMask << 24 ) | ( (uint64)s_DynStencil.m_nWriteMask << 32 );
	for ( int i = 0; i < s_ComposedDepthStates.Count(); ++i )
	{
		if ( s_ComposedDepthStates[i].m_nKey == nKey )
			return s_ComposedDepthStates[i].m_pState;
	}
	if ( !D3D11Device() )
		return NULL;

	D3D11_DEPTH_STENCIL_DESC desc;
	ZeroMemory( &desc, sizeof( desc ) );
	desc.DepthEnable = bDepthTest;
	desc.DepthWriteMask = bDepthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
	desc.DepthFunc = (D3D11_COMPARISON_FUNC)nDepthFunc;
	desc.StencilEnable = TRUE;
	desc.StencilReadMask = s_DynStencil.m_nTestMask;
	desc.StencilWriteMask = s_DynStencil.m_nWriteMask;
	desc.FrontFace.StencilFailOp = (D3D11_STENCIL_OP)s_DynStencil.m_nFailOp;
	desc.FrontFace.StencilDepthFailOp = (D3D11_STENCIL_OP)s_DynStencil.m_nZFailOp;
	desc.FrontFace.StencilPassOp = (D3D11_STENCIL_OP)s_DynStencil.m_nPassOp;
	desc.FrontFace.StencilFunc = (D3D11_COMPARISON_FUNC)s_DynStencil.m_nFunc;
	desc.BackFace = desc.FrontFace;	// dx9 two-sided stencil is off in these paths

	ID3D11DepthStencilState *pState = NULL;
	if ( FAILED( D3D11Device()->CreateDepthStencilState( &desc, &pState ) ) )
		return NULL;
	ComposedDepthStencilDx11_t entry = { nKey, pState };
	s_ComposedDepthStates.AddToTail( entry );
	return pState;
}

// defined later in the file (resolves backbuffer vs texture-RT + aux depth)
static void GetCurrentTargets( bool bSRGBWrite, ID3D11RenderTargetView **ppRTV, ID3D11DepthStencilView **ppDSV );

void StateDx11_ClearStencilRect( int nValue )
{
	// The histogram clears a sub-rect, but its mark + count draws are
	// scissored to that same rect — a full-target stencil clear is
	// semantically equivalent (pixels outside stay unmarked either way).
	ID3D11RenderTargetView *pRTV = NULL;
	ID3D11DepthStencilView *pDSV = NULL;
	GetCurrentTargets( false, &pRTV, &pDSV );
	if ( pDSV && D3D11Context() )
		D3D11Context()->ClearDepthStencilView( pDSV, D3D11_CLEAR_STENCIL, 1.0f, (UINT8)nValue );
}

// Water family (water.cpp Water_DX90): ONE material draws up to two passes —
// expensive (perm 15: reflect/refract RTs) and cheap (perm 16: envmap cube) —
// told apart per-pass by the snapshot (only cheap enables s6, the dx9
// normalize cube). Material-level flag bits staged for g_SpriteControl.w:
// 4 abovewater, 8 multitexture ($scroll1.x != 0), 16 cheap-blend
// (!$forcecheap, water.cpp:565), 32 fresnel (!$nofresnel). The per-pass
// reflect/refract bits (1/2) OR in at draw time from the snapshot's sampler
// enables (s2/s0), since they differ between the two passes.
static bool s_bMaterialWater = false;
static float s_flWaterControl = 0.0f;
// vertexlitgeneric EXTRA passes (TF2 effects): $cloakpassenabled (spy cloak,
// perm 17) / $sheenpassenabled (killstreak sheen, perm 18). The helper
// snapshots are the only vlg passes with blend ON + depth writes ON
// (EnableAlphaBlending + the explicit EnableDepthWrites(true),
// cloak_blended_pass_helper.cpp:256-260) — that signature routes the pass;
// sheen additionally enables s2 (sheen cube) + s3 (mask).
static bool s_bMaterialCloakPass = false;
static bool s_bMaterialSheenPass = false;
// Refract family (perm 19): screen-space warp effects — underwater overlay,
// ubercharge overlay, teleporter fx. x = sniffed flag bits for
// g_SpriteControl.w: 1 = BLUR ($bluramount/$refractblur > 0),
// 2 = FADEOUTONSILHOUETTE; the per-pass CUBEMAP (4, s4) and
// REFRACTTINTTEXTURE (8, s5) bits OR in from the snapshot at draw time.
static bool s_bMaterialRefract = false;
static float s_flRefractControl = 0.0f;
// UnlitTwoTexture family (perm 20): the control-point hologram materials —
// base x scrolling $texture2 x modulation, additive
static bool s_bMaterialUnlitTwoTex = false;
// Modulate family (perm 21): framebuffer-modulation (the hologram's $mod2x
// "dark" backing pass). Prefix "modulate" does NOT match DecalModulate.
static bool s_bMaterialModulate = false;
// LDR bloom chain (perms 22/23/24): Downsample_nohdr / BlurFilterX+Y /
// the bloomadd combine (screenspace_general whose $pixshader names
// bloomadd_ps*). Their quads arrive in raw clip space.
static bool s_bMaterialSSDownsample = false;
static bool s_bMaterialSSBlur = false;
static bool s_bMaterialSSAdd = false;
// Engine_Post (perm 25): the bloom + color-correction combine
static bool s_bMaterialEnginePost = false;
// color_projection (perm 26): the FullViewColorAdjustment colorblind filter
// (mat_color_projection) — engine/view.cpp draws it over the finished frame
static bool s_bMaterialColorProjection = false;
// dev/lumcompare (perm 27): the integer-HDR autoexposure histogram's
// stencil-marking pass (screenspace_general $pixshader luminance_compare)
static bool s_bMaterialLumCompare = false;
// Legacy RTT shadow pipeline (CClientShadowMgr): 28 = ShadowBuild (caster ->
// atlas), 29 = Shadow (atlas -> world geometry), 30 = ShadowModel (atlas ->
// model geometry). Sniff order matters: "shadow" prefixes the other two.
static bool s_bMaterialShadowBuild = false;
static bool s_bMaterialShadowModel = false;
static bool s_bMaterialShadowProj = false;
// IntroScreenSpaceEffect (perm 31): the HL2 G-Man intro blend passes.
// $mode is set PER PASS on the same material, so it rides the control float.
static bool s_bMaterialIntroEffect = false;
static float s_flIntroMode = 0.0f;
// MotionBlur (perm 32): engine DoImageSpaceMotionBlur fullscreen pass
// (dev/motion_blur — HL2 ships mat_motion_blur_enabled 1)
static bool s_bMaterialMotionBlur = false;
// MonitorScreen (perm 33): func_monitor screens ($basetexture = _rt_Camera)
static bool s_bMaterialMonitorScreen = false;
// vertexlitgeneric/unlitgeneric by NAME: their dx9 helper handles the
// flashlight INLINE (base s0, cookie s7, PS-side projection via c24-27)
// instead of the shared DrawFlashlight_dx90 (cookie s0, base s1) — perm 34
// selects the variant per draw.
static bool s_bMaterialVLGName = false;
// "DepthWrite" procedural materials: the flashlight shadow caster pass
// (perm 35 — position-only depth fill; must beat the InFlashlightMode pick)
static bool s_bMaterialDepthWrite = false;
// lightmappedgeneric envmap path (perm 1): $envmapcontrast / $envmapsaturation /
// $fresnelreflection only reach ps c2-c4 on the dx9 helper's SLOW path (fastpath
// materials — contrast 0/1, saturation 1, fresnel 1 — never upload them), so the
// param values ride PerDraw. w = mask mode (0 none / 1 $envmapmask s5 /
// 2 $basealphaenvmapmask = INVERTED base alpha, lightmapped semantics).
static float s_flEnvmapControl[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
// SetFlashlightStateEx said shadows are on AND handed us a depth texture:
// perm 34 raises flag bit 8 (sample the family's depth map + manual compare)
static bool s_bFlashlightShadows = false;

void StateDx11_SetFlashlightShadows( bool bShadows )
{
	s_bFlashlightShadows = bShadows;
}

// Case-insensitive shader-name prefix test (resolved names carry _DX9/_DX8
// fallback suffixes, so prefix matching covers the whole family).
static bool ShaderNameHasPrefix( const char *pShaderName, const char *pPrefix )
{
	if ( !pShaderName )
		return false;
	for ( ; *pPrefix; ++pPrefix, ++pShaderName )
	{
		char a = *pShaderName, b = *pPrefix;
		if ( a >= 'A' && a <= 'Z' ) a += 'a' - 'A';
		if ( b >= 'A' && b <= 'Z' ) b += 'a' - 'A';
		if ( a != b )
			return false;
	}
	return true;
}

void StateDx11_BindMaterialTint( IMaterial *pMaterial )
{
	float flBlendTint = 0.0f, flReplace = 0.0f, flSelfIllum = 0.0f, flSIFresnel = 0.0f;
	float flTransformReg = -1.0f, flDetailMode = -1.0f;
	float flFlags[4] = { 0, 0, 0, 0 };
	bool bPhong = false;
	bool bVertexLit = false;
	bool bCable = false;
	bool bEyes = false;
	bool bWindowImposter = false;
	bool bEyeRefract = false;
	bool bTeeth = false;
	bool bLMModC12 = false;
	bool bPyro = false;
	bool bSpriteCard = false;
	bool bWater = false;
	float flWaterControl = 0.0f;
	bool bCloakPass = false;
	bool bSheenPass = false;
	bool bRefract = false;
	float flRefractControl = 0.0f;
	bool bUnlitTwoTex = false;
	bool bModulate = false;
	bool bSSDownsample = false;
	bool bSSBlur = false;
	bool bSSAdd = false;
	bool bEnginePost = false;
	bool bColorProjection = false;
	bool bLumCompare = false;
	bool bShadowBuild = false;
	bool bShadowModel = false;
	bool bShadowProj = false;
	bool bIntroEffect = false;
	float flIntroMode = 0.0f;
	bool bMotionBlur = false;
	bool bMonitorScreen = false;
	bool bVLGName = false;
	bool bDepthWrite = false;
	float flEyeControl[2] = { 0, 0 };
	float flPyroControl[2] = { 0, 0 };
	float flSpriteControl[3] = { 0, 0, 0 };
	float flEnvmapControl[4] = { 0.0f, 1.0f, 1.0f, 0.0f };	// dx9 param defaults
	if ( pMaterial )
	{
		const char *pShaderName = pMaterial->GetShaderName();
		bCable = ShaderNameHasPrefix( pShaderName, "cable" );	// Cable / Cable_DX9
		bEyes = ShaderNameHasPrefix( pShaderName, "eyes" );		// Eyes / Eyes_DX9 (not EyeRefract)
		bWindowImposter = ShaderNameHasPrefix( pShaderName, "windowimposter" );
		bEyeRefract = ShaderNameHasPrefix( pShaderName, "eyerefract" );
		bTeeth = ShaderNameHasPrefix( pShaderName, "teeth" );
		bLMModC12 = ShaderNameHasPrefix( pShaderName, "lightmappedgeneric" ) ||
			ShaderNameHasPrefix( pShaderName, "worldvertextransition" );
		static unsigned int s_nSCFlags2Token = 0;
		IMaterialVar *pSCFlags2 = pMaterial->FindVarFast( "$flags2", &s_nSCFlags2Token );
		bSpriteCard = ( pSCFlags2 && ( pSCFlags2->GetIntValue() & MATERIAL_VAR2_IS_SPRITECARD ) != 0 );
		if ( bSpriteCard )
		{
			int nFlags = 0;
			static unsigned int s_nSCAdd2Token = 0;
			IMaterialVar *pAdd2 = pMaterial->FindVarFast( "$addbasetexture2", &s_nSCAdd2Token );
			if ( pAdd2 && pAdd2->GetFloatValue() != 0.0f )
				nFlags |= 1;
			static unsigned int s_nSCAddSelfToken = 0;
			IMaterialVar *pAddSelf = pMaterial->FindVarFast( "$addself", &s_nSCAddSelfToken );
			if ( pAddSelf && pAddSelf->GetFloatValue() != 0.0f )
				nFlags |= 2;
			static unsigned int s_nSCSplineToken = 0;
			IMaterialVar *pSpline = pMaterial->FindVarFast( "$splinetype", &s_nSCSplineToken );
			bool bSpline = pSpline && pSpline->GetIntValue() != 0;
			static unsigned int s_nSCBlendFramesToken = 0;
			IMaterialVar *pBlendFrames = pMaterial->FindVarFast( "$blendframes", &s_nSCBlendFramesToken );
			// dx9 default = 1; spline cards never frame-blend
			if ( ( !pBlendFrames || pBlendFrames->GetIntValue() != 0 ) && !bSpline )
				nFlags |= 4;
			static unsigned int s_nSCDualToken = 0;
			IMaterialVar *pDual = pMaterial->FindVarFast( "$dualsequence", &s_nSCDualToken );
			bool bDual = pDual && pDual->GetIntValue() != 0;
			if ( bDual )
				nFlags |= 8;
			static unsigned int s_nSCMaxLum1Token = 0;
			IMaterialVar *pMaxLum1 = pMaterial->FindVarFast( "$maxlumframeblend1", &s_nSCMaxLum1Token );
			bool bMaxLum1 = pMaxLum1 && pMaxLum1->GetIntValue() != 0;
			if ( bMaxLum1 )
				nFlags |= 16;
			if ( bDual && bMaxLum1 )	// dx9 passes MAXLUMFRAMEBLEND1 for the 2nd too
				nFlags |= 32;
			static unsigned int s_nSCRampToken = 0;
			IMaterialVar *pRamp = pMaterial->FindVarFast( "$ramptexture", &s_nSCRampToken );
			if ( pRamp && pRamp->IsTexture() )
				nFlags |= 64;
			static unsigned int s_nSCExtractToken = 0;
			IMaterialVar *pExtract = pMaterial->FindVarFast( "$extractgreenalpha", &s_nSCExtractToken );
			if ( pExtract && pExtract->GetIntValue() )
				nFlags |= 128;
			static unsigned int s_nSCDepthBlendToken = 0;
			IMaterialVar *pDepthBlend = pMaterial->FindVarFast( "$depthblend", &s_nSCDepthBlendToken );
			if ( pDepthBlend && pDepthBlend->GetIntValue() )
				nFlags |= 256;
			flSpriteControl[0] = (float)nFlags;
			static unsigned int s_nSCOrientToken = 0;
			IMaterialVar *pOrient = pMaterial->FindVarFast( "$orientation", &s_nSCOrientToken );
			int nOrient = pOrient ? pOrient->GetIntValue() : 0;
			flSpriteControl[1] = (float)( nOrient < 0 ? 0 : ( nOrient > 2 ? 2 : nOrient ) );
			static unsigned int s_nSCSeqBlendToken = 0;
			IMaterialVar *pSeqBlend = pMaterial->FindVarFast( "$sequence_blend_mode", &s_nSCSeqBlendToken );
			flSpriteControl[2] = ( bDual && pSeqBlend ) ? (float)pSeqBlend->GetIntValue() : 0.0f;
		}
		bPyro = ShaderNameHasPrefix( pShaderName, "pyro_vision" );
		if ( bPyro )
		{
			static unsigned int s_nPyroEffectToken = 0;
			IMaterialVar *pEffect = pMaterial->FindVarFast( "$effect", &s_nPyroEffectToken );
			flPyroControl[0] = pEffect ? (float)pEffect->GetIntValue() : 0.0f;
			int nFlags = 0;
			if ( pMaterial->GetMaterialVarFlag( MATERIAL_VAR_VERTEXCOLOR ) )
				nFlags |= 1;
			static unsigned int s_nPyroFullbrightToken = 0;
			IMaterialVar *pFullbright = pMaterial->FindVarFast( "$fullbright", &s_nPyroFullbrightToken );
			if ( pFullbright && pFullbright->GetIntValue() )
				nFlags |= 2;
			static unsigned int s_nPyroBase2Token = 0;
			IMaterialVar *pBase2 = pMaterial->FindVarFast( "$basetexture2", &s_nPyroBase2Token );
			bool bBase2 = pBase2 && pBase2->IsTexture();
			if ( bBase2 )
				nFlags |= 4;
			static unsigned int s_nPyroBlendModToken = 0;
			IMaterialVar *pBlendMod = pMaterial->FindVarFast( "$blendmodulatetexture", &s_nPyroBlendModToken );
			if ( bBase2 && pBlendMod && pBlendMod->IsTexture() )
				nFlags |= 8;
			static unsigned int s_nPyroColorbarToken = 0;
			IMaterialVar *pColorbar = pMaterial->FindVarFast( "$colorbar", &s_nPyroColorbarToken );
			if ( pColorbar && pColorbar->IsTexture() )
				nFlags |= 16;
			static unsigned int s_nPyroStripeToken = 0;
			IMaterialVar *pStripe = pMaterial->FindVarFast( "$stripetexture", &s_nPyroStripeToken );
			if ( pStripe && pStripe->IsTexture() )
				nFlags |= 32;
			flPyroControl[1] = (float)nFlags;
		}
		// Water_DX90 / Water_DX9_HDR / the dx8 Water fallbacks all prefix-match;
		// the cheap-vs-expensive split happens at the perm pick (per snapshot).
		bWater = ShaderNameHasPrefix( pShaderName, "water" );
		if ( bWater )
		{
			int nFlags = 0;
			static unsigned int s_nWaterAboveToken = 0;
			IMaterialVar *pAbove = pMaterial->FindVarFast( "$abovewater", &s_nWaterAboveToken );
			if ( pAbove && pAbove->GetIntValue() )
				nFlags |= 4;
			static unsigned int s_nWaterScroll1Token = 0;
			IMaterialVar *pScroll1 = pMaterial->FindVarFast( "$scroll1", &s_nWaterScroll1Token );
			if ( pScroll1 && pScroll1->IsDefined() )
			{
				float vScroll[4] = { 0, 0, 0, 0 };
				pScroll1->GetVecValue( vScroll, 4 );
				if ( fabsf( vScroll[0] ) > 0.0f )
					nFlags |= 8;	// MULTITEXTURE combo (water.cpp:229)
			}
			static unsigned int s_nWaterForceCheapToken = 0;
			IMaterialVar *pForceCheap = pMaterial->FindVarFast( "$forcecheap", &s_nWaterForceCheapToken );
			if ( !pForceCheap || !pForceCheap->GetIntValue() )
				nFlags |= 16;	// DrawCheapWater bBlend = !bForceCheap
			static unsigned int s_nWaterNoFresnelToken = 0;
			IMaterialVar *pNoFresnel = pMaterial->FindVarFast( "$nofresnel", &s_nWaterNoFresnelToken );
			if ( !pNoFresnel || !pNoFresnel->GetIntValue() )
				nFlags |= 32;	// FRESNEL combo (cheap pass only)
			flWaterControl = (float)nFlags;
		}
		// UnlitTwoTexture (CP holograms) — name-routed like cable/eyes
		bUnlitTwoTex = ShaderNameHasPrefix( pShaderName, "unlittwotexture" );
		bModulate = ShaderNameHasPrefix( pShaderName, "modulate" );
		// LDR bloom chain. The combine is screenspace_general with
		// $pixshader = bloomadd_ps20 (dev/bloomadd.vmt) — other
		// screenspace_general materials keep their current fallback.
		bSSDownsample = ShaderNameHasPrefix( pShaderName, "downsample" );
		bSSBlur = ShaderNameHasPrefix( pShaderName, "blurfilter" );
		bEnginePost = ShaderNameHasPrefix( pShaderName, "engine_post" );
		// FullViewColorAdjustment's dev/red_green_projection (engine/view.cpp:
		// drawn over the FINISHED frame every frame — menu included — whenever
		// mat_color_projection != 0)
		bColorProjection = ShaderNameHasPrefix( pShaderName, "color_projection" );
		// Legacy RTT shadows. ORDER: "shadow" prefixes "shadowbuild" and
		// "shadowmodel", so the specific families test first.
		bShadowBuild = ShaderNameHasPrefix( pShaderName, "shadowbuild" );
		bShadowModel = !bShadowBuild && ShaderNameHasPrefix( pShaderName, "shadowmodel" );
		bShadowProj = !bShadowBuild && !bShadowModel && ShaderNameHasPrefix( pShaderName, "shadow" );
		// HL2 G-Man intro (scripted/intro_screenspaceeffect): $mode changes
		// per blend pass on the same material — track it like a control.
		bIntroEffect = ShaderNameHasPrefix( pShaderName, "introscreenspaceeffect" );
		if ( bIntroEffect )
		{
			static unsigned int s_nIntroModeToken = 0;
			IMaterialVar *pIntroMode = pMaterial->FindVarFast( "$mode", &s_nIntroModeToken );
			flIntroMode = pIntroMode ? (float)pIntroMode->GetIntValue() : 0.0f;
		}
		// engine DoImageSpaceMotionBlur (dev/motion_blur, MotionBlur/_DX9)
		bMotionBlur = ShaderNameHasPrefix( pShaderName, "motionblur" );
		// func_monitor screens (MonitorScreen/_DX9/_DX8)
		bMonitorScreen = ShaderNameHasPrefix( pShaderName, "monitorscreen" );
		// vertexlitgeneric/unlitgeneric family (inline flashlight sampler map)
		bVLGName = ShaderNameHasPrefix( pShaderName, "vertexlitgeneric" ) ||
			ShaderNameHasPrefix( pShaderName, "unlitgeneric" );
		// The flashlight shadow caster pass (engine __DepthWrite/studiorender
		// procedural materials, shader "DepthWrite"): position-only depth fill
		// into _rt_ShadowDepthTexture_N, alphatest variants sample s0.
		bDepthWrite = ShaderNameHasPrefix( pShaderName, "depthwrite" );
		if ( ShaderNameHasPrefix( pShaderName, "screenspace_general" ) )
		{
			static unsigned int s_nPixShaderToken = 0;
			IMaterialVar *pPixShader = pMaterial->FindVarFast( "$pixshader", &s_nPixShaderToken );
			const char *pPixName = pPixShader ? pPixShader->GetStringValue() : "";
			// constant_color (dev/no_pixel_write, the histogram's stencil-gated
			// count quad) rides MODE 24's clip-space passthrough: its color
			// never lands ($disable_color_writes) — only rasterization counts.
			bSSAdd = ShaderNameHasPrefix( pPixName, "bloomadd" ) ||
				ShaderNameHasPrefix( pPixName, "constant_color" );
			// dev/lumcompare: the histogram's stencil-marking luminance test
			bLumCompare = ShaderNameHasPrefix( pPixName, "luminance_compare" );
		}
		// Refract family (Refract / Refract_DX90/80/60 resolve by prefix)
		bRefract = ShaderNameHasPrefix( pShaderName, "refract" );
		if ( bRefract )
		{
			int nFlags = 0;
			static unsigned int s_nRefrBlurToken = 0;
			IMaterialVar *pBlur = pMaterial->FindVarFast( "$bluramount", &s_nRefrBlurToken );
			static unsigned int s_nRefrBlur2Token = 0;
			IMaterialVar *pBlur2 = pMaterial->FindVarFast( "$refractblur", &s_nRefrBlur2Token );
			if ( ( pBlur && pBlur->GetIntValue() > 0 ) || ( pBlur2 && pBlur2->GetIntValue() > 0 ) )
				nFlags |= 1;	// BLUR (clamped to 1 = dx9 MAXBLUR)
			static unsigned int s_nRefrFadeToken = 0;
			IMaterialVar *pFade = pMaterial->FindVarFast( "$fadeoutonsilhouette", &s_nRefrFadeToken );
			if ( pFade && pFade->GetIntValue() )
				nFlags |= 2;
			flRefractControl = (float)nFlags;
		}
		// vertexlitgeneric extra passes (only vlg declares these params)
		static unsigned int s_nCloakPassToken = 0;
		IMaterialVar *pCloakPass = pMaterial->FindVarFast( "$cloakpassenabled", &s_nCloakPassToken );
		bCloakPass = pCloakPass && pCloakPass->GetIntValue() != 0;
		static unsigned int s_nSheenPassToken = 0;
		IMaterialVar *pSheenPass = pMaterial->FindVarFast( "$sheenpassenabled", &s_nSheenPassToken );
		bSheenPass = pSheenPass && pSheenPass->GetIntValue() != 0;
		if ( bEyeRefract )
		{
			static unsigned int s_nRaytraceToken = 0;
			IMaterialVar *pRaytrace = pMaterial->FindVarFast( "$raytracesphere", &s_nRaytraceToken );
			flEyeControl[0] = ( pRaytrace && pRaytrace->GetIntValue() ) ? 1.0f : 0.0f;
			static unsigned int s_nTexKillToken = 0;
			IMaterialVar *pTexKill = pMaterial->FindVarFast( "$spheretexkillcombo", &s_nTexKillToken );
			flEyeControl[1] = ( pTexKill && pTexKill->GetIntValue() ) ? 1.0f : 0.0f;
		}

		static unsigned int s_nFlags2Token = 0;
		IMaterialVar *pFlags2 = pMaterial->FindVarFast( "$flags2", &s_nFlags2Token );
		bVertexLit = pFlags2 && ( pFlags2->GetIntValue() & MATERIAL_VAR2_LIGHTING_VERTEX_LIT ) != 0;

		static unsigned int s_nDetailToken = 0;
		IMaterialVar *pDetail = pMaterial->FindVarFast( "$detail", &s_nDetailToken );
		if ( pDetail && pDetail->IsDefined() )
		{
			flDetailMode = 0.0f;
			static unsigned int s_nDetailModeToken = 0;
			IMaterialVar *pDetailMode = pMaterial->FindVarFast( "$detailblendmode", &s_nDetailModeToken );
			if ( pDetailMode )
				flDetailMode = (float)pDetailMode->GetIntValue();
		}

		static unsigned int s_nBaseTransformToken = 0;
		IMaterialVar *pBaseTransform = pMaterial->FindVarFast( "$basetexturetransform", &s_nBaseTransformToken );
		if ( pBaseTransform )
		{
			bool bSky = pShaderName && ( pShaderName[0] == 'S' || pShaderName[0] == 's' ) &&
				( pShaderName[1] == 'k' || pShaderName[1] == 'K' ) &&
				( pShaderName[2] == 'y' || pShaderName[2] == 'Y' );
			flTransformReg = bSky ? 49.0f : 48.0f;
		}

		if ( pMaterial->GetMaterialVarFlag( MATERIAL_VAR_SELFILLUM ) )
		{
			flSelfIllum = 1.0f;
			static unsigned int s_nSIFresnelToken = 0;
			IMaterialVar *pSIFresnel = pMaterial->FindVarFast( "$selfillumfresnel", &s_nSIFresnelToken );
			if ( pSIFresnel && pSIFresnel->GetIntValue() )
				flSIFresnel = 1.0f;
		}
		static unsigned int s_nBlendTintToken = 0;
		IMaterialVar *pBlendTint = pMaterial->FindVarFast( "$blendtintbybasealpha", &s_nBlendTintToken );
		if ( pBlendTint && pBlendTint->GetIntValue() )
		{
			flBlendTint = 1.0f;
			// the actual VMT param name (vertexlitgeneric_dx9.cpp:211 maps it
			// onto info.m_nTintReplacesBaseColor)
			static unsigned int s_nReplaceToken = 0;
			IMaterialVar *pReplace = pMaterial->FindVarFast( "$blendtintcoloroverbase", &s_nReplaceToken );
			if ( pReplace )
				flReplace = pReplace->GetFloatValue();
		}

		// lightmappedgeneric envmap params (the cube itself is signalled by the
		// snapshot's s2 enable; these shape the term). Defaults match the dx9
		// param init: contrast 0, saturation 1, $fresnelreflection 1 (= off).
		static unsigned int s_nEnvContrastToken = 0;
		IMaterialVar *pEnvContrast = pMaterial->FindVarFast( "$envmapcontrast", &s_nEnvContrastToken );
		if ( pEnvContrast && pEnvContrast->IsDefined() )
			flEnvmapControl[0] = pEnvContrast->GetFloatValue();
		static unsigned int s_nEnvSaturationToken = 0;
		IMaterialVar *pEnvSaturation = pMaterial->FindVarFast( "$envmapsaturation", &s_nEnvSaturationToken );
		if ( pEnvSaturation && pEnvSaturation->IsDefined() )
			flEnvmapControl[1] = pEnvSaturation->GetFloatValue();
		static unsigned int s_nFresnelReflToken = 0;
		IMaterialVar *pFresnelRefl = pMaterial->FindVarFast( "$fresnelreflection", &s_nFresnelReflToken );
		if ( pFresnelRefl && pFresnelRefl->IsDefined() )
			flEnvmapControl[2] = pFresnelRefl->GetFloatValue();
		// Mask select: $envmapmask texture (s5) beats $basealphaenvmapmask —
		// the dx9 SKIP rules make them exclusive in valid content.
		static unsigned int s_nEnvMaskToken = 0;
		IMaterialVar *pEnvMask = pMaterial->FindVarFast( "$envmapmask", &s_nEnvMaskToken );
		if ( pEnvMask && pEnvMask->IsTexture() )
			flEnvmapControl[3] = 1.0f;
		else if ( pMaterial->GetMaterialVarFlag( MATERIAL_VAR_BASEALPHAENVMAPMASK ) )
			flEnvmapControl[3] = 2.0f;
		else if ( pMaterial->GetMaterialVarFlag( MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK ) )
			flEnvmapControl[3] = 3.0f;	// mask = $bumpmap alpha (HL2 concrete/metal/tile)
		// dx9 FASTPATH quirk (lightmappedgeneric_dx9_helper.cpp:711-718): the
		// slow path — the only one that uploads c2-c4 and applies saturation/
		// fresnel — is taken when (contrast∉{0,1} AND saturation≠1) OR
		// fresnel≠1 OR a non-white $selfillumtint. On the FAST path the
		// authored saturation/fresnel are DROPPED and contrast snaps to
		// exactly 0 or 1 (the FASTPATHENVMAPCONTRAST combo fires only at
		// contrast==1). Reproduce by value so the picture matches dx9.
		{
			bool bEnvSlowPath =
				( flEnvmapControl[0] != 0.0f && flEnvmapControl[0] != 1.0f &&
				  flEnvmapControl[1] != 1.0f ) ||
				( flEnvmapControl[2] != 1.0f );
			if ( !bEnvSlowPath && flSelfIllum > 0.5f )
			{
				static unsigned int s_nSITintToken = 0;
				IMaterialVar *pSITint = pMaterial->FindVarFast( "$selfillumtint", &s_nSITintToken );
				if ( pSITint )
				{
					float flSITint[3] = { 1.0f, 1.0f, 1.0f };
					pSITint->GetVecValue( flSITint, 3 );
					bEnvSlowPath = flSITint[0] != 1.0f || flSITint[1] != 1.0f || flSITint[2] != 1.0f;
				}
			}
			if ( !bEnvSlowPath )
			{
				flEnvmapControl[0] = ( flEnvmapControl[0] == 1.0f ) ? 1.0f : 0.0f;
				flEnvmapControl[1] = 1.0f;
				flEnvmapControl[2] = 1.0f;
			}
		}

		static unsigned int s_nPhongToken = 0;
		IMaterialVar *pPhong = pMaterial->FindVarFast( "$phong", &s_nPhongToken );
		bPhong = pPhong && pPhong->GetIntValue() != 0;
		// $halflambert is sniffed for phong AND eyes (the eyes VS DoLighting
		// takes the same half-lambert option), so it lives outside the gate.
		// It's a shader STATE FLAG (shadersystem.cpp s_pShaderStateString), not
		// a material var — FindVarFast("$halflambert") always returned NULL,
		// which silently disabled half-lambert on the phong path too.
		flFlags[0] = pMaterial->GetMaterialVarFlag( MATERIAL_VAR_HALFLAMBERT ) ? 1.0f : 0.0f;
		// $lightwarptexture is shared by phong AND EyeRefract (TF NPR eyes)
		static unsigned int s_nLightwarpToken = 0;
		IMaterialVar *pLightwarp = pMaterial->FindVarFast( "$lightwarptexture", &s_nLightwarpToken );
		flFlags[1] = ( pLightwarp && pLightwarp->IsDefined() ) ? 1.0f : 0.0f;
		if ( bPhong )
		{
			static unsigned int s_nBumpToken = 0;
			IMaterialVar *pBump = pMaterial->FindVarFast( "$bumpmap", &s_nBumpToken );
			flFlags[2] = ( pBump && pBump->IsDefined() ) ? 1.0f : 0.0f;
			static unsigned int s_nSpecExpTexToken = 0;
			IMaterialVar *pSpecExpTex = pMaterial->FindVarFast( "$phongexponenttexture", &s_nSpecExpTexToken );
			flFlags[3] = ( pSpecExpTex && pSpecExpTex->IsDefined() ) ? 1.0f : 0.0f;
		}
	}
	if ( flBlendTint != s_flTintControl[0] || flReplace != s_flTintControl[1] ||
		 flSelfIllum != s_flTintControl[2] || flSIFresnel != s_flSelfIllumFresnel ||
		 flTransformReg != s_flVSTransformReg || flDetailMode != s_flDetailBlendMode ||
		 bPhong != s_bMaterialPhong || bVertexLit != s_bMaterialVertexLit ||
		 bCable != s_bMaterialCable || bEyes != s_bMaterialEyes ||
		 bWindowImposter != s_bMaterialWindowImposter ||
		 bEyeRefract != s_bMaterialEyeRefract || bTeeth != s_bMaterialTeeth ||
		 bLMModC12 != s_bMaterialLMModC12 || bPyro != s_bMaterialPyro ||
		 bSpriteCard != s_bMaterialSpriteCard ||
		 bWater != s_bMaterialWater || flWaterControl != s_flWaterControl ||
		 bCloakPass != s_bMaterialCloakPass || bSheenPass != s_bMaterialSheenPass ||
		 bRefract != s_bMaterialRefract || flRefractControl != s_flRefractControl ||
		 bUnlitTwoTex != s_bMaterialUnlitTwoTex || bModulate != s_bMaterialModulate ||
		 bSSDownsample != s_bMaterialSSDownsample || bSSBlur != s_bMaterialSSBlur ||
		 bSSAdd != s_bMaterialSSAdd || bEnginePost != s_bMaterialEnginePost ||
		 bColorProjection != s_bMaterialColorProjection ||
		 bLumCompare != s_bMaterialLumCompare ||
		 bShadowBuild != s_bMaterialShadowBuild || bShadowModel != s_bMaterialShadowModel ||
		 bShadowProj != s_bMaterialShadowProj ||
		 bIntroEffect != s_bMaterialIntroEffect || flIntroMode != s_flIntroMode ||
		 bMotionBlur != s_bMaterialMotionBlur ||
		 bMonitorScreen != s_bMaterialMonitorScreen ||
		 bVLGName != s_bMaterialVLGName ||
		 bDepthWrite != s_bMaterialDepthWrite ||
		 memcmp( flEyeControl, s_flEyeControl, sizeof( flEyeControl ) ) != 0 ||
		 memcmp( flPyroControl, s_flPyroControl, sizeof( flPyroControl ) ) != 0 ||
		 memcmp( flSpriteControl, s_flSpriteControl, sizeof( flSpriteControl ) ) != 0 ||
		 memcmp( flEnvmapControl, s_flEnvmapControl, sizeof( flEnvmapControl ) ) != 0 ||
		 memcmp( flFlags, s_flPhongFlags, sizeof( flFlags ) ) != 0 )
	{
		s_flTintControl[0] = flBlendTint;
		s_flTintControl[1] = flReplace;
		s_flTintControl[2] = flSelfIllum;
		s_flSelfIllumFresnel = flSIFresnel;
		s_flVSTransformReg = flTransformReg;
		s_flDetailBlendMode = flDetailMode;
		s_bMaterialPhong = bPhong;
		s_bMaterialVertexLit = bVertexLit;
		s_bMaterialCable = bCable;
		s_bMaterialEyes = bEyes;
		s_bMaterialWindowImposter = bWindowImposter;
		s_bMaterialEyeRefract = bEyeRefract;
		s_bMaterialTeeth = bTeeth;
		s_bMaterialLMModC12 = bLMModC12;
		s_bMaterialPyro = bPyro;
		s_bMaterialSpriteCard = bSpriteCard;
		s_bMaterialWater = bWater;
		s_flWaterControl = flWaterControl;
		s_bMaterialCloakPass = bCloakPass;
		s_bMaterialSheenPass = bSheenPass;
		s_bMaterialRefract = bRefract;
		s_flRefractControl = flRefractControl;
		s_bMaterialUnlitTwoTex = bUnlitTwoTex;
		s_bMaterialModulate = bModulate;
		s_bMaterialSSDownsample = bSSDownsample;
		s_bMaterialSSBlur = bSSBlur;
		s_bMaterialSSAdd = bSSAdd;
		s_bMaterialEnginePost = bEnginePost;
		s_bMaterialColorProjection = bColorProjection;
		s_bMaterialLumCompare = bLumCompare;
		s_bMaterialShadowBuild = bShadowBuild;
		s_bMaterialShadowModel = bShadowModel;
		s_bMaterialShadowProj = bShadowProj;
		s_bMaterialIntroEffect = bIntroEffect;
		s_flIntroMode = flIntroMode;
		s_bMaterialMotionBlur = bMotionBlur;
		s_bMaterialMonitorScreen = bMonitorScreen;
		s_bMaterialVLGName = bVLGName;
		s_bMaterialDepthWrite = bDepthWrite;
		memcpy( s_flSpriteControl, flSpriteControl, sizeof( s_flSpriteControl ) );
		memcpy( s_flPyroControl, flPyroControl, sizeof( s_flPyroControl ) );
		memcpy( s_flEyeControl, flEyeControl, sizeof( s_flEyeControl ) );
		memcpy( s_flEnvmapControl, flEnvmapControl, sizeof( s_flEnvmapControl ) );
		memcpy( s_flPhongFlags, flFlags, sizeof( s_flPhongFlags ) );
		s_bModulationDirty = true;
	}
}

// Captures every dx9 PS float-constant write into the b2 mirror. The c1
// modulation latch keeps its dedicated PerDraw path (vgui/world perms read
// g_Modulation there) and is updated from the same write.
void StateDx11_SetPSConstants( int nFirst, const float *pValues, int nCount )
{
	if ( nFirst < 0 || !pValues || nCount <= 0 )
		return;
	if ( nFirst <= 1 && nFirst + nCount > 1 )
		StateDx11_SetModulation( pValues + ( 1 - nFirst ) * 4 );
	if ( nFirst >= DX11_PS_MIRROR_CONSTANTS )
		return;
	if ( nFirst + nCount > DX11_PS_MIRROR_CONSTANTS )
		nCount = DX11_PS_MIRROR_CONSTANTS - nFirst;
	memcpy( s_flPSMirror + nFirst * 4, pValues, nCount * 4 * sizeof( float ) );
	for ( int i = 0; i < nCount; ++i )
		s_nPSMirrorValid |= 1u << ( nFirst + i );
	s_bPSMirrorDirty = true;
}

// dx9 lighting origin (SetLightingOrigin): used to point-ify directional
// lights for the per-pixel light array.
static float s_vLightingOrigin[3] = { 0, 0, 0 };

void StateDx11_SetLightingOrigin( const float *pOrigin3 )
{
	if ( pOrigin3 )
		memcpy( s_vLightingOrigin, pOrigin3, sizeof( s_vLightingOrigin ) );
}

// dx9 CacheWorldSpaceCameraPosition (shaderapidx8.cpp:9971): the camera
// position is recovered from the row-vector VIEW matrix as -(t · R^T).
// Writes exactly 3 floats (dx9 memcpy's float[3]; callers own [3]).
void StateDx11_GetWorldSpaceCameraPosition( float *pPos )
{
	EnsureMatrixInit();
	const Matrix44Dx11_t &v = s_MatrixStacks[MATERIAL_VIEW][s_nStackTop[MATERIAL_VIEW]];
	pPos[0] = -( v[3][0] * v[0][0] + v[3][1] * v[0][1] + v[3][2] * v[0][2] );
	pPos[1] = -( v[3][0] * v[1][0] + v[3][1] * v[1][1] + v[3][2] * v[1][2] );
	pPos[2] = -( v[3][0] * v[2][0] + v[3][1] * v[2][1] + v[3][2] * v[2][2] );
	// dx9 protects z against zero (water-fog shaders divide by it)
	if ( fabsf( pPos[2] ) <= 0.00001f )
		pPos[2] = 0.01f;
}

void StateDx11_SetModulation( const float *pColor4 )
{
	if ( memcmp( s_flModulation, pColor4, sizeof( s_flModulation ) ) != 0 )
	{
		memcpy( s_flModulation, pColor4, sizeof( s_flModulation ) );
	}
	// The per-draw constants must refresh even if the value matches: validity
	// may have been false for the previous draw.
	s_bModulationDirty = true;
	s_bModulationValid = true;
}

// Offscreen render targets: texture RTs with an RTV bind to the OM stage with
// a per-size auxiliary depth buffer (D3D11 requires RTV/DSV dims to match —
// unlike D3D9, the shared default depth can't back smaller RTs). RT textures
// we can't bind (no RTV — e.g. depth-format textures) fall back to suppression
// so stray draws/clears can't wipe the backbuffer frame.
static bool s_bOffscreenRT = false;
static ShaderAPITextureHandle_t s_hRenderTexture = INVALID_SHADERAPI_TEXTURE_HANDLE;
// Explicit depth-texture attach (flashlight shadow caster pass); INVALID =
// the default per-size aux depth below.
static ShaderAPITextureHandle_t s_hRenderDepthTexture = INVALID_SHADERAPI_TEXTURE_HANDLE;

void StateDx11_SetOffscreenRT( bool bOffscreen )
{
	s_bOffscreenRT = bOffscreen;
}

bool StateDx11_IsOffscreenRT()
{
	return s_bOffscreenRT;
}

struct AuxDepthDx11_t
{
	int m_nWidth, m_nHeight;
	ID3D11Texture2D *m_pTexture;
	ID3D11DepthStencilView *m_pDSV;
};
static CUtlVector<AuxDepthDx11_t> s_AuxDepths;

static ID3D11DepthStencilView *GetAuxDepth( int nWidth, int nHeight )
{
	for ( int i = 0; i < s_AuxDepths.Count(); ++i )
	{
		if ( s_AuxDepths[i].m_nWidth == nWidth && s_AuxDepths[i].m_nHeight == nHeight )
			return s_AuxDepths[i].m_pDSV;
	}
	if ( !D3D11Device() )
		return NULL;

	D3D11_TEXTURE2D_DESC desc = { (UINT)nWidth, (UINT)nHeight, 1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT,
		{ 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0 };
	ID3D11Texture2D *pTex = NULL;
	ID3D11DepthStencilView *pDSV = NULL;
	if ( FAILED( D3D11Device()->CreateTexture2D( &desc, NULL, &pTex ) ) ||
		 FAILED( D3D11Device()->CreateDepthStencilView( pTex, NULL, &pDSV ) ) )
	{
		Warning( "shaderapidx11: aux depth %dx%d creation failed\n", nWidth, nHeight );
		if ( pTex )
			pTex->Release();
		return NULL;
	}
	char szName[64];
	V_snprintf( szName, sizeof( szName ), "aux_depth_%dx%d", nWidth, nHeight );
	Dx11_SetDebugName( pTex, szName );
	AuxDepthDx11_t entry = { nWidth, nHeight, pTex, pDSV };
	s_AuxDepths.AddToTail( entry );
	return pDSV;
}

// ON by default since M7: water refract/reflect, FB copies (cloak, refract,
// color correction, the HL2 intro effect), the shadow atlas and the HDR
// histogram all depend on texture RTs, and every shader family they feed is
// ported. The 0 setting remains as the bring-up fallback (suppresses
// offscreen-RT draws/clears and FB-copy fills).
static ConVar dx11_rt_textures( "dx11_rt_textures", "1", 0,
	"Bind texture render targets for real; 0 = suppress offscreen-RT draws/clears (bring-up fallback)" );

void StateDx11_SetRenderTexture( ShaderAPITextureHandle_t hTexture, ShaderAPITextureHandle_t hDepthTexture )
{
	// Explicit depth-texture attach (the flashlight shadow caster pass:
	// SetRenderTargetEx(0, _rt_ShadowDummy, _rt_ShadowDepthTexture_N)). Any
	// other depth argument (DEPTHBUFFER/NONE/non-depth handles) keeps the
	// default per-size aux depth — NONE intentionally stays on the aux depth,
	// matching the pre-depth-stage behavior every other RT path was built on.
	ShaderAPITextureHandle_t hDepth = INVALID_SHADERAPI_TEXTURE_HANDLE;
	if ( hDepthTexture != INVALID_SHADERAPI_TEXTURE_HANDLE && hDepthTexture >= 0 &&
		 TextureDx11_IsDepth( hDepthTexture ) && TextureDx11_GetDSV( hDepthTexture ) )
	{
		hDepth = hDepthTexture;
		// The DSV may still be bound as a depth-map SRV from last frame's
		// flashlight passes; D3D11 nulls such SRVs at OMSetRenderTargets with
		// a debug-layer warning per slot. Clear the PS slots up front.
		if ( D3D11Context() )
		{
			ID3D11ShaderResourceView *pNullSRVs[DX11_MAX_SAMPLERS] = {};
			D3D11Context()->PSSetShaderResources( 0, DX11_MAX_SAMPLERS, pNullSRVs );
		}
	}

	if ( hTexture == INVALID_SHADERAPI_TEXTURE_HANDLE )
	{
		s_hRenderTexture = INVALID_SHADERAPI_TEXTURE_HANDLE;
		s_hRenderDepthTexture = INVALID_SHADERAPI_TEXTURE_HANDLE;
		s_bOffscreenRT = false;
		return;
	}
	if ( dx11_rt_textures.GetBool() && TextureDx11_GetRTV( hTexture, false ) )
	{
		s_hRenderTexture = hTexture;
		s_hRenderDepthTexture = hDepth;
		s_bOffscreenRT = false;
		return;
	}
	// Unbindable (or intentionally suppressed) target: keep draws/clears off
	// the backbuffer
	if ( dx11_rt_textures.GetBool() )
	{
		static bool s_bWarnedUnbindable = false;
		if ( !s_bWarnedUnbindable )
		{
			s_bWarnedUnbindable = true;
			Warning( "shaderapidx11: render-target texture %d has no RTV; suppressing its draws\n", (int)hTexture );
		}
	}
	s_hRenderTexture = INVALID_SHADERAPI_TEXTURE_HANDLE;
	s_hRenderDepthTexture = INVALID_SHADERAPI_TEXTURE_HANDLE;
	s_bOffscreenRT = true;
}

ShaderAPITextureHandle_t StateDx11_GetRenderTexture()
{
	return s_hRenderTexture;
}

bool StateDx11_RTTexturesEnabled()
{
	return dx11_rt_textures.GetBool();
}

// Resolve the OM views for the active target (backbuffer or texture RT)
static void GetCurrentTargets( bool bSRGBWrite, ID3D11RenderTargetView **ppRTV, ID3D11DepthStencilView **ppDSV )
{
	if ( s_hRenderTexture != INVALID_SHADERAPI_TEXTURE_HANDLE )
	{
		*ppRTV = TextureDx11_GetRTV( s_hRenderTexture, bSRGBWrite );
		// Explicit depth-texture attach (the shadow caster pass renders into
		// _rt_ShadowDepthTexture_N's DSV; the dummy color RT just absorbs the
		// disabled color writes). Falls back to the per-size aux depth.
		if ( s_hRenderDepthTexture != INVALID_SHADERAPI_TEXTURE_HANDLE )
		{
			*ppDSV = TextureDx11_GetDSV( s_hRenderDepthTexture );
			if ( *ppDSV )
				return;
		}
		int nW = 0, nH = 0;
		TextureDx11_GetDims( s_hRenderTexture, nW, nH );
		*ppDSV = GetAuxDepth( nW, nH );
		return;
	}
	*ppRTV = ( bSRGBWrite && g_pD3D11RTV_SRGB ) ? g_pD3D11RTV_SRGB : g_pD3D11RTV;
	*ppDSV = g_pD3D11DSV;
}

// Dimensions of the active target (for viewport-scoped partial clears)
static void GetCurrentTargetDims( int &nWidth, int &nHeight )
{
	if ( s_hRenderTexture != INVALID_SHADERAPI_TEXTURE_HANDLE &&
		 TextureDx11_GetDims( s_hRenderTexture, nWidth, nHeight ) )
		return;
	nWidth = nHeight = 0;
	if ( g_pShaderDeviceDx11 )
		g_pShaderDeviceDx11->GetBackBufferDimensions( nWidth, nHeight );
}

void StateDx11_ClearViews( bool bClearColor, bool bClearDepth, bool bClearStencil, const float pColor[4] )
{
	ID3D11DeviceContext *pCtx = D3D11Context();
	if ( !pCtx || s_bOffscreenRT )
		return;

	ID3D11RenderTargetView *pRTV = NULL;
	ID3D11DepthStencilView *pDSV = NULL;
	GetCurrentTargets( false, &pRTV, &pDSV );

	if ( bClearColor && pRTV )
	{
		// dx9 Clear() is viewport-scoped; ClearRenderTargetView is whole-target.
		// The engine relies on this (e.g. mid-frame clears with a small
		// viewport set), so clip to the current viewport via ClearView.
		int nW = 0, nH = 0;
		GetCurrentTargetDims( nW, nH );
		bool bPartial = s_Viewport.m_nTopLeftX > 0 || s_Viewport.m_nTopLeftY > 0 ||
			( s_Viewport.m_nWidth > 0 && s_Viewport.m_nWidth < nW ) ||
			( s_Viewport.m_nHeight > 0 && s_Viewport.m_nHeight < nH );

		ID3D11DeviceContext1 *pCtx1 = NULL;
		if ( bPartial && SUCCEEDED( pCtx->QueryInterface( __uuidof( ID3D11DeviceContext1 ), (void **)&pCtx1 ) ) )
		{
			D3D11_RECT rect;
			rect.left = s_Viewport.m_nTopLeftX;
			rect.top = s_Viewport.m_nTopLeftY;
			rect.right = s_Viewport.m_nTopLeftX + s_Viewport.m_nWidth;
			rect.bottom = s_Viewport.m_nTopLeftY + s_Viewport.m_nHeight;
			pCtx1->ClearView( pRTV, pColor, &rect, 1 );
			pCtx1->Release();
		}
		else
		{
			if ( pCtx1 )
				pCtx1->Release();
			pCtx->ClearRenderTargetView( pRTV, pColor );
		}
		if ( g_pShaderDeviceDx11 )
			g_pShaderDeviceDx11->OnFrameCleared();
	}

	UINT nDepthFlags = ( bClearDepth ? D3D11_CLEAR_DEPTH : 0 ) | ( bClearStencil ? D3D11_CLEAR_STENCIL : 0 );
	if ( nDepthFlags && pDSV )
	{
		pCtx->ClearDepthStencilView( pDSV, nDepthFlags, 1.0f, 0 );
	}
}

void StateDx11_ReapplyViewport()
{
	if ( !D3D11Context() )
		return;
	D3D11_VIEWPORT vp;
	vp.TopLeftX = (float)s_Viewport.m_nTopLeftX;
	vp.TopLeftY = (float)s_Viewport.m_nTopLeftY;
	vp.Width = (float)s_Viewport.m_nWidth;
	vp.Height = (float)s_Viewport.m_nHeight;
	vp.MinDepth = s_Viewport.m_flMinZ;
	vp.MaxDepth = s_Viewport.m_flMaxZ;
	D3D11Context()->RSSetViewports( 1, &vp );
}

void StateDx11_SetScissorRect( int l, int t, int r, int b, bool bEnable )
{
	s_bScissorEnabled = bEnable;
	s_ScissorRect.left = l;
	s_ScissorRect.top = t;
	s_ScissorRect.right = r;
	s_ScissorRect.bottom = b;
}

void StateDx11_CullMode( MaterialCullMode_t mode )
{
	s_nDynamicCullMode = mode;
}

void StateDx11_BindTexture( int nSampler, ShaderAPITextureHandle_t hTexture )
{
	if ( nSampler >= 0 && nSampler < DX11_MAX_SAMPLERS )
		s_BoundTextures[nSampler] = hTexture;
}


//-----------------------------------------------------------------------------
// Rasterizer state cache (cull x wireframe x scissor x poly-offset mode)
//-----------------------------------------------------------------------------
static ID3D11RasterizerState *s_RasterStates[3][2][2][3];	// [cull none/ccw/cw][wire][scissor][offset none/decal/shadow]

// Shadow-bias factors (clientshadowmgr SetShadowDepthBiasFactors, per
// flashlight from mat_slopescaledepthbias_shadowmap=16 /
// mat_depthbias_shadowmap=0.0005). Defaults match those cvars.
static float s_flShadowSlopeScaleDepthBias = 16.0f;
static float s_flShadowDepthBias = 0.0005f;

static ID3D11RasterizerState *GetRasterState( int nCull, bool bWire, bool bScissor, int nPolyOffset )
{
	ID3D11RasterizerState *&pState = s_RasterStates[nCull][bWire][bScissor][nPolyOffset];
	if ( !pState && D3D11Device() )
	{
		D3D11_RASTERIZER_DESC desc;
		ZeroMemory( &desc, sizeof( desc ) );
		desc.FillMode = bWire ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
		desc.CullMode = ( nCull == 0 ) ? D3D11_CULL_NONE : D3D11_CULL_BACK;
		desc.FrontCounterClockwise = ( nCull == 2 );	// cull-CW mode = front faces are CCW
		desc.DepthClipEnable = TRUE;
		desc.ScissorEnable = bScissor;
		if ( nPolyOffset == 1 )
		{
			// dx9 decal polygon offset (ApplyZBias, shaderapidx8.cpp:4793):
			// slope scale = 1/mat_slopescaledepthbias_decal = 1/-0.5 = -2,
			// depth bias = 1/mat_depthbias_decal = 1/-262144 = -3.8147e-6.
			// D3D9's float DEPTHBIAS is in normalized depth; D3D11's int
			// DepthBias is in units of 2^-24 on D24S8: -3.8147e-6 * 2^24 = -64
			// (the parity-register conversion, Plan.md risk #5).
			desc.DepthBias = -64;
			desc.SlopeScaledDepthBias = -2.0f;
		}
		else if ( nPolyOffset == 2 )
		{
			// Flashlight shadow caster bias (dx9 CommitShadowDepthBias):
			// same float -> 2^-24-units conversion as the decal offset.
			desc.DepthBias = (int)( s_flShadowDepthBias * 16777216.0f );
			desc.SlopeScaledDepthBias = s_flShadowSlopeScaleDepthBias;
		}
		D3D11Device()->CreateRasterizerState( &desc, &pState );
	}
	return pState;
}

void StateDx11_SetShadowDepthBias( float flSlopeScale, float flDepthBias )
{
	if ( flSlopeScale == s_flShadowSlopeScaleDepthBias && flDepthBias == s_flShadowDepthBias )
		return;
	s_flShadowSlopeScaleDepthBias = flSlopeScale;
	s_flShadowDepthBias = flDepthBias;
	// Shadow-bias raster states bake the factors; drop them so they rebuild.
	for ( int c = 0; c < 3; ++c )
		for ( int w = 0; w < 2; ++w )
			for ( int s = 0; s < 2; ++s )
			{
				if ( s_RasterStates[c][w][s][2] )
				{
					s_RasterStates[c][w][s][2]->Release();
					s_RasterStates[c][w][s][2] = NULL;
				}
			}
}


//-----------------------------------------------------------------------------
// Universal shaders + input layouts + constant buffers
//-----------------------------------------------------------------------------
// Permutation (universal.hlsl MODE): 0 = plain, 1 = LIGHTMAP (texcoord1 +
// lightmap on s1), 2 = model rigid (bone0 + ambient cube), 3 = model skinned,
// 4 = model rigid + baked vertex light (static-prop color mesh on COLOR1),
// 5 = model rigid per-pixel phong (dx9 skin), 6 = skinned per-pixel phong,
// 7 = Cable (ropes/wires: s0 normal map, s1 base, vertex color = rope light),
// 8 = Eyes (sphere normal + planar iris/glint projections, skinned),
// 9 = WindowImposter (areaportal window glass: eye ray into cube at s0),
// 10 = EyeRefract (TF2 player eyeballs: cornea normal + parallax iris +
// caustics + cube reflection at s2), 11 = Teeth (vertexlit x $illumfactor
// forward-dim), 12 = pyro_vision world (lightmapped posterize/colorbar),
// 13 = pyro_vision VERTEX_LIT model (rigid or skinned), 14 = SpriteCard
// (particle billboard expansion)
#define DX11_UNIVERSAL_PERMS 36
#define DX11_MAX_BONES 53

// Model path state: dx9 cModel contract — 3 float4 rows per bone, world.x =
// dot(row0, float4(pos,1)). Bone 0 doubles as the rigid model transform; when
// the engine drives the MODEL matrix stack instead of LoadBoneMatrix, bone 0
// is refreshed from the stack top at draw time (last-writer-wins like dx9).
static float s_flBoneRows[DX11_MAX_BONES * 3 * 4];
// World-space camera position appended to b1 (dx9 cEyePos c2 — the dx9
// backend committed it internally, so the VS mirror never sees it)
static float s_flEyePosPM[4];
static float s_flAmbientCube[6 * 4];
static bool s_bModelStateDirty = true;
static float s_flViewProj[16];

void StateDx11_LoadBoneMatrix( int nBone, const float *pRowMajor3x4 )
{
	if ( nBone < 0 || nBone >= DX11_MAX_BONES || !pRowMajor3x4 )
		return;
	memcpy( s_flBoneRows + nBone * 12, pRowMajor3x4, 12 * sizeof( float ) );
	s_bModelStateDirty = true;

	// dx9 parity (shaderapidx8.cpp:10286): bone 0 IS the model transform —
	// LoadBoneMatrix(0) also drives the MODEL stack (transposed into the
	// v*M row-vector convention), and bone 0 always commits from the stack
	// at draw. Both writers (stack loads, bone loads) thus converge.
	if ( nBone == 0 )
	{
		EnsureMatrixInit();
		Matrix44Dx11_t &m = s_MatrixStacks[MATERIAL_MODEL][s_nStackTop[MATERIAL_MODEL]];
		for ( int nRow = 0; nRow < 3; ++nRow )
		{
			m[0][nRow] = pRowMajor3x4[nRow * 4 + 0];
			m[1][nRow] = pRowMajor3x4[nRow * 4 + 1];
			m[2][nRow] = pRowMajor3x4[nRow * 4 + 2];
			m[3][nRow] = pRowMajor3x4[nRow * 4 + 3];
		}
		m[0][3] = m[1][3] = m[2][3] = 0.0f;
		m[3][3] = 1.0f;
		s_bMatricesDirty = true;
	}
}

// dx9 SetNumBoneWeights: per-STRIP skinning signal. Rigid strips (0) carry
// garbage in their weight/index vertex fields by design — the unskinned
// shader path must be used for them (dx9 SKINNING combo parity).
static int s_nNumBones = 0;

void StateDx11_SetNumBones( int nBones )
{
	s_nNumBones = nBones;
}

int StateDx11_GetNumBones()
{
	return s_nNumBones;
}

// Set around CStaticMeshDx11 draws: static meshes with normals are studio
// geometry (props) — vgui only ever draws dynamic meshes, so this is a safe
// signal for the lit model path even when no bone state was pushed.
static bool s_bDrawingStaticMesh = false;

void StateDx11_SetDrawingStaticMesh( bool bStatic )
{
	s_bDrawingStaticMesh = bStatic;
}

// Baked static-prop lighting (dx9 STATIC_LIGHT_VERTEX): while set, MODE2
// model draws upgrade to MODE4 and the color mesh streams COLOR1 from IA
// slot 2 (dx9 stream 1, meshdx8.cpp:3166).
static ID3D11Buffer *s_pStaticColorVB = NULL;
static UINT s_nStaticColorStride = 0;
static UINT s_nStaticColorOffset = 0;

void StateDx11_SetStaticColorMesh( ID3D11Buffer *pVB, int nStrideBytes, int nOffsetBytes )
{
	s_pStaticColorVB = pVB;
	s_nStaticColorStride = (UINT)nStrideBytes;
	s_nStaticColorOffset = (UINT)nOffsetBytes;
}

// Facial flex deltas (dx9 stream 2 → IA slot 3): pos delta + wrinkle +
// normal delta, 28-byte stride. NULL = no flex this draw (the layout's
// POSITION1/NORMAL1 then read constant zeros from the slot-1 fallback).
static ID3D11Buffer *s_pFlexVB = NULL;
static UINT s_nFlexStride = 0;
static UINT s_nFlexOffset = 0;

void StateDx11_SetFlexMesh( ID3D11Buffer *pVB, unsigned int nStrideBytes, unsigned int nOffsetBytes )
{
	s_pFlexVB = pVB;
	s_nFlexStride = nStrideBytes;
	s_nFlexOffset = nOffsetBytes;
}

void StateDx11_SetAmbientLightCube( const float *pCube6x4 )
{
	if ( pCube6x4 && memcmp( s_flAmbientCube, pCube6x4, sizeof( s_flAmbientCube ) ) != 0 )
	{
		memcpy( s_flAmbientCube, pCube6x4, sizeof( s_flAmbientCube ) );
		s_bModelStateDirty = true;
	}
}

// Local lights (dx9 cLightInfo): 4 lights x 5 float4 —
// [0] color.rgb + type (0 disabled, 1 point, 2 directional, 3 spot)
// [1] pos.xyz + range  [2] dir.xyz + spot exponent
// [3] atten (a0, a1, a2) + stopdot (cos theta)  [4] stopdot2 (cos phi)
#define DX11_MAX_LIGHTS 4
static float s_flLights[DX11_MAX_LIGHTS * 5 * 4];

void StateDx11_SetLight( int nLight, const LightDesc_t &desc )
{
	if ( nLight < 0 || nLight >= DX11_MAX_LIGHTS )
		return;
	float *pDest = s_flLights + nLight * 20;
	memset( pDest, 0, 20 * sizeof( float ) );
	switch ( desc.m_Type )
	{
	case MATERIAL_LIGHT_POINT:		pDest[3] = 1.0f; break;
	case MATERIAL_LIGHT_DIRECTIONAL:pDest[3] = 2.0f; break;
	case MATERIAL_LIGHT_SPOT:		pDest[3] = 3.0f; break;
	default:						pDest[3] = 0.0f; s_bModelStateDirty = true; return;
	}
	pDest[0] = desc.m_Color.x; pDest[1] = desc.m_Color.y; pDest[2] = desc.m_Color.z;
	pDest[4] = desc.m_Position.x; pDest[5] = desc.m_Position.y; pDest[6] = desc.m_Position.z;
	pDest[7] = desc.m_Range;
	pDest[8] = desc.m_Direction.x; pDest[9] = desc.m_Direction.y; pDest[10] = desc.m_Direction.z;
	pDest[11] = desc.m_Falloff;
	pDest[12] = desc.m_Attenuation0; pDest[13] = desc.m_Attenuation1; pDest[14] = desc.m_Attenuation2;
	pDest[15] = desc.m_ThetaDot;
	pDest[16] = desc.m_PhiDot;
	s_bModelStateDirty = true;
}

void StateDx11_DisableAllLights()
{
	for ( int i = 0; i < DX11_MAX_LIGHTS; ++i )
		s_flLights[i * 20 + 3] = 0.0f;
	s_bModelStateDirty = true;
}

// dx9 GetDX9LightState sources (shaderapidx8.cpp:12770-12812)
int StateDx11_GetNumEnabledLights()
{
	int nCount = 0;
	for ( int i = 0; i < DX11_MAX_LIGHTS; ++i )
	{
		if ( s_flLights[i * 20 + 3] != 0.0f )
			++nCount;
	}
	return nCount;
}

bool StateDx11_IsAmbientCubeNonZero()
{
	for ( int i = 0; i < 6; ++i )
	{
		if ( s_flAmbientCube[i * 4 + 0] != 0.0f || s_flAmbientCube[i * 4 + 1] != 0.0f ||
			 s_flAmbientCube[i * 4 + 2] != 0.0f )
			return true;
	}
	return false;
}

// dx9 SetPixelShaderStateAmbientLightCube (shaderapidx8.cpp:8880): the
// dynamic-state cube (or black) into 6 consecutive mirror registers.
void StateDx11_SetPSAmbientCube( int nReg, bool bForceBlack )
{
	if ( bForceBlack )
	{
		float flBlack[6 * 4] = {};
		StateDx11_SetPSConstants( nReg, flBlack, 6 );
	}
	else
	{
		StateDx11_SetPSConstants( nReg, s_flAmbientCube, 6 );
	}
}

// dx9 CommitPixelShaderLighting packing (shaderapidx8.cpp:11145): up to 4
// POINT-ified lights in 6 float4s — lights 0..2 as (color,pos) register
// pairs, light 3 spread across the six w components. Directionals become
// points at lightingOrigin - dir * 10000. dx9's type sort is skipped on
// purpose: our VS attenuation and this array use the same slot order, and
// the summed lighting result is order-independent.
void StateDx11_CommitPSLighting( int nReg )
{
	const float flFarAway = 10000.0f;
	float ls[6 * 4] = {};
	int nOut = 0;
	for ( int i = 0; i < DX11_MAX_LIGHTS && nOut < 4; ++i )
	{
		const float *pL = s_flLights + i * 20;
		int nType = (int)pL[3];
		if ( nType == 0 )
			continue;

		float vPos[3];
		if ( nType == 2 )	// directional
		{
			vPos[0] = s_vLightingOrigin[0] - pL[8] * flFarAway;
			vPos[1] = s_vLightingOrigin[1] - pL[9] * flFarAway;
			vPos[2] = s_vLightingOrigin[2] - pL[10] * flFarAway;
		}
		else
		{
			vPos[0] = pL[4]; vPos[1] = pL[5]; vPos[2] = pL[6];
		}

		if ( nOut < 3 )
		{
			float *pColorReg = ls + ( nOut * 2 ) * 4;
			float *pPosReg = ls + ( nOut * 2 + 1 ) * 4;
			pColorReg[0] = pL[0]; pColorReg[1] = pL[1]; pColorReg[2] = pL[2];
			pPosReg[0] = vPos[0]; pPosReg[1] = vPos[1]; pPosReg[2] = vPos[2];
		}
		else
		{
			ls[0 * 4 + 3] = pL[0]; ls[1 * 4 + 3] = pL[1]; ls[2 * 4 + 3] = pL[2];
			ls[3 * 4 + 3] = vPos[0]; ls[4 * 4 + 3] = vPos[1]; ls[5 * 4 + 3] = vPos[2];
		}
		++nOut;
	}
	StateDx11_SetPSConstants( nReg, ls, 6 );
}
static ID3D11VertexShader *s_pUniversalVS[DX11_UNIVERSAL_PERMS];
static ID3D11PixelShader *s_pUniversalPS[DX11_UNIVERSAL_PERMS];
static CUtlBuffer s_UniversalVSBlob[DX11_UNIVERSAL_PERMS];
static bool s_bUniversalLoadAttempted;

struct LayoutEntryDx11_t
{
	VertexFormat_t m_Format;
	int m_nPerm;
	bool m_bFlex;	// POSITION1/NORMAL1 sourced from IA slot 3 vs the zero fallback
	ID3D11InputLayout *m_pLayout;
};
static CUtlVector<LayoutEntryDx11_t> s_Layouts;

static ID3D11Buffer *s_pPerFrameCB;	// b0: row_major float4x4 mvp
static ID3D11Buffer *s_pPerDrawCB;	// b3: alphatest + modulation + tint control
static ID3D11Buffer *s_pPerModelCB;	// b1: viewproj + ambient cube + bone rows
static ID3D11Buffer *s_pPSMirrorCB;	// b2 (PS): dx9 ps c0..c31 mirror (per pass)
static ID3D11Buffer *s_pVSMirrorCB;	// b2 (VS): dx9 vs c0..c63 mirror (per pass)

// Slot-1 constant attributes for vertex formats that lack COLOR or TEXCOORD:
// zero UV at offset 0, white at offset 8, fetched per-instance with step rate
// 0 so the value never advances.
static ID3D11Buffer *s_pDefaultAttrVB;

static bool EnsureUniversalShaders()
{
	if ( s_pUniversalVS[0] )
		return true;
	if ( s_bUniversalLoadAttempted )
		return false;
	s_bUniversalLoadAttempted = true;

	for ( int nPerm = 0; nPerm < DX11_UNIVERSAL_PERMS; ++nPerm )
	{
		CUtlBuffer psBlob;
		if ( !ShaderPackDx11_LoadBlob( "universal_vs", nPerm, s_UniversalVSBlob[nPerm] ) ||
			 !ShaderPackDx11_LoadBlob( "universal_ps", nPerm, psBlob ) )
			return false;

		if ( FAILED( D3D11Device()->CreateVertexShader( s_UniversalVSBlob[nPerm].Base(), s_UniversalVSBlob[nPerm].TellPut(), NULL, &s_pUniversalVS[nPerm] ) ) ||
			 FAILED( D3D11Device()->CreatePixelShader( psBlob.Base(), psBlob.TellPut(), NULL, &s_pUniversalPS[nPerm] ) ) )
		{
			Warning( "shaderapidx11: universal shader creation failed (perm %d)\n", nPerm );
			return false;
		}
	}

	D3D11_BUFFER_DESC cb = { 64, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
	D3D11Device()->CreateBuffer( &cb, NULL, &s_pPerFrameCB );
	cb.ByteWidth = 160;	// 10 float4: alphatest, modulation, tint, phong, misc, eye, sprite, fog control, fog color, envmap control
	D3D11Device()->CreateBuffer( &cb, NULL, &s_pPerDrawCB );
	cb.ByteWidth = ( 4 + 6 + DX11_MAX_LIGHTS * 5 + DX11_MAX_BONES * 3 + 1 ) * 16;	// viewproj + cube + lights + bones + eyepos
	D3D11Device()->CreateBuffer( &cb, NULL, &s_pPerModelCB );
	cb.ByteWidth = DX11_PS_MIRROR_CONSTANTS * 16;
	D3D11Device()->CreateBuffer( &cb, NULL, &s_pPSMirrorCB );
	cb.ByteWidth = DX11_VS_MIRROR_CONSTANTS * 16;
	D3D11Device()->CreateBuffer( &cb, NULL, &s_pVSMirrorCB );

	// Slot-1 constant attributes: zero UV @0, white color @8, and 16 zero
	// bytes @16 — the flex POSITION1/NORMAL1 fallback (zero deltas).
	const struct { float uv[2]; uint32 color; uint32 pad; float zero[4]; } defaultAttrs =
		{ { 0.0f, 0.0f }, 0xFFFFFFFF, 0, { 0.0f, 0.0f, 0.0f, 0.0f } };
	D3D11_BUFFER_DESC vb = { sizeof( defaultAttrs ), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
	D3D11_SUBRESOURCE_DATA vbData = { &defaultAttrs, 0, 0 };
	D3D11Device()->CreateBuffer( &vb, &vbData, &s_pDefaultAttrVB );

	Msg( "shaderapidx11: universal bring-up shaders loaded\n" );
	return s_pPerFrameCB && s_pPerDrawCB && s_pPerModelCB && s_pPSMirrorCB && s_pDefaultAttrVB;
}

// dx11_reload_shaders: recompile every universal permutation from the HLSL
// source tree (d3dcompiler_47, same debug flags as the packs). All perms must
// compile before anything is swapped — a failed reload keeps the live set.
// Cached input layouts are dropped (they pair with the VS blobs) and lazily
// recreate against the new signatures.
bool StateDx11_ReloadUniversalShaders( const char *pHlslPath )
{
	if ( !D3D11Device() )
		return false;

	ID3D11VertexShader *pNewVS[DX11_UNIVERSAL_PERMS] = {};
	ID3D11PixelShader *pNewPS[DX11_UNIVERSAL_PERMS] = {};
	CUtlBuffer newVSBlob[DX11_UNIVERSAL_PERMS];

	bool bOK = true;
	for ( int nPerm = 0; nPerm < DX11_UNIVERSAL_PERMS && bOK; ++nPerm )
	{
		CUtlBuffer psBlob;
		bOK = ShaderDx11_CompileFromSource( pHlslPath, "MainVs", "vs_5_0", newVSBlob[nPerm], "MODE", nPerm ) &&
			  ShaderDx11_CompileFromSource( pHlslPath, "MainPs", "ps_5_0", psBlob, "MODE", nPerm );
		if ( bOK )
		{
			bOK = SUCCEEDED( D3D11Device()->CreateVertexShader( newVSBlob[nPerm].Base(), newVSBlob[nPerm].TellPut(), NULL, &pNewVS[nPerm] ) ) &&
				  SUCCEEDED( D3D11Device()->CreatePixelShader( psBlob.Base(), psBlob.TellPut(), NULL, &pNewPS[nPerm] ) );
			if ( !bOK )
				Warning( "shaderapidx11: shader object creation failed for reloaded perm %d\n", nPerm );
		}
	}

	if ( !bOK )
	{
		for ( int i = 0; i < DX11_UNIVERSAL_PERMS; ++i )
		{
			if ( pNewVS[i] ) pNewVS[i]->Release();
			if ( pNewPS[i] ) pNewPS[i]->Release();
		}
		return false;
	}

	for ( int i = 0; i < DX11_UNIVERSAL_PERMS; ++i )
	{
		if ( s_pUniversalVS[i] ) s_pUniversalVS[i]->Release();
		if ( s_pUniversalPS[i] ) s_pUniversalPS[i]->Release();
		s_pUniversalVS[i] = pNewVS[i];
		s_pUniversalPS[i] = pNewPS[i];
		s_UniversalVSBlob[i].Clear();
		s_UniversalVSBlob[i].Put( newVSBlob[i].Base(), newVSBlob[i].TellPut() );
	}
	for ( int i = 0; i < s_Layouts.Count(); ++i )
	{
		if ( s_Layouts[i].m_pLayout )
			s_Layouts[i].m_pLayout->Release();
	}
	s_Layouts.Purge();
	return true;
}

// dx9 occlusion query parity (shaderapidx8.cpp:12933-12977): the sun/lens
// glow pixel-visibility system creates queries, brackets glow quads with
// Begin/End, and polls counts on later frames.
struct OcclusionQueryDx11_t
{
	ID3D11Query *m_pQuery;
	bool m_bIssued;	// End() ran at least once — results can be polled
};

ShaderAPIOcclusionQuery_t StateDx11_CreateOcclusionQuery()
{
	if ( !D3D11Device() )
		return INVALID_SHADERAPI_OCCLUSION_QUERY_HANDLE;
	D3D11_QUERY_DESC desc = { D3D11_QUERY_OCCLUSION, 0 };
	ID3D11Query *pQuery = NULL;
	if ( FAILED( D3D11Device()->CreateQuery( &desc, &pQuery ) ) || !pQuery )
		return INVALID_SHADERAPI_OCCLUSION_QUERY_HANDLE;
	OcclusionQueryDx11_t *pWrap = new OcclusionQueryDx11_t;
	pWrap->m_pQuery = pQuery;
	pWrap->m_bIssued = false;
	return (ShaderAPIOcclusionQuery_t)pWrap;
}

void StateDx11_DestroyOcclusionQuery( ShaderAPIOcclusionQuery_t h )
{
	OcclusionQueryDx11_t *pWrap = (OcclusionQueryDx11_t *)h;
	if ( !pWrap )
		return;
	if ( pWrap->m_pQuery )
		pWrap->m_pQuery->Release();
	delete pWrap;
}

void StateDx11_BeginOcclusionQuery( ShaderAPIOcclusionQuery_t h )
{
	OcclusionQueryDx11_t *pWrap = (OcclusionQueryDx11_t *)h;
	if ( pWrap && pWrap->m_pQuery && D3D11Context() )
		D3D11Context()->Begin( pWrap->m_pQuery );
}

void StateDx11_EndOcclusionQuery( ShaderAPIOcclusionQuery_t h )
{
	OcclusionQueryDx11_t *pWrap = (OcclusionQueryDx11_t *)h;
	if ( pWrap && pWrap->m_pQuery && D3D11Context() )
	{
		D3D11Context()->End( pWrap->m_pQuery );
		pWrap->m_bIssued = true;
	}
}

int StateDx11_GetOcclusionQueryPixels( ShaderAPIOcclusionQuery_t h, bool bFlush )
{
	OcclusionQueryDx11_t *pWrap = (OcclusionQueryDx11_t *)h;
	if ( !pWrap || !pWrap->m_pQuery || !D3D11Context() )
		return OCCLUSION_QUERY_RESULT_ERROR;
	if ( !pWrap->m_bIssued )
		return OCCLUSION_QUERY_RESULT_ERROR;	// dx9: not-available → error → caller reissues

	UINT64 nPixels = 0;
	// dx9 D3DGETDATA_FLUSH inverts to D3D11's DONOTFLUSH
	HRESULT hr = D3D11Context()->GetData( pWrap->m_pQuery, &nPixels, sizeof( nPixels ),
		bFlush ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH );
	if ( hr == S_FALSE )
		return OCCLUSION_QUERY_RESULT_PENDING;
	if ( FAILED( hr ) )
		return OCCLUSION_QUERY_RESULT_ERROR;
	if ( nPixels > (UINT64)INT_MAX )
		nPixels = (UINT64)INT_MAX;
	return (int)nPixels;
}

// Element offsets come from the same vertex-description math the dx9 backend
// uses, so buffer layout and input layout can never diverge.
#include "meshbase.h"

static ID3D11InputLayout *GetInputLayout( VertexFormat_t fmt, int nPerm, bool bFlex )
{
	for ( int i = 0; i < s_Layouts.Count(); ++i )
	{
		if ( s_Layouts[i].m_Format == fmt && s_Layouts[i].m_nPerm == nPerm && s_Layouts[i].m_bFlex == bFlex )
			return s_Layouts[i].m_pLayout;
	}

	VertexDesc_t desc;
	CVertexBufferBase::ComputeVertexDescription( NULL, fmt, desc );

	if ( !desc.m_VertexSize_Position )
	{
		static CUtlVector<VertexFormat_t> s_Warned;
		if ( s_Warned.Find( fmt ) < 0 )
		{
			s_Warned.AddToTail( fmt );
			Warning( "shaderapidx11: vertex format 0x%llx lacks position; draw skipped (M3 universal path)\n", (unsigned long long)fmt );
		}
		LayoutEntryDx11_t entry = { fmt, nPerm, bFlex, NULL };
		s_Layouts.AddToTail( entry );
		return NULL;
	}

	// Missing color/texcoord come from the slot-1 constant-attribute buffer
	// (white / zero UV) instead of skipping the draw.
	D3D11_INPUT_ELEMENT_DESC elements[10] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, (UINT)(uintp)desc.m_pPosition, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, (UINT)(uintp)desc.m_pColor, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)(uintp)desc.m_pTexCoord[0], D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)(uintp)desc.m_pTexCoord[1], D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, (UINT)(uintp)desc.m_pNormal, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)(uintp)desc.m_pBoneWeight, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	if ( !desc.m_VertexSize_Color )
	{
		elements[1].InputSlot = 1;
		elements[1].AlignedByteOffset = 8;
		elements[1].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
	}
	if ( !desc.m_VertexSize_TexCoord[0] )
	{
		elements[2].InputSlot = 1;
		elements[2].AlignedByteOffset = 0;
		elements[2].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
	}
	if ( !desc.m_VertexSize_TexCoord[1] )
	{
		elements[3].InputSlot = 1;
		elements[3].AlignedByteOffset = 0;
		elements[3].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
	}

	// Model perms consume NORMAL (and BLENDWEIGHT/BLENDINDICES when skinned);
	// the element list is rearranged so the used entries are contiguous.
	// Perm 9 (windowimposter) keeps the default pos/color/uv0 set — its format
	// is position-only and the unused attributes ride the slot-1 fallbacks.
	int nElements = 3;
	if ( nPerm == 1 || nPerm == 7 || nPerm == 12 )
	{
		// lightmap + cable + pyro world: pos/color/texcoord0/texcoord1.
		// Cable's tangent fields are dead vertex bytes (the dx9 VS computes
		// but never outputs them) — the texcoord offsets from
		// ComputeVertexDescription already step over them.
		nElements = 4;
		if ( nPerm == 1 )
		{
			// The envmap term reads the world NORMAL (elements[4] is already
			// the NORMAL template) + TANGENT/BINORMAL for the $bumpmap
			// perturbation (the engine adds tangent fields to every envmapped
			// world format — helper:460). Fields absent from the format read
			// zeros from the slot-1 fallback — the VS safe-normalize lands
			// the normal on +Z; zero tangents only occur with the bump bit
			// off, where the PS never uses them.
			if ( !desc.m_VertexSize_Normal )
			{
				elements[4].InputSlot = 1;
				elements[4].AlignedByteOffset = 16;
				elements[4].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
			}
			D3D11_INPUT_ELEMENT_DESC tanS = { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
				(UINT)(uintp)desc.m_pTangentS, D3D11_INPUT_PER_VERTEX_DATA, 0 };
			D3D11_INPUT_ELEMENT_DESC tanT = { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
				(UINT)(uintp)desc.m_pTangentT, D3D11_INPUT_PER_VERTEX_DATA, 0 };
			if ( !desc.m_VertexSize_TangentS )
			{
				tanS.InputSlot = 1;
				tanS.AlignedByteOffset = 16;
				tanS.InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
			}
			if ( !desc.m_VertexSize_TangentT )
			{
				tanT.InputSlot = 1;
				tanT.AlignedByteOffset = 16;
				tanT.InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
			}
			elements[5] = tanS;
			elements[6] = tanT;
			nElements = 7;
		}
	}
	else if ( nPerm == 14 )
	{
		// SpriteCard: pos, tint, texcoords 0-7 at their NATIVE dims
		// ({4,4,4,2,4} + {4,4,4} with dualsequence). Texcoords absent from the
		// format read constant zeros from the slot-1 fallback.
		nElements = 2;	// keep POSITION + COLOR0 templates
		for ( int t = 0; t < 8; ++t )
		{
			D3D11_INPUT_ELEMENT_DESC tc = { "TEXCOORD", (UINT)t, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
				(UINT)(uintp)desc.m_pTexCoord[t], D3D11_INPUT_PER_VERTEX_DATA, 0 };
			if ( desc.m_VertexSize_TexCoord[t] == 8 )
			{
				tc.Format = DXGI_FORMAT_R32G32_FLOAT;
			}
			else if ( !desc.m_VertexSize_TexCoord[t] )
			{
				tc.InputSlot = 1;
				tc.AlignedByteOffset = 16;
				tc.InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
			}
			elements[nElements++] = tc;
		}
	}
	else if ( nPerm == 13 || nPerm == 17 || nPerm == 18 || nPerm == 19 || nPerm == 20
		|| nPerm == 21 || nPerm == 28 || nPerm == 30 || nPerm == 33 || nPerm == 34
		|| nPerm == 35 )
	{
		// pyro VERTEX_LIT model + cloak/sheen/refract + shadow build/model +
		// monitorscreen + flashlight/depthwrite passes: pos, color (fallback), uv0, NORMAL, weights, indices —
		// where RIGID formats (and refract's screen-overlay quads) lack the
		// bone fields and read constant ZEROS from the slot-1 fallback
		// instead (zero weights blend to pure bone 0 in the VS), so one perm
		// serves rigid and skinned.
		// The COLOR1 slot-2 element only matters for 13 (the others leave
		// slot 2 unbound; their VS never reads COLOR1).
		elements[3] = elements[4];	// NORMAL
		elements[4] = elements[5];	// BLENDWEIGHT template
		D3D11_INPUT_ELEMENT_DESC idx = { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0,
			(UINT)(uintp)desc.m_pBoneMatrixIndex, D3D11_INPUT_PER_VERTEX_DATA, 0 };
		elements[5] = idx;
		if ( !desc.m_VertexSize_BoneWeight )
		{
			elements[4].InputSlot = 1;
			elements[4].AlignedByteOffset = 16;
			elements[4].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
		}
		if ( !desc.m_VertexSize_BoneMatrixIndex )
		{
			elements[5].InputSlot = 1;
			elements[5].AlignedByteOffset = 16;
			elements[5].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
		}
		// Baked static-prop lighting on slot 2 (left UNBOUND for props without
		// a color mesh — D3D11 defines unbound-slot reads as zeros)
		D3D11_INPUT_ELEMENT_DESC spec = { "COLOR", 1, DXGI_FORMAT_B8G8R8A8_UNORM, 2,
			0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
		elements[6] = spec;
		nElements = 7;
	}
	else if ( nPerm == 8 || nPerm == 10 )
	{
		// Eyes/EyeRefract: skinned but NO normal input (the VS synthesizes the
		// eyeball-sphere normal) — pos, color (fallback), texcoord0, weights,
		// indices. HL2-era dynamic eyeball draws use the material's declared
		// format (pos|normal|uv only — no bone fields): those read constant
		// zeros from the slot-1 fallback, which blends to pure bone 0 (= the
		// identity MODEL stack top for the world-space eyeball verts).
		elements[3] = elements[5];	// BLENDWEIGHT
		D3D11_INPUT_ELEMENT_DESC idx = { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0,
			(UINT)(uintp)desc.m_pBoneMatrixIndex, D3D11_INPUT_PER_VERTEX_DATA, 0 };
		elements[4] = idx;
		if ( !desc.m_VertexSize_BoneWeight )
		{
			elements[3].InputSlot = 1;
			elements[3].AlignedByteOffset = 16;
			elements[3].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
		}
		if ( !desc.m_VertexSize_BoneMatrixIndex )
		{
			elements[4].InputSlot = 1;
			elements[4].AlignedByteOffset = 16;
			elements[4].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
		}
		nElements = 5;
	}
	else if ( nPerm == 15 || nPerm == 16 )
	{
		// Water: pos, color (fallback), uv0, NORMAL, TANGENT, BINORMAL. The
		// perm pick requires the normal/tangent fields, so no fallbacks here
		// (brush water always carries them — water.cpp:213).
		elements[3] = elements[4];	// NORMAL
		D3D11_INPUT_ELEMENT_DESC tanS = { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
			(UINT)(uintp)desc.m_pTangentS, D3D11_INPUT_PER_VERTEX_DATA, 0 };
		D3D11_INPUT_ELEMENT_DESC tanT = { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
			(UINT)(uintp)desc.m_pTangentT, D3D11_INPUT_PER_VERTEX_DATA, 0 };
		elements[4] = tanS;
		elements[5] = tanT;
		nElements = 6;
	}
	else if ( ( nPerm >= 2 && nPerm <= 6 ) || nPerm == 11 )
	{
		elements[3] = elements[4];	// NORMAL replaces the TEXCOORD1 slot
		nElements = 4;
		if ( nPerm == 3 || nPerm == 11 )
		{
			elements[4] = elements[5];	// BLENDWEIGHT (teeth = skinned vertexlit)
			D3D11_INPUT_ELEMENT_DESC idx = { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0,
				(UINT)(uintp)desc.m_pBoneMatrixIndex, D3D11_INPUT_PER_VERTEX_DATA, 0 };
			elements[5] = idx;
			nElements = 6;
		}
		else if ( nPerm == 4 )
		{
			// Baked-light color mesh (dx9 STATIC_LIGHT_VERTEX): COLOR1 streams
			// per-vertex from IA slot 2 (dx9 stream 1, meshdx8.cpp:3166). The
			// vertex byte offset rides in the slot-2 bind, so the element
			// offset is 0. D3DCOLOR bytes = BGRA, same as COLOR0.
			D3D11_INPUT_ELEMENT_DESC spec = { "COLOR", 1, DXGI_FORMAT_B8G8R8A8_UNORM, 2,
				0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
			elements[4] = spec;
			nElements = 5;
		}
		else if ( nPerm >= 5 )
		{
			// Per-pixel phong: the studio tangent (USERDATA float4, xyz +
			// binormal sign) builds the TBN; perm 6 adds the bone fields.
			// The perm pick guarantees m_VertexSize_UserData != 0.
			D3D11_INPUT_ELEMENT_DESC tan = { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
				(UINT)(uintp)desc.m_pUserData, D3D11_INPUT_PER_VERTEX_DATA, 0 };
			elements[4] = tan;
			nElements = 5;
			if ( nPerm == 6 )
			{
				// elements[5] still holds the BLENDWEIGHT template
				D3D11_INPUT_ELEMENT_DESC idx = { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0,
					(UINT)(uintp)desc.m_pBoneMatrixIndex, D3D11_INPUT_PER_VERTEX_DATA, 0 };
				elements[6] = idx;
				nElements = 7;
			}
		}
	}

	// Studio flex deltas (HLSL HASFLEX modes): POSITION1 = pos delta + wrinkle
	// (float4 @0), NORMAL1 = normal delta (@16) — the dx9 stream-2 layout,
	// 28-byte stride on IA slot 3. Without a flex mesh this draw, both read
	// constant zeros from the slot-1 fallback buffer (offset 16) so the VS
	// adds are no-ops.
	if ( ( nPerm >= 2 && nPerm <= 6 ) || nPerm == 8 || nPerm == 10 || nPerm == 11
		|| nPerm == 17 || nPerm == 18 )
	{
		D3D11_INPUT_ELEMENT_DESC posFlex = { "POSITION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 3,
			0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
		D3D11_INPUT_ELEMENT_DESC normFlex = { "NORMAL", 1, DXGI_FORMAT_R32G32B32_FLOAT, 3,
			16, D3D11_INPUT_PER_VERTEX_DATA, 0 };
		if ( !bFlex )
		{
			posFlex.InputSlot = 1;
			posFlex.AlignedByteOffset = 16;
			posFlex.InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
			normFlex.InputSlot = 1;
			normFlex.AlignedByteOffset = 16;
			normFlex.InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
		}
		elements[nElements++] = posFlex;
		elements[nElements++] = normFlex;
	}

	ID3D11InputLayout *pLayout = NULL;
	HRESULT hr = D3D11Device()->CreateInputLayout( elements, nElements,
		s_UniversalVSBlob[nPerm].Base(), s_UniversalVSBlob[nPerm].TellPut(), &pLayout );
	if ( FAILED( hr ) )
	{
		Warning( "shaderapidx11: CreateInputLayout failed for format 0x%llx perm %d (hr=0x%08x)\n", (unsigned long long)fmt, nPerm, hr );
	}
	LayoutEntryDx11_t entry = { fmt, nPerm, bFlex, pLayout };
	s_Layouts.AddToTail( entry );
	return pLayout;
}


//-----------------------------------------------------------------------------
// Per-draw commit
//-----------------------------------------------------------------------------
bool StateDx11_SetupForDraw( VertexFormat_t fmt )
{
	ID3D11DeviceContext *pCtx = D3D11Context();
	if ( !pCtx || !EnsureUniversalShaders() )
		return false;
	if ( s_bOffscreenRT )
		return false;

	const SnapshotDx11_t *pSnap = CurrentSnapshot();

	// While texture RTs are suppressed (dx11_rt_textures 0), no RT texture can
	// hold valid content — any material sampling one (water $reflecttexture,
	// post-chain _rt_FullFrameFB/_rt_SmallFB overlays) would paint garbage
	// over the frame. Skip those draws entirely until M6/M7 port the sources.
	// Exception: the eyes glint (s2 = _rt_eyeglint for close-up eyes) — the
	// bind loop substitutes BLACK there (glint is additive; black = no glint,
	// the same look as the distance-LOD fallback), so eyes don't vanish.
	if ( pSnap && !dx11_rt_textures.GetBool() )
	{
		for ( int i = 0; i < DX11_MAX_SAMPLERS; ++i )
		{
			// Slots that substitute a fallback instead of skipping the draw:
			// eyes glint (s2, additive) and spritecard frame depth (s2 —
			// depth blend is masked off via g_SpriteControl while RTs are
			// gated, so the bound RT is never actually sampled).
			if ( ( s_bMaterialEyes || s_bMaterialSpriteCard ) && i == 2 )
				continue;
			// Cheap water (snapshot has s6 = normalize cube): s2 is the
			// refract-alpha RT — flag-masked off while RTs are gated, white
			// substituted in the bind loop. The EXPENSIVE pass stays skipped
			// (its s0/s2 RT content is the whole draw), leaving cubemap-only
			// water as the dx11_rt_textures 0 fallback look.
			if ( s_bMaterialWater && i == 2 && pSnap->m_Key.m_bSamplerEnabled[6] )
				continue;
			if ( pSnap->m_Key.m_bSamplerEnabled[i] && TextureDx11_IsRenderTarget( s_BoundTextures[i] ) )
				return false;
		}
	}

	// Permutation pick: LIGHTMAP when the material enabled sampler 1 and the
	// mesh carries lightmap coords; SKINNED only when the mesh actually has
	// bone weights (studio hw-skin format). "Has a normal" is NOT a model
	// signal — plenty of vgui/HUD/dynamic formats carry normals, and routing
	// them through the bone path corrupted every UI element (bone0 = transposed
	// 2D transform, ambient cube = black). Rigid models use the plain MVP path
	// (correct transform, full-bright) until per-family shaders land.
	VertexDesc_t fmtDesc;
	CVertexBufferBase::ComputeVertexDescription( NULL, fmt, fmtDesc );
	int nPerm = 0;
	if ( s_bMaterialDepthWrite )
		nPerm = 35;	// Flashlight shadow caster (DepthWrite procedural
					// materials): position-only depth fill into the shadow
					// map. Beats the flashlight pick — the caster pass is
					// scoped by its own view, never by flashlight mode, but
					// the material is unmistakable either way.
	else if ( ShaderUtil()->InFlashlightMode() )
		nPerm = 34;	// Flashlight-pass scope (SetFlashlightMode around the
					// CShadowMgr world re-renders + studiorender DrawShadows):
					// EVERY draw in scope is a flashlight pass — dx9 routes
					// the lightmapped/eyes/teeth families to
					// DrawFlashlight_dx90 and vertexlitgeneric/unlitgeneric
					// to their INLINE flashlight block; the PerDraw flag
					// below picks the variant. Beats every family pick.
	else if ( s_bMaterialSpriteCard )
		nPerm = 14;	// SpriteCard particles: the VS does the billboard corner
					// expansion — every other perm leaves the per-corner verts
					// coincident (invisible quads). Spline cards route here too
					// for now (drawn as point-expanded sprites, not splines).
	else if ( s_bMaterialPyro )
		nPerm = s_bMaterialVertexLit ? 13 : 12;	// pyro_vision: world vs
					// VERTEX_LIT model (rigid formats feed fallback weights).
					// Must beat every heuristic — its c12 holds TIME and its
					// s2 a colorbar LUT; generic paths painted the pyroland
					// world solid green / single-color.
	else if ( s_bMaterialWater && fmtDesc.m_VertexSize_TangentS && fmtDesc.m_VertexSize_TangentT
		&& fmtDesc.m_VertexSize_Normal )
		nPerm = ( pSnap && pSnap->m_Key.m_bSamplerEnabled[6] ) ? 16 : 15;
					// Water: one material, two passes. Only the cheap pass
					// enables s6 (dx9 normalize cube) — that's the per-pass
					// signature. Expensive = reflect/refract RTs (15), cheap =
					// envmap cube reflection (16). Brush water always carries
					// the full pos/normal/tangent format (water.cpp:213);
					// water materials on meshes without it fall through.
	else if ( s_bMaterialCable && fmtDesc.m_VertexSize_TexCoord[1] && fmtDesc.m_VertexSize_Color )
		nPerm = 7;	// Cable family: s0 = normal map, s1 = base — the inverse
					// slot use of every other family, so it must win over the
					// lightmap heuristic below (which drew the wires' normal
					// map as albedo = flat pink).
	else if ( s_bMaterialWindowImposter )
		nPerm = 9;	// areaportal window glass: position-only format, cube at
					// s0 — any other perm samples the cube as a Texture2D
					// (white fallback) or paints it black.
	else if ( s_bMaterialEyes && ( ( s_nNumBones >= 1 && fmtDesc.m_VertexSize_BoneWeight ) || s_bDrawingStaticMesh ) )
		nPerm = 8;	// Eyes: must beat the vertex-lit picks below (the format
					// has a normal, but the dx9 eyes VS ignores it — the
					// normal is the eyeball-sphere direction). The
					// s_bDrawingStaticMesh arm covers HL2-era DYNAMIC eyeball
					// draws (R_StudioDrawEyeball: world-space verts, zero
					// bones pushed, boneless material format — the layout's
					// zero-weight fallback lands on bone 0 = the identity
					// MODEL stack top). Without it those fell to MODE2 =
					// lit sclera with NO IRIS: white eyes.
	else if ( s_bMaterialEyeRefract && s_nNumBones >= 1 && fmtDesc.m_VertexSize_BoneWeight )
		nPerm = 10;	// EyeRefract: the TF2 player eyeball. Same routing rules
					// as Eyes — beat the vertex-lit picks (s0 is the cornea
					// NORMAL map; MODE3 painted it as albedo = green eyes).
	else if ( s_bMaterialRefract )
		nPerm = 19;	// Refract family: screen-space warp (underwater/uber
					// overlays, teleporter fx). Name-routed like cable/eyes —
					// its s2 is the FB copy and s3 the warp normal; any other
					// perm draws the normal map as albedo (the user-reported
					// opaque light-blue underwater screen).
	else if ( s_bMaterialUnlitTwoTex )
		nPerm = 20;	// UnlitTwoTexture: CP holograms — base x scrolling
					// $texture2 x modulation. Generic perms drew only the
					// base additively (black logo backing, no scan lines).
	else if ( s_bMaterialModulate )
		nPerm = 21;	// Modulate: framebuffer mod2x (the hologram's "dark"
					// pass). The generic path skipped the gray-lerp by
					// $alpha, so the full-strength multiply darkened the
					// bright logo underneath.
	else if ( s_bMaterialSSDownsample )
		nPerm = 22;	// LDR bloom downsample (clip-space quad + bright pass)
	else if ( s_bMaterialSSBlur )
		nPerm = 23;	// LDR bloom gaussian (BlurFilterX/Y share the shader)
	else if ( s_bMaterialSSAdd )
		nPerm = 24;	// bloomadd combine (additive fullscreen quad)
	else if ( s_bMaterialEnginePost )
		nPerm = 25;	// engine_post: bloom + color-correction combine
	else if ( s_bMaterialColorProjection )
		nPerm = 26;	// color_projection: mat_color_projection colorblind filter
	else if ( s_bMaterialLumCompare )
		nPerm = 27;	// dev/lumcompare: HDR autoexposure histogram marking
	else if ( s_bMaterialShadowBuild )
		nPerm = 28;	// ShadowBuild: caster model -> _rt_Shadows atlas
	else if ( s_bMaterialShadowProj )
		nPerm = 29;	// Shadow: atlas projection onto world geometry
	else if ( s_bMaterialShadowModel )
		nPerm = 30;	// ShadowModel: atlas projection onto model geometry
	else if ( s_bMaterialIntroEffect )
		nPerm = 31;	// IntroScreenSpaceEffect: HL2 G-Man intro blends
	else if ( s_bMaterialMotionBlur )
		nPerm = 32;	// MotionBlur: DoImageSpaceMotionBlur fullscreen pass
	else if ( s_bMaterialMonitorScreen )
		nPerm = 33;	// MonitorScreen: func_monitor screens (_rt_Camera feed).
					// Name-routed before the lit heuristics — monitor brush
					// faces carry normals but must not take the ambient cube.
	else if ( ( s_bMaterialCloakPass || s_bMaterialSheenPass ) && pSnap &&
		pSnap->m_Key.m_bBlend && pSnap->m_Key.m_bDepthWrite &&
		fmtDesc.m_VertexSize_Normal )
		nPerm = ( pSnap->m_Key.m_bSamplerEnabled[2] && pSnap->m_Key.m_bSamplerEnabled[3] )
			? 18 : 17;
				// vertexlitgeneric EXTRA passes (spy cloak / killstreak
				// sheen): the helpers are the ONLY vlg passes snapshotted
				// with blend ON + depth writes ON (EnableAlphaBlending +
				// explicit EnableDepthWrites(true)); the base pass keeps
				// drawing through the picks below. Sheen = s2 (cube) + s3
				// (mask) enabled; cloak has neither. Must beat the generic
				// model picks or the cloak pass draws as a lit model.
	else if ( s_bMaterialTeeth && s_nNumBones >= 1 && fmtDesc.m_VertexSize_BoneWeight
		&& fmtDesc.m_VertexSize_Normal )
		nPerm = 11;	// Teeth: skinned vertexlit dimmed by $illumfactor x
					// dot(N, $forward) (VS c48). Must beat the generic
					// skinned pick so the factor applies.
	else if ( pSnap && pSnap->m_Key.m_bSamplerEnabled[1] && fmtDesc.m_VertexSize_TexCoord[1] )
		nPerm = 1;
	else if ( s_bMaterialVertexLit && s_nNumBones >= 1 && fmtDesc.m_VertexSize_BoneWeight && fmtDesc.m_VertexSize_Normal )
		nPerm = 3;	// dx9 parity: SKINNING combo = numBones > 0 (vertexlitgeneric
					// _dx9_helper.cpp:1228) — even 1-influence strips transform by
					// PER-VERTEX bone indices (cModel[idx], common_vs_fxc.h:638).
					// Routing them through bone 0 glues the strip to whatever bone
					// a previous strip's state change left in slot 0 (the minigun
					// spun whole around its barrel bone).
	else if ( s_bMaterialVertexLit && ( s_nNumBones >= 1 || s_bDrawingStaticMesh ) && fmtDesc.m_VertexSize_Normal
		&& !fmtDesc.m_VertexSize_TexCoord[1] )
		nPerm = 2;	// rigid studio strip / static prop: bone 0 + lighting.
					// texcoord1 = lightmap coords = WORLD geometry (brush
					// models/displacements on non-lightmap passes) — those
					// stay on the unlit path, not the model path.
					// Both lit perms additionally require the material's
					// $flags2 VERTEX_LIT bit: unlit materials must not pick
					// up the ambient cube (a zeroed cube painted the 2fort
					// sun — an unlit 3D-skybox prop — solid black).
	else if ( s_nNumBones >= 1 && fmtDesc.m_VertexSize_BoneWeight && fmtDesc.m_VertexSize_Normal )
		nPerm = 3;	// unlit but skinned: keep per-vertex bone transforms; the
					// PerModel path lights it (cube zero = dx9's unlit look
					// minus nothing — vColor stays the lighting sum)…
					// NOTE: dx9 unlit ignores lighting entirely; MODE3 adds
					// it. Acceptable until unlit-skinned shows up visually.
	if ( nPerm == 2 && s_pStaticColorVB )
		nPerm = 4;	// baked static-prop vertex lighting (color mesh bound)
	if ( s_bMaterialPhong && fmtDesc.m_VertexSize_UserData && ( nPerm >= 2 && nPerm <= 4 )
		&& ( s_nPSMirrorValid & ( 1u << 11 ) ) )
	{
		// $phong routes to the per-pixel skin path (dx9: vertexlitgeneric
		// hands phong materials to the skin shader family). Requires the
		// studio tangent stream (USERDATA) for the TBN, AND the pass must
		// actually have run the skin dynamic block — c11
		// (PSREG_EYEPOS_SPEC_EXPONENT) written this pass is the signal.
		// Without it (sheen/extra passes on phong materials), a stale-zero
		// exponent made pow(LdotR, 0) paint uniform shine + NaN black.
		// Baked color meshes drop their term here — the dx9 skin family has
		// no static-light combo either (phong props are lit dynamically).
		nPerm = ( nPerm == 3 ) ? 6 : 5;
	}

	bool bFlexDraw = s_pFlexVB != NULL;
	ID3D11InputLayout *pLayout = GetInputLayout( fmt, nPerm, bFlexDraw );
	if ( !pLayout )
		return false;

	pCtx->IASetInputLayout( pLayout );
	pCtx->VSSetShader( s_pUniversalVS[nPerm], NULL, 0 );
	pCtx->GSSetShader( NULL, NULL, 0 );
	pCtx->PSSetShader( s_pUniversalPS[nPerm], NULL, 0 );

	UINT nAttrStride = 32, nAttrOffset = 0;
	pCtx->IASetVertexBuffers( 1, 1, &s_pDefaultAttrVB, &nAttrStride, &nAttrOffset );

	if ( nPerm == 4 || nPerm == 13 )
	{
		// Baked prop lighting on slot 2. For pyro models (13) without a color
		// mesh this binds NULL = unbound, whose reads are zeros by D3D11 spec
		// — and it can't leak a stale perm-4 bind into the wrong prop.
		pCtx->IASetVertexBuffers( 2, 1, &s_pStaticColorVB, &s_nStaticColorStride, &s_nStaticColorOffset );
	}
	if ( bFlexDraw )
	{
		// Facial flex deltas (dx9 stream 2): byte offset rides the bind
		pCtx->IASetVertexBuffers( 3, 1, &s_pFlexVB, &s_nFlexStride, &s_nFlexOffset );
	}

	if ( s_bMatricesDirty )
	{
		EnsureMatrixInit();

		// Half-pixel fold: shift clip space by (-1/W, +1/H) so vertices tuned
		// for the DX9 raster convention land on the same pixels (see Plan.md).
		int nW = MAX( 1, s_Viewport.m_nWidth );
		int nH = MAX( 1, s_Viewport.m_nHeight );
		Matrix44Dx11_t halfPixel;
		Mat44Identity( halfPixel );
		halfPixel[3][0] = -1.0f / nW;
		halfPixel[3][1] = 1.0f / nH;

		// View*Proj (model path: bones supply the model transform)
		Matrix44Dx11_t vp;
		Mat44Multiply( s_MatrixStacks[MATERIAL_VIEW][s_nStackTop[MATERIAL_VIEW]],
			s_MatrixStacks[MATERIAL_PROJECTION][s_nStackTop[MATERIAL_PROJECTION]], vp );
		Mat44Multiply( vp, halfPixel, vp );
		memcpy( s_flViewProj, vp, sizeof( s_flViewProj ) );
		// World-space camera pos for b1 (dx9 cEyePos — spritecard size fades)
		StateDx11_GetWorldSpaceCameraPosition( s_flEyePosPM );
		s_bModelStateDirty = true;

		Matrix44Dx11_t mvp;
		Mat44Multiply( s_MatrixStacks[MATERIAL_MODEL][s_nStackTop[MATERIAL_MODEL]], vp, mvp );

		D3D11_MAPPED_SUBRESOURCE map;
		if ( SUCCEEDED( pCtx->Map( s_pPerFrameCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &map ) ) )
		{
			memcpy( map.pData, mvp, sizeof( mvp ) );
			pCtx->Unmap( s_pPerFrameCB, 0 );
		}
		s_bMatricesDirty = false;
	}
	pCtx->VSSetConstantBuffers( 0, 1, &s_pPerFrameCB );

	// Bone 0 always commits from the MODEL stack top (transposed — stack
	// matrices are v*M row-vector, bone rows are out.x = dot(row, v4)).
	// LoadBoneMatrix(0) writes the stack too, so both sources agree. b1 now
	// binds for EVERY perm — the world perms read bone 0 as the model
	// transform for the water-fog dest-alpha worldZ. memcmp keeps unchanged
	// draws (the whole static world) from re-uploading b1 per draw.
	{
		const Matrix44Dx11_t &m = s_MatrixStacks[MATERIAL_MODEL][s_nStackTop[MATERIAL_MODEL]];
		float flRows[12];
		for ( int nRow = 0; nRow < 3; ++nRow )
		{
			flRows[nRow * 4 + 0] = m[0][nRow];
			flRows[nRow * 4 + 1] = m[1][nRow];
			flRows[nRow * 4 + 2] = m[2][nRow];
			flRows[nRow * 4 + 3] = m[3][nRow];
		}
		if ( memcmp( flRows, s_flBoneRows, sizeof( flRows ) ) != 0 )
		{
			memcpy( s_flBoneRows, flRows, sizeof( flRows ) );
			s_bModelStateDirty = true;
		}
	}
	if ( s_bModelStateDirty )
	{
		D3D11_MAPPED_SUBRESOURCE map;
		if ( SUCCEEDED( pCtx->Map( s_pPerModelCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &map ) ) )
		{
			float *pDest = (float *)map.pData;
			memcpy( pDest, s_flViewProj, sizeof( s_flViewProj ) );
			memcpy( pDest + 16, s_flAmbientCube, sizeof( s_flAmbientCube ) );
			memcpy( pDest + 16 + 24, s_flLights, sizeof( s_flLights ) );
			memcpy( pDest + 16 + 24 + DX11_MAX_LIGHTS * 20, s_flBoneRows, sizeof( s_flBoneRows ) );
			memcpy( pDest + 16 + 24 + DX11_MAX_LIGHTS * 20 + DX11_MAX_BONES * 12, s_flEyePosPM, sizeof( s_flEyePosPM ) );
			pCtx->Unmap( s_pPerModelCB, 0 );
		}
		s_bModelStateDirty = false;
	}
	pCtx->VSSetConstantBuffers( 1, 1, &s_pPerModelCB );

	static int s_nLastPerDrawPerm = -1;
	static bool s_bLastC12Valid = false;
	// ps c12 = MODULATION only for the lightmappedgeneric helper families
	// (pyro_vision keeps TIME there; vertexlitgeneric its shader controls) —
	// per-pass validity AND family sniff, refreshed when either flips.
	bool bC12Valid = s_bMaterialLMModC12 && ( s_nPSMirrorValid & ( 1u << 12 ) ) != 0;
	if ( s_bSnapshotDirty || s_bModulationDirty || nPerm != s_nLastPerDrawPerm ||
		bC12Valid != s_bLastC12Valid )
	{
		float flPerDraw[40] = { 0, 0, 0.5f, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0 };	// alphatest + modulation + tint + phong + misc + eye + sprite + fog x2 + envmap control
		if ( pSnap )
		{
			flPerDraw[0] = pSnap->m_Key.m_bAlphaTest ? 1.0f : 0.0f;
			flPerDraw[1] = (float)pSnap->m_Key.m_nAlphaFunc;
			flPerDraw[2] = pSnap->m_Key.m_flAlphaRef;
			// Pair the dx9 VERTEXCOLOR gamma-decode with the sRGB write that
			// re-encodes it (see universal.hlsl MainVs).
			flPerDraw[3] = pSnap->m_Key.m_bSRGBWrite ? 1.0f : 0.0f;
		}
		if ( nPerm == 1 )
		{
			// dx9 folds tint * lightmap scale into ps c12; tint is 1 for the
			// bring-up shader, the scale rides in the modulation color.
			float flScale = ShaderApiDx11_LightMapScaleFactor();
			flPerDraw[4] = flPerDraw[5] = flPerDraw[6] = flScale;
		}
		else if ( s_bModulationValid )
		{
			// dx9 PSREG_DIFFUSE_MODULATION ($color/$alpha incl. HUD team tints)
			memcpy( flPerDraw + 4, s_flModulation, sizeof( s_flModulation ) );
		}
		flPerDraw[8] = s_flTintControl[0];
		flPerDraw[9] = s_flTintControl[1];
		// envmap on = the dx9 shadow block enabled the family's envmap sampler
		// ($envmap set): s8 for vertexlitgeneric/skin, s2 for lightmappedgeneric
		flPerDraw[10] = ( pSnap && pSnap->m_Key.m_bSamplerEnabled[( nPerm == 1 ) ? 2 : 8] ) ? 1.0f : 0.0f;
		flPerDraw[11] = s_flTintControl[2];	// $selfillum
		memcpy( flPerDraw + 12, s_flPhongFlags, sizeof( s_flPhongFlags ) );
		flPerDraw[16] = s_flSelfIllumFresnel;
		flPerDraw[17] = s_flVSTransformReg;	// base texcoord transform VS reg or -1
		flPerDraw[18] = s_flDetailBlendMode;	// TCOMBINE_* mode or -1
		flPerDraw[19] = bC12Valid ? 1.0f : 0.0f;	// lightmapped c12 modulation valid
		flPerDraw[20] = s_flEyeControl[0];	// $raytracesphere
		flPerDraw[21] = s_flEyeControl[1];	// $spheretexkillcombo
		flPerDraw[22] = s_flPyroControl[0];	// pyro_vision $effect
		flPerDraw[23] = s_flPyroControl[1];	// pyro_vision flag bits
		// SpriteCard: depth blend masked off while RT textures are gated
		// (the depth texture would be skip-suppressed garbage otherwise)
		float flSCFlags = s_flSpriteControl[0];
		if ( !dx11_rt_textures.GetBool() )
			flSCFlags = (float)( (int)flSCFlags & ~256 );
		flPerDraw[24] = flSCFlags;
		flPerDraw[25] = s_flSpriteControl[1];	// $orientation
		flPerDraw[26] = s_flSpriteControl[2];	// $sequence_blend_mode
		// Water pass flags (g_SpriteControl.w): material bits from the sniff
		// plus the per-pass reflect/refract bits read off the snapshot's
		// sampler enables (expensive: s2 reflect RT / s0 refract RT; cheap:
		// s2 = refract-alpha border feather, masked off while RT textures are
		// gated — the RT alpha would be garbage).
		float flWaterFlags = s_flWaterControl;
		if ( pSnap && ( nPerm == 15 || nPerm == 16 ) )
		{
			int nWF = (int)flWaterFlags;
			if ( nPerm == 15 )
			{
				if ( pSnap->m_Key.m_bSamplerEnabled[2] )
					nWF |= 1;	// REFLECT
				if ( pSnap->m_Key.m_bSamplerEnabled[0] )
					nWF |= 2;	// REFRACT
			}
			else if ( pSnap->m_Key.m_bSamplerEnabled[2] && dx11_rt_textures.GetBool() )
			{
				nWF |= 2;	// cheap REFRACTALPHA
			}
			flWaterFlags = (float)nWF;
		}
		else if ( pSnap && nPerm == 19 )
		{
			// Refract flags share the slot (exclusive perms): sniffed
			// blur/fadeout bits + the per-pass cube/tint-texture samplers
			int nRF = (int)s_flRefractControl;
			if ( pSnap->m_Key.m_bSamplerEnabled[4] )
				nRF |= 4;	// CUBEMAP
			if ( pSnap->m_Key.m_bSamplerEnabled[5] )
				nRF |= 8;	// REFRACTTINTTEXTURE
			flWaterFlags = (float)nRF;
		}
		else if ( nPerm == 26 )
		{
			// color_projection dynamic combos as flag bits (1 blindMK,
			// 2 monochrome, 4 anomylize). The dx9 shader picks its DYNAMIC
			// combo from mat_color_projection (color_projection.cpp:265) with
			// out-of-range values clamping to row 0 — mirror that here.
			static ConVarRef s_CvarColorProjection( "mat_color_projection", true );
			int nIndex = ( s_CvarColorProjection.IsValid() ? s_CvarColorProjection.GetInt() : 0 ) - 1;
			if ( nIndex < 0 || nIndex >= 8 )
				nIndex = 0;
			int nCP = ( nIndex == 3 || nIndex == 7 ) ? 2 : 1;	// achromatopsia rows : confusion-line rows
			if ( nIndex >= 4 )
				nCP |= 4;	// anomalous (partial) variants blend back toward the original
			flWaterFlags = (float)nCP;
		}
		else if ( nPerm == 31 )
		{
			// IntroScreenSpaceEffect: $mode (the dx9 MODE dynamic combo)
			flWaterFlags = s_flIntroMode;
		}
		else if ( nPerm == 32 )
		{
			// MotionBlur QUALITY combo, dx9-style (motion_blur_dx9.cpp): from
			// the source-texture height (= the FB copy = backbuffer) — >=1080
			// 3, >=720 2, else 1 — forced 0 when the proxy's MOTIONBLURINTERNAL
			// (mirrored ps c1; the dx9 DYNAMIC block writes it every pass) is
			// all zeros: no blur this frame.
			int nMBW = 0, nMBH = 0;
			g_pShaderDeviceDx11->GetBackBufferDimensions( nMBW, nMBH );
			int nMBQuality = ( nMBH >= 1080 ) ? 3 : ( ( nMBH >= 720 ) ? 2 : 1 );
			const float *pMBInternal = s_flPSMirror + 1 * 4;
			if ( fabsf( pMBInternal[0] ) + fabsf( pMBInternal[1] ) +
				 fabsf( pMBInternal[2] ) + fabsf( pMBInternal[3] ) == 0.0f )
				nMBQuality = 0;
			flWaterFlags = (float)nMBQuality;
		}
		else if ( nPerm == 34 || nPerm == 35 )
		{
			// Flashlight family variant: bit 1 = the vertexlitgeneric/
			// unlitgeneric INLINE flashlight map (base s0, cookie s7,
			// PS-side projection via mirror c24-27, atten/pos c22/c23).
			// Phong materials route through the dx9 SKIN flashlight block
			// (different map again) — left on the dx90 variant until the
			// skin port lands. Bit 2 = WORLD/BRUSH geometry (lightmap
			// coords in texcoord1 — the same signature the model perms use):
			// their BASE pass transforms through the folded MVP, so the
			// flashlight pass must too or it z-fights (the trainstation
			// tracktrain/turnstile shimmered). Boneless STUDIO formats
			// (static props) must NOT take this path — their base pass is
			// the bone-0 path, and giving them MVP z-fought every prop.
			// The DEPTHWRITE caster (35) shares bit 2: world depth fills
			// through the folded MVP, studio casters through the bone path.
			int nFL = ( s_bMaterialVLGName && !s_bMaterialPhong ) ? 1 : 0;
			if ( !fmtDesc.m_VertexSize_BoneWeight && fmtDesc.m_VertexSize_TexCoord[1] )
				nFL |= 2;
			// Bit 4 = the EYES family: its dx9 flashlight is a THIRD shader
			// (eyes_flashlight_vs20/ps20) — iris composited x0.5 over the
			// whites, per-vertex atten with unsaturated NdotL, iris uv via
			// CONST_8/9. The generic dx90 math drew the white sclera only
			// (citizen eyes blew out fully white under the beam).
			if ( s_bMaterialEyes )
				nFL |= 4;
			// Bit 8 = depth-mapped shadows: SetFlashlightStateEx delivered a
			// depth texture with m_bEnableShadows — the dx9 family blocks
			// bound it (dx90 s7, VLG s8, eyes s4) and the PS compares the
			// spot projection's z against it.
			if ( nPerm == 34 && s_bFlashlightShadows )
				nFL |= 8;
			flWaterFlags = (float)nFL;
		}
		flPerDraw[27] = flWaterFlags;
		// Scene fog → per-pass fog bits + color (g_FogControl/g_SceneFogColor).
		// Bit 1 = water-fog dest-alpha write (dx9 WRITEWATERFOGTODESTALPHA:
		// height fog + OPAQUE only — a blending draw's source alpha feeds the
		// blend factor and must stay material alpha). Bit 4 = height-fog rgb
		// lerp (BlendPixelFog — the underwater murk). Bit 2 = range fog with
		// the dx9 fixed-function vertex-fog math (UpdateVertexShaderFogParams:
		// factor = max(1 - maxdensity, fogEndOverRange - projZ * ooRange),
		// 1 = no fog). The pass's ShaderFogMode_t picks the color; GREY and
		// FOGCOLOR gamma-decode on sRGB-write passes exactly like dx9
		// ApplyFogMode/ComputeGammaCorrectedFogColor (BLACK/WHITE are pow()
		// fixed points and skip it there too).
		int nFogBits = 0;
		float flFogY = 0.0f;
		float flFogColor[3] = { 0, 0, 0 };
		bool bShadowFogOn = pSnap && pSnap->m_Key.m_nFogMode != 0;	// != SHADER_FOGMODE_DISABLED
		if ( s_nSceneFogMode == 2 )			// MATERIAL_FOG_LINEAR_BELOW_FOG_Z
		{
			if ( pSnap && !pSnap->m_Key.m_bBlend )
				nFogBits |= 1;
			if ( bShadowFogOn )
				nFogBits |= 4;
			flFogY = s_flFogWaterZ;
		}
		else if ( s_nSceneFogMode == 1 && bShadowFogOn )	// MATERIAL_FOG_LINEAR
		{
			nFogBits |= 2;
			flFogY = s_flFogMaxDensityFloor;
		}
		if ( ( nFogBits & ( 2 | 4 ) ) != 0 )
		{
			switch ( pSnap->m_Key.m_nFogMode )
			{
			case 2:		// SHADER_FOGMODE_BLACK (additive passes)
				break;
			case 1:		// SHADER_FOGMODE_OO_OVERBRIGHT
			case 3:		// SHADER_FOGMODE_GREY (mod2x passes)
				flFogColor[0] = flFogColor[1] = flFogColor[2] = 0.5f;
				break;
			case 5:		// SHADER_FOGMODE_WHITE (multiplicative passes)
				flFogColor[0] = flFogColor[1] = flFogColor[2] = 1.0f;
				break;
			default:	// SHADER_FOGMODE_FOGCOLOR
				memcpy( flFogColor, s_flSceneFogColor, sizeof( flFogColor ) );
				break;
			}
			if ( pSnap->m_Key.m_nFogMode != 2 && pSnap->m_Key.m_nFogMode != 5 &&
				 !pSnap->m_Key.m_bDisableFogGammaCorrection && pSnap->m_Key.m_bSRGBWrite )
			{
				for ( int f = 0; f < 3; ++f )
					flFogColor[f] = powf( flFogColor[f], 2.2f );
			}
			// Integer HDR: the shaders tonemap inline (× cLightScale.x before
			// fog blends), so the fog color scales to match — dx9
			// UpdatePixelFogColorConstant, gated exactly like its
			// bShouldGammaCorrect (BLACK and WHITE passes skip).
			if ( s_flToneMapScale != 1.0f && pSnap->m_Key.m_nFogMode != 2 &&
				 pSnap->m_Key.m_nFogMode != 5 && !pSnap->m_Key.m_bDisableFogGammaCorrection )
			{
				for ( int f = 0; f < 3; ++f )
					flFogColor[f] *= s_flToneMapScale;
			}
		}
		flPerDraw[28] = (float)nFogBits;
		flPerDraw[29] = flFogY;
		flPerDraw[30] = s_flFogOORange;
		flPerDraw[31] = s_flEyePosPM[2];
		flPerDraw[32] = flFogColor[0];
		flPerDraw[33] = flFogColor[1];
		flPerDraw[34] = flFogColor[2];
		flPerDraw[35] = s_flFogEndOverRange;
		// lightmappedgeneric envmap path (g_EnvmapControl): x = $envmapcontrast,
		// y = $envmapsaturation, z = $fresnelreflection, w = mask mode bits 0-1
		// (0 none / 1 $envmapmask s5 / 2 $basealphaenvmapmask = INVERTED base
		// alpha / 3 $normalmapalphaenvmapmask = bump alpha) + bit 4 = $bumpmap
		// bound (snapshot s4, helper:484): perturb the reflection per pixel.
		flPerDraw[36] = s_flEnvmapControl[0];
		flPerDraw[37] = s_flEnvmapControl[1];
		flPerDraw[38] = s_flEnvmapControl[2];
		flPerDraw[39] = s_flEnvmapControl[3] +
			( ( nPerm == 1 && pSnap && pSnap->m_Key.m_bSamplerEnabled[4] ) ? 4.0f : 0.0f );
		D3D11_MAPPED_SUBRESOURCE map;
		if ( SUCCEEDED( pCtx->Map( s_pPerDrawCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &map ) ) )
		{
			memcpy( map.pData, flPerDraw, sizeof( flPerDraw ) );
			pCtx->Unmap( s_pPerDrawCB, 0 );
		}
		s_bSnapshotDirty = false;
		s_bModulationDirty = false;
		s_nLastPerDrawPerm = nPerm;
		s_bLastC12Valid = bC12Valid;
	}
	pCtx->PSSetConstantBuffers( 3, 1, &s_pPerDrawCB );
	pCtx->VSSetConstantBuffers( 3, 1, &s_pPerDrawCB );	// MainVs reads the decode flag

	if ( s_bPSMirrorDirty )
	{
		// Stale registers upload as zero: a pass must never read another
		// family's constants (same trap as the c1 latch).
		float flStage[DX11_PS_MIRROR_CONSTANTS * 4];
		for ( int i = 0; i < DX11_PS_MIRROR_CONSTANTS; ++i )
		{
			if ( s_nPSMirrorValid & ( 1u << i ) )
				memcpy( flStage + i * 4, s_flPSMirror + i * 4, 4 * sizeof( float ) );
			else
				memset( flStage + i * 4, 0, 4 * sizeof( float ) );
		}
		D3D11_MAPPED_SUBRESOURCE map;
		if ( SUCCEEDED( pCtx->Map( s_pPSMirrorCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &map ) ) )
		{
			memcpy( map.pData, flStage, sizeof( flStage ) );
			pCtx->Unmap( s_pPSMirrorCB, 0 );
		}
		s_bPSMirrorDirty = false;
	}
	pCtx->PSSetConstantBuffers( 2, 1, &s_pPSMirrorCB );

	if ( s_bVSMirrorDirty )
	{
		float flStage[DX11_VS_MIRROR_CONSTANTS * 4];
		for ( int i = 0; i < DX11_VS_MIRROR_CONSTANTS; ++i )
		{
			if ( s_nVSMirrorValid & ( 1ull << i ) )
				memcpy( flStage + i * 4, s_flVSMirror + i * 4, 4 * sizeof( float ) );
			else
				memset( flStage + i * 4, 0, 4 * sizeof( float ) );
		}
		D3D11_MAPPED_SUBRESOURCE map;
		if ( SUCCEEDED( pCtx->Map( s_pVSMirrorCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &map ) ) )
		{
			memcpy( map.pData, flStage, sizeof( flStage ) );
			pCtx->Unmap( s_pVSMirrorCB, 0 );
		}
		s_bVSMirrorDirty = false;
	}
	pCtx->VSSetConstantBuffers( 2, 1, &s_pVSMirrorCB );

	bool bSRGBWrite = false;
	if ( pSnap )
	{
		pCtx->OMSetBlendState( pSnap->m_pBlend, NULL, 0xFFFFFFFF );
		if ( s_DynStencil.m_bEnable )
		{
			// Dynamic stencil overlay (histogram mark/count draws): compose
			// the snapshot's depth fields with the dynamic stencil block.
			ID3D11DepthStencilState *pComposed = GetComposedDepthStencil(
				pSnap->m_Key.m_bDepthTest, pSnap->m_Key.m_bDepthWrite, pSnap->m_Key.m_nDepthFunc );
			pCtx->OMSetDepthStencilState( pComposed ? pComposed : pSnap->m_pDepth, s_DynStencil.m_nRef );
		}
		else
		{
			pCtx->OMSetDepthStencilState( pSnap->m_pDepth, 0 );
		}
		bSRGBWrite = pSnap->m_Key.m_bSRGBWrite;
	}
	else
	{
		pCtx->OMSetBlendState( NULL, NULL, 0xFFFFFFFF );
		pCtx->OMSetDepthStencilState( NULL, 0 );
	}

	int nCull = 0;
	if ( !pSnap || pSnap->m_Key.m_bCullEnable )
	{
		nCull = ( s_nDynamicCullMode == MATERIAL_CULLMODE_CW ) ? 2 : 1;
		if ( !pSnap )
			nCull = 0;
	}
	pCtx->RSSetState( GetRasterState( nCull, pSnap && pSnap->m_Key.m_bWireframe, s_bScissorEnabled,
		pSnap ? pSnap->m_Key.m_nPolyOffsetMode : 0 ) );
	if ( s_bScissorEnabled )
		pCtx->RSSetScissorRects( 1, &s_ScissorRect );

	ID3D11RenderTargetView *pRTV = NULL;
	ID3D11DepthStencilView *pDSV = NULL;
	GetCurrentTargets( bSRGBWrite, &pRTV, &pDSV );
	if ( !pRTV )
		return false;
	pCtx->OMSetRenderTargets( 1, &pRTV, pDSV );

	for ( int i = 0; i < DX11_MAX_SAMPLERS; ++i )
	{
		// Cube-typed slots: the phong perms declare s8 as TextureCube (envmap),
		// windowimposter s0, eyerefract s2 (reflection cube), lightmapped s2
		// ($envmap brush cubemaps) — the slots where a cube SRV may bind; their
		// fallback must be a cube too or the debug layer flags a view-dimension
		// mismatch on draw.
		bool bCubeSlot = ( ( nPerm == 5 || nPerm == 6 ) && i == 8 ) || ( nPerm == 9 && i == 0 )
			|| ( nPerm == 10 && i == 2 ) || ( nPerm == 16 && i == 0 )
			|| ( nPerm == 18 && i == 2 ) || ( nPerm == 19 && i == 4 )
			|| ( nPerm == 1 && i == 2 );
		ID3D11ShaderResourceView *pSRV;
		ID3D11SamplerState *pSamp;
		if ( pSnap && pSnap->m_Key.m_bSamplerEnabled[i] && TextureDx11_IsValid( s_BoundTextures[i] ) )
		{
			pSRV = TextureDx11_GetSRV( s_BoundTextures[i], pSnap->m_Key.m_bSamplerSRGB[i], bCubeSlot );
			pSamp = TextureDx11_GetSamplerState( s_BoundTextures[i] );
		}
		else
		{
			pSRV = TextureDx11_GetWhiteSRV();
			pSamp = TextureDx11_GetDefaultSampler();
		}
		if ( nPerm == 8 && i == 2 && !dx11_rt_textures.GetBool() &&
			TextureDx11_IsRenderTarget( s_BoundTextures[i] ) )
		{
			// _rt_eyeglint while RTs are suppressed: additive term → black
			pSRV = TextureDx11_GetBlackSRV();
		}
		if ( nPerm == 14 && i == 2 && !dx11_rt_textures.GetBool() &&
			TextureDx11_IsRenderTarget( s_BoundTextures[i] ) )
		{
			// spritecard frame depth while RTs are suppressed: depth blend is
			// flag-masked off, the slot just needs a legal SRV
			pSRV = TextureDx11_GetWhiteSRV();
		}
		if ( nPerm == 16 && i == 2 && !dx11_rt_textures.GetBool() &&
			TextureDx11_IsRenderTarget( s_BoundTextures[i] ) )
		{
			// cheap-water refract-alpha while RTs are suppressed: the border
			// feather is flag-masked off, the slot just needs a legal SRV
			pSRV = TextureDx11_GetWhiteSRV();
		}
		if ( nPerm == 25 && i >= 2 && i <= 5 &&
			!( pSnap && pSnap->m_Key.m_bSamplerEnabled[i] &&
			   TextureDx11_IsValid( s_BoundTextures[i] ) && TextureDx11_IsVolume( s_BoundTextures[i] ) ) )
		{
			// engine_post CC LUT slots declare Texture3D. dx9 binds only as
			// many LUT stages as there are active lookups and leaves the rest
			// STALE (flat-normal/random material textures from earlier draws)
			// — its NUM_LOOKUPS combo never samples them. We sample all four,
			// so anything that isn't a real volume (stale 2D bind or the white
			// fallback) must go to NULL: defined zero samples, cancelled
			// exactly by the zero lookup weight.
			pSRV = NULL;
		}
		if ( bCubeSlot && pSRV == TextureDx11_GetWhiteSRV() )
			pSRV = TextureDx11_GetWhiteCubeSRV();
		pCtx->PSSetShaderResources( i, 1, &pSRV );
		pCtx->PSSetSamplers( i, 1, &pSamp );
	}

	return true;
}


//-----------------------------------------------------------------------------
// Device lifecycle
//-----------------------------------------------------------------------------
void StateDx11_ReleaseDevice()
{
	for ( int i = 0; i < s_Snapshots.Count(); ++i )
	{
		if ( s_Snapshots[i].m_pBlend ) s_Snapshots[i].m_pBlend->Release();
		if ( s_Snapshots[i].m_pDepth ) s_Snapshots[i].m_pDepth->Release();
	}
	s_Snapshots.RemoveAll();
	s_nCurrentSnapshot = -1;

	for ( int c = 0; c < 3; ++c )
		for ( int w = 0; w < 2; ++w )
			for ( int s = 0; s < 2; ++s )
				for ( int d = 0; d < 3; ++d )
				{
					if ( s_RasterStates[c][w][s][d] ) { s_RasterStates[c][w][s][d]->Release(); s_RasterStates[c][w][s][d] = NULL; }
				}

	for ( int i = 0; i < s_Layouts.Count(); ++i )
	{
		if ( s_Layouts[i].m_pLayout ) s_Layouts[i].m_pLayout->Release();
	}
	s_Layouts.RemoveAll();

	for ( int i = 0; i < s_AuxDepths.Count(); ++i )
	{
		if ( s_AuxDepths[i].m_pDSV ) s_AuxDepths[i].m_pDSV->Release();
		if ( s_AuxDepths[i].m_pTexture ) s_AuxDepths[i].m_pTexture->Release();
	}
	s_AuxDepths.RemoveAll();
	for ( int i = 0; i < s_ComposedDepthStates.Count(); ++i )
	{
		if ( s_ComposedDepthStates[i].m_pState ) s_ComposedDepthStates[i].m_pState->Release();
	}
	s_ComposedDepthStates.RemoveAll();
	s_hRenderTexture = INVALID_SHADERAPI_TEXTURE_HANDLE;
	s_bOffscreenRT = false;

	for ( int i = 0; i < DX11_UNIVERSAL_PERMS; ++i )
	{
		if ( s_pUniversalVS[i] ) { s_pUniversalVS[i]->Release(); s_pUniversalVS[i] = NULL; }
		if ( s_pUniversalPS[i] ) { s_pUniversalPS[i]->Release(); s_pUniversalPS[i] = NULL; }
		s_UniversalVSBlob[i].Clear();
	}
	if ( s_pPerFrameCB ) { s_pPerFrameCB->Release(); s_pPerFrameCB = NULL; }
	if ( s_pPerDrawCB ) { s_pPerDrawCB->Release(); s_pPerDrawCB = NULL; }
	if ( s_pPerModelCB ) { s_pPerModelCB->Release(); s_pPerModelCB = NULL; }
	if ( s_pDefaultAttrVB ) { s_pDefaultAttrVB->Release(); s_pDefaultAttrVB = NULL; }
	s_bUniversalLoadAttempted = false;
	s_bModelStateDirty = true;
	s_bBonesExplicit = false;
}
