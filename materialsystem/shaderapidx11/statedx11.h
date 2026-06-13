//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX11 render state: snapshots (shadow state -> precreated state
// objects), matrix stacks, dynamic state, and the per-draw commit that binds
// the universal bring-up shaders (M3). See Plan.md.
//
//===========================================================================//

#ifndef STATEDX11_H
#define STATEDX11_H

#ifdef _WIN32
#pragma once
#endif

#include "shaderapi/ishadershadow.h"
#include "shaderapi/ishaderapi.h"
#include "materialsystem/imaterial.h"

// ---- shadow (snapshot building) ----
void StateDx11_ShadowSetDefault();
void StateDx11_ShadowDepthFunc( ShaderDepthFunc_t f );
void StateDx11_ShadowEnableDepthWrites( bool b );
void StateDx11_ShadowEnableDepthTest( bool b );
void StateDx11_ShadowEnableColorWrites( bool b );
void StateDx11_ShadowEnableAlphaWrites( bool b );
void StateDx11_ShadowEnableBlending( bool b );
void StateDx11_ShadowBlendFunc( ShaderBlendFactor_t s, ShaderBlendFactor_t d );
void StateDx11_ShadowEnableBlendingSeparateAlpha( bool b );
void StateDx11_ShadowBlendFuncSeparateAlpha( ShaderBlendFactor_t s, ShaderBlendFactor_t d );
void StateDx11_ShadowEnableAlphaTest( bool b );
void StateDx11_ShadowAlphaFunc( ShaderAlphaFunc_t f, float flRef );
void StateDx11_ShadowEnableCulling( bool b );
void StateDx11_ShadowPolyMode( bool bWireframe );
// PolygonOffsetMode_t: 0 disable, 1 decal, 2 shadow bias (DepthWrite caster)
void StateDx11_ShadowPolyOffset( int nMode );
// Facial flex deltas for the next draw (IA slot 3, dx9 stream 2); cleared
// after SetupForDraw by the mesh code.
void StateDx11_SetFlexMesh( struct ID3D11Buffer *pVB, unsigned int nStrideBytes, unsigned int nOffsetBytes );
void StateDx11_ShadowEnableSRGBWrite( bool b );
void StateDx11_ShadowEnableSRGBRead( int nSampler, bool b );
void StateDx11_ShadowEnableTexture( int nSampler, bool b );
void StateDx11_ShadowVertexFormat( VertexFormat_t fmt );

StateSnapshot_t StateDx11_TakeSnapshot();
void StateDx11_UseSnapshot( StateSnapshot_t id );
bool StateDx11_IsTranslucent( StateSnapshot_t id );
bool StateDx11_IsAlphaTested( StateSnapshot_t id );
bool StateDx11_IsDepthWriteEnabled( StateSnapshot_t id );
VertexFormat_t StateDx11_ComputeVertexFormat( int nCount, StateSnapshot_t *pIds );

// ---- dynamic state ----
void StateDx11_MatrixMode( MaterialMatrixMode_t mode );
void StateDx11_PushMatrix();
void StateDx11_PopMatrix();
void StateDx11_LoadMatrix( const float *pM );
void StateDx11_MultMatrix( const float *pM );
void StateDx11_MultMatrixLocal( const float *pM );
void StateDx11_GetMatrix( MaterialMatrixMode_t mode, float *pDst );
void StateDx11_LoadIdentity();
void StateDx11_Ortho( double l, double t, double r, double b, double zn, double zf );
void StateDx11_PerspectiveX( double fovx, double aspect, double zn, double zf );
void StateDx11_PerspectiveOffCenterX( double fovx, double aspect, double zn, double zf,
	double bottom, double top, double left, double right );
void StateDx11_Rotate( float flAngleDegrees, float x, float y, float z );
void StateDx11_Translate( float x, float y, float z );
void StateDx11_Scale( float x, float y, float z );
void StateDx11_SetViewports( int nCount, const ShaderViewport_t *pViewports );
int  StateDx11_GetViewports( ShaderViewport_t *pViewports, int nMax );
void StateDx11_SetScissorRect( int l, int t, int r, int b, bool bEnable );
void StateDx11_CullMode( MaterialCullMode_t mode );
void StateDx11_SetModulation( const float *pColor4 );	// dx9 ps c1 ($color tint)
void StateDx11_BindMaterialTint( IMaterial *pMaterial );	// $blendtintbybasealpha info
// dx9 PS float-constant mirror c0..c31 -> cbuffer b2 (per-pass validity);
// feeds the per-pixel shading permutations with the register layout the dx9
// family dynamic blocks already upload (shader_constant_register_map.h).
void StateDx11_SetPSConstants( int nFirst, const float *pValues, int nCount );
void StateDx11_SetVSConstants( int nFirst, const float *pValues, int nCount );	// vs c0..c63 mirror
// Occlusion queries (dx9 parity; the sun/lens glow pixel-visibility system)
ShaderAPIOcclusionQuery_t StateDx11_CreateOcclusionQuery();
void StateDx11_DestroyOcclusionQuery( ShaderAPIOcclusionQuery_t h );
void StateDx11_BeginOcclusionQuery( ShaderAPIOcclusionQuery_t h );
void StateDx11_EndOcclusionQuery( ShaderAPIOcclusionQuery_t h );
int  StateDx11_GetOcclusionQueryPixels( ShaderAPIOcclusionQuery_t h, bool bFlush );
// dx11_reload_shaders: recompile all universal perms from HLSL source;
// returns false (live set untouched) if any permutation fails.
bool StateDx11_ReloadUniversalShaders( const char *pHlslPath );
void StateDx11_SetPSAmbientCube( int nReg, bool bForceBlack );	// PSREG_AMBIENT_CUBE
void StateDx11_CommitPSLighting( int nReg );	// PSREG_LIGHT_INFO_ARRAY packing
void StateDx11_SetLightingOrigin( const float *pOrigin3 );
void StateDx11_GetWorldSpaceCameraPosition( float *pPos );	// 3 floats, dx9 parity
int  StateDx11_GetNumEnabledLights();		// dx9 LightState_t sources
bool StateDx11_IsAmbientCubeNonZero();
// Model rendering state (M5): bone matrices (dx9 cModel layout: 3 float4 rows
// per bone, row-vector mul) + ambient light cube
void StateDx11_LoadBoneMatrix( int nBone, const float *pRowMajor3x4 );
void StateDx11_SetNumBones( int nBones );
int  StateDx11_GetNumBones();
void StateDx11_SetAmbientLightCube( const float *pCube6x4 );
struct LightDesc_t;
void StateDx11_SetLight( int nLight, const LightDesc_t &desc );
void StateDx11_DisableAllLights();
void StateDx11_SetDrawingStaticMesh( bool bStatic );	// lit model path signal
// Baked static-prop vertex lighting: CStaticMeshDx11 sets its bound color
// mesh (VERTEX_SPECULAR bytes) around the draw; offset/stride are in bytes
// (dx9 binds it as stream 1, meshdx8.cpp:3166). NULL = no color mesh.
struct ID3D11Buffer;
void StateDx11_SetStaticColorMesh( ID3D11Buffer *pVB, int nStrideBytes, int nOffsetBytes );
// Render targets: texture RTs bind with a per-size aux depth buffer; the
// suppression flag covers RT textures we can't bind (no RTV) so their draws
// and clears can't wipe the backbuffer. hDepthTexture: a depth-map texture
// handle attaches its DSV (the flashlight shadow caster pass); anything else
// (SHADER_RENDERTARGET_DEPTHBUFFER/NONE, color handles) keeps the aux depth.
void StateDx11_SetRenderTexture( ShaderAPITextureHandle_t hTexture,
	ShaderAPITextureHandle_t hDepthTexture = INVALID_SHADERAPI_TEXTURE_HANDLE );	// INVALID = backbuffer
ShaderAPITextureHandle_t StateDx11_GetRenderTexture();
// Flashlight depth-shadow state: bias factors for the caster's SHADOW_BIAS
// poly offset (SetShadowDepthBiasFactors) and the perm-34 "shadows on" flag
// (SetFlashlightStateEx stored a depth texture with m_bEnableShadows).
void StateDx11_SetShadowDepthBias( float flSlopeScale, float flDepthBias );
void StateDx11_SetFlashlightShadows( bool bShadows );
bool StateDx11_RTTexturesEnabled();	// dx11_rt_textures convar (off until M6)
void StateDx11_SetOffscreenRT( bool bOffscreen );
bool StateDx11_IsOffscreenRT();
// Clears the active target, viewport-scoped like dx9 Clear()
void StateDx11_ClearViews( bool bClearColor, bool bClearDepth, bool bClearStencil, const float pColor[4] );
// dx9 scene-fog state (SceneFogMode/FogStart/End/SetFogZ/SceneFogColor3ub).
// nSceneFogMode is MaterialFogMode_t: 0 none, 1 linear (range fog, the dx9
// fixed-function vertex-fog math), 2 linear-below-fog-z (height fog = the
// water-refraction/underwater views; also turns on the water-fog dest-alpha
// write for opaque draws).
void StateDx11_SetSceneFogState( int nSceneFogMode, float flWaterZ, float flOORange,
	float flFogEndOverRange, float flMaxDensityFloor, const float pFogColor3[3] );
// Per-pass shadow fog state (IShaderShadow::FogMode — picks the fog COLOR:
// FogToFogColor/FogToBlack/FogToGrey/FogToWhite/disabled)
void StateDx11_ShadowFogMode( int nShaderFogMode );
void StateDx11_ShadowDisableFogGammaCorrection( bool bDisable );
// Re-issues RSSetViewports from the tracked viewport (after a blit disturbed it)
void StateDx11_ReapplyViewport();
void StateDx11_BindTexture( int nSampler, ShaderAPITextureHandle_t hTexture );

// Integer-HDR tonemap scale (SetToneMappingScaleLinear .x): scales the fog
// colors computed at PerDraw fill, matching dx9 UpdatePixelFogColorConstant.
void StateDx11_SetToneMapScale( float flScale );

// Dynamic stencil state (IShaderAPI::SetStencil* — the autoexposure histogram
// drives it around its mark/count draws). When enabled it overlays the
// snapshot's depth state with a composed depth-stencil object + ref value.
void StateDx11_SetStencilEnable( bool bEnable );
void StateDx11_SetStencilFailOp( int nOp );			// StencilOperation_t == D3D11_STENCIL_OP
void StateDx11_SetStencilZFailOp( int nOp );
void StateDx11_SetStencilPassOp( int nOp );
void StateDx11_SetStencilCompareFunc( int nFunc );	// StencilComparisonFunction_t == D3D11_COMPARISON_FUNC
void StateDx11_SetStencilReference( int nRef );
void StateDx11_SetStencilTestMask( unsigned int nMask );
void StateDx11_SetStencilWriteMask( unsigned int nMask );
// Stencil clear for the histogram rects. D3D11 has no rect-scoped stencil
// clear; the histogram's mark + count draws are scissored to the same rect,
// so a full-target stencil clear is semantically equivalent here.
void StateDx11_ClearStencilRect( int nValue );

// Binds shaders/states/textures/constants for the current snapshot + vertex
// format and returns true if drawing may proceed. The caller owns IA setup.
bool StateDx11_SetupForDraw( VertexFormat_t fmt );

// Device lifecycle
void StateDx11_ReleaseDevice();

#endif // STATEDX11_H
