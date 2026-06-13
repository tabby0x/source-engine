//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX11 backend scaffold (migration milestone M0).
//
// Stub implementation of the full shaderapi interface surface, adapted from
// shaderapiempty. It exists so shaderapidx11.dll loads through the normal
// CMaterialSystem::SetShaderAPI seam and fails loudly at SetMode until the
// real D3D11 device layer lands (milestone M1). See Plan.md at the repo root.
//
// $NoKeywords: $
//
//===========================================================================//

#include <d3d11.h>
#include <d3d11_1.h>

#include "utlvector.h"
#include "materialsystem/imaterialsystem.h"
#include "IHardwareConfigInternal.h"
#include "shaderapi/ishaderutil.h"
#include "shaderapi/ishaderapi.h"
#include "shaderapi/ishadershadow.h"
#include "shaderapi/commandbuffer.h"
#include "materialsystem/materialsystem_config.h"
#include "materialsystem/imesh.h"
#include "tier0/dbg.h"
#include "materialsystem/idebugtextureinfo.h"
#include "materialsystem/deformations.h"
#include "shaderapibase.h"
#include "hardwareconfig.h"
#include "mathlib/mathlib.h"
#include "shaderapidx11_global.h"
#include "shaderdevicedx11.h"
#include "statedx11.h"
#include "blitdx11.h"
#include "texturedx11.h"
#include "meshdx11.h"
#include "meshbase.h"
#include "vtf/vtf.h"
#include "../stdshaders/common_hlsl_cpp_consts.h"	// TONE_MAPPING_SCALE_PSH_CONSTANT (ps c30)

// dx8-parity lightmap scale (see CShaderAPIDx8::GetLightMapScaleFactor): the
// LDR overbright 2.0 lives in gamma space; shaders get it pre-linearized.
float ShaderApiDx11_LightMapScaleFactor()
{
	switch ( g_pHardwareConfig->GetHDRType() )
	{
	case HDR_TYPE_FLOAT:
		return 1.0f;
	case HDR_TYPE_INTEGER:
		return 16.0f;
	case HDR_TYPE_NONE:
	default:
		return GammaToLinearFullRange( 2.0f );
	}
}


//-----------------------------------------------------------------------------
// The empty mesh
//-----------------------------------------------------------------------------
class CStubMeshDx11 : public IMesh
{
public:
	CStubMeshDx11( bool bIsDynamic );
	virtual ~CStubMeshDx11();

	// FIXME: Make this work! Unsupported methods of IIndexBuffer + IVertexBuffer
	virtual bool Lock( int nMaxIndexCount, bool bAppend, IndexDesc_t& desc );
	virtual void Unlock( int nWrittenIndexCount, IndexDesc_t& desc );
	virtual void ModifyBegin( bool bReadOnly, int nFirstIndex, int nIndexCount, IndexDesc_t& desc );
	virtual void ModifyEnd( IndexDesc_t& desc );
	virtual void Spew( int nIndexCount, const IndexDesc_t & desc );
	virtual void ValidateData( int nIndexCount, const IndexDesc_t &desc );
	virtual bool Lock( int nVertexCount, bool bAppend, VertexDesc_t &desc );
	virtual void Unlock( int nVertexCount, VertexDesc_t &desc );
	virtual void Spew( int nVertexCount, const VertexDesc_t &desc );
	virtual void ValidateData( int nVertexCount, const VertexDesc_t & desc );
	virtual bool IsDynamic() const { return m_bIsDynamic; }
	virtual void BeginCastBuffer( VertexFormat_t format ) {}
	virtual void BeginCastBuffer( MaterialIndexFormat_t format ) {}
	virtual void EndCastBuffer( ) {}
	virtual int GetRoomRemaining() const { return 0; }
	virtual MaterialIndexFormat_t IndexFormat() const { return MATERIAL_INDEX_FORMAT_UNKNOWN; }

	void LockMesh( int numVerts, int numIndices, MeshDesc_t& desc );
	void UnlockMesh( int numVerts, int numIndices, MeshDesc_t& desc );

	void ModifyBeginEx( bool bReadOnly, int firstVertex, int numVerts, int firstIndex, int numIndices, MeshDesc_t& desc );
	void ModifyBegin( int firstVertex, int numVerts, int firstIndex, int numIndices, MeshDesc_t& desc );
	void ModifyEnd( MeshDesc_t& desc );

	// returns the # of vertices (static meshes only)
	int  VertexCount() const;

	// Sets the primitive type
	void SetPrimitiveType( MaterialPrimitiveType_t type );
	 
	// Draws the entire mesh
	void Draw(int firstIndex, int numIndices);

	void Draw(CPrimList *pPrims, int nPrims);

	// Copy verts and/or indices to a mesh builder. This only works for temp meshes!
	virtual void CopyToMeshBuilder( 
		int iStartVert,		// Which vertices to copy.
		int nVerts, 
		int iStartIndex,	// Which indices to copy.
		int nIndices, 
		int indexOffset,	// This is added to each index.
		CMeshBuilder &builder );

	// Spews the mesh data
	void Spew( int numVerts, int numIndices, const MeshDesc_t & desc );

	void ValidateData( int numVerts, int numIndices, const MeshDesc_t & desc );

	// gets the associated material
	IMaterial* GetMaterial();

	void SetColorMesh( IMesh *pColorMesh, int nVertexOffset )
	{
	}


	virtual int IndexCount() const
	{
		return 0;
	}

	virtual void SetFlexMesh( IMesh *pMesh, int nVertexOffset ) {}

	virtual void DisableFlexMesh() {}

	virtual void MarkAsDrawn() {}

	virtual unsigned ComputeMemoryUsed() { return 0; }

	virtual VertexFormat_t GetVertexFormat() const { return VERTEX_POSITION; }

	virtual IMesh *GetMesh()
	{
		return this;
	}

private:
	enum
	{
		VERTEX_BUFFER_SIZE = 1024 * 1024
	};

	unsigned char* m_pVertexMemory;
	bool m_bIsDynamic;
};


//-----------------------------------------------------------------------------
// The empty shader shadow
//-----------------------------------------------------------------------------
class CShaderShadowDx11 : public IShaderShadow
{
public:
	CShaderShadowDx11();
	virtual ~CShaderShadowDx11();

	// Sets the default *shadow* state
	void SetDefaultState();

	// Methods related to depth buffering
	void DepthFunc( ShaderDepthFunc_t depthFunc );
	void EnableDepthWrites( bool bEnable );
	void EnableDepthTest( bool bEnable );
	void EnablePolyOffset( PolygonOffsetMode_t nOffsetMode );

	// Suppresses/activates color writing 
	void EnableColorWrites( bool bEnable );
	void EnableAlphaWrites( bool bEnable );

	// Methods related to alpha blending
	void EnableBlending( bool bEnable );
	void BlendFunc( ShaderBlendFactor_t srcFactor, ShaderBlendFactor_t dstFactor );

	// Alpha testing
	void EnableAlphaTest( bool bEnable );
	void AlphaFunc( ShaderAlphaFunc_t alphaFunc, float alphaRef /* [0-1] */ );

	// Wireframe/filled polygons
	void PolyMode( ShaderPolyModeFace_t face, ShaderPolyMode_t polyMode );

	// Back face culling
	void EnableCulling( bool bEnable );
	
	// constant color + transparency
	void EnableConstantColor( bool bEnable );

	// Indicates the vertex format for use with a vertex shader
	// The flags to pass in here come from the VertexFormatFlags_t enum
	// If pTexCoordDimensions is *not* specified, we assume all coordinates
	// are 2-dimensional
	void VertexShaderVertexFormat( unsigned int nFlags, 
		int nTexCoordCount, int* pTexCoordDimensions, int nUserDataSize );
	
	// Indicates we're going to light the model
	void EnableLighting( bool bEnable );
	void EnableSpecular( bool bEnable );

	// vertex blending
	void EnableVertexBlend( bool bEnable );

	// per texture unit stuff
	void OverbrightValue( TextureStage_t stage, float value );
	void EnableTexture( Sampler_t stage, bool bEnable );
	void EnableTexGen( TextureStage_t stage, bool bEnable );
	void TexGen( TextureStage_t stage, ShaderTexGenParam_t param );

	// alternate method of specifying per-texture unit stuff, more flexible and more complicated
	// Can be used to specify different operation per channel (alpha/color)...
	void EnableCustomPixelPipe( bool bEnable );
	void CustomTextureStages( int stageCount );
	void CustomTextureOperation( TextureStage_t stage, ShaderTexChannel_t channel, 
		ShaderTexOp_t op, ShaderTexArg_t arg1, ShaderTexArg_t arg2 );

	// indicates what per-vertex data we're providing
	void DrawFlags( unsigned int drawFlags );

	// A simpler method of dealing with alpha modulation
	void EnableAlphaPipe( bool bEnable );
	void EnableConstantAlpha( bool bEnable );
	void EnableVertexAlpha( bool bEnable );
	void EnableTextureAlpha( TextureStage_t stage, bool bEnable );

	// GR - Separate alpha blending
	void EnableBlendingSeparateAlpha( bool bEnable );
	void BlendFuncSeparateAlpha( ShaderBlendFactor_t srcFactor, ShaderBlendFactor_t dstFactor );

	// Sets the vertex and pixel shaders
	void SetVertexShader( const char *pFileName, int vshIndex );
	void SetPixelShader( const char *pFileName, int pshIndex );

	// Convert from linear to gamma color space on writes to frame buffer.
	void EnableSRGBWrite( bool bEnable )
	{
		StateDx11_ShadowEnableSRGBWrite( bEnable );
	}

	void EnableSRGBRead( Sampler_t stage, bool bEnable )
	{
		StateDx11_ShadowEnableSRGBRead( stage, bEnable );
	}

	virtual void FogMode( ShaderFogMode_t fogMode )
	{
		StateDx11_ShadowFogMode( (int)fogMode );
	}

	virtual void DisableFogGammaCorrection( bool bDisable )
	{
		StateDx11_ShadowDisableFogGammaCorrection( bDisable );
	}

	virtual void SetDiffuseMaterialSource( ShaderMaterialSource_t materialSource )
	{
	}

	virtual void SetMorphFormat( MorphFormat_t flags )
	{
	}

	virtual void EnableStencil( bool bEnable )
	{
	}
	virtual void StencilFunc( ShaderStencilFunc_t stencilFunc )
	{
	}
	virtual void StencilPassOp( ShaderStencilOp_t stencilOp )
	{
	}
	virtual void StencilFailOp( ShaderStencilOp_t stencilOp )
	{
	}
	virtual void StencilDepthFailOp( ShaderStencilOp_t stencilOp )
	{
	}
	virtual void StencilReference( int nReference )
	{
	}
	virtual void StencilMask( int nMask )
	{
	}
	virtual void StencilWriteMask( int nMask )
	{
	}

	virtual void ExecuteCommandBuffer( uint8 *pBuf ) 
	{
	}
	// Alpha to coverage
	void EnableAlphaToCoverage( bool bEnable );
	
	virtual void SetShadowDepthFiltering( Sampler_t stage )
	{
	}

	virtual void BlendOp( ShaderBlendOp_t blendOp ) {}
	virtual void BlendOpSeparateAlpha( ShaderBlendOp_t blendOp ) {}

	bool m_IsTranslucent;
	bool m_IsAlphaTested;
	bool m_bIsDepthWriteEnabled;
	bool m_bUsesVertexAndPixelShaders;
};


//-----------------------------------------------------------------------------
// Stub mesh instances handed out by CShaderDeviceDx11 until the real mesh
// system lands (M3). The real device manager and device live in
// shaderdevicedx11.cpp as of M1.
//-----------------------------------------------------------------------------
static CStubMeshDx11 s_StubMesh( false );
static CStubMeshDx11 s_StubDynamicMesh( true );


//-----------------------------------------------------------------------------
// Scene fog state (dx8 m_SceneFogMode / m_VertexShaderFogParams / m_FogZ).
// stdshaders read it back via GetPixelFogCombo/GetSceneFogMode;
// SetPixelShaderFogParams stages the dx9 fog-param vector into the PS mirror;
// statedx11 keys the water-fog dest-alpha write off height mode
// (MATERIAL_FOG_LINEAR_BELOW_FOG_Z = the engine's water-refraction view).
//-----------------------------------------------------------------------------
static MaterialFogMode_t s_nSceneFogMode = MATERIAL_FOG_NONE;
static float s_flFogStart = 0.0f;
static float s_flFogEnd = 0.0f;
static float s_flFogZ = 0.0f;
static float s_flFogMaxDensity = 1.0f;
static unsigned char s_SceneFogColor[3] = { 0, 0, 0 };

static void UpdatePixelFogState()
{
	float flOORange = 0.0f;
	if ( s_flFogEnd != s_flFogStart )
		flOORange = 1.0f / ( s_flFogEnd - s_flFogStart );
	float flColor[3] = { s_SceneFogColor[0] / 255.0f, s_SceneFogColor[1] / 255.0f,
		s_SceneFogColor[2] / 255.0f };
	// floor = dx9 UpdateVertexShaderFogParams' cFogMaxDensity = 1 - $fogmaxdensity
	StateDx11_SetSceneFogState( (int)s_nSceneFogMode, s_flFogZ, flOORange,
		s_flFogEnd * flOORange, 1.0f - clamp( s_flFogMaxDensity, 0.0f, 1.0f ), flColor );
}


//-----------------------------------------------------------------------------
// The DX8 implementation of the shader API
//-----------------------------------------------------------------------------
class CShaderAPIDx11 : public CShaderAPIBase, public IHardwareConfigInternal, public IDebugTextureInfo
{
public:
	// constructor, destructor
	CShaderAPIDx11( );
	virtual ~CShaderAPIDx11();

	// IDebugTextureInfo implementation.
public:

	virtual bool IsDebugTextureListFresh( int numFramesAllowed = 1 ) { return false; }
	virtual bool SetDebugTextureRendering( bool bEnable ) { return false; }
	virtual void EnableDebugTextureList( bool bEnable ) {}
	virtual void EnableGetAllTextures( bool bEnable ) {}
	virtual KeyValues* GetDebugTextureList() { return NULL; }
	virtual int GetTextureMemoryUsed( TextureMemoryType eTextureMemory ) { return 0; }

	// Methods of CShaderAPIBase (BeginPIXEvent/EndPIXEvent/ResetRenderState
	// are implemented further down with the rest of the stub surface)
public:
	virtual bool OnDeviceInit() { return true; }
	virtual void OnDeviceShutdown() {}
	virtual void AdvancePIXFrame() {}
	virtual void ReleaseShaderObjects() {}
	virtual void RestoreShaderObjects() {}
	virtual IDirect3DBaseTexture* GetD3DTexture( ShaderAPITextureHandle_t hTexture ) { return NULL; }
	virtual void QueueResetRenderState() {}

	// Methods of IShaderDynamicAPI
	virtual void GetBackBufferDimensions( int& width, int& height ) const
	{
		g_pShaderDeviceDx11->GetBackBufferDimensions( width, height );
	}
	virtual void GetCurrentColorCorrection( ShaderColorCorrectionInfo_t* pInfo )
	{
		// The materialsystem's CC manager owns the weights (dx8 parity:
		// engine_post reads them through here every frame)
		if ( ShaderUtil() )
		{
			ShaderUtil()->GetCurrentColorCorrection( pInfo );
			return;
		}
		pInfo->m_bIsEnabled = false;
		pInfo->m_nLookupCount = 0;
		pInfo->m_flDefaultWeight = 1.0f;
		pInfo->m_pLookupWeights[0] = pInfo->m_pLookupWeights[1] = 0.0f;
		pInfo->m_pLookupWeights[2] = pInfo->m_pLookupWeights[3] = 0.0f;
	}


	// Methods of IShaderAPI
public:
	virtual void SetViewports( int nCount, const ShaderViewport_t* pViewports );
	virtual int GetViewports( ShaderViewport_t* pViewports, int nMax ) const;
	virtual void ClearBuffers( bool bClearColor, bool bClearDepth, bool bClearStencil, int renderTargetWidth, int renderTargetHeight );
	virtual void ClearColor3ub( unsigned char r, unsigned char g, unsigned char b );
	virtual void ClearColor4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a );
	virtual void BindVertexShader( VertexShaderHandle_t hVertexShader ) {}
	virtual void BindGeometryShader( GeometryShaderHandle_t hGeometryShader ) {}
	virtual void BindPixelShader( PixelShaderHandle_t hPixelShader ) {}
	virtual void SetRasterState( const ShaderRasterState_t& state ) {}
	virtual void MarkUnusedVertexFields( unsigned int nFlags, int nTexCoordCount, bool *pUnusedTexCoords ) {}
	virtual bool OwnGPUResources( bool bEnable ) { return false; }

	virtual bool DoRenderTargetsNeedSeparateDepthBuffer() const;

	// Used to clear the transition table when we know it's become invalid.
	void ClearSnapshots();

	// Sets the mode...
	bool SetMode( void* hwnd, int nAdapter, const ShaderDeviceInfo_t &info )
	{
		return g_pShaderDeviceMgr && ( g_pShaderDeviceMgr->SetMode( hwnd, nAdapter, info ) != NULL );
	}

	void ChangeVideoMode( const ShaderDeviceInfo_t &info )
	{
		// Runtime mat_antialias / resolution / vsync edits
		// (CMaterialSystem::OverrideConfig "bVideoModeChange" path): the
		// swapchain + backbuffer views recreate in place on the live device.
		if ( g_pShaderDeviceDx11 )
		{
			g_pShaderDeviceDx11->ChangeVideoMode( info );
		}
	}

	// Called when the dx support level has changed
	virtual void DXSupportLevelChanged() {}

	virtual void EnableUserClipTransformOverride( bool bEnable ) {}
	virtual void UserClipTransform( const VMatrix &worldToView ) {}

	// Sets the default *dynamic* state
	void SetDefaultState( );

	// Returns the snapshot id for the shader state
	StateSnapshot_t	 TakeSnapshot( );

	// Returns true if the state snapshot is transparent
	bool IsTranslucent( StateSnapshot_t id ) const;
	bool IsAlphaTested( StateSnapshot_t id ) const;
	bool UsesVertexAndPixelShaders( StateSnapshot_t id ) const;
	virtual bool IsDepthWriteEnabled( StateSnapshot_t id ) const;

	// Gets the vertex format for a set of snapshot ids
	VertexFormat_t ComputeVertexFormat( int numSnapshots, StateSnapshot_t* pIds ) const;

	// Gets the vertex format for a set of snapshot ids
	VertexFormat_t ComputeVertexUsage( int numSnapshots, StateSnapshot_t* pIds ) const;

	// Begins a rendering pass that uses a state snapshot
	void BeginPass( StateSnapshot_t snapshot  );

	// Uses a state snapshot
	void UseSnapshot( StateSnapshot_t snapshot );

	// Use this to get the mesh builder that allows us to modify vertex data
	CMeshBuilder* GetVertexModifyBuilder();

	// Sets the color to modulate by
	void Color3f( float r, float g, float b );
	void Color3fv( float const* pColor );
	void Color4f( float r, float g, float b, float a );
	void Color4fv( float const* pColor );

	// Faster versions of color
	void Color3ub( unsigned char r, unsigned char g, unsigned char b );
	void Color3ubv( unsigned char const* rgb );
	void Color4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a );
	void Color4ubv( unsigned char const* rgba );

	// Sets the lights
	void SetLight( int lightNum, const LightDesc_t& desc );
	void SetLightingOrigin( Vector vLightingOrigin );
	void SetAmbientLight( float r, float g, float b );
	void SetAmbientLightCube( Vector4D cube[6] );

	// Get the lights
	int GetMaxLights( void ) const;
	const LightDesc_t& GetLight( int lightNum ) const;

	// Render state for the ambient light cube (vertex shaders)
	void SetVertexShaderStateAmbientLightCube();
	void SetPixelShaderStateAmbientLightCube( int pshReg, bool bForceToBlack = false )
	{
		// dx9 parity (shaderapidx8.cpp:8880): dynamic-state ambient cube (or
		// black) into the PS register mirror — skin/phong reads PSREG_AMBIENT_CUBE.
		StateDx11_SetPSAmbientCube( pshReg, bForceToBlack );
	}

	float GetAmbientLightCubeLuminance(void)
	{
		return 0.0f;
	}

	void SetSkinningMatrices();

	// Lightmap texture binding
	void BindLightmap( TextureStage_t stage );
	void BindLightmapAlpha( TextureStage_t stage )
	{
	}
	void BindBumpLightmap( TextureStage_t stage );
	void BindFullbrightLightmap( TextureStage_t stage );
	void BindWhite( TextureStage_t stage );
	void BindBlack( TextureStage_t stage );
	void BindGrey( TextureStage_t stage );
	void BindFBTexture( TextureStage_t stage, int textureIdex );
	void CopyRenderTargetToTexture( ShaderAPITextureHandle_t texID )
	{
		BlitDx11_RTToTexture( texID, NULL, NULL );
	}

	void CopyRenderTargetToTextureEx( ShaderAPITextureHandle_t texID, int nRenderTargetID, Rect_t *pSrcRect, Rect_t *pDstRect )
	{
		BlitDx11_RTToTexture( texID, pSrcRect, pDstRect );
	}

	void CopyTextureToRenderTargetEx( int nRenderTargetID, ShaderAPITextureHandle_t textureHandle, Rect_t *pSrcRect, Rect_t *pDstRect )
	{
		BlitDx11_TextureToRT( textureHandle, pSrcRect, pDstRect );
	}

	// Special system flat normal map binding.
	void BindFlatNormalMap( TextureStage_t stage );
	void BindNormalizationCubeMap( TextureStage_t stage );
	void BindSignedNormalizationCubeMap( TextureStage_t stage );

	// Set the number of bone weights
	void SetNumBoneWeights( int numBones );
	void EnableHWMorphing( bool bEnable );

	// Flushes any primitives that are buffered
	void FlushBufferedPrimitives();

	// Gets the dynamic mesh; note that you've got to render the mesh
	// before calling this function a second time. Clients should *not*
	// call DestroyStaticMesh on the mesh returned by this call.
	IMesh* GetDynamicMesh( IMaterial* pMaterial, int nHWSkinBoneCount, bool buffered, IMesh* pVertexOverride, IMesh* pIndexOverride );
	IMesh* GetDynamicMeshEx( IMaterial* pMaterial, VertexFormat_t fmt, int nHWSkinBoneCount, bool buffered, IMesh* pVertexOverride, IMesh* pIndexOverride );

	IMesh* GetFlexMesh();

	// Renders a single pass of a material
	void RenderPass( int nPass, int nPassCount );

	// stuff related to matrix stacks
	void MatrixMode( MaterialMatrixMode_t matrixMode );
	void PushMatrix();
	void PopMatrix();
	void LoadMatrix( float *m );
	void LoadBoneMatrix( int boneIndex, const float *m )
	{
		// dx9 cModel contract: matrix3x4_t rows land as 3 float4 bone rows
		StateDx11_LoadBoneMatrix( boneIndex, m );
	}
	void MultMatrix( float *m );
	void MultMatrixLocal( float *m );
	void GetMatrix( MaterialMatrixMode_t matrixMode, float *dst );
	void LoadIdentity( void );
	void LoadCameraToWorld( void );
	void Ortho( double left, double top, double right, double bottom, double zNear, double zFar );
	void PerspectiveX( double fovx, double aspect, double zNear, double zFar );
	void PerspectiveOffCenterX( double fovx, double aspect, double zNear, double zFar, double bottom, double top, double left, double right );
	void PickMatrix( int x, int y, int width, int height );
	void Rotate( float angle, float x, float y, float z );
	void Translate( float x, float y, float z );
	void Scale( float x, float y, float z );
	void ScaleXY( float x, float y );

	// Fog methods...
	void FogMode( MaterialFogMode_t fogMode );
	void FogStart( float fStart );
	void FogEnd( float fEnd );
	void SetFogZ( float fogZ );
	void FogMaxDensity( float flMaxDensity );
	void GetFogDistances( float *fStart, float *fEnd, float *fFogZ );
	void FogColor3f( float r, float g, float b );
	void FogColor3fv( float const* rgb );
	void FogColor3ub( unsigned char r, unsigned char g, unsigned char b );
	void FogColor3ubv( unsigned char const* rgb );

	virtual void SceneFogColor3ub( unsigned char r, unsigned char g, unsigned char b );
	virtual void SceneFogMode( MaterialFogMode_t fogMode );
	virtual void GetSceneFogColor( unsigned char *rgb );
	virtual MaterialFogMode_t GetSceneFogMode( );
	virtual int GetPixelFogCombo( );

	void SetHeightClipZ( float z ); 
	void SetHeightClipMode( enum MaterialHeightClipMode_t heightClipMode ); 

	void SetClipPlane( int index, const float *pPlane );
	void EnableClipPlane( int index, bool bEnable );

	void SetFastClipPlane( const float *pPlane );
	void EnableFastClip( bool bEnable );
	
	// We use smaller dynamic VBs during level transitions, to free up memory
	virtual int  GetCurrentDynamicVBSize( void );
	virtual void DestroyVertexBuffers( bool bExitingLevel = false );

	// Sets the vertex and pixel shaders
	void SetVertexShaderIndex( int vshIndex );
	void SetPixelShaderIndex( int pshIndex );

	// Sets the constant register for vertex and pixel shaders
	void SetVertexShaderConstant( int var, float const* pVec, int numConst = 1, bool bForce = false );
	void SetBooleanVertexShaderConstant( int var, BOOL const* pVec, int numConst = 1, bool bForce = false );
	void SetIntegerVertexShaderConstant( int var, int const* pVec, int numConst = 1, bool bForce = false );
	void SetPixelShaderConstant( int var, float const* pVec, int numConst = 1, bool bForce = false );
	void SetBooleanPixelShaderConstant( int var, BOOL const* pVec, int numBools = 1, bool bForce = false );
	void SetIntegerPixelShaderConstant( int var, int const* pVec, int numIntVecs = 1, bool bForce = false );

	void InvalidateDelayedShaderConstants( void );

	// Gamma<->Linear conversions according to the video hardware we're running on
	float GammaToLinear_HardwareSpecific( float fGamma ) const;
	float LinearToGamma_HardwareSpecific( float fLinear ) const;

	//Set's the linear->gamma conversion textures to use for this hardware for both srgb writes enabled and disabled(identity)
	void SetLinearToGammaConversionTextures( ShaderAPITextureHandle_t hSRGBWriteEnabledTexture, ShaderAPITextureHandle_t hIdentityTexture );

	// Cull mode
	void CullMode( MaterialCullMode_t cullMode );

	// Force writes only when z matches. . . useful for stenciling things out
	// by rendering the desired Z values ahead of time.
	void ForceDepthFuncEquals( bool bEnable );

	// Forces Z buffering on or off
	void OverrideDepthEnable( bool bEnable, bool bDepthEnable );
	void OverrideAlphaWriteEnable( bool bOverrideEnable, bool bAlphaWriteEnable );
	void OverrideColorWriteEnable( bool bOverrideEnable, bool bColorWriteEnable );

	// Sets the shade mode
	void ShadeMode( ShaderShadeMode_t mode );

	// Binds a particular material to render with
	void Bind( IMaterial* pMaterial );

	// Returns the nearest supported format
	ImageFormat GetNearestSupportedFormat( ImageFormat fmt, bool bFilteringRequired = true ) const;
 	ImageFormat GetNearestRenderTargetFormat( ImageFormat fmt ) const;

	// Sets the texture state
	void BindTexture( Sampler_t stage, ShaderAPITextureHandle_t textureHandle );

	void SetRenderTarget( ShaderAPITextureHandle_t colorTextureHandle, ShaderAPITextureHandle_t depthTextureHandle )
	{
		SetRenderTargetEx( 0, colorTextureHandle, depthTextureHandle );
	}

	void SetRenderTargetEx( int nRenderTargetID, ShaderAPITextureHandle_t colorTextureHandle, ShaderAPITextureHandle_t depthTextureHandle )
	{
		// MRT slots beyond 0 aren't used by TF2's passes
		if ( nRenderTargetID != 0 )
			return;
		if ( colorTextureHandle == SHADER_RENDERTARGET_BACKBUFFER )
		{
			StateDx11_SetRenderTexture( INVALID_SHADERAPI_TEXTURE_HANDLE );
		}
		else
		{
			// Texture RTs bind with a per-size aux depth; unbindable ones
			// (no RTV) suppress draws/clears so they can't hit the backbuffer.
			// A depth-map texture handle in the depth slot (the flashlight
			// shadow caster: color = _rt_ShadowDummy, depth =
			// _rt_ShadowDepthTexture_N) attaches that texture's DSV instead.
			StateDx11_SetRenderTexture( colorTextureHandle, depthTextureHandle );
		}
	}

	// Indicates we're going to be modifying this texture
	// TexImage2D, TexSubImage2D, TexWrap, TexMinFilter, and TexMagFilter
	// all use the texture specified by this function.
	void ModifyTexture( ShaderAPITextureHandle_t textureHandle );

	// Texture management methods
	void TexImage2D( int level, int cubeFace, ImageFormat dstFormat, int zOffset, int width, int height, 
							 ImageFormat srcFormat, bool bSrcIsTiled, void *imageData );
	void TexSubImage2D( int level, int cubeFace, int xOffset, int yOffset, int zOffset, int width, int height,
							 ImageFormat srcFormat, int srcStride, bool bSrcIsTiled, void *imageData );

	void TexImageFromVTF( IVTFTexture *pVTF, int iVTFFrame );

	bool TexLock( int level, int cubeFaceID, int xOffset, int yOffset, 
									int width, int height, CPixelWriter& writer );
	void TexUnlock( );
	
	// These are bound to the texture, not the texture environment
	void TexMinFilter( ShaderTexFilterMode_t texFilterMode );
	void TexMagFilter( ShaderTexFilterMode_t texFilterMode );
	void TexWrap( ShaderTexCoordComponent_t coord, ShaderTexWrapMode_t wrapMode );
	void TexSetPriority( int priority );	

	ShaderAPITextureHandle_t CreateTexture( 
		int width, 
		int height,
		int depth,
		ImageFormat dstImageFormat, 
		int numMipLevels, 
		int numCopies, 
		int flags, 
		const char *pDebugName,
		const char *pTextureGroupName );
	// Create a multi-frame texture (equivalent to calling "CreateTexture" multiple times, but more efficient)
	void CreateTextures( 
		ShaderAPITextureHandle_t *pHandles,
		int count,
		int width, 
		int height,
		int depth,
		ImageFormat dstImageFormat, 
		int numMipLevels, 
		int numCopies, 
		int flags, 
		const char *pDebugName,
		const char *pTextureGroupName );
	ShaderAPITextureHandle_t CreateDepthTexture( ImageFormat renderFormat, int width, int height, const char *pDebugName, bool bTexture );
	void DeleteTexture( ShaderAPITextureHandle_t textureHandle );
	bool IsTexture( ShaderAPITextureHandle_t textureHandle );
	bool IsTextureResident( ShaderAPITextureHandle_t textureHandle );

	// stuff that isn't to be used from within a shader
	void ClearBuffersObeyStencil( bool bClearColor, bool bClearDepth );
	void ClearBuffersObeyStencilEx( bool bClearColor, bool bClearAlpha, bool bClearDepth );
	void PerformFullScreenStencilOperation( void );
	void ReadPixels( int x, int y, int width, int height, unsigned char *data, ImageFormat dstFormat );
	virtual void ReadPixels( Rect_t *pSrcRect, Rect_t *pDstRect, unsigned char *data, ImageFormat dstFormat, int nDstStride );

	// Selection mode methods
	int SelectionMode( bool selectionMode );
	void SelectionBuffer( unsigned int* pBuffer, int size );
	void ClearSelectionNames( );
	void LoadSelectionName( int name );
	void PushSelectionName( int name );
	void PopSelectionName();

	void FlushHardware();
	void ResetRenderState( bool bFullReset = true );

	void SetScissorRect( const int nLeft, const int nTop, const int nRight, const int nBottom, const bool bEnableScissor );

	// Can we download textures?
	virtual bool CanDownloadTextures() const;

	// Board-independent calls, here to unify how shaders set state
	// Implementations should chain back to IShaderUtil->BindTexture(), etc.

	// Use this to begin and end the frame
	void BeginFrame();
	void EndFrame();

	// returns current time
	double CurrentTime() const;

	// Get the current camera position in world space.
	void GetWorldSpaceCameraPosition( float * pPos ) const;

	// Members of IMaterialSystemHardwareConfig
	bool HasDestAlphaBuffer() const;
	bool HasStencilBuffer() const;
	virtual int  MaxViewports() const;
	virtual void OverrideStreamOffsetSupport( bool bOverrideEnabled, bool bEnableSupport ) {}
	virtual int  GetShadowFilterMode() const;
	int  StencilBufferBits() const;
	int	 GetFrameBufferColorDepth() const;
	int  GetSamplerCount() const;
	bool HasSetDeviceGammaRamp() const;
	bool SupportsCompressedTextures() const;
	VertexCompressionType_t SupportsCompressedVertices() const;
	bool SupportsVertexAndPixelShaders() const;
	bool SupportsPixelShaders_1_4() const;
	bool SupportsPixelShaders_2_0() const;
	bool SupportsPixelShaders_2_b() const;
	bool ActuallySupportsPixelShaders_2_b() const;
	bool SupportsStaticControlFlow() const;
	bool SupportsVertexShaders_2_0() const;
	bool SupportsShaderModel_3_0() const;
	int  MaximumAnisotropicLevel() const;
	int  MaxTextureWidth() const;
	int  MaxTextureHeight() const;
	int  MaxTextureAspectRatio() const;
	int  GetDXSupportLevel() const;
	const char *GetShaderDLLName() const
	{
		return "UNKNOWN";
	}
	int	 TextureMemorySize() const;
	bool SupportsOverbright() const;
	bool SupportsCubeMaps() const;
	bool SupportsMipmappedCubemaps() const;
	bool SupportsNonPow2Textures() const;
	int  GetTextureStageCount() const;
	int	 NumVertexShaderConstants() const;
	int	 NumBooleanVertexShaderConstants() const;
	int	 NumIntegerVertexShaderConstants() const;
	int	 NumPixelShaderConstants() const;
	int	 MaxNumLights() const;
	bool SupportsHardwareLighting() const;
	int	 MaxBlendMatrices() const;
	int	 MaxBlendMatrixIndices() const;
	int	 MaxVertexShaderBlendMatrices() const;
	int	 MaxUserClipPlanes() const;
	bool UseFastClipping() const
	{
		return false;
	}
	bool SpecifiesFogColorInLinearSpace() const;
	virtual bool SupportsSRGB() const;
	virtual bool FakeSRGBWrite() const;
	virtual bool CanDoSRGBReadFromRTs() const;
	virtual bool SupportsGLMixedSizeTargets() const;

	const char *GetHWSpecificShaderDLLName() const;
	bool NeedsAAClamp() const
	{
		return false;
	}
	bool SupportsSpheremapping() const;
	virtual int MaxHWMorphBatchCount() const { return 0; }

	// This is the max dx support level supported by the card
	virtual int	 GetMaxDXSupportLevel() const;

	bool ReadPixelsFromFrontBuffer() const;
	bool PreferDynamicTextures() const;
	virtual bool PreferReducedFillrate() const;
	bool HasProjectedBumpEnv() const;
	void ForceHardwareSync( void );
	
	int GetCurrentNumBones( void ) const;
	bool IsHWMorphingEnabled( void ) const;
	int GetCurrentLightCombo( void ) const;
	void GetDX9LightState( LightState_t *state ) const;
	MaterialFogMode_t GetCurrentFogType( void ) const;

	void RecordString( const char *pStr );

	void EvictManagedResources();

	void SetTextureTransformDimension( TextureStage_t textureStage, int dimension, bool projected );
	void DisableTextureTransform( TextureStage_t textureStage )
	{
	}
	void SetBumpEnvMatrix( TextureStage_t textureStage, float m00, float m01, float m10, float m11 );

	// Gets the lightmap dimensions
	virtual void GetLightmapDimensions( int *w, int *h );

	virtual void SyncToken( const char *pToken );

	// Setup standard vertex shader constants (that don't change)
	// This needs to be called anytime that overbright changes.
	virtual void SetStandardVertexShaderConstants( float fOverbright )
	{
	}
	
	// Level of anisotropic filtering
	virtual void SetAnisotropicLevel( int nAnisotropyLevel );

	bool SupportsHDR() const
	{
		// Defers to the shared CHardwareConfig (caps m_HDRType + the engine's
		// SetHDREnabled per mat_hdr_level) — same flow as dx9.
		return g_pHardwareConfig->GetHardwareHDRType() != HDR_TYPE_NONE;
	}
	HDRType_t GetHDRType() const
	{
		return g_pHardwareConfig->GetHDRType();
	}
	HDRType_t GetHardwareHDRType() const
	{
		return g_pHardwareConfig->GetHardwareHDRType();
	}
	virtual bool NeedsATICentroidHack() const
	{
		return false;
	}
	virtual bool SupportsColorOnSecondStream() const
	{
		return false;
	}
	virtual bool SupportsStaticPlusDynamicLighting() const
	{
		return false;
	}
	virtual bool SupportsStreamOffset() const
	{
		return false;
	}
	void SetDefaultDynamicState()
	{
	}
	virtual void CommitPixelShaderLighting( int pshReg )
	{
		// dx9 parity (shaderapidx8.cpp:11145): 4 point-ified lights packed
		// into 6 mirror registers (PSREG_LIGHT_INFO_ARRAY) for per-pixel paths.
		StateDx11_CommitPSLighting( pshReg );
	}

	// dx9 occlusion query parity (D3D11_QUERY_OCCLUSION) — drives the
	// sun/lens glow pixel-visibility system; invalid handles previously
	// collapsed the glow alpha by view angle (sun went black, rays vanished).
	ShaderAPIOcclusionQuery_t CreateOcclusionQueryObject( void )
	{
		return StateDx11_CreateOcclusionQuery();
	}

	void DestroyOcclusionQueryObject( ShaderAPIOcclusionQuery_t handle )
	{
		StateDx11_DestroyOcclusionQuery( handle );
	}

	void BeginOcclusionQueryDrawing( ShaderAPIOcclusionQuery_t handle )
	{
		StateDx11_BeginOcclusionQuery( handle );
	}

	void EndOcclusionQueryDrawing( ShaderAPIOcclusionQuery_t handle )
	{
		StateDx11_EndOcclusionQuery( handle );
	}

	int OcclusionQuery_GetNumPixelsRendered( ShaderAPIOcclusionQuery_t handle, bool bFlush )
	{
		return StateDx11_GetOcclusionQueryPixels( handle, bFlush );
	}

	virtual void AcquireThreadOwnership() {}
	virtual void ReleaseThreadOwnership() {}

	virtual bool SupportsBorderColor() const { return false; }
	virtual bool SupportsFetch4() const { return false; }
	virtual bool CanStretchRectFromTextures( void ) const { return false; }
	virtual void EnableBuffer2FramesAhead( bool bEnable ) {}

	virtual void SetPSNearAndFarZ( int pshReg ) { }

	virtual void SetDepthFeatheringPixelShaderConstant( int iConstant, float fDepthBlendScale ) {}

	void SetPixelShaderFogParams( int reg )
	{
		// dx8 SetPixelShaderFogParams (shaderapidx8.cpp:12980): height fog
		// rigs x/z to {0,1} for unified PS math; MATERIAL_FOG_NONE rigs the
		// vector so CalcRangeFog returns 0. Lands in the PS mirror like any
		// other family constant write.
		float fogParams[4];
		if ( s_nSceneFogMode != MATERIAL_FOG_NONE )
		{
			float flOORange = 1.0f;
			if ( s_flFogEnd != s_flFogStart )
				flOORange = 1.0f / ( s_flFogEnd - s_flFogStart );
			fogParams[0] = s_flFogStart * flOORange;
			fogParams[1] = s_flFogZ;
			fogParams[2] = clamp( s_flFogMaxDensity, 0.0f, 1.0f );
			fogParams[3] = flOORange;
			if ( s_nSceneFogMode == MATERIAL_FOG_LINEAR_BELOW_FOG_Z )
			{
				fogParams[0] = 0.0f;
				fogParams[2] = 1.0f;
			}
		}
		else
		{
			fogParams[0] = 0.0f;
			fogParams[1] = s_flFogZ;
			fogParams[2] = 1.0f;
			fogParams[3] = 0.0f;
		}
		StateDx11_SetPSConstants( reg, fogParams, 1 );
	}

	virtual bool InFlashlightMode() const
	{
		// dx9 parity (shaderapidx8.cpp:4760): the materialsystem tracks the
		// flashlight-pass scope (SetFlashlightMode around CShadowMgr/
		// studiorender flashlight re-renders).
		return ShaderUtil()->InFlashlightMode();
	}

	virtual bool InEditorMode() const
	{
		return false;
	}

	// What fields in the morph do we actually use?
	virtual MorphFormat_t ComputeMorphFormat( int numSnapshots, StateSnapshot_t* pIds ) const
	{
		return 0;
	}

	// Gets the bound morph's vertex format; returns 0 if no morph is bound
	virtual MorphFormat_t GetBoundMorphFormat()
	{
		return 0;
	}

	// Binds a standard texture — same routing as the command-buffer case and
	// CShaderAPIDx8: registered handles (materialsystem 1x1s) bind directly;
	// everything else resolves through the materialsystem (FB-copy RTs for
	// cloak/refract, normalization cubemaps, lightmap pages, CC LUTs), which
	// calls back into BindTexture with a concrete handle. The local 1x1
	// fallback only covers calls before ShaderUtil exists.
	virtual void BindStandardTexture( Sampler_t stage, StandardTextureId_t id )
	{
		if ( id >= 0 && id < TEXTURE_MAX_STD_TEXTURES &&
			 m_StdTextureHandles[id] != INVALID_SHADERAPI_TEXTURE_HANDLE )
		{
			StateDx11_BindTexture( stage, m_StdTextureHandles[id] );
		}
		else if ( ShaderUtil() )
		{
			ShaderUtil()->BindStandardTexture( stage, id );
		}
		else
		{
			StateDx11_BindTexture( stage, TextureDx11_GetStandardHandle( id ) );
		}
	}

	virtual void BindStandardVertexTexture( VertexTextureSampler_t stage, StandardTextureId_t id )
	{
	}

	virtual void GetStandardTextureDimensions( int *pWidth, int *pHeight, StandardTextureId_t id )
	{
		*pWidth = *pHeight = 1;
	}


	virtual void SetFlashlightState( const FlashlightState_t &state, const VMatrix &worldToTexture )
	{
		SetFlashlightStateEx( state, worldToTexture, NULL );
	}

	virtual void SetFlashlightStateEx( const FlashlightState_t &state, const VMatrix &worldToTexture, ITexture *pFlashlightDepthTexture )
	{
		// dx9 parity (shaderapidx8.cpp:13034): store for the shader DYNAMIC
		// blocks to read back — they upload the VS/PS constants themselves
		// through the existing mirrors and bind the depth map to the family's
		// depth sampler (dx90 s7, VLG s8, eyes s4). Perm 34 raises flag bit 8
		// from this and compares the spot projection's z manually.
		m_FlashlightState = state;
		m_FlashlightWorldToTexture = worldToTexture;
		m_pFlashlightDepthTexture = pFlashlightDepthTexture;
		bool bShadows = state.m_bEnableShadows && pFlashlightDepthTexture != NULL;
		if ( !bShadows )
		{
			m_pFlashlightDepthTexture = NULL;
			m_FlashlightState.m_bEnableShadows = false;
		}
		StateDx11_SetFlashlightShadows( bShadows );
	}

	virtual const FlashlightState_t &GetFlashlightState( VMatrix &worldToTexture ) const
	{
		worldToTexture = m_FlashlightWorldToTexture;
		return m_FlashlightState;
	}

	virtual const FlashlightState_t &GetFlashlightStateEx( VMatrix &worldToTexture, ITexture **ppFlashlightDepthTexture ) const
	{
		worldToTexture = m_FlashlightWorldToTexture;
		*ppFlashlightDepthTexture = m_pFlashlightDepthTexture;
		return m_FlashlightState;
	}

	virtual void ClearVertexAndPixelShaderRefCounts()
	{
	}

	virtual void PurgeUnusedVertexAndPixelShaders()
	{
	}

	virtual bool IsAAEnabled() const
	{
		return false;
	}

	virtual int GetVertexTextureCount() const
	{
		return 0;
	}

	virtual int GetMaxVertexTextureDimension() const
	{
		return 0;
	}

	virtual int  MaxTextureDepth() const
	{
		return 0;
	}

	// Binds a vertex texture to a particular texture stage in the vertex pipe
	virtual void BindVertexTexture( VertexTextureSampler_t nSampler, ShaderAPITextureHandle_t hTexture )
	{
	}

	// Sets morph target factors
	virtual void SetFlexWeights( int nFirstWeight, int nCount, const MorphWeight_t* pWeights )
	{
	}

	// NOTE: Stuff after this is added after shipping HL2.
	ITexture *GetRenderTargetEx( int nRenderTargetID )
	{
		return NULL;
	}

	void SetToneMappingScaleLinear( const Vector &scale )
	{
		// dx9 CShaderAPIDx8::SetToneMappingScaleLinear: cLightScale (ps c30) =
		// { output scale, lightmap scale, envmap scale, gamma output scale }.
		// The mirror carries it to every universal perm; the fog colors
		// computed at PerDraw fill scale through StateDx11_SetToneMapScale.
		float flOutScale = 1.0f, flEnvScale = 1.0f;
		switch ( g_pHardwareConfig->GetHDRType() )
		{
		case HDR_TYPE_FLOAT:
			flOutScale = scale.x;
			break;
		case HDR_TYPE_INTEGER:
			flOutScale = scale.x;
			flEnvScale = 16.0f;
			break;
		case HDR_TYPE_NONE:
		default:
			break;
		}
		float vLightScale[4] = { flOutScale, ShaderApiDx11_LightMapScaleFactor(),
			flEnvScale, LinearToGammaFullRange( flOutScale ) };
		SetPixelShaderConstant( TONE_MAPPING_SCALE_PSH_CONSTANT, vLightScale, 1 );
		StateDx11_SetToneMapScale( flOutScale );
		m_vToneMappingScale.Init( vLightScale[0], vLightScale[1], vLightScale[2] );
	}

	const Vector &GetToneMappingScaleLinear( void ) const
	{
		return m_vToneMappingScale;
	}

	virtual float GetLightMapScaleFactor( void ) const
	{
		return ShaderApiDx11_LightMapScaleFactor();
	}


	// For dealing with device lost in cases where SwapBuffers isn't called all the time (Hammer)
	virtual void HandleDeviceLost()
	{
	}

	virtual void EnableLinearColorSpaceFrameBuffer( bool bEnable )
	{
	}

	// Lets the shader know about the full-screen texture so it can 
	virtual void SetFullScreenTextureHandle( ShaderAPITextureHandle_t h )
	{
	}

	void SetFloatRenderingParameter(int parm_number, float value)
	{
	}

	void SetIntRenderingParameter(int parm_number, int value)
	{
	}
	void SetVectorRenderingParameter(int parm_number, Vector const &value)
	{
	}

	float GetFloatRenderingParameter(int parm_number) const
	{
		return 0;
	}

	int GetIntRenderingParameter(int parm_number) const
	{
		return 0;
	}

	Vector GetVectorRenderingParameter(int parm_number) const
	{
		return Vector(0,0,0);
	}

	// Methods related to stencil
	// Dynamic stencil state (the HDR autoexposure histogram drives these
	// around its lumcompare mark + stencil-gated count draws). The enums
	// share D3D9's numeric values, which D3D11 kept — they pass through.
	void SetStencilEnable(bool onoff)
	{
		StateDx11_SetStencilEnable( onoff );
	}

	void SetStencilFailOperation(StencilOperation_t op)
	{
		StateDx11_SetStencilFailOp( (int)op );
	}

	void SetStencilZFailOperation(StencilOperation_t op)
	{
		StateDx11_SetStencilZFailOp( (int)op );
	}

	void SetStencilPassOperation(StencilOperation_t op)
	{
		StateDx11_SetStencilPassOp( (int)op );
	}

	void SetStencilCompareFunction(StencilComparisonFunction_t cmpfn)
	{
		StateDx11_SetStencilCompareFunc( (int)cmpfn );
	}

	void SetStencilReferenceValue(int ref)
	{
		StateDx11_SetStencilReference( ref );
	}

	void SetStencilTestMask(uint32 msk)
	{
		StateDx11_SetStencilTestMask( msk );
	}

	void SetStencilWriteMask(uint32 msk)
	{
		StateDx11_SetStencilWriteMask( msk );
	}

	void ClearStencilBufferRectangle( int xmin, int ymin, int xmax, int ymax,int value)
	{
		StateDx11_ClearStencilRect( value );
	}

	virtual void GetDXLevelDefaults(uint &max_dxlevel,uint &recommended_dxlevel)
	{
		max_dxlevel=recommended_dxlevel=90;
	}

	virtual void GetMaxToRender( IMesh *pMesh, bool bMaxUntilFlush, int *pMaxVerts, int *pMaxIndices )
	{
		*pMaxVerts = 32768;
		*pMaxIndices = 32768;
	}

	// Returns the max possible vertices + indices to render in a single draw call
	virtual int GetMaxVerticesToRender( IMaterial *pMaterial )
	{
		return 32768;
	}

	virtual int GetMaxIndicesToRender( )
	{
		return 32768;
	}
	virtual int CompareSnapshots( StateSnapshot_t snapshot0, StateSnapshot_t snapshot1 ) { return 0; }

	virtual void DisableAllLocalLights() { StateDx11_DisableAllLights(); }

	virtual bool SupportsMSAAMode( int nMSAAMode )
	{
		// Gates the video-options AA dropdown and the engine-side clamp; the
		// swapchain itself is multisampled (blt-model, dx9 parity) so the
		// query is just CheckMultisampleQualityLevels on color + depth.
		return g_pShaderDeviceDx11 && g_pShaderDeviceDx11->SupportsMSAAMode( nMSAAMode );
	}

	virtual bool SupportsCSAAMode( int nNumSamples, int nQualityLevel ) { return false; }

	// Hooks for firing PIX events from outside the Material System: grouped
	// draws + markers in RenderDoc captures via ID3DUserDefinedAnnotation
	virtual void BeginPIXEvent( unsigned long color, const char *szName )
	{
		if ( ID3DUserDefinedAnnotation *pAnnotation = Dx11_Annotation() )
		{
			wchar_t wszName[128];
			mbstowcs( wszName, szName ? szName : "?", ARRAYSIZE( wszName ) - 1 );
			wszName[ARRAYSIZE( wszName ) - 1] = 0;
			pAnnotation->BeginEvent( wszName );
		}
	}
	virtual void EndPIXEvent()
	{
		if ( ID3DUserDefinedAnnotation *pAnnotation = Dx11_Annotation() )
		{
			pAnnotation->EndEvent();
		}
	}
	virtual void SetPIXMarker( unsigned long color, const char *szName )
	{
		if ( ID3DUserDefinedAnnotation *pAnnotation = Dx11_Annotation() )
		{
			wchar_t wszName[128];
			mbstowcs( wszName, szName ? szName : "?", ARRAYSIZE( wszName ) - 1 );
			wszName[ARRAYSIZE( wszName ) - 1] = 0;
			pAnnotation->SetMarker( wszName );
		}
	}

	// The queued render context sizes its CPU-side vertex copies through this
	// (CMatQueuedMesh::OnGetDynamicMesh) — a stub here breaks mat_queue_mode.
	virtual void ComputeVertexDescription( unsigned char* pBuffer, VertexFormat_t vertexFormat, MeshDesc_t& desc ) const
	{
		CVertexBufferBase::ComputeVertexDescription( pBuffer, vertexFormat, (VertexDesc_t &)desc );
	}

	// Flashlight depth shadows: clientshadowmgr gates InitDepthTextureShadows
	// on this (r_flashlightdepthtexture self-zeroes when it's false).
	virtual bool SupportsShadowDepthTextures() { return true; }

	virtual bool SupportsFetch4() { return false; }

	virtual int NeedsShaderSRGBConversion(void) const { return 0; }
	virtual bool UsesSRGBCorrectBlending() const { return false; }

	virtual bool HasFastVertexTextures() const { return false; }

	virtual void SetShadowDepthBiasFactors( float fShadowSlopeScaleDepthBias, float fShadowDepthBias )
	{
		// clientshadowmgr per-flashlight (mat_slopescaledepthbias_shadowmap /
		// mat_depthbias_shadowmap); feeds the SHADOW_BIAS raster states.
		StateDx11_SetShadowDepthBias( fShadowSlopeScaleDepthBias, fShadowDepthBias );
	}

	virtual void SetDisallowAccess( bool ) {}
	virtual void EnableShaderShaderMutex( bool ) {}
	virtual void ShaderLock() {}
	virtual void ShaderUnlock() {}

// ------------ New Vertex/Index Buffer interface ----------------------------
	void BindVertexBuffer( int streamID, IVertexBuffer *pVertexBuffer, int nOffsetInBytes, int nFirstVertex, int nVertexCount, VertexFormat_t fmt, int nRepetitions1 )
	{
	}
	void BindIndexBuffer( IIndexBuffer *pIndexBuffer, int nOffsetInBytes )
	{
	}
	void Draw( MaterialPrimitiveType_t primitiveType, int firstIndex, int numIndices )
	{
	}
// ------------ End ----------------------------

	virtual int  GetVertexBufferCompression( void ) const { return 0; };

	virtual bool ShouldWriteDepthToDestAlpha( void ) const { return false; };
	virtual bool SupportsHDRMode( HDRType_t nHDRMode ) const
	{
		// Integer HDR is the dx9-PC TF2 mode we restore; float HDR stays off.
		return nHDRMode == HDR_TYPE_NONE || nHDRMode == HDR_TYPE_INTEGER;
	};
	virtual bool IsDX10Card() const { return false; };

	void PushDeformation( const DeformationBase_t *pDeformation )
	{
	}

	virtual void PopDeformation( )
	{
	}

	int GetNumActiveDeformations( ) const
	{
		return 0;
	}

	// for shaders to set vertex shader constants. returns a packed state which can be used to set the dynamic combo
	int GetPackedDeformationInformation( int nMaskOfUnderstoodDeformations,
										 float *pConstantValuesOut,
										 int nBufferSize,
										 int nMaximumDeformations,
										 int *pNumDefsOut ) const
	{
		*pNumDefsOut = 0;
		return 0;
	}

	void SetStandardTextureHandle( StandardTextureId_t nId, ShaderAPITextureHandle_t hTexture )
	{
		if ( nId >= 0 && nId < TEXTURE_MAX_STD_TEXTURES )
			m_StdTextureHandles[nId] = hTexture;
	}

	// The stdshader dynamic blocks deliver nearly all per-pass state (texture
	// binds, constants) through here, not through direct IShaderAPI calls.
	virtual void ExecuteCommandBuffer( uint8 *pData );

	ShaderAPITextureHandle_t m_StdTextureHandles[TEXTURE_MAX_STD_TEXTURES];
	virtual bool GetHDREnabled( void ) const { return true; }
	virtual void SetHDREnabled( bool bEnable ) {}

	virtual void CopyRenderTargetToScratchTexture( ShaderAPITextureHandle_t srcRt, ShaderAPITextureHandle_t dstTex, Rect_t *pSrcRect = NULL, Rect_t *pDstRect = NULL ) 
	{
	}

	// Allows locking and unlocking of very specific surface types.
	virtual void LockRect( void** pOutBits, int* pOutPitch, ShaderAPITextureHandle_t texHandle, int mipmap, int x, int y, int w, int h, bool bWrite, bool bRead ) 
	{
	}

	virtual void UnlockRect( ShaderAPITextureHandle_t texHandle, int mipmap )
	{
	}

	virtual void TexLodClamp( int finest ) {}

	virtual void TexLodBias( float bias ) {}

	virtual void CopyTextureToTexture( ShaderAPITextureHandle_t srcTex, ShaderAPITextureHandle_t dstTex ) {}
	
	void PrintfVA( char *fmt, va_list vargs ) {}
	void Printf( const char *fmt, ... ) {}
	float Knob( char *knobname, float *setvalue = NULL ) { return 0.0f; };

private:
	enum
	{
		TRANSLUCENT = 0x1,
		ALPHATESTED = 0x2,
		VERTEX_AND_PIXEL_SHADERS = 0x4,
		DEPTHWRITE = 0x8,
	};

	CStubMeshDx11 m_Mesh;

	// cLightScale.xyz as last computed by SetToneMappingScaleLinear (dx9
	// GetToneMappingScaleLinear returns the COMPUTED vector, not the input)
	Vector m_vToneMappingScale{ 1.0f, 1.0f, 1.0f };

	// Flashlight pass state (dx9 m_FlashlightState): the shader DYNAMIC
	// blocks read these back and upload the constants through the mirrors
	FlashlightState_t m_FlashlightState;
	VMatrix m_FlashlightWorldToTexture;
	ITexture *m_pFlashlightDepthTexture = NULL;

	void EnableAlphaToCoverage() {} ;
	void DisableAlphaToCoverage() {} ;

	// Flashlight depth shadows: clientshadowmgr creates
	// _rt_ShadowDepthTexture_N with this format (texturedx11 maps the dx9
	// vendor depth formats to R24G8_TYPELESS + DSV + raw-depth SRV) and
	// _rt_ShadowDummy with the null format (no NULL-RT hack on dx11 — plain
	// color, its writes are masked off by the DepthWrite snapshot anyway).
	ImageFormat GetShadowDepthTextureFormat() { return IMAGE_FORMAT_NV_DST24; };
	ImageFormat GetNullTextureFormat() { return IMAGE_FORMAT_RGBA8888; };
};


//-----------------------------------------------------------------------------
// Class Factory
//-----------------------------------------------------------------------------

static CShaderAPIDx11 g_ShaderAPIDx11;
static CShaderShadowDx11 g_ShaderShadowDx11;

// FIXME: Remove; it's for backward compat with the materialsystem only for now
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderAPIDx11, IShaderAPI, 
									SHADERAPI_INTERFACE_VERSION, g_ShaderAPIDx11 )

EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderShadowDx11, IShaderShadow,
								SHADERSHADOW_INTERFACE_VERSION, g_ShaderShadowDx11 )

EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderAPIDx11, IDebugTextureInfo,
				DEBUG_TEXTURE_INFO_VERSION, g_ShaderAPIDx11 )

// NOTE: IMaterialSystemHardwareConfig is exposed by the real CHardwareConfig
// (../shaderapidx9/hardwareconfig.cpp, compiled into this module), and
// IShaderDevice/IShaderDeviceMgr by shaderdevicedx11.cpp.


//-----------------------------------------------------------------------------
// Accessors used by the device manager to wire up module globals (M1)
//-----------------------------------------------------------------------------
CShaderAPIBase *ShaderApiDx11_GetShaderAPI()
{
	return &g_ShaderAPIDx11;
}

IShaderShadow *ShaderApiDx11_GetShaderShadow()
{
	return &g_ShaderShadowDx11;
}

IMesh *ShaderApiDx11_GetStubMesh( bool bDynamic )
{
	return bDynamic ? &s_StubDynamicMesh : &s_StubMesh;
}

IVertexBuffer *ShaderApiDx11_GetStubVertexBuffer()
{
	return &s_StubDynamicMesh;
}

IIndexBuffer *ShaderApiDx11_GetStubIndexBuffer( bool bDynamic )
{
	return bDynamic ? &s_StubDynamicMesh : &s_StubMesh;
}



//-----------------------------------------------------------------------------
//
// The empty mesh...
//
//-----------------------------------------------------------------------------
CStubMeshDx11::CStubMeshDx11( bool bIsDynamic ) : m_bIsDynamic( bIsDynamic )
{
	m_pVertexMemory = new unsigned char[VERTEX_BUFFER_SIZE];
}

CStubMeshDx11::~CStubMeshDx11()
{
	delete[] m_pVertexMemory;
}

bool CStubMeshDx11::Lock( int nMaxIndexCount, bool bAppend, IndexDesc_t& desc )
{
	static int s_BogusIndex;
	desc.m_pIndices = (unsigned short*)&s_BogusIndex;
	desc.m_nIndexSize = 0;
	desc.m_nFirstIndex = 0;
	desc.m_nOffset = 0;
	return true;
}

void CStubMeshDx11::Unlock( int nWrittenIndexCount, IndexDesc_t& desc )
{
}

void CStubMeshDx11::ModifyBegin( bool bReadOnly, int nFirstIndex, int nIndexCount, IndexDesc_t& desc )
{
	Lock( nIndexCount, false, desc );
}

void CStubMeshDx11::ModifyEnd( IndexDesc_t& desc )
{
}

void CStubMeshDx11::Spew( int nIndexCount, const IndexDesc_t & desc )
{
}

void CStubMeshDx11::ValidateData( int nIndexCount, const IndexDesc_t &desc )
{
}

bool CStubMeshDx11::Lock( int nVertexCount, bool bAppend, VertexDesc_t &desc )
{
	// Who cares about the data? But EVERY desc pointer must be valid:
	// studiorender walks base+i*stride loops off these (e.g. the per-vertex
	// specular clear), and an unset field is stale stack garbage -> AV.
	desc.m_pPosition = (float*)m_pVertexMemory;
	desc.m_pNormal = (float*)m_pVertexMemory;
	desc.m_pColor = m_pVertexMemory;
	desc.m_pSpecular = m_pVertexMemory;

	int i;
	for ( i = 0; i < VERTEX_MAX_TEXTURE_COORDINATES; ++i)
	{
		desc.m_pTexCoord[i] = (float*)m_pVertexMemory;
	}

	desc.m_pBoneWeight = (float*)m_pVertexMemory;
	desc.m_pBoneMatrixIndex = (unsigned char*)m_pVertexMemory;
	desc.m_pTangentS = (float*)m_pVertexMemory;
	desc.m_pTangentT = (float*)m_pVertexMemory;
	desc.m_pWrinkle = (float*)m_pVertexMemory;
	desc.m_pUserData = (float*)m_pVertexMemory;
	desc.m_NumBoneWeights = 2;

	desc.m_VertexSize_Position = 0;
	desc.m_VertexSize_BoneWeight = 0;
	desc.m_VertexSize_BoneMatrixIndex = 0;
	desc.m_VertexSize_Normal = 0;
	desc.m_VertexSize_Color = 0;
	desc.m_VertexSize_Specular = 0;
	for( i=0; i < VERTEX_MAX_TEXTURE_COORDINATES; i++ )
	{
		desc.m_VertexSize_TexCoord[i] = 0;
	}
	desc.m_VertexSize_TangentS = 0;
	desc.m_VertexSize_TangentT = 0;
	desc.m_VertexSize_Wrinkle = 0;
	desc.m_VertexSize_UserData = 0;
	desc.m_ActualVertexSize = 0;	// Size of the vertices.. Some of the m_VertexSize_ elements above

	desc.m_nFirstVertex = 0;
	desc.m_nOffset = 0;
	desc.m_CompressionType = VERTEX_COMPRESSION_NONE;
	return true;
}

void CStubMeshDx11::Unlock( int nVertexCount, VertexDesc_t &desc )
{
}

void CStubMeshDx11::Spew( int nVertexCount, const VertexDesc_t &desc ) 
{
}

void CStubMeshDx11::ValidateData( int nVertexCount, const VertexDesc_t & desc )
{
}

void CStubMeshDx11::LockMesh( int numVerts, int numIndices, MeshDesc_t& desc )
{
	Lock( numVerts, false, *static_cast<VertexDesc_t*>( &desc ) );
	Lock( numIndices, false, *static_cast<IndexDesc_t*>( &desc ) );
}

void CStubMeshDx11::UnlockMesh( int numVerts, int numIndices, MeshDesc_t& desc )
{
}

void CStubMeshDx11::ModifyBeginEx( bool bReadOnly, int firstVertex, int numVerts, int firstIndex, int numIndices, MeshDesc_t& desc )
{
	Lock( numVerts, false, *static_cast<VertexDesc_t*>( &desc ) );
	Lock( numIndices, false, *static_cast<IndexDesc_t*>( &desc ) );
}

void CStubMeshDx11::ModifyBegin( int firstVertex, int numVerts, int firstIndex, int numIndices, MeshDesc_t& desc )
{
	ModifyBeginEx( false, firstVertex, numVerts, firstIndex, numIndices, desc );
}

void CStubMeshDx11::ModifyEnd( MeshDesc_t& desc )
{
}

// returns the # of vertices (static meshes only)
int CStubMeshDx11::VertexCount() const
{
	return 0;
}

// Sets the primitive type
void CStubMeshDx11::SetPrimitiveType( MaterialPrimitiveType_t type )
{
}

// Draws the entire mesh
void CStubMeshDx11::Draw( int firstIndex, int numIndices )
{
}

void CStubMeshDx11::Draw(CPrimList *pPrims, int nPrims)
{
}

// Copy verts and/or indices to a mesh builder. This only works for temp meshes!
void CStubMeshDx11::CopyToMeshBuilder( 
	int iStartVert,		// Which vertices to copy.
	int nVerts, 
	int iStartIndex,	// Which indices to copy.
	int nIndices, 
	int indexOffset,	// This is added to each index.
	CMeshBuilder &builder )
{
}

// Spews the mesh data
void CStubMeshDx11::Spew( int numVerts, int numIndices, const MeshDesc_t & desc )
{
}

void CStubMeshDx11::ValidateData( int numVerts, int numIndices, const MeshDesc_t & desc )
{
}

// gets the associated material
IMaterial* CStubMeshDx11::GetMaterial()
{
	// umm. this don't work none
	Assert(0);
	return 0;
}

//-----------------------------------------------------------------------------
// The shader shadow interface
//-----------------------------------------------------------------------------
CShaderShadowDx11::CShaderShadowDx11()
{
	m_IsTranslucent = false;
	m_IsAlphaTested = false;
	m_bIsDepthWriteEnabled = true;
	m_bUsesVertexAndPixelShaders = false;
}

CShaderShadowDx11::~CShaderShadowDx11()
{
}

// Sets the default *shadow* state
void CShaderShadowDx11::SetDefaultState()
{
	m_IsTranslucent = false;
	m_IsAlphaTested = false;
	m_bIsDepthWriteEnabled = true;
	m_bUsesVertexAndPixelShaders = false;
	StateDx11_ShadowSetDefault();
}

// Methods related to depth buffering
void CShaderShadowDx11::DepthFunc( ShaderDepthFunc_t depthFunc )
{
	StateDx11_ShadowDepthFunc( depthFunc );
}

void CShaderShadowDx11::EnableDepthWrites( bool bEnable )
{
	m_bIsDepthWriteEnabled = bEnable;
	StateDx11_ShadowEnableDepthWrites( bEnable );
}

void CShaderShadowDx11::EnableDepthTest( bool bEnable )
{
	StateDx11_ShadowEnableDepthTest( bEnable );
}

void CShaderShadowDx11::EnablePolyOffset( PolygonOffsetMode_t nOffsetMode )
{
	// Decals: coplanar surface copies z-fight without the bias.
	// SHADOW_BIAS: the DepthWrite caster snapshots — slope-scaled bias keeps
	// receivers acne-free (factors via SetShadowDepthBiasFactors).
	int nMode = 0;
	if ( nOffsetMode == SHADER_POLYOFFSET_DECAL )
		nMode = 1;
	else if ( nOffsetMode == SHADER_POLYOFFSET_SHADOW_BIAS )
		nMode = 2;
	StateDx11_ShadowPolyOffset( nMode );
}

// Suppresses/activates color writing
void CShaderShadowDx11::EnableColorWrites( bool bEnable )
{
	StateDx11_ShadowEnableColorWrites( bEnable );
}

// Suppresses/activates alpha writing
void CShaderShadowDx11::EnableAlphaWrites( bool bEnable )
{
	StateDx11_ShadowEnableAlphaWrites( bEnable );
}

// Methods related to alpha blending
void CShaderShadowDx11::EnableBlending( bool bEnable )
{
	m_IsTranslucent = bEnable;
	StateDx11_ShadowEnableBlending( bEnable );
}

void CShaderShadowDx11::BlendFunc( ShaderBlendFactor_t srcFactor, ShaderBlendFactor_t dstFactor )
{
	StateDx11_ShadowBlendFunc( srcFactor, dstFactor );
}

// A simpler method of dealing with alpha modulation
void CShaderShadowDx11::EnableAlphaPipe( bool bEnable )
{
}

void CShaderShadowDx11::EnableConstantAlpha( bool bEnable )
{
}

void CShaderShadowDx11::EnableVertexAlpha( bool bEnable )
{
}

void CShaderShadowDx11::EnableTextureAlpha( TextureStage_t stage, bool bEnable )
{
}


// Alpha testing
void CShaderShadowDx11::EnableAlphaTest( bool bEnable )
{
	m_IsAlphaTested = bEnable;
	StateDx11_ShadowEnableAlphaTest( bEnable );
}

void CShaderShadowDx11::AlphaFunc( ShaderAlphaFunc_t alphaFunc, float alphaRef /* [0-1] */ )
{
	StateDx11_ShadowAlphaFunc( alphaFunc, alphaRef );
}


// Wireframe/filled polygons
void CShaderShadowDx11::PolyMode( ShaderPolyModeFace_t face, ShaderPolyMode_t polyMode )
{
	StateDx11_ShadowPolyMode( polyMode == SHADER_POLYMODE_LINE );
}


// Back face culling
void CShaderShadowDx11::EnableCulling( bool bEnable )
{
	StateDx11_ShadowEnableCulling( bEnable );
}


// Alpha to coverage
void CShaderShadowDx11::EnableAlphaToCoverage( bool bEnable )
{
}


// constant color + transparency
void CShaderShadowDx11::EnableConstantColor( bool bEnable )
{
}

// Indicates the vertex format for use with a vertex shader
// The flags to pass in here come from the VertexFormatFlags_t enum
// If pTexCoordDimensions is *not* specified, we assume all coordinates
// are 2-dimensional
void CShaderShadowDx11::VertexShaderVertexFormat( unsigned int nFlags,
												   int nTexCoordCount,
												   int* pTexCoordDimensions,
												   int nUserDataSize )
{
	// Meshes specify bone weights/indices themselves, not the shader
	nFlags &= ~VERTEX_BONE_INDEX;

	VertexFormat_t fmt = nFlags | VERTEX_USERDATA_SIZE( nUserDataSize );
	for ( int i = 0; i < nTexCoordCount; ++i )
	{
		fmt |= VERTEX_TEXCOORD_SIZE( i, pTexCoordDimensions ? pTexCoordDimensions[i] : 2 );
	}
	StateDx11_ShadowVertexFormat( fmt );
}

// Indicates we're going to light the model
void CShaderShadowDx11::EnableLighting( bool bEnable )
{
}

void CShaderShadowDx11::EnableSpecular( bool bEnable )
{
}

// Activate/deactivate skinning
void CShaderShadowDx11::EnableVertexBlend( bool bEnable )
{
}

// per texture unit stuff
void CShaderShadowDx11::OverbrightValue( TextureStage_t stage, float value )
{
}

void CShaderShadowDx11::EnableTexture( Sampler_t stage, bool bEnable )
{
	StateDx11_ShadowEnableTexture( stage, bEnable );
}

void CShaderShadowDx11::EnableCustomPixelPipe( bool bEnable )
{
}

void CShaderShadowDx11::CustomTextureStages( int stageCount )
{
}

void CShaderShadowDx11::CustomTextureOperation( TextureStage_t stage, ShaderTexChannel_t channel, 
	ShaderTexOp_t op, ShaderTexArg_t arg1, ShaderTexArg_t arg2 )
{
}

void CShaderShadowDx11::EnableTexGen( TextureStage_t stage, bool bEnable )
{
}

void CShaderShadowDx11::TexGen( TextureStage_t stage, ShaderTexGenParam_t param )
{
}

// Sets the vertex and pixel shaders
void CShaderShadowDx11::SetVertexShader( const char *pShaderName, int vshIndex )
{
	m_bUsesVertexAndPixelShaders = ( pShaderName != NULL );
}

void CShaderShadowDx11::EnableBlendingSeparateAlpha( bool bEnable )
{
	StateDx11_ShadowEnableBlendingSeparateAlpha( bEnable );
}
void CShaderShadowDx11::SetPixelShader( const char *pShaderName, int pshIndex )
{
	m_bUsesVertexAndPixelShaders = ( pShaderName != NULL );
}

void CShaderShadowDx11::BlendFuncSeparateAlpha( ShaderBlendFactor_t srcFactor, ShaderBlendFactor_t dstFactor )
{
	StateDx11_ShadowBlendFuncSeparateAlpha( srcFactor, dstFactor );
}
// indicates what per-vertex data we're providing
void CShaderShadowDx11::DrawFlags( unsigned int drawFlags )
{
}



//-----------------------------------------------------------------------------
//
// Shader API Empty
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Constructor, destructor
//-----------------------------------------------------------------------------

CShaderAPIDx11::CShaderAPIDx11()  : m_Mesh( false )
{
	for ( int i = 0; i < TEXTURE_MAX_STD_TEXTURES; ++i )
		m_StdTextureHandles[i] = INVALID_SHADERAPI_TEXTURE_HANDLE;
}

//-----------------------------------------------------------------------------
// Command-buffer interpreter (mirrors CShaderAPIDx8::ExecuteCommandBuffer).
// Layouts come from stdshaders/commandbuilder.h; that builder only emits the
// commands handled below. An unknown command has unknown length, so bail.
//-----------------------------------------------------------------------------
template<class T> static FORCEINLINE T CmdData( uint8 const *pData )
{
	return *( reinterpret_cast<T const *>( pData ) );
}

void CShaderAPIDx11::ExecuteCommandBuffer( uint8 *pCmdBuf )
{
	for ( ;; )
	{
		int nCmd = CmdData<int>( pCmdBuf );
		switch ( nCmd )
		{
		case CBCMD_END:
			return;

		case CBCMD_JUMP:
			pCmdBuf = CmdData<uint8 *>( pCmdBuf + sizeof( int ) );
			break;

		case CBCMD_JSR:
			ExecuteCommandBuffer( CmdData<uint8 *>( pCmdBuf + sizeof( int ) ) );
			pCmdBuf += sizeof( int ) + sizeof( uint8 * );
			break;

		case CBCMD_SET_PIXEL_SHADER_FLOAT_CONST:
		{
			int nStartConst = CmdData<int>( pCmdBuf + sizeof( int ) );
			int nNumConsts = CmdData<int>( pCmdBuf + 2 * sizeof( int ) );
			const float *pValues = reinterpret_cast<const float *>( pCmdBuf + 3 * sizeof( int ) );
			pCmdBuf += nNumConsts * 4 * sizeof( float ) + 3 * sizeof( int );
			SetPixelShaderConstant( nStartConst, pValues, nNumConsts );
			break;
		}

		case CBCMD_SET_VERTEX_SHADER_FLOAT_CONST:
		{
			int nStartConst = CmdData<int>( pCmdBuf + sizeof( int ) );
			int nNumConsts = CmdData<int>( pCmdBuf + 2 * sizeof( int ) );
			const float *pValues = reinterpret_cast<const float *>( pCmdBuf + 3 * sizeof( int ) );
			pCmdBuf += nNumConsts * 4 * sizeof( float ) + 3 * sizeof( int );
			SetVertexShaderConstant( nStartConst, pValues, nNumConsts );
			break;
		}

		case CBCMD_SETPIXELSHADERFOGPARAMS:
		case CBCMD_STORE_EYE_POS_IN_PSCONST:
		case CBCMD_COMMITPIXELSHADERLIGHTING:
		case CBCMD_SETPIXELSHADERSTATEAMBIENTLIGHTCUBE:
			pCmdBuf += 2 * sizeof( int );	// lighting/fog constants: M4+ work
			break;

		case CBCMD_SETAMBIENTCUBEDYNAMICSTATEVERTEXSHADER:
			pCmdBuf += sizeof( int );
			break;

		case CBCMD_SET_DEPTH_FEATHERING_CONST:
			pCmdBuf += 2 * sizeof( int ) + sizeof( float );
			break;

		case CBCMD_BIND_STANDARD_TEXTURE:
		{
			int nSampler = CmdData<int>( pCmdBuf + sizeof( int ) );
			int nTextureID = CmdData<int>( pCmdBuf + 2 * sizeof( int ) );
			pCmdBuf += 3 * sizeof( int );
			if ( nTextureID >= 0 && nTextureID < TEXTURE_MAX_STD_TEXTURES &&
				 m_StdTextureHandles[nTextureID] != INVALID_SHADERAPI_TEXTURE_HANDLE )
			{
				StateDx11_BindTexture( nSampler, m_StdTextureHandles[nTextureID] );
			}
			else
			{
				// materialsystem owns the real standard textures (lightmap
				// pages bind through here); it calls back BindTexture with a
				// concrete handle. Same routing as CShaderAPIDx8.
				ShaderUtil()->BindStandardTexture( (Sampler_t)nSampler, (StandardTextureId_t)nTextureID );
			}
			break;
		}

		case CBCMD_BIND_SHADERAPI_TEXTURE_HANDLE:
		{
			int nSampler = CmdData<int>( pCmdBuf + sizeof( int ) );
			ShaderAPITextureHandle_t hTexture = CmdData<ShaderAPITextureHandle_t>( pCmdBuf + 2 * sizeof( int ) );
			pCmdBuf += 2 * sizeof( int ) + sizeof( ShaderAPITextureHandle_t );
			StateDx11_BindTexture( nSampler, hTexture );
			break;
		}

		case CBCMD_SET_PSHINDEX:
		case CBCMD_SET_VSHINDEX:
			pCmdBuf += 2 * sizeof( int );	// combo indices target the dx9 shader sets
			break;

		default:
		{
			static bool s_bWarnedCmd = false;
			if ( !s_bWarnedCmd )
			{
				s_bWarnedCmd = true;
				Warning( "shaderapidx11: unknown command-buffer cmd %d; stream abandoned\n", nCmd );
			}
			return;
		}
		}
	}
}

CShaderAPIDx11::~CShaderAPIDx11()
{
}


bool CShaderAPIDx11::DoRenderTargetsNeedSeparateDepthBuffer() const
{
	return false;
}

// Can we download textures?
bool CShaderAPIDx11::CanDownloadTextures() const
{
	return g_pShaderDeviceDx11->IsUsingGraphics();
}

// Used to clear the transition table when we know it's become invalid.
void CShaderAPIDx11::ClearSnapshots()
{
}

// Members of IMaterialSystemHardwareConfig
bool CShaderAPIDx11::HasDestAlphaBuffer() const
{
	return false;
}

bool CShaderAPIDx11::HasStencilBuffer() const
{
	return false;
}

int CShaderAPIDx11::MaxViewports() const
{
	return 1;
}

int CShaderAPIDx11::GetShadowFilterMode() const
{
	return 0;
}

int CShaderAPIDx11::StencilBufferBits() const
{
	return 0;
}

int	 CShaderAPIDx11::GetFrameBufferColorDepth() const
{
	return 0;
}

int  CShaderAPIDx11::GetSamplerCount() const
{
	if ((ShaderUtil()->GetConfig().dxSupportLevel > 0) &&
	    (ShaderUtil()->GetConfig().dxSupportLevel < 60))
		return 1;
	if (( ShaderUtil()->GetConfig().dxSupportLevel >= 60 ) && ( ShaderUtil()->GetConfig().dxSupportLevel < 80 ))
		return 2;
	return 4;
}

bool CShaderAPIDx11::HasSetDeviceGammaRamp() const
{
	return false;
}

bool CShaderAPIDx11::SupportsCompressedTextures() const
{
	return false;
}

VertexCompressionType_t CShaderAPIDx11::SupportsCompressedVertices() const
{
	return VERTEX_COMPRESSION_NONE;
}

bool CShaderAPIDx11::SupportsVertexAndPixelShaders() const
{
	if ((ShaderUtil()->GetConfig().dxSupportLevel > 0) &&
	    (ShaderUtil()->GetConfig().dxSupportLevel < 80))
		return false;

	return true;
}

bool CShaderAPIDx11::SupportsPixelShaders_1_4() const
{
	if ((ShaderUtil()->GetConfig().dxSupportLevel > 0) &&
	    (ShaderUtil()->GetConfig().dxSupportLevel < 81))
		return false;

	return true;
}

bool CShaderAPIDx11::SupportsPixelShaders_2_0() const
{
	if ((ShaderUtil()->GetConfig().dxSupportLevel > 0) &&
	    (ShaderUtil()->GetConfig().dxSupportLevel < 90))
		return false;

	return true;
}

bool CShaderAPIDx11::SupportsPixelShaders_2_b() const
{
	if ((ShaderUtil()->GetConfig().dxSupportLevel > 0) &&
	    (ShaderUtil()->GetConfig().dxSupportLevel < 90))
		return false;

	return true;
}

bool CShaderAPIDx11::ActuallySupportsPixelShaders_2_b() const
{
	return true;
}

bool CShaderAPIDx11::SupportsShaderModel_3_0() const
{
	if ((ShaderUtil()->GetConfig().dxSupportLevel > 0) &&
		(ShaderUtil()->GetConfig().dxSupportLevel < 95))
		return false;

	return true;
}

bool CShaderAPIDx11::SupportsStaticControlFlow() const
{
	if ( IsOpenGL() )
		return false;

	return SupportsVertexShaders_2_0();
}

bool CShaderAPIDx11::SupportsVertexShaders_2_0() const
{
	if ((ShaderUtil()->GetConfig().dxSupportLevel > 0) &&
	    (ShaderUtil()->GetConfig().dxSupportLevel < 90))
		return false;

	return true;
}

int  CShaderAPIDx11::MaximumAnisotropicLevel() const
{
	return 0;
}

void CShaderAPIDx11::SetAnisotropicLevel( int nAnisotropyLevel )
{
}

int  CShaderAPIDx11::MaxTextureWidth() const
{
	// Should be big enough to cover all cases
	return 16384;
}

int  CShaderAPIDx11::MaxTextureHeight() const
{
	// Should be big enough to cover all cases
	return 16384;
}

int  CShaderAPIDx11::MaxTextureAspectRatio() const
{
	// Should be big enough to cover all cases
	return 16384;
}


int	 CShaderAPIDx11::TextureMemorySize() const
{
	// fake it
	return 64 * 1024 * 1024;
}

int  CShaderAPIDx11::GetDXSupportLevel() const 
{ 
	return 90; 
}

bool CShaderAPIDx11::SupportsOverbright() const
{
	return false;
}

bool CShaderAPIDx11::SupportsCubeMaps() const
{
	if ((ShaderUtil()->GetConfig().dxSupportLevel > 0) &&
	    (ShaderUtil()->GetConfig().dxSupportLevel < 70))
		return false;

	return true;
}

bool CShaderAPIDx11::SupportsNonPow2Textures() const
{
	return true;
}

bool CShaderAPIDx11::SupportsMipmappedCubemaps() const
{
	if ((ShaderUtil()->GetConfig().dxSupportLevel > 0) &&
	    (ShaderUtil()->GetConfig().dxSupportLevel < 70))
		return false;

	return true;
}

int  CShaderAPIDx11::GetTextureStageCount() const
{
	return 4;
}

int	 CShaderAPIDx11::NumVertexShaderConstants() const
{
	return 128;
}

int	 CShaderAPIDx11::NumBooleanVertexShaderConstants() const
{
	return 0;
}

int	 CShaderAPIDx11::NumIntegerVertexShaderConstants() const
{
	return 0;
}

int	 CShaderAPIDx11::NumPixelShaderConstants() const
{
	return 8;
}

int	 CShaderAPIDx11::MaxNumLights() const
{
	return 4;
}

bool CShaderAPIDx11::SupportsSpheremapping() const
{
	return false;
}


// This is the max dx support level supported by the card
int	CShaderAPIDx11::GetMaxDXSupportLevel() const
{
	return 90;
}

bool CShaderAPIDx11::SupportsHardwareLighting() const
{
	if ((ShaderUtil()->GetConfig().dxSupportLevel > 0) &&
	    (ShaderUtil()->GetConfig().dxSupportLevel < 70))
		return false;

	return true;
}

int	 CShaderAPIDx11::MaxBlendMatrices() const
{
	if ((ShaderUtil()->GetConfig().dxSupportLevel > 0) &&
	    (ShaderUtil()->GetConfig().dxSupportLevel < 70))
	{
		return 1;
	}

	return 0;
}

int	 CShaderAPIDx11::MaxBlendMatrixIndices() const
{
	if ((ShaderUtil()->GetConfig().dxSupportLevel > 0) &&
	    (ShaderUtil()->GetConfig().dxSupportLevel < 70))
	{
		return 1;
	}

	return 0;
}

int	 CShaderAPIDx11::MaxVertexShaderBlendMatrices() const
{
	return 0;
}

int	CShaderAPIDx11::MaxUserClipPlanes() const
{
	return 0;
}

bool CShaderAPIDx11::SpecifiesFogColorInLinearSpace() const
{
	return false;
}

bool CShaderAPIDx11::SupportsSRGB() const
{
	return false;
}

bool CShaderAPIDx11::FakeSRGBWrite() const
{
	return false;
}

bool CShaderAPIDx11::CanDoSRGBReadFromRTs() const
{
	return true;
}

bool CShaderAPIDx11::SupportsGLMixedSizeTargets() const
{
	return false;
}

const char *CShaderAPIDx11::GetHWSpecificShaderDLLName() const
{
	return 0;
}

// Sets the default *dynamic* state
void CShaderAPIDx11::SetDefaultState()
{
}


// Returns the snapshot id for the shader state
StateSnapshot_t	 CShaderAPIDx11::TakeSnapshot( )
{
	return StateDx11_TakeSnapshot();
}

// Returns true if the state snapshot is transparent
bool CShaderAPIDx11::IsTranslucent( StateSnapshot_t id ) const
{
	return StateDx11_IsTranslucent( id );
}

bool CShaderAPIDx11::IsAlphaTested( StateSnapshot_t id ) const
{
	return StateDx11_IsAlphaTested( id );
}

bool CShaderAPIDx11::IsDepthWriteEnabled( StateSnapshot_t id ) const
{
	return StateDx11_IsDepthWriteEnabled( id );
}

bool CShaderAPIDx11::UsesVertexAndPixelShaders( StateSnapshot_t id ) const
{
	return true;
}

// Gets the vertex format for a set of snapshot ids
VertexFormat_t CShaderAPIDx11::ComputeVertexFormat( int numSnapshots, StateSnapshot_t* pIds ) const
{
	return StateDx11_ComputeVertexFormat( numSnapshots, pIds );
}

// Gets the vertex format for a set of snapshot ids
VertexFormat_t CShaderAPIDx11::ComputeVertexUsage( int numSnapshots, StateSnapshot_t* pIds ) const
{
	return StateDx11_ComputeVertexFormat( numSnapshots, pIds );
}

// Uses a state snapshot
void CShaderAPIDx11::UseSnapshot( StateSnapshot_t snapshot )
{
	StateDx11_UseSnapshot( snapshot );
}

// Sets the color to modulate by
void CShaderAPIDx11::Color3f( float r, float g, float b )
{
}

void CShaderAPIDx11::Color3fv( float const* pColor )
{
}

void CShaderAPIDx11::Color4f( float r, float g, float b, float a )
{
}

void CShaderAPIDx11::Color4fv( float const* pColor )
{
}

// Faster versions of color
void CShaderAPIDx11::Color3ub( unsigned char r, unsigned char g, unsigned char b )
{
}

void CShaderAPIDx11::Color3ubv( unsigned char const* rgb )
{
}

void CShaderAPIDx11::Color4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
}

void CShaderAPIDx11::Color4ubv( unsigned char const* rgba )
{
}

// The shade mode
void CShaderAPIDx11::ShadeMode( ShaderShadeMode_t mode )
{
}

// Binds a particular material to render with
void CShaderAPIDx11::Bind( IMaterial* pMaterial )
{
	MeshDx11_BindMaterial( pMaterial );
}

// Cull mode
void CShaderAPIDx11::CullMode( MaterialCullMode_t cullMode )
{
	StateDx11_CullMode( cullMode );
}

void CShaderAPIDx11::ForceDepthFuncEquals( bool bEnable )
{
}

// Forces Z buffering on or off
void CShaderAPIDx11::OverrideDepthEnable( bool bEnable, bool bDepthEnable )
{
}

void CShaderAPIDx11::OverrideAlphaWriteEnable( bool bOverrideEnable, bool bAlphaWriteEnable )
{
}

void CShaderAPIDx11::OverrideColorWriteEnable( bool bOverrideEnable, bool bColorWriteEnable )
{
}

//legacy fast clipping linkage
void CShaderAPIDx11::SetHeightClipZ( float z )
{
}

void CShaderAPIDx11::SetHeightClipMode( enum MaterialHeightClipMode_t heightClipMode )
{
}

// Sets the lights
void CShaderAPIDx11::SetLight( int lightNum, const LightDesc_t& desc )
{
	StateDx11_SetLight( lightNum, desc );
}

// Sets lighting origin for the current model
void CShaderAPIDx11::SetLightingOrigin( Vector vLightingOrigin )
{
	// Used to point-ify directional lights in the PS light array (dx9 parity).
	StateDx11_SetLightingOrigin( vLightingOrigin.Base() );
}

void CShaderAPIDx11::SetAmbientLight( float r, float g, float b )
{
}

void CShaderAPIDx11::SetAmbientLightCube( Vector4D cube[6] )
{
	StateDx11_SetAmbientLightCube( cube ? &cube[0].x : NULL );
}

// Get lights
int CShaderAPIDx11::GetMaxLights( void ) const
{
	return 0;
}

const LightDesc_t& CShaderAPIDx11::GetLight( int lightNum ) const
{
	static LightDesc_t blah;
	return blah;
}

// Render state for the ambient light cube (vertex shaders)
void CShaderAPIDx11::SetVertexShaderStateAmbientLightCube()
{
}

void CShaderAPIDx11::SetSkinningMatrices()
{
}

// Lightmap texture binding
void CShaderAPIDx11::BindLightmap( TextureStage_t stage )
{
}

void CShaderAPIDx11::BindBumpLightmap( TextureStage_t stage )
{
}

void CShaderAPIDx11::BindFullbrightLightmap( TextureStage_t stage )
{
}

void CShaderAPIDx11::BindWhite( TextureStage_t stage )
{
}

void CShaderAPIDx11::BindBlack( TextureStage_t stage )
{
}

void CShaderAPIDx11::BindGrey( TextureStage_t stage )
{
}

// Gets the lightmap dimensions
void CShaderAPIDx11::GetLightmapDimensions( int *w, int *h )
{
	g_pShaderUtil->GetLightmapDimensions( w, h );
}

// Special system flat normal map binding.
void CShaderAPIDx11::BindFlatNormalMap( TextureStage_t stage )
{
}

void CShaderAPIDx11::BindNormalizationCubeMap( TextureStage_t stage )
{
}

void CShaderAPIDx11::BindSignedNormalizationCubeMap( TextureStage_t stage )
{
}

void CShaderAPIDx11::BindFBTexture( TextureStage_t stage, int textureIndex )
{
}

// Flushes any primitives that are buffered
void CShaderAPIDx11::FlushBufferedPrimitives()
{
}

// Gets the dynamic mesh; note that you've got to render the mesh
// before calling this function a second time. Clients should *not*
// call DestroyStaticMesh on the mesh returned by this call.
IMesh* CShaderAPIDx11::GetDynamicMesh( IMaterial* pMaterial, int nHWSkinBoneCount, bool buffered, IMesh* pVertexOverride, IMesh* pIndexOverride )
{
	if ( pVertexOverride && !pIndexOverride )
	{
		// World batches: dynamic indices over a static vertex buffer
		IMesh *pMesh = MeshDx11_GetDynamicWithVertexOverride( pVertexOverride );
		if ( pMesh )
			return pMesh;
	}
	if ( !pVertexOverride && pIndexOverride )
	{
		// Old-format studio flex/SW-skin/eyeball draws: fresh vertices drawn
		// through the group's own static strip index buffer
		IMesh *pMesh = MeshDx11_GetDynamicWithIndexOverride( pMaterial, 0, pIndexOverride );
		if ( pMesh )
			return pMesh;
	}
	if ( pVertexOverride && pIndexOverride )
	{
		// BindBatch: re-bind another static mesh against the index window the
		// engine just built into the dynamic mesh (passed as pIndexOverride)
		IMesh *pMesh = MeshDx11_BindBatch( pVertexOverride, pIndexOverride );
		if ( pMesh )
			return pMesh;
	}
	if ( pVertexOverride || pIndexOverride )
		return &m_Mesh;
	return MeshDx11_GetDynamic( pMaterial, 0 );
}

IMesh* CShaderAPIDx11::GetDynamicMeshEx( IMaterial* pMaterial, VertexFormat_t fmt, int nHWSkinBoneCount, bool buffered, IMesh* pVertexOverride, IMesh* pIndexOverride )
{
	if ( pVertexOverride && !pIndexOverride )
	{
		IMesh *pMesh = MeshDx11_GetDynamicWithVertexOverride( pVertexOverride );
		if ( pMesh )
			return pMesh;
	}
	if ( !pVertexOverride && pIndexOverride )
	{
		IMesh *pMesh = MeshDx11_GetDynamicWithIndexOverride( pMaterial, fmt, pIndexOverride );
		if ( pMesh )
			return pMesh;
	}
	if ( pVertexOverride && pIndexOverride )
	{
		IMesh *pMesh = MeshDx11_BindBatch( pVertexOverride, pIndexOverride );
		if ( pMesh )
			return pMesh;
	}
	if ( pVertexOverride || pIndexOverride )
		return &m_Mesh;
	return MeshDx11_GetDynamic( pMaterial, fmt );
}

IMesh* CShaderAPIDx11::GetFlexMesh()
{
	// Real flex mesh (dedicated ring, dx9 stream-2 layout): studiorender
	// CPU-morphs facial deltas into it, then SetFlexMesh attaches it to the
	// studio mesh for the slot-3 bind.
	return MeshDx11_GetFlexMesh();
}

// Begins a rendering pass that uses a state snapshot
void CShaderAPIDx11::BeginPass( StateSnapshot_t snapshot  )
{
	// The material pass loop (CShaderSystem::DrawElements) hands the per-pass
	// snapshot through here, not through UseSnapshot.
	StateDx11_UseSnapshot( snapshot );
}

// Renders a single pass of a material
void CShaderAPIDx11::RenderPass( int nPass, int nPassCount )
{
	MeshDx11_RenderPass();
}

// stuff related to matrix stacks
void CShaderAPIDx11::MatrixMode( MaterialMatrixMode_t matrixMode )
{
	StateDx11_MatrixMode( matrixMode );
}

void CShaderAPIDx11::PushMatrix()
{
	StateDx11_PushMatrix();
}

void CShaderAPIDx11::PopMatrix()
{
	StateDx11_PopMatrix();
}

void CShaderAPIDx11::LoadMatrix( float *m )
{
	StateDx11_LoadMatrix( m );
}

void CShaderAPIDx11::MultMatrix( float *m )
{
	StateDx11_MultMatrix( m );
}

void CShaderAPIDx11::MultMatrixLocal( float *m )
{
	StateDx11_MultMatrixLocal( m );
}

void CShaderAPIDx11::GetMatrix( MaterialMatrixMode_t matrixMode, float *dst )
{
	StateDx11_GetMatrix( matrixMode, dst );
}

void CShaderAPIDx11::LoadIdentity( void )
{
	StateDx11_LoadIdentity();
}

void CShaderAPIDx11::LoadCameraToWorld( void )
{
	StateDx11_LoadIdentity();
}

void CShaderAPIDx11::Ortho( double left, double top, double right, double bottom, double zNear, double zFar )
{
	StateDx11_Ortho( left, top, right, bottom, zNear, zFar );
}

void CShaderAPIDx11::PerspectiveX( double fovx, double aspect, double zNear, double zFar )
{
	StateDx11_PerspectiveX( fovx, aspect, zNear, zFar );
}

void CShaderAPIDx11::PerspectiveOffCenterX( double fovx, double aspect, double zNear, double zFar, double bottom, double top, double left, double right )
{
	StateDx11_PerspectiveOffCenterX( fovx, aspect, zNear, zFar, bottom, top, left, right );
}

void CShaderAPIDx11::PickMatrix( int x, int y, int width, int height )
{
}

void CShaderAPIDx11::Rotate( float angle, float x, float y, float z )
{
	StateDx11_Rotate( angle, x, y, z );
}

void CShaderAPIDx11::Translate( float x, float y, float z )
{
	StateDx11_Translate( x, y, z );
}

void CShaderAPIDx11::Scale( float x, float y, float z )
{
	StateDx11_Scale( x, y, z );
}

void CShaderAPIDx11::ScaleXY( float x, float y )
{
	StateDx11_Scale( x, y, 1.0f );
}

// Fog methods (state lives at the top of the file; the in-class
// SetPixelShaderFogParams reads it too).
void CShaderAPIDx11::FogMode( MaterialFogMode_t fogMode )
{
	s_nSceneFogMode = fogMode;
	UpdatePixelFogState();
}

void CShaderAPIDx11::FogStart( float fStart )
{
	s_flFogStart = fStart;
	UpdatePixelFogState();
}

void CShaderAPIDx11::FogEnd( float fEnd )
{
	s_flFogEnd = fEnd;
	UpdatePixelFogState();
}

void CShaderAPIDx11::SetFogZ( float fogZ )
{
	s_flFogZ = fogZ;
	UpdatePixelFogState();
}

void CShaderAPIDx11::FogMaxDensity( float flMaxDensity )
{
	s_flFogMaxDensity = flMaxDensity;
	UpdatePixelFogState();
}

void CShaderAPIDx11::GetFogDistances( float *fStart, float *fEnd, float *fFogZ )
{
	if ( fStart ) *fStart = s_flFogStart;
	if ( fEnd ) *fEnd = s_flFogEnd;
	if ( fFogZ ) *fFogZ = s_flFogZ;
}


void CShaderAPIDx11::SceneFogColor3ub( unsigned char r, unsigned char g, unsigned char b )
{
	s_SceneFogColor[0] = r;
	s_SceneFogColor[1] = g;
	s_SceneFogColor[2] = b;
	UpdatePixelFogState();
}


void CShaderAPIDx11::SceneFogMode( MaterialFogMode_t fogMode )
{
	s_nSceneFogMode = fogMode;
	UpdatePixelFogState();
}

void CShaderAPIDx11::GetSceneFogColor( unsigned char *rgb )
{
	if ( rgb )
	{
		rgb[0] = s_SceneFogColor[0];
		rgb[1] = s_SceneFogColor[1];
		rgb[2] = s_SceneFogColor[2];
	}
}

MaterialFogMode_t CShaderAPIDx11::GetSceneFogMode( )
{
	return s_nSceneFogMode;
}

int CShaderAPIDx11::GetPixelFogCombo( )
{
	// dx8: PIXELFOGTYPE combos are the fog mode shifted down one (NONE is
	// emulated with rigged range-fog params)
	if ( s_nSceneFogMode != MATERIAL_FOG_NONE )
		return s_nSceneFogMode - 1;
	return 0;
}

void CShaderAPIDx11::FogColor3f( float r, float g, float b )
{
}

void CShaderAPIDx11::FogColor3fv( float const* rgb )
{
}

void CShaderAPIDx11::FogColor3ub( unsigned char r, unsigned char g, unsigned char b )
{
}

void CShaderAPIDx11::FogColor3ubv( unsigned char const* rgb )
{
}

void CShaderAPIDx11::SetViewports( int nCount, const ShaderViewport_t* pViewports )
{
	StateDx11_SetViewports( nCount, pViewports );
}

int CShaderAPIDx11::GetViewports( ShaderViewport_t* pViewports, int nMax ) const
{
	return StateDx11_GetViewports( pViewports, nMax );
}

// Sets the vertex and pixel shaders
void CShaderAPIDx11::SetVertexShaderIndex( int vshIndex )
{
}

void CShaderAPIDx11::SetPixelShaderIndex( int pshIndex )
{
}

// Sets the constant registers for vertex and pixel shaders
void CShaderAPIDx11::SetVertexShaderConstant( int var, float const* pVec, int numConst, bool bForce )
{
	// dx9 VS register mirror (c0..c63) — the family dynamic blocks deliver
	// texture transforms here (SetVertexShaderTextureTransform → c48+).
	StateDx11_SetVSConstants( var, pVec, numConst );
}

void CShaderAPIDx11::SetBooleanVertexShaderConstant( int var, BOOL const* pVec, int numConst, bool bForce )
{
}

void CShaderAPIDx11::SetIntegerVertexShaderConstant( int var, int const* pVec, int numConst, bool bForce )
{
}

void CShaderAPIDx11::SetPixelShaderConstant( int var, float const* pVec, int numConst, bool bForce )
{
	// Full dx9 PS register mirror (c0..c31) for the per-pixel shading perms;
	// also feeds the c1 modulation latch (team-colored HUD panels etc.).
	StateDx11_SetPSConstants( var, pVec, numConst );
}

void CShaderAPIDx11::SetBooleanPixelShaderConstant( int var, BOOL const* pVec, int numBools, bool bForce )
{
}

void CShaderAPIDx11::SetIntegerPixelShaderConstant( int var, int const* pVec, int numIntVecs, bool bForce )
{
}

void CShaderAPIDx11::InvalidateDelayedShaderConstants( void )
{
}

float CShaderAPIDx11::GammaToLinear_HardwareSpecific( float fGamma ) const
{
	return 0.0f;
}

float CShaderAPIDx11::LinearToGamma_HardwareSpecific( float fLinear ) const
{
	return 0.0f;
}

void CShaderAPIDx11::SetLinearToGammaConversionTextures( ShaderAPITextureHandle_t hSRGBWriteEnabledTexture, ShaderAPITextureHandle_t hIdentityTexture )
{
}


// Returns the nearest supported format
ImageFormat CShaderAPIDx11::GetNearestSupportedFormat( ImageFormat fmt, bool bFilteringRequired /* = true */ ) const
{
	return fmt;
}

ImageFormat CShaderAPIDx11::GetNearestRenderTargetFormat( ImageFormat fmt ) const
{
	return fmt;
}

// Sets the texture state
void CShaderAPIDx11::BindTexture( Sampler_t stage, ShaderAPITextureHandle_t textureHandle )
{
	StateDx11_BindTexture( stage, textureHandle );
}

static float s_flClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

void CShaderAPIDx11::ClearColor3ub( unsigned char r, unsigned char g, unsigned char b )
{
	s_flClearColor[0] = r / 255.0f;
	s_flClearColor[1] = g / 255.0f;
	s_flClearColor[2] = b / 255.0f;
	s_flClearColor[3] = 1.0f;
}

void CShaderAPIDx11::ClearColor4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
	s_flClearColor[0] = r / 255.0f;
	s_flClearColor[1] = g / 255.0f;
	s_flClearColor[2] = b / 255.0f;
	s_flClearColor[3] = a / 255.0f;
}

// Indicates we're going to be modifying this texture
// TexImage2D, TexSubImage2D, TexWrap, TexMinFilter, and TexMagFilter
// all use the texture specified by this function.
void CShaderAPIDx11::ModifyTexture( ShaderAPITextureHandle_t textureHandle )
{
	TextureDx11_Modify( textureHandle );
}

// Texture management methods
void CShaderAPIDx11::TexImage2D( int level, int cubeFace, ImageFormat dstFormat, int zOffset, int width, int height,
						 ImageFormat srcFormat, bool bSrcIsTiled, void *imageData )
{
	// zOffset = the volume-texture slice (color-correction LUTs upload per-z)
	TextureDx11_Image2D( level, cubeFace, dstFormat, width, height, srcFormat, imageData, zOffset );
}

void CShaderAPIDx11::TexSubImage2D( int level, int cubeFace, int xOffset, int yOffset, int zOffset, int width, int height,
						 ImageFormat srcFormat, int srcStride, bool bSrcIsTiled, void *imageData )
{
	TextureDx11_SubImage2D( level, cubeFace, xOffset, yOffset, width, height, srcFormat, srcStride, imageData );
}

void CShaderAPIDx11::TexImageFromVTF( IVTFTexture *pVTF, int iVTFFrame )
{
	TextureDx11_ImageFromVTF( pVTF, iVTFFrame );
}

bool CShaderAPIDx11::TexLock( int level, int cubeFaceID, int xOffset, int yOffset, 
								int width, int height, CPixelWriter& writer )
{
	return TextureDx11_TexLock( level, cubeFaceID, xOffset, yOffset, width, height, writer );
}

void CShaderAPIDx11::TexUnlock( )
{
	TextureDx11_TexUnlock();
}


// These are bound to the texture, not the texture environment
void CShaderAPIDx11::TexMinFilter( ShaderTexFilterMode_t texFilterMode )
{
	TextureDx11_MinFilter( texFilterMode );
}

void CShaderAPIDx11::TexMagFilter( ShaderTexFilterMode_t texFilterMode )
{
	TextureDx11_MagFilter( texFilterMode );
}

void CShaderAPIDx11::TexWrap( ShaderTexCoordComponent_t coord, ShaderTexWrapMode_t wrapMode )
{
	TextureDx11_Wrap( coord, wrapMode );
}

void CShaderAPIDx11::TexSetPriority( int priority )
{
}

ShaderAPITextureHandle_t CShaderAPIDx11::CreateTexture( 
	int width, 
	int height,
	int depth,
	ImageFormat dstImageFormat, 
	int numMipLevels, 
	int numCopies, 
	int flags, 
	const char *pDebugName,
	const char *pTextureGroupName )
{
	ShaderAPITextureHandle_t hTexture = INVALID_SHADERAPI_TEXTURE_HANDLE;
	TextureDx11_CreateTextures( &hTexture, 1, width, height, depth, dstImageFormat, numMipLevels, numCopies, flags, pDebugName );
	return hTexture;
}

// Create a multi-frame texture (equivalent to calling "CreateTexture" multiple times, but more efficient)
void CShaderAPIDx11::CreateTextures( 
							ShaderAPITextureHandle_t *pHandles,
							int count,
							int width, 
							int height,
							int depth,
							ImageFormat dstImageFormat, 
							int numMipLevels, 
							int numCopies, 
							int flags, 
							const char *pDebugName,
							const char *pTextureGroupName )
{
	TextureDx11_CreateTextures( pHandles, count, width, height, depth, dstImageFormat, numMipLevels, numCopies, flags, pDebugName );
}


ShaderAPITextureHandle_t CShaderAPIDx11::CreateDepthTexture( ImageFormat renderFormat, int width, int height, const char *pDebugName, bool bTexture )
{
	// Separate zbuffer for an RT (CTexture handle[1] on RENDER_TARGET_WITH_
	// DEPTH). The flashlight shadow maps do NOT come through here — they're
	// MATERIAL_RT_DEPTH_NONE textures whose depth FORMAT routes CreateTextures
	// itself to the depth path.
	return TextureDx11_CreateDepth( width, height, pDebugName );
}

void CShaderAPIDx11::DeleteTexture( ShaderAPITextureHandle_t textureHandle )
{
	TextureDx11_Delete( textureHandle );
}

bool CShaderAPIDx11::IsTexture( ShaderAPITextureHandle_t textureHandle )
{
	return TextureDx11_IsValid( textureHandle );
}

bool CShaderAPIDx11::IsTextureResident( ShaderAPITextureHandle_t textureHandle )
{
	return false;
}

// stuff that isn't to be used from within a shader
void CShaderAPIDx11::ClearBuffers( bool bClearColor, bool bClearDepth, bool bClearStencil, int renderTargetWidth, int renderTargetHeight )
{
	// Targets the active RT (backbuffer or texture); unbindable RTs suppress
	StateDx11_ClearViews( bClearColor, bClearDepth, bClearStencil, s_flClearColor );
}

void CShaderAPIDx11::ClearBuffersObeyStencil( bool bClearColor, bool bClearDepth )
{
	// dx9 clears only stencil-unprotected pixels here (the engine uses this to
	// fill around the 3D view). With no stencil support yet, clearing nothing
	// matches dx9 output far better than clearing the whole target.
	if ( bClearDepth )
		StateDx11_ClearViews( false, true, false, s_flClearColor );
}

void CShaderAPIDx11::ClearBuffersObeyStencilEx( bool bClearColor, bool bClearAlpha, bool bClearDepth )
{
	if ( bClearDepth )
		StateDx11_ClearViews( false, true, false, s_flClearColor );
}

void CShaderAPIDx11::PerformFullScreenStencilOperation( void )
{
}

void CShaderAPIDx11::SetScissorRect( const int nLeft, const int nTop, const int nRight, const int nBottom, const bool bEnableScissor )
{
	StateDx11_SetScissorRect( nLeft, nTop, nRight, nBottom, bEnableScissor );
}

void CShaderAPIDx11::ReadPixels( int x, int y, int width, int height, unsigned char *data, ImageFormat dstFormat )
{
	// Backbuffer readback (screenshot command, etc.): copy to a staging
	// texture, map, and convert the requested rect into the caller's buffer.
	if ( !data || width <= 0 || height <= 0 || !D3D11Context() || !g_pD3D11RTV )
		return;

	ID3D11Resource *pBackRes = NULL;
	g_pD3D11RTV->GetResource( &pBackRes );
	ID3D11Texture2D *pBackTex = NULL;
	if ( !pBackRes || FAILED( pBackRes->QueryInterface( __uuidof( ID3D11Texture2D ), (void **)&pBackTex ) ) )
	{
		if ( pBackRes ) pBackRes->Release();
		return;
	}
	pBackRes->Release();

	D3D11_TEXTURE2D_DESC desc;
	pBackTex->GetDesc( &desc );
	bool bMultisampled = desc.SampleDesc.Count > 1;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.MiscFlags = 0;
	// Staging textures cannot be multisampled; with mat_antialias the MS
	// backbuffer resolves through a DEFAULT-usage temp first.
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	ID3D11Texture2D *pStaging = NULL;
	if ( FAILED( D3D11Device()->CreateTexture2D( &desc, NULL, &pStaging ) ) )
	{
		pBackTex->Release();
		return;
	}
	if ( bMultisampled )
	{
		D3D11_TEXTURE2D_DESC resolveDesc = desc;
		resolveDesc.Usage = D3D11_USAGE_DEFAULT;
		resolveDesc.CPUAccessFlags = 0;
		resolveDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		ID3D11Texture2D *pResolved = NULL;
		if ( SUCCEEDED( D3D11Device()->CreateTexture2D( &resolveDesc, NULL, &pResolved ) ) )
		{
			D3D11Context()->ResolveSubresource( pResolved, 0, pBackTex, 0, desc.Format );
			D3D11Context()->CopyResource( pStaging, pResolved );
			pResolved->Release();
		}
	}
	else
	{
		D3D11Context()->CopyResource( pStaging, pBackTex );
	}
	pBackTex->Release();

	D3D11_MAPPED_SUBRESOURCE map;
	if ( SUCCEEDED( D3D11Context()->Map( pStaging, 0, D3D11_MAP_READ, 0, &map ) ) )
	{
		// Backbuffer is R8G8B8A8_UNORM; clamp the rect and convert per row.
		int nMaxW = (int)desc.Width, nMaxH = (int)desc.Height;
		int nW = MIN( width, MAX( 0, nMaxW - x ) );
		int nH = MIN( height, MAX( 0, nMaxH - y ) );
		if ( nW > 0 && nH > 0 )
		{
			const unsigned char *pSrc = (const unsigned char *)map.pData + y * map.RowPitch + x * 4;
			ShaderUtil()->ConvertImageFormat( const_cast<unsigned char *>( pSrc ), IMAGE_FORMAT_RGBA8888,
				data, dstFormat, nW, nH, map.RowPitch, 0 );
		}
		D3D11Context()->Unmap( pStaging, 0 );
	}
	pStaging->Release();
}

void CShaderAPIDx11::ReadPixels( Rect_t *pSrcRect, Rect_t *pDstRect, unsigned char *data, ImageFormat dstFormat, int nDstStride )
{
	if ( !pSrcRect || !pDstRect )
		return;
	// 1:1 blits only for now (the screenshot path); scaled reads are M6+.
	if ( pSrcRect->width == pDstRect->width && pSrcRect->height == pDstRect->height )
	{
		ReadPixels( pSrcRect->x, pSrcRect->y, pSrcRect->width, pSrcRect->height, data, dstFormat );
	}
}

void CShaderAPIDx11::FlushHardware()
{
}

void CShaderAPIDx11::ResetRenderState( bool bFullReset )
{
	// Make cLightScale (ps c30) valid before the first engine call lands:
	// LDR = {1, lightmap scale, 1, 1}, exactly dx9's startup state.
	SetToneMappingScaleLinear( Vector( 1.0f, 1.0f, 1.0f ) );
}

// Set the number of bone weights
void CShaderAPIDx11::SetNumBoneWeights( int numBones )
{
	// Per-strip skinning signal (studiorender sets this before each strip);
	// rigid strips must not read their (garbage) weight vertex fields.
	StateDx11_SetNumBones( numBones );
}

void CShaderAPIDx11::EnableHWMorphing( bool bEnable )
{
}

// Selection mode methods
int CShaderAPIDx11::SelectionMode( bool selectionMode )
{
	return 0;
}

void CShaderAPIDx11::SelectionBuffer( unsigned int* pBuffer, int size )
{
}

void CShaderAPIDx11::ClearSelectionNames( )
{
}

void CShaderAPIDx11::LoadSelectionName( int name )
{
}

void CShaderAPIDx11::PushSelectionName( int name )
{
}

void CShaderAPIDx11::PopSelectionName()
{
}


// Use this to get the mesh builder that allows us to modify vertex data
CMeshBuilder* CShaderAPIDx11::GetVertexModifyBuilder()
{
	return 0;
}

// Board-independent calls, here to unify how shaders set state
// Implementations should chain back to IShaderUtil->BindTexture(), etc.

// Use this to begin and end the frame
void CShaderAPIDx11::BeginFrame()
{
}

void CShaderAPIDx11::EndFrame()
{
}

// returns the current time in seconds....
double CShaderAPIDx11::CurrentTime() const
{
	return Sys_FloatTime();
}

// Get the current camera position in world space.
void CShaderAPIDx11::GetWorldSpaceCameraPosition( float * pPos ) const
{
	// dx9 parity (shaderapidx8.cpp:9971): recovered from the VIEW matrix.
	// A zero eye position broke every view-dependent phong term (fresnel,
	// specular reflect) — PSREG_EYEPOS_SPEC_EXPONENT.xyz arrived as (0,0,0).
	StateDx11_GetWorldSpaceCameraPosition( pPos );
}

void CShaderAPIDx11::ForceHardwareSync( void )
{
}

void CShaderAPIDx11::SetClipPlane( int index, const float *pPlane )
{
}

void CShaderAPIDx11::EnableClipPlane( int index, bool bEnable )
{
}

void CShaderAPIDx11::SetFastClipPlane( const float *pPlane )
{
}

void CShaderAPIDx11::EnableFastClip( bool bEnable )
{
}

int CShaderAPIDx11::GetCurrentNumBones( void ) const
{
	// dx9 parity (shaderapidx8.cpp:12568): the live SetNumBoneWeights value.
	// Callers that break on a hardcoded 0: dynamic-mesh format selection
	// (cmatrendercontext.cpp:1728) and r_studioflex.cpp:645/756, whose
	// save/restore would stomp the bone count to 0 after every flexed group.
	return StateDx11_GetNumBones();
}

bool CShaderAPIDx11::IsHWMorphingEnabled( void ) const
{
	return false;
}

int CShaderAPIDx11::GetCurrentLightCombo( void ) const
{
	return 0;
}

void CShaderAPIDx11::GetDX9LightState( LightState_t *state ) const
{
	// dx9 parity (shaderapidx8.cpp:12770-12812). The all-false stub made the
	// skin helper force every phong material's ambient cube to BLACK
	// (SetPixelShaderStateAmbientLightCube( reg, !m_bAmbientLight )).
	state->m_nNumLights = StateDx11_GetNumEnabledLights();
	state->m_bAmbientLight = StateDx11_IsAmbientCubeNonZero();
	state->m_bStaticLightVertex = MeshDx11_RenderMeshHasColorMesh();
	state->m_bStaticLightTexel = false;
}

MaterialFogMode_t CShaderAPIDx11::GetCurrentFogType( void ) const
{
	return MATERIAL_FOG_NONE;
}

void CShaderAPIDx11::RecordString( const char *pStr )
{
}

bool CShaderAPIDx11::ReadPixelsFromFrontBuffer() const
{
	return true;
}

bool CShaderAPIDx11::PreferDynamicTextures() const
{
	return false;
}

bool CShaderAPIDx11::PreferReducedFillrate() const
{ 
	return false; 
}

bool CShaderAPIDx11::HasProjectedBumpEnv() const
{
	return true;
}

int  CShaderAPIDx11::GetCurrentDynamicVBSize( void )
{
	// The queued render context sizes its staging pool from this; zero breaks
	// its dynamic-mesh bookkeeping ("without resolving the previous one").
	return MeshDx11_DynamicVBSize();
}

void CShaderAPIDx11::DestroyVertexBuffers( bool bExitingLevel )
{
}

void CShaderAPIDx11::EvictManagedResources()
{
}

void CShaderAPIDx11::SetTextureTransformDimension( TextureStage_t textureStage, int dimension, bool projected )
{
}

void CShaderAPIDx11::SetBumpEnvMatrix( TextureStage_t textureStage, float m00, float m01, float m10, float m11 )
{
}

void CShaderAPIDx11::SyncToken( const char *pToken )
{
}
