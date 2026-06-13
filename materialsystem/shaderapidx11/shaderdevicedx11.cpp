//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: D3D11 device manager + device (migration milestone M1).
//
// Creates the DXGI factory/adapters, a feature-level 11_0 device, swapchain,
// backbuffer RTV + D24S8 DSV, and the present/clear path. Draw paths remain
// stubbed until M2/M3. See Plan.md at the repo root.
//
//===========================================================================//

#include <d3d11_1.h>
#include <dxgi1_2.h>

#include "shaderdevicedx11.h"
#include "shaderapidx11_global.h"
#include "shadermanagerdx11.h"
#include "statedx11.h"
#include "blitdx11.h"
#include "texturedx11.h"
#include "meshdx11.h"
#include "shaderapibase.h"
#include "shaderapi/ishaderutil.h"
#include "materialsystem/imesh.h"
#include "materialsystem/materialsystem_config.h"
#include "tier1/KeyValues.h"
#include "tier1/convar.h"
#include "tier2/tier2.h"
#include "tier0/icommandline.h"
#include "filesystem.h"
#include "renderdoc_app.h"

// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"

// Referenced by the shared hardwareconfig.cpp (defined in shaderdevicedx8.cpp
// for the DX9 module; each shaderapi DLL registers its own copy).
ConVar mat_hdr_level( "mat_hdr_level", "2", FCVAR_ARCHIVE );
ConVar mat_fastclip( "mat_fastclip", "0", FCVAR_CHEAT );

#ifndef VENDORID_NVIDIA
#define VENDORID_NVIDIA	0x10DE
#endif
#ifndef VENDORID_ATI
#define VENDORID_ATI	0x1002
#endif


//-----------------------------------------------------------------------------
// D3D11 globals (see shaderapidx11_global.h)
//-----------------------------------------------------------------------------
ID3D11Device *g_pD3D11Device = NULL;
ID3D11DeviceContext *g_pD3D11Context = NULL;
IDXGISwapChain *g_pD3D11SwapChain = NULL;
ID3D11RenderTargetView *g_pD3D11RTV = NULL;
ID3D11RenderTargetView *g_pD3D11RTV_SRGB = NULL;
ID3D11DepthStencilView *g_pD3D11DSV = NULL;

static RENDERDOC_API_1_1_2 *s_pRenderDocAPI = NULL;
static ID3DUserDefinedAnnotation *s_pAnnotation = NULL;

// Debug names show up in RenderDoc's resource browser
void Dx11_SetDebugName( ID3D11DeviceChild *pObject, const char *pName )
{
	if ( pObject && pName && pName[0] )
	{
		pObject->SetPrivateData( WKPDID_D3DDebugObjectName, (UINT)strlen( pName ), pName );
	}
}

ID3DUserDefinedAnnotation *Dx11_Annotation()
{
	return s_pAnnotation;
}


//-----------------------------------------------------------------------------
//
// Device manager
//
//-----------------------------------------------------------------------------
static CShaderDeviceMgrDx11 g_ShaderDeviceMgrDx11;
CShaderDeviceMgrDx11 *g_pShaderDeviceMgrDx11 = &g_ShaderDeviceMgrDx11;

EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderDeviceMgrDx11, IShaderDeviceMgr,
	SHADER_DEVICE_MGR_INTERFACE_VERSION, g_ShaderDeviceMgrDx11 )

static CShaderDeviceDx11 g_ShaderDeviceDx11;
CShaderDeviceDx11 *g_pShaderDeviceDx11 = &g_ShaderDeviceDx11;

EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderDeviceDx11, IShaderDevice,
	SHADER_DEVICE_INTERFACE_VERSION, g_ShaderDeviceDx11 )


//-----------------------------------------------------------------------------
// constructor, destructor
//-----------------------------------------------------------------------------
CShaderDeviceMgrDx11::CShaderDeviceMgrDx11()
{
	m_pDXGIFactory = NULL;
	m_bObeyDxCommandlineOverride = true;
}

CShaderDeviceMgrDx11::~CShaderDeviceMgrDx11()
{
}


//-----------------------------------------------------------------------------
// Connect, disconnect
//-----------------------------------------------------------------------------
bool CShaderDeviceMgrDx11::Connect( CreateInterfaceFn factory )
{
	LOCK_SHADERAPI();

	if ( !BaseClass::Connect( factory ) )
		return false;

	HRESULT hr = CreateDXGIFactory1( __uuidof(IDXGIFactory1), (void**)(&m_pDXGIFactory) );
	if ( FAILED( hr ) )
	{
		Warning( "shaderapidx11: failed to create the DXGI factory (hr=0x%08x)\n", hr );
		return false;
	}

	InitAdapterInfo();

	// Prime the hardware config with adapter-0 caps now: the material system
	// decides which stdshader DLLs to load from GetMaxDXSupportLevel() before
	// any mode is set. The mode overload is required — only it fills
	// m_ActualCaps, which GetMaxDXSupportLevel() reads. InitDevice re-runs
	// this with the real mode later.
	for ( int i = 0; i < m_Adapters.Count(); ++i )
	{
		if ( m_Adapters[i].m_ActualCaps.m_bDeviceOk )
		{
			ShaderDeviceInfo_t primeMode;
			primeMode.m_nDXLevel = 95;
			g_pHardwareConfig->SetupHardwareCaps( primeMode, m_Adapters[i].m_ActualCaps );
			Msg( "shaderapidx11: primed hwconfig from adapter %d (MaxDXSupportLevel=%d, driver: %s)\n",
				i, g_pHardwareConfig->GetMaxDXSupportLevel(), m_Adapters[i].m_ActualCaps.m_pDriverName );
			break;
		}
	}

	return true;
}

void CShaderDeviceMgrDx11::Disconnect()
{
	LOCK_SHADERAPI();

	if ( m_pDXGIFactory )
	{
		m_pDXGIFactory->Release();
		m_pDXGIFactory = NULL;
	}

	BaseClass::Disconnect();
}


//-----------------------------------------------------------------------------
// Initialization, shutdown
//-----------------------------------------------------------------------------
InitReturnVal_t CShaderDeviceMgrDx11::Init()
{
	LOCK_SHADERAPI();

	// The engine's autoconfig only assigns mat_dxlevel when max != recommended
	// (matsys_interface.cpp); we report both as 95, so an unset mat_dxlevel
	// floors the material config at ABSOLUTE_MINIMUM_DXLEVEL (80) and pushes
	// every dx9 shader onto its _DX8 fallback. Pin it for this backend.
	// (Done here, not in Connect: mat_dxlevel isn't registered until the
	// material system finishes connecting.)
	ConVarRef mat_dxlevel( "mat_dxlevel" );
	if ( mat_dxlevel.IsValid() && mat_dxlevel.GetInt() < 90 )
	{
		Msg( "shaderapidx11: forcing mat_dxlevel %d -> 95\n", mat_dxlevel.GetInt() );
		mat_dxlevel.SetValue( 95 );
	}

	return INIT_OK;
}

void CShaderDeviceMgrDx11::Shutdown()
{
	LOCK_SHADERAPI();

	if ( g_pShaderDevice )
	{
		g_pShaderDevice->ShutdownDevice();
		g_pShaderDevice = NULL;
	}
}


//-----------------------------------------------------------------------------
// Initialize adapter information
//-----------------------------------------------------------------------------
void CShaderDeviceMgrDx11::InitAdapterInfo()
{
	m_Adapters.RemoveAll();

	IDXGIAdapter *pAdapter;
	for ( UINT nCount = 0; m_pDXGIFactory->EnumAdapters( nCount, &pAdapter ) != DXGI_ERROR_NOT_FOUND; ++nCount )
	{
		int j = m_Adapters.AddToTail();
		AdapterInfo_t &info = m_Adapters[j];

		IDXGIOutput *pOutput = GetAdapterOutput( nCount );
		info.m_ActualCaps.m_bDeviceOk = ComputeCapsFromD3D11( &info.m_ActualCaps, pAdapter, pOutput );
		if ( pOutput )
		{
			pOutput->Release();
		}
		pAdapter->Release();
	}
}


//-----------------------------------------------------------------------------
// Determines hardware caps. The DX11 backend reports a fixed dxlevel-95-class
// cap set on FL 11_0 hardware; dxsupport.cfg is deliberately NOT consulted
// (it encodes DX9-era card quirks that do not apply to this backend).
//-----------------------------------------------------------------------------
bool CShaderDeviceMgrDx11::ComputeCapsFromD3D11( HardwareCaps_t *pCaps, IDXGIAdapter *pAdapter, IDXGIOutput *pOutput )
{
	memset( pCaps, 0, sizeof( HardwareCaps_t ) );

	DXGI_ADAPTER_DESC desc;
	HRESULT hr = pAdapter->GetDesc( &desc );
	if ( FAILED( hr ) )
		return false;

	// Reject software adapters (Microsoft Basic Render Driver)
	if ( desc.VendorId == 0x1414 && desc.DeviceId == 0x8c )
		return false;

	Q_UnicodeToUTF8( desc.Description, pCaps->m_pDriverName, MATERIAL_ADAPTER_NAME_LENGTH );
	pCaps->m_VendorID = desc.VendorId;
	pCaps->m_DeviceID = desc.DeviceId;
	pCaps->m_SubSysID = desc.SubSysId;
	pCaps->m_Revision = desc.Revision;
	pCaps->m_TextureMemorySize = desc.DedicatedVideoMemory;

	// dxlevel-95-class capabilities; the stdshader_dx9 combo set keys off these.
	pCaps->m_NumSamplers = 16;
	pCaps->m_NumTextureStages = 0;
	pCaps->m_HasSetDeviceGammaRamp = true;
	pCaps->m_bSoftwareVertexProcessing = false;
	pCaps->m_SupportsVertexShaders = true;
	pCaps->m_SupportsVertexShaders_2_0 = true;
	pCaps->m_SupportsPixelShaders = true;
	pCaps->m_SupportsPixelShaders_1_4 = true;
	pCaps->m_SupportsPixelShaders_2_0 = true;
	pCaps->m_SupportsPixelShaders_2_b = true;
	pCaps->m_SupportsShaderModel_3_0 = true;
	pCaps->m_SupportsCompressedTextures = COMPRESSED_TEXTURES_ON;
	// No compressed verts until the backend implements the SHORT2-weight /
	// packed-normal layouts: studiorender builds COMPRESSED studio meshes when
	// this is on, and CStaticMeshDx11 strips the format bit — writer and
	// reader then disagree on every field after position (the skinned-mesh
	// "explosions": weights read 6.07, normals 5.8e13). M8 can add support.
	pCaps->m_SupportsCompressedVertices = VERTEX_COMPRESSION_NONE;
	pCaps->m_bSupportsAnisotropicFiltering = true;
	pCaps->m_bSupportsMagAnisotropicFiltering = true;
	pCaps->m_bSupportsVertexTextures = true;
	pCaps->m_nMaxAnisotropy = 16;
	pCaps->m_MaxTextureWidth = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
	pCaps->m_MaxTextureHeight = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
	pCaps->m_MaxTextureDepth = D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
	pCaps->m_MaxTextureAspectRatio = 1024;
	pCaps->m_MaxPrimitiveCount = 65536;
	pCaps->m_ZBiasAndSlopeScaledDepthBiasSupported = true;
	pCaps->m_SupportsMipmapping = true;
	pCaps->m_SupportsOverbright = true;
	pCaps->m_SupportsCubeMaps = true;
	pCaps->m_NumPixelShaderConstants = 32;
	pCaps->m_NumBooleanPixelShaderConstants = 16;
	pCaps->m_NumIntegerPixelShaderConstants = 16;
	pCaps->m_NumVertexShaderConstants = 256;
	pCaps->m_NumBooleanVertexShaderConstants = 16;
	pCaps->m_NumIntegerVertexShaderConstants = 16;
	pCaps->m_MaxNumLights = 4;
	pCaps->m_SupportsHardwareLighting = false;
	pCaps->m_MaxBlendMatrices = 0;
	pCaps->m_MaxBlendMatrixIndices = 0;
	pCaps->m_MaxVertexShaderBlendMatrices = 53;
	pCaps->m_SupportsMipmappedCubemaps = true;
	pCaps->m_SupportsNonPow2Textures = true;
	pCaps->m_nDXSupportLevel = 95;
	pCaps->m_nMaxDXSupportLevel = 95;
	pCaps->m_PreferDynamicTextures = false;
	pCaps->m_HasProjectedBumpEnv = true;
	pCaps->m_MaxUserClipPlanes = 6;
	// M7: integer HDR restored. The post family (bloom MODES 22-24,
	// engine_post 25) is live, the histogram materials route safely
	// (lumcompare = MODE 27 with the dynamic stencil overlay,
	// no_pixel_write = MODE 24 passthrough with color writes off), shaders
	// tonemap inline via cLightScale c30 (SetToneMappingScaleLinear), and
	// 16-bit lightmap pages/HDR cubemaps load through the existing
	// RGBA16161616(F) format-map entries.
	pCaps->m_HDRType = HDR_TYPE_INTEGER;
	pCaps->m_SupportsSRGB = true;
	pCaps->m_FakeSRGBWrite = false;
	pCaps->m_CanDoSRGBReadFromRTs = true;
	pCaps->m_bSupportsSpheremapping = true;
	pCaps->m_UseFastClipping = false;
	pCaps->m_pShaderDLL[0] = 0;
	pCaps->m_bNeedsATICentroidHack = false;
	pCaps->m_bColorOnSecondStream = true;
	pCaps->m_bSupportsStreamOffset = true;
	pCaps->m_bFogColorSpecifiedInLinearSpace = ( desc.VendorId == VENDORID_NVIDIA );
	pCaps->m_nVertexTextureCount = 16;
	pCaps->m_nMaxVertexTextureDimension = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
	pCaps->m_bSupportsAlphaToCoverage = true;
	pCaps->m_bSupportsShadowDepthTextures = true;
	pCaps->m_bSupportsFetch4 = false;
	pCaps->m_bSupportsBorderColor = true;
	// Matches the IShaderAPI GetShadowDepthTextureFormat answer — texturedx11
	// maps the dx9 vendor depth formats onto R24G8_TYPELESS (DSV + depth SRV).
	pCaps->m_ShadowDepthTextureFormat = IMAGE_FORMAT_NV_DST24;
	pCaps->m_NullTextureFormat = IMAGE_FORMAT_RGBA8888;
	pCaps->m_nMaxViewports = 1;
	pCaps->m_bDX10Card = true;
	pCaps->m_bDX10Blending = true;
	pCaps->m_bSupportsStaticControlFlow = true;
	pCaps->m_bCanStretchRectFromTextures = true;
	pCaps->m_MaxSimultaneousRenderTargets = 4;
	pCaps->m_bDeviceOk = true;

	if ( pOutput )
	{
		DXGI_GAMMA_CONTROL_CAPABILITIES gammaCaps;
		if ( SUCCEEDED( pOutput->GetGammaControlCapabilities( &gammaCaps ) ) )
		{
			pCaps->m_flMinGammaControlPoint = gammaCaps.MinConvertedValue;
			pCaps->m_flMaxGammaControlPoint = gammaCaps.MaxConvertedValue;
			pCaps->m_nGammaControlPointCount = gammaCaps.NumGammaControlPoints;
		}
	}

	return true;
}


//-----------------------------------------------------------------------------
// Adapter queries
//-----------------------------------------------------------------------------
int CShaderDeviceMgrDx11::GetAdapterCount() const
{
	return m_Adapters.Count();
}

void CShaderDeviceMgrDx11::GetAdapterInfo( int nAdapter, MaterialAdapterInfo_t& info ) const
{
	Assert( ( nAdapter >= 0 ) && ( nAdapter < m_Adapters.Count() ) );
	const HardwareCaps_t &caps = m_Adapters[ nAdapter ].m_ActualCaps;
	memcpy( &info, &caps, sizeof(MaterialAdapterInfo_t) );
}

IDXGIAdapter* CShaderDeviceMgrDx11::GetAdapter( int nAdapter ) const
{
	Assert( m_pDXGIFactory && ( nAdapter < GetAdapterCount() ) );

	IDXGIAdapter *pAdapter;
	HRESULT hr = m_pDXGIFactory->EnumAdapters( nAdapter, &pAdapter );
	return ( FAILED(hr) ) ? NULL : pAdapter;
}

int CShaderDeviceMgrDx11::GetVidMemBytes( int nAdapter ) const
{
	LOCK_SHADERAPI();
	IDXGIAdapter *pAdapter = GetAdapter( nAdapter );
	if ( !pAdapter )
		return 0;

	DXGI_ADAPTER_DESC desc;
	int nBytes = SUCCEEDED( pAdapter->GetDesc( &desc ) ) ? (int)desc.DedicatedVideoMemory : 0;
	pAdapter->Release();
	return nBytes;
}

IDXGIOutput* CShaderDeviceMgrDx11::GetAdapterOutput( int nAdapter ) const
{
	LOCK_SHADERAPI();
	IDXGIAdapter *pAdapter = GetAdapter( nAdapter );
	if ( !pAdapter )
		return NULL;

	IDXGIOutput *pOutput;
	for ( UINT i = 0; pAdapter->EnumOutputs( i, &pOutput ) != DXGI_ERROR_NOT_FOUND; ++i )
	{
		DXGI_OUTPUT_DESC desc;
		if ( FAILED( pOutput->GetDesc( &desc ) ) || !desc.AttachedToDesktop )
		{
			pOutput->Release();
			continue;
		}

		pAdapter->Release();
		return pOutput;
	}

	pAdapter->Release();
	return NULL;
}


//-----------------------------------------------------------------------------
// Mode enumeration
//-----------------------------------------------------------------------------
int CShaderDeviceMgrDx11::GetModeCount( int nAdapter ) const
{
	LOCK_SHADERAPI();

	IDXGIOutput *pOutput = GetAdapterOutput( nAdapter );
	if ( !pOutput )
		return 0;

	UINT num = 0;
	pOutput->GetDisplayModeList( DXGI_FORMAT_R8G8B8A8_UNORM, 0, &num, NULL );
	pOutput->Release();
	return num;
}

void CShaderDeviceMgrDx11::GetModeInfo( ShaderDisplayMode_t* pInfo, int nAdapter, int nMode ) const
{
	pInfo->m_nWidth = pInfo->m_nHeight = 0;
	pInfo->m_Format = IMAGE_FORMAT_UNKNOWN;
	pInfo->m_nRefreshRateNumerator = pInfo->m_nRefreshRateDenominator = 0;

	LOCK_SHADERAPI();

	IDXGIOutput *pOutput = GetAdapterOutput( nAdapter );
	if ( !pOutput )
		return;

	UINT num = 0;
	pOutput->GetDisplayModeList( DXGI_FORMAT_R8G8B8A8_UNORM, 0, &num, NULL );
	if ( (UINT)nMode >= num )
	{
		pOutput->Release();
		return;
	}

	DXGI_MODE_DESC *pDescs = (DXGI_MODE_DESC*)stackalloc( num * sizeof( DXGI_MODE_DESC ) );
	pOutput->GetDisplayModeList( DXGI_FORMAT_R8G8B8A8_UNORM, 0, &num, pDescs );

	pInfo->m_nWidth = pDescs[nMode].Width;
	pInfo->m_nHeight = pDescs[nMode].Height;
	pInfo->m_Format = IMAGE_FORMAT_BGRA8888;
	pInfo->m_nRefreshRateNumerator = pDescs[nMode].RefreshRate.Numerator;
	pInfo->m_nRefreshRateDenominator = pDescs[nMode].RefreshRate.Denominator;

	pOutput->Release();
}

void CShaderDeviceMgrDx11::GetCurrentModeInfo( ShaderDisplayMode_t* pInfo, int nAdapter ) const
{
	pInfo->m_nWidth = pInfo->m_nHeight = 0;
	pInfo->m_Format = IMAGE_FORMAT_BGRA8888;
	pInfo->m_nRefreshRateNumerator = 60;
	pInfo->m_nRefreshRateDenominator = 1;

	LOCK_SHADERAPI();

	IDXGIOutput *pOutput = GetAdapterOutput( nAdapter );
	if ( !pOutput )
		return;

	DXGI_OUTPUT_DESC desc;
	if ( SUCCEEDED( pOutput->GetDesc( &desc ) ) )
	{
		pInfo->m_nWidth = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
		pInfo->m_nHeight = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
	}
	pOutput->Release();
}


//-----------------------------------------------------------------------------
// Sets the adapter
//-----------------------------------------------------------------------------
bool CShaderDeviceMgrDx11::SetAdapter( int nAdapter, int nFlags )
{
	return true;
}


//-----------------------------------------------------------------------------
// Sets the mode: shuts down the previous device state, creates the D3D11
// device, and wires up the module globals the engine-facing factory serves.
//-----------------------------------------------------------------------------
CreateInterfaceFn CShaderDeviceMgrDx11::SetMode( void *hWnd, int nAdapter, const ShaderDeviceInfo_t& mode )
{
	LOCK_SHADERAPI();

	Assert( nAdapter < GetAdapterCount() );

	// This backend is dxlevel-95-class only; ignore -dxlevel and config values.
	if ( m_bObeyDxCommandlineOverride )
	{
		int nRequested = CommandLine()->ParmValue( "-dxlevel", 95 );
		if ( nRequested != 95 )
		{
			Warning( "shaderapidx11: -dxlevel %d ignored; this backend always runs dxlevel-95-class caps.\n", nRequested );
		}
		m_bObeyDxCommandlineOverride = false;
	}

	bool bReacquireResourcesNeeded = false;
	if ( g_pShaderDevice )
	{
		bReacquireResourcesNeeded = true;
		g_pShaderDevice->ReleaseResources();
	}

	if ( g_pShaderAPI )
	{
		g_pShaderAPI->OnDeviceShutdown();
		g_pShaderAPI = NULL;
	}

	if ( g_pShaderDevice )
	{
		g_pShaderDevice->ShutdownDevice();
		g_pShaderDevice = NULL;
	}

	g_pShaderShadow = NULL;

	ShaderDeviceInfo_t adjustedMode = mode;
	adjustedMode.m_nDXLevel = 95;
	if ( !g_ShaderDeviceDx11.InitDevice( hWnd, nAdapter, adjustedMode ) )
		return NULL;

	if ( !ShaderApiDx11_GetShaderAPI()->OnDeviceInit() )
		return NULL;

	g_pShaderDevice = &g_ShaderDeviceDx11;
	g_pShaderAPI = ShaderApiDx11_GetShaderAPI();
	g_pShaderShadow = ShaderApiDx11_GetShaderShadow();

	if ( bReacquireResourcesNeeded )
	{
		g_pShaderDevice->ReacquireResources();
	}

	return ShaderInterfaceFactory;
}


//-----------------------------------------------------------------------------
//
// Device
//
//-----------------------------------------------------------------------------
CShaderDeviceDx11::CShaderDeviceDx11()
{
	m_pDevice = NULL;
	m_pContext = NULL;
	m_pSwapChain = NULL;
	m_pOutput = NULL;
	m_pRenderTargetView = NULL;
	m_pDepthStencilTexture = NULL;
	m_pDepthStencilView = NULL;
	m_nBackBufferWidth = 0;
	m_nBackBufferHeight = 0;
	m_nAASamples = 1;
	m_nFrameCount = 0;
	m_nRenderDocCaptureFrame = -1;
	m_bVSync = false;
	m_bFrameCleared = false;
	m_bUsingDebugLayer = false;
}

CShaderDeviceDx11::~CShaderDeviceDx11()
{
}


//-----------------------------------------------------------------------------
// Creates the device, context and swapchain
//-----------------------------------------------------------------------------
bool CShaderDeviceDx11::InitDevice( void *hWnd, int nAdapter, const ShaderDeviceInfo_t& mode )
{
	LOCK_SHADERAPI();

	// Support in-place mode changes by tearing down the previous device.
	if ( m_nAdapter != -1 )
	{
		ShutdownDevice();
	}

	IDXGIAdapter *pAdapter = g_ShaderDeviceMgrDx11.GetAdapter( nAdapter );
	if ( !pAdapter )
		return false;

	m_pOutput = g_ShaderDeviceMgrDx11.GetAdapterOutput( nAdapter );

	int nWidth = mode.m_DisplayMode.m_nWidth;
	int nHeight = mode.m_DisplayMode.m_nHeight;
	m_nAASamples = MAX( 1, mode.m_nAASamples );
	m_bVSync = mode.m_bWaitForVSync;

	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory( &sd, sizeof(sd) );
	sd.BufferDesc.Width = nWidth;
	sd.BufferDesc.Height = nHeight;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = mode.m_DisplayMode.m_nRefreshRateNumerator;
	sd.BufferDesc.RefreshRate.Denominator = mode.m_DisplayMode.m_nRefreshRateDenominator;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 1;
	sd.OutputWindow = (HWND)hWnd;
	sd.Windowed = mode.m_bWindowed ? TRUE : FALSE;
	sd.Flags = mode.m_bWindowed ? 0 : DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	// Legacy blt-model swapchain on purpose: it mirrors the DX9 present
	// semantics (incl. MSAA backbuffers) for parity. Flip-model is an M8 task.
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	sd.SampleDesc.Count = m_nAASamples;
	sd.SampleDesc.Quality = 0;

	UINT nDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	m_bUsingDebugLayer = ( CommandLine()->FindParm( "-dx11debug" ) != 0 );
	if ( m_bUsingDebugLayer )
	{
		nDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	}

	D3D_FEATURE_LEVEL pFeatureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
	D3D_FEATURE_LEVEL flCreated;

	HRESULT hr = D3D11CreateDeviceAndSwapChain( pAdapter, D3D_DRIVER_TYPE_UNKNOWN,
		NULL, nDeviceFlags, pFeatureLevels, ARRAYSIZE( pFeatureLevels ), D3D11_SDK_VERSION,
		&sd, &m_pSwapChain, &m_pDevice, &flCreated, &m_pContext );

	if ( FAILED( hr ) && m_bUsingDebugLayer )
	{
		// Debug layer requires the optional Graphics Tools feature; retry without.
		Warning( "shaderapidx11: debug-layer device creation failed (hr=0x%08x); retrying without -dx11debug.\n", hr );
		m_bUsingDebugLayer = false;
		nDeviceFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
		hr = D3D11CreateDeviceAndSwapChain( pAdapter, D3D_DRIVER_TYPE_UNKNOWN,
			NULL, nDeviceFlags, pFeatureLevels, ARRAYSIZE( pFeatureLevels ), D3D11_SDK_VERSION,
			&sd, &m_pSwapChain, &m_pDevice, &flCreated, &m_pContext );
	}

	// An unsupported MSAA sample count fails swapchain creation outright
	// (mat_antialias is user data and dx9 clamped it against caps) — fall
	// back by halving until something sticks.
	while ( FAILED( hr ) && m_nAASamples > 1 )
	{
		int nFallback = ( m_nAASamples > 2 ) ? m_nAASamples / 2 : 1;
		Warning( "shaderapidx11: %dxAA swapchain failed (hr=0x%08x); retrying with %dxAA.\n",
			m_nAASamples, hr, nFallback );
		m_nAASamples = nFallback;
		sd.SampleDesc.Count = m_nAASamples;
		hr = D3D11CreateDeviceAndSwapChain( pAdapter, D3D_DRIVER_TYPE_UNKNOWN,
			NULL, nDeviceFlags, pFeatureLevels, ARRAYSIZE( pFeatureLevels ), D3D11_SDK_VERSION,
			&sd, &m_pSwapChain, &m_pDevice, &flCreated, &m_pContext );
	}

	pAdapter->Release();

	if ( FAILED( hr ) )
	{
		Warning( "shaderapidx11: D3D11CreateDeviceAndSwapChain failed (hr=0x%08x). Feature level 11_0 is required.\n", hr );
		return false;
	}

	// Source owns fullscreen/mode transitions; keep DXGI's automation out.
	IDXGIFactory1 *pFactory = NULL;
	if ( SUCCEEDED( m_pSwapChain->GetParent( __uuidof(IDXGIFactory1), (void**)&pFactory ) ) )
	{
		pFactory->MakeWindowAssociation( (HWND)hWnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER );
		pFactory->Release();
	}

	if ( !CreateBackBufferViews( nWidth, nHeight, m_nAASamples, 0 ) )
	{
		ShutdownDevice();
		return false;
	}

	m_hWnd = hWnd;
	m_nAdapter = nAdapter;
	m_ViewHWnd = hWnd;
	m_nBackBufferWidth = nWidth;
	m_nBackBufferHeight = nHeight;
	GetWindowSize( m_nWindowWidth, m_nWindowHeight );
	m_bInitialized = true;
	m_nFrameCount = 0;
	m_bFrameCleared = false;

	g_pD3D11Device = m_pDevice;
	g_pD3D11Context = m_pContext;
	g_pD3D11SwapChain = m_pSwapChain;

	g_pHardwareConfig->SetupHardwareCaps( mode, g_ShaderDeviceMgrDx11.GetHardwareCaps( nAdapter ) );

	// RenderDoc in-app API: lets -rdccapture <frame> trigger captures
	// autonomously when launched under renderdoccmd (see Plan.md verification).
	if ( !s_pRenderDocAPI )
	{
		if ( HMODULE hRenderDoc = GetModuleHandleA( "renderdoc.dll" ) )
		{
			pRENDERDOC_GetAPI pGetAPI = (pRENDERDOC_GetAPI)GetProcAddress( hRenderDoc, "RENDERDOC_GetAPI" );
			if ( pGetAPI && pGetAPI( eRENDERDOC_API_Version_1_1_2, (void**)&s_pRenderDocAPI ) == 1 )
			{
				Msg( "shaderapidx11: RenderDoc in-app API connected.\n" );
			}
		}
	}
	m_nRenderDocCaptureFrame = CommandLine()->ParmValue( "-rdccapture", -1 );

	// Pass markers (BeginPIXEvent) + debug names land in RenderDoc captures
	if ( !s_pAnnotation && m_pContext )
	{
		m_pContext->QueryInterface( __uuidof( ID3DUserDefinedAnnotation ), (void **)&s_pAnnotation );
	}
	Dx11_SetDebugName( m_pRenderTargetView, "backbuffer_rtv" );
	Dx11_SetDebugName( g_pD3D11RTV_SRGB, "backbuffer_rtv_srgb" );
	Dx11_SetDebugName( m_pDepthStencilView, "default_dsv" );
	Dx11_SetDebugName( m_pDepthStencilTexture, "default_depth" );

	Msg( "shaderapidx11: created FL 11_0 device on adapter %d (%dx%d, %dxAA, vsync %d, debug layer %d)\n",
		nAdapter, nWidth, nHeight, m_nAASamples, m_bVSync ? 1 : 0, m_bUsingDebugLayer ? 1 : 0 );
	Msg( "shaderapidx11: dxlevel state: mode=%d caps=%d config=%d effective=%d\n",
		mode.m_nDXLevel, g_pHardwareConfig->Caps().m_nDXSupportLevel,
		ShaderUtil()->GetConfig().dxSupportLevel, g_pHardwareConfig->GetDXSupportLevel() );

	return true;
}


//-----------------------------------------------------------------------------
// Backbuffer RTV + depth/stencil DSV
//-----------------------------------------------------------------------------
bool CShaderDeviceDx11::CreateBackBufferViews( int nWidth, int nHeight, int nAASamples, int nAAQuality )
{
	ID3D11Texture2D *pBackBuffer = NULL;
	HRESULT hr = m_pSwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (LPVOID*)&pBackBuffer );
	if ( FAILED( hr ) )
		return false;

	hr = m_pDevice->CreateRenderTargetView( pBackBuffer, NULL, &m_pRenderTargetView );
	if ( SUCCEEDED( hr ) )
	{
		// Second view for snapshot-driven sRGB writes (legal on blt-model
		// swapchains: UNORM <-> UNORM_SRGB are cast-compatible).
		D3D11_RENDER_TARGET_VIEW_DESC rtv;
		ZeroMemory( &rtv, sizeof( rtv ) );
		rtv.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		rtv.ViewDimension = nAASamples > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
		if ( FAILED( m_pDevice->CreateRenderTargetView( pBackBuffer, &rtv, &g_pD3D11RTV_SRGB ) ) )
		{
			g_pD3D11RTV_SRGB = NULL;
		}
	}
	pBackBuffer->Release();
	if ( FAILED( hr ) )
		return false;

	D3D11_TEXTURE2D_DESC depthDesc;
	ZeroMemory( &depthDesc, sizeof(depthDesc) );
	depthDesc.Width = nWidth;
	depthDesc.Height = nHeight;
	depthDesc.MipLevels = 1;
	depthDesc.ArraySize = 1;
	depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthDesc.SampleDesc.Count = nAASamples;
	depthDesc.SampleDesc.Quality = nAAQuality;
	depthDesc.Usage = D3D11_USAGE_DEFAULT;
	depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	hr = m_pDevice->CreateTexture2D( &depthDesc, NULL, &m_pDepthStencilTexture );
	if ( FAILED( hr ) )
		return false;

	hr = m_pDevice->CreateDepthStencilView( m_pDepthStencilTexture, NULL, &m_pDepthStencilView );
	if ( FAILED( hr ) )
		return false;

	m_pContext->OMSetRenderTargets( 1, &m_pRenderTargetView, m_pDepthStencilView );

	D3D11_VIEWPORT viewport;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = (float)nWidth;
	viewport.Height = (float)nHeight;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	m_pContext->RSSetViewports( 1, &viewport );

	g_pD3D11RTV = m_pRenderTargetView;
	g_pD3D11DSV = m_pDepthStencilView;
	return true;
}

void CShaderDeviceDx11::ReleaseBackBufferViews()
{
	g_pD3D11RTV = NULL;
	g_pD3D11DSV = NULL;
	if ( g_pD3D11RTV_SRGB )
	{
		g_pD3D11RTV_SRGB->Release();
		g_pD3D11RTV_SRGB = NULL;
	}

	if ( m_pDepthStencilView )
	{
		m_pDepthStencilView->Release();
		m_pDepthStencilView = NULL;
	}
	if ( m_pDepthStencilTexture )
	{
		m_pDepthStencilTexture->Release();
		m_pDepthStencilTexture = NULL;
	}
	if ( m_pRenderTargetView )
	{
		m_pRenderTargetView->Release();
		m_pRenderTargetView = NULL;
	}
}


//-----------------------------------------------------------------------------
// Shuts down the device
//-----------------------------------------------------------------------------
void CShaderDeviceDx11::ShutdownDevice()
{
	DebugTriangleDx11_Shutdown();
	BlitDx11_ReleaseDevice();
	StateDx11_ReleaseDevice();
	TextureDx11_ReleaseDevice();
	MeshDx11_ReleaseDevice();
	if ( s_pAnnotation )
	{
		s_pAnnotation->Release();
		s_pAnnotation = NULL;
	}
	ReleaseBackBufferViews();

	if ( m_pContext )
	{
		m_pContext->ClearState();
		m_pContext->Flush();
	}

	g_pD3D11Device = NULL;
	g_pD3D11Context = NULL;
	g_pD3D11SwapChain = NULL;

	if ( m_pSwapChain )
	{
		// DXGI requires leaving fullscreen before releasing a swapchain.
		m_pSwapChain->SetFullscreenState( FALSE, NULL );
		m_pSwapChain->Release();
		m_pSwapChain = NULL;
	}

	if ( m_pContext )
	{
		m_pContext->Release();
		m_pContext = NULL;
	}

	if ( m_pDevice )
	{
		if ( m_bUsingDebugLayer )
		{
			ID3D11Debug *pDebug = NULL;
			if ( SUCCEEDED( m_pDevice->QueryInterface( __uuidof(ID3D11Debug), (void**)&pDebug ) ) )
			{
				pDebug->ReportLiveDeviceObjects( D3D11_RLDO_SUMMARY );
				pDebug->Release();
			}
		}
		m_pDevice->Release();
		m_pDevice = NULL;
	}

	if ( m_pOutput )
	{
		m_pOutput->Release();
		m_pOutput = NULL;
	}

	m_hWnd = NULL;
	m_nAdapter = -1;
	m_bInitialized = false;
}


//-----------------------------------------------------------------------------
// Device state
//-----------------------------------------------------------------------------
bool CShaderDeviceDx11::IsDeactivated() const
{
	return !m_bInitialized;
}

int CShaderDeviceDx11::GetCurrentAdapter() const
{
	return m_nAdapter;
}

bool CShaderDeviceDx11::IsUsingGraphics() const
{
	return m_bInitialized;
}

void CShaderDeviceDx11::SpewDriverInfo() const
{
	if ( m_nAdapter >= 0 )
	{
		const HardwareCaps_t &caps = g_ShaderDeviceMgrDx11.GetHardwareCaps( m_nAdapter );
		Warning( "Shader API: Direct3D 11 (FL 11_0)\n" );
		Warning( "Driver: %s (vendor 0x%x, device 0x%x)\n", caps.m_pDriverName, caps.m_VendorID, caps.m_DeviceID );
		Warning( "Backbuffer: %d x %d, %d MSAA samples, vsync %d\n", m_nBackBufferWidth, m_nBackBufferHeight, m_nAASamples, m_bVSync ? 1 : 0 );
	}
	else
	{
		Warning( "Shader API: Direct3D 11 (no device)\n" );
	}
}

ImageFormat CShaderDeviceDx11::GetBackBufferFormat() const
{
	// The swapchain is DXGI R8G8B8A8; report the dx9-era PC backbuffer format
	// so existing material-system assumptions hold. Channel order only matters
	// for readback paths, which handle conversion explicitly (M7).
	return IMAGE_FORMAT_BGRA8888;
}

void CShaderDeviceDx11::GetBackBufferDimensions( int& width, int& height ) const
{
	width = m_nBackBufferWidth ? m_nBackBufferWidth : 1024;
	height = m_nBackBufferHeight ? m_nBackBufferHeight : 768;
}

int CShaderDeviceDx11::StencilBufferBits() const
{
	return 8;
}

bool CShaderDeviceDx11::IsAAEnabled() const
{
	return m_nAASamples > 1;
}

// Drives the video-options AA dropdown (gameui asks per sample count) and the
// engine-side clamp. Both the color and depth formats must support the count.
bool CShaderDeviceDx11::SupportsMSAAMode( int nSamples )
{
	if ( !m_pDevice || nSamples < 2 )
		return false;

	UINT nColorLevels = 0, nDepthLevels = 0;
	if ( FAILED( m_pDevice->CheckMultisampleQualityLevels( DXGI_FORMAT_R8G8B8A8_UNORM, nSamples, &nColorLevels ) ) || !nColorLevels )
		return false;
	if ( FAILED( m_pDevice->CheckMultisampleQualityLevels( DXGI_FORMAT_D24_UNORM_S8_UINT, nSamples, &nDepthLevels ) ) || !nDepthLevels )
		return false;
	return true;
}

//-----------------------------------------------------------------------------
// In-place video mode change (mat_antialias / resolution / vsync edits at
// runtime — CMaterialSystem::OverrideConfig routes them here). A swapchain's
// SampleDesc is immutable, so the swapchain and backbuffer views are
// recreated on the LIVE device: textures, buffers and shaders all survive,
// unlike a full InitDevice teardown.
//-----------------------------------------------------------------------------
bool CShaderDeviceDx11::ChangeVideoMode( const ShaderDeviceInfo_t &mode )
{
	if ( !m_pDevice || !m_pSwapChain || !m_hWnd )
		return false;

	int nWidth = mode.m_DisplayMode.m_nWidth > 0 ? mode.m_DisplayMode.m_nWidth : m_nBackBufferWidth;
	int nHeight = mode.m_DisplayMode.m_nHeight > 0 ? mode.m_DisplayMode.m_nHeight : m_nBackBufferHeight;
	int nAASamples = MAX( 1, mode.m_nAASamples );
	if ( nAASamples > 1 && !SupportsMSAAMode( nAASamples ) )
	{
		Warning( "shaderapidx11: %dxAA not supported; disabling AA.\n", nAASamples );
		nAASamples = 1;
	}

	if ( nWidth == m_nBackBufferWidth && nHeight == m_nBackBufferHeight &&
		 nAASamples == m_nAASamples && mode.m_bWaitForVSync == m_bVSync )
		return true;	// nothing to do

	// Unbind anything that references the old backbuffer, then drop it
	m_pContext->OMSetRenderTargets( 0, NULL, NULL );
	ReleaseBackBufferViews();
	m_pSwapChain->SetFullscreenState( FALSE, NULL );
	m_pSwapChain->Release();
	m_pSwapChain = NULL;
	g_pD3D11SwapChain = NULL;

	m_nAASamples = nAASamples;
	m_bVSync = mode.m_bWaitForVSync;

	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory( &sd, sizeof( sd ) );
	sd.BufferDesc.Width = nWidth;
	sd.BufferDesc.Height = nHeight;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = mode.m_DisplayMode.m_nRefreshRateNumerator;
	sd.BufferDesc.RefreshRate.Denominator = mode.m_DisplayMode.m_nRefreshRateDenominator;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 1;
	sd.OutputWindow = (HWND)m_hWnd;
	sd.Windowed = mode.m_bWindowed ? TRUE : FALSE;
	sd.Flags = mode.m_bWindowed ? 0 : DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	sd.SampleDesc.Count = m_nAASamples;
	sd.SampleDesc.Quality = 0;

	IDXGIDevice *pDXGIDevice = NULL;
	IDXGIAdapter *pAdapter = NULL;
	IDXGIFactory1 *pFactory = NULL;
	HRESULT hr = m_pDevice->QueryInterface( __uuidof( IDXGIDevice ), (void **)&pDXGIDevice );
	if ( SUCCEEDED( hr ) )
		hr = pDXGIDevice->GetAdapter( &pAdapter );
	if ( SUCCEEDED( hr ) )
		hr = pAdapter->GetParent( __uuidof( IDXGIFactory1 ), (void **)&pFactory );
	if ( SUCCEEDED( hr ) )
	{
		hr = pFactory->CreateSwapChain( m_pDevice, &sd, &m_pSwapChain );
		while ( FAILED( hr ) && m_nAASamples > 1 )
		{
			int nFallback = ( m_nAASamples > 2 ) ? m_nAASamples / 2 : 1;
			Warning( "shaderapidx11: %dxAA swapchain failed (hr=0x%08x); retrying with %dxAA.\n",
				m_nAASamples, hr, nFallback );
			m_nAASamples = nFallback;
			sd.SampleDesc.Count = m_nAASamples;
			hr = pFactory->CreateSwapChain( m_pDevice, &sd, &m_pSwapChain );
		}
		if ( SUCCEEDED( hr ) )
			pFactory->MakeWindowAssociation( (HWND)m_hWnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER );
	}
	if ( pFactory ) pFactory->Release();
	if ( pAdapter ) pAdapter->Release();
	if ( pDXGIDevice ) pDXGIDevice->Release();

	if ( FAILED( hr ) || !m_pSwapChain )
	{
		Warning( "shaderapidx11: ChangeVideoMode swapchain recreation failed (hr=0x%08x).\n", hr );
		return false;
	}

	g_pD3D11SwapChain = m_pSwapChain;

	if ( !CreateBackBufferViews( nWidth, nHeight, m_nAASamples, 0 ) )
	{
		Warning( "shaderapidx11: ChangeVideoMode backbuffer view recreation failed.\n" );
		return false;
	}

	m_nBackBufferWidth = nWidth;
	m_nBackBufferHeight = nHeight;
	Dx11_SetDebugName( m_pRenderTargetView, "backbuffer_rtv" );
	Dx11_SetDebugName( g_pD3D11RTV_SRGB, "backbuffer_rtv_srgb" );
	Dx11_SetDebugName( m_pDepthStencilView, "default_dsv" );
	Dx11_SetDebugName( m_pDepthStencilTexture, "default_depth" );

	Msg( "shaderapidx11: video mode changed (%dx%d, %dxAA, vsync %d)\n",
		nWidth, nHeight, m_nAASamples, m_bVSync ? 1 : 0 );
	return true;
}


//-----------------------------------------------------------------------------
// Clears issued by the shader API
//-----------------------------------------------------------------------------
void CShaderDeviceDx11::ClearViews( bool bClearColor, bool bClearDepth, bool bClearStencil, const float pColor[4] )
{
	if ( !m_pContext )
		return;

	if ( bClearColor && m_pRenderTargetView )
	{
		// dx9 Clear() is viewport-scoped; ClearRenderTargetView is whole-target.
		// The engine relies on this (e.g. mid-frame clears with a small
		// viewport set), so clip to the current viewport via ClearView.
		ShaderViewport_t vp;
		bool bPartial = false;
		if ( StateDx11_GetViewports( &vp, 1 ) == 1 )
		{
			int nW = 0, nH = 0;
			GetBackBufferDimensions( nW, nH );
			bPartial = vp.m_nTopLeftX > 0 || vp.m_nTopLeftY > 0 ||
				( vp.m_nWidth > 0 && vp.m_nWidth < nW ) || ( vp.m_nHeight > 0 && vp.m_nHeight < nH );
		}

		ID3D11DeviceContext1 *pCtx1 = NULL;
		if ( bPartial && SUCCEEDED( m_pContext->QueryInterface( __uuidof( ID3D11DeviceContext1 ), (void **)&pCtx1 ) ) )
		{
			D3D11_RECT rect;
			rect.left = vp.m_nTopLeftX;
			rect.top = vp.m_nTopLeftY;
			rect.right = vp.m_nTopLeftX + vp.m_nWidth;
			rect.bottom = vp.m_nTopLeftY + vp.m_nHeight;
			pCtx1->ClearView( m_pRenderTargetView, pColor, &rect, 1 );
			pCtx1->Release();
		}
		else
		{
			if ( pCtx1 )
				pCtx1->Release();
			m_pContext->ClearRenderTargetView( m_pRenderTargetView, pColor );
		}
		m_bFrameCleared = true;
	}

	UINT nDepthFlags = ( bClearDepth ? D3D11_CLEAR_DEPTH : 0 ) | ( bClearStencil ? D3D11_CLEAR_STENCIL : 0 );
	if ( nDepthFlags && m_pDepthStencilView )
	{
		m_pContext->ClearDepthStencilView( m_pDepthStencilView, nDepthFlags, 1.0f, 0 );
	}
}


//-----------------------------------------------------------------------------
// Present
//-----------------------------------------------------------------------------
void CShaderDeviceDx11::Present()
{
	if ( !m_pSwapChain )
		return;

	// (The M1-era "debug clear if nothing cleared" fallback lived here; real
	// frames don't color-clear the backbuffer — the skybox covers it — so the
	// fallback was wiping finished frames at Present once the offscreen-RT
	// clears stopped counting as the frame's color clear.)
	m_bFrameCleared = false;

	DebugTriangleDx11_DrawIfEnabled();

	CheckRenderDocCapture();

	HRESULT hr = m_pSwapChain->Present( m_bVSync ? 1 : 0, 0 );
	if ( hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET )
	{
		HRESULT hrReason = m_pDevice ? m_pDevice->GetDeviceRemovedReason() : hr;
		Error( "shaderapidx11: D3D11 device removed/reset during Present (hr=0x%08x, reason=0x%08x).\n", hr, hrReason );
	}

	++m_nFrameCount;
}

void CShaderDeviceDx11::CheckRenderDocCapture()
{
	if ( !s_pRenderDocAPI || m_nRenderDocCaptureFrame < 0 )
		return;

	if ( m_nFrameCount == m_nRenderDocCaptureFrame )
	{
		Msg( "shaderapidx11: triggering RenderDoc capture at frame %d\n", m_nFrameCount );
		s_pRenderDocAPI->TriggerCapture();
	}
}

// On-demand capture for test tooling (devtools/tf2ctl.py): trigger exactly
// when the scene is staged instead of guessing frame numbers.
CON_COMMAND( dev_rdc_capture, "Trigger a RenderDoc capture of the next frame" )
{
	if ( !s_pRenderDocAPI )
	{
		Warning( "dev_rdc_capture: RenderDoc not attached (launch under renderdoccmd)\n" );
		return;
	}
	s_pRenderDocAPI->TriggerCapture();
	Msg( "dev_rdc_capture: capture triggered\n" );
}

// Live shader iteration: recompile the universal permutations straight from
// the HLSL source tree without restarting (or rebuilding the .vcsx packs).
// No-op when the source tree isn't present (shipping content only has packs):
// requires BOTH the .hlsl and the manifest json. Source dir comes from
// -shadersrcpath, defaulting to the repo location relative to the build dir.
CON_COMMAND( dx11_reload_shaders, "Recompile DX11 universal shaders from HLSL source (no-op without the source tree)" )
{
	const char *pSrcDir = CommandLine()->ParmValue( "-shadersrcpath",
		"..\\materialsystem\\stdshaders_dx11\\hlsl" );

	char szHlsl[MAX_PATH], szManifest[MAX_PATH];
	V_snprintf( szHlsl, sizeof( szHlsl ), "%s\\universal.hlsl", pSrcDir );
	V_snprintf( szManifest, sizeof( szManifest ), "%s\\..\\manifest\\shaders.json", pSrcDir );

	if ( !g_pFullFileSystem || !g_pFullFileSystem->FileExists( szHlsl ) ||
		 !g_pFullFileSystem->FileExists( szManifest ) )
	{
		Msg( "dx11_reload_shaders: shader source tree not found (%s + manifest required); nothing to do\n", szHlsl );
		return;
	}

	float flStart = Plat_FloatTime();
	if ( StateDx11_ReloadUniversalShaders( szHlsl ) )
	{
		Msg( "dx11_reload_shaders: reloaded all universal permutations from %s (%.2f s)\n",
			szHlsl, Plat_FloatTime() - flStart );
	}
	else
	{
		Warning( "dx11_reload_shaders: compile failed — keeping the previous shaders (errors above)\n" );
	}
}


//-----------------------------------------------------------------------------
// Release/reacquire are resize hooks in DX11 (no lost-device model)
//-----------------------------------------------------------------------------
void CShaderDeviceDx11::ReleaseResources()
{
}

void CShaderDeviceDx11::ReacquireResources()
{
}


//-----------------------------------------------------------------------------
// Mesh/buffer/shader creation
//-----------------------------------------------------------------------------
IMesh* CShaderDeviceDx11::CreateStaticMesh( VertexFormat_t format, const char *pTextureBudgetGroup, IMaterial * pMaterial )
{
	return MeshDx11_CreateStatic( format, pMaterial );
}

void CShaderDeviceDx11::DestroyStaticMesh( IMesh* pMesh )
{
	// The shared stub mesh is handed out by a few legacy paths; never free it.
	if ( !pMesh || pMesh == ShaderApiDx11_GetStubMesh( false ) || pMesh == ShaderApiDx11_GetStubMesh( true ) )
		return;
	MeshDx11_DestroyStatic( pMesh );
}

IShaderBuffer* CShaderDeviceDx11::CompileShader( const char *pProgram, size_t nBufLen, const char *pShaderVersion )
{
	static bool s_bWarned = false;
	if ( !s_bWarned )
	{
		s_bWarned = true;
		Warning( "shaderapidx11: CompileShader not implemented until milestone M2.\n" );
	}
	return NULL;
}

VertexShaderHandle_t CShaderDeviceDx11::CreateVertexShader( IShaderBuffer* pShaderBuffer )
{
	return VERTEX_SHADER_HANDLE_INVALID;
}

void CShaderDeviceDx11::DestroyVertexShader( VertexShaderHandle_t hShader )
{
}

GeometryShaderHandle_t CShaderDeviceDx11::CreateGeometryShader( IShaderBuffer* pShaderBuffer )
{
	return GEOMETRY_SHADER_HANDLE_INVALID;
}

void CShaderDeviceDx11::DestroyGeometryShader( GeometryShaderHandle_t hShader )
{
}

PixelShaderHandle_t CShaderDeviceDx11::CreatePixelShader( IShaderBuffer* pShaderBuffer )
{
	return PIXEL_SHADER_HANDLE_INVALID;
}

void CShaderDeviceDx11::DestroyPixelShader( PixelShaderHandle_t hShader )
{
}

IVertexBuffer *CShaderDeviceDx11::CreateVertexBuffer( ShaderBufferType_t type, VertexFormat_t fmt, int nVertexCount, const char *pBudgetGroup )
{
	return ShaderApiDx11_GetStubVertexBuffer();
}

void CShaderDeviceDx11::DestroyVertexBuffer( IVertexBuffer *pVertexBuffer )
{
}

IIndexBuffer *CShaderDeviceDx11::CreateIndexBuffer( ShaderBufferType_t bufferType, MaterialIndexFormat_t fmt, int nIndexCount, const char *pBudgetGroup )
{
	return ShaderApiDx11_GetStubIndexBuffer( bufferType == SHADER_BUFFER_TYPE_DYNAMIC || bufferType == SHADER_BUFFER_TYPE_DYNAMIC_TEMP );
}

void CShaderDeviceDx11::DestroyIndexBuffer( IIndexBuffer *pIndexBuffer )
{
}

IVertexBuffer *CShaderDeviceDx11::GetDynamicVertexBuffer( int nStreamID, VertexFormat_t vertexFormat, bool bBuffered )
{
	return ShaderApiDx11_GetStubVertexBuffer();
}

IIndexBuffer *CShaderDeviceDx11::GetDynamicIndexBuffer( MaterialIndexFormat_t fmt, bool bBuffered )
{
	return ShaderApiDx11_GetStubIndexBuffer( true );
}


//-----------------------------------------------------------------------------
// Gamma ramp via DXGI output (exclusive fullscreen only, same as DX9)
//-----------------------------------------------------------------------------
void CShaderDeviceDx11::SetHardwareGammaRamp( float fGamma, float fGammaTVRangeMin, float fGammaTVRangeMax, float fGammaTVExponent, bool bTVEnabled )
{
	if ( !m_pOutput )
		return;

	int nGammaPoints = g_pHardwareConfig->Caps().m_nGammaControlPointCount;
	if ( nGammaPoints < 2 || nGammaPoints > 1025 )
		return;

	float flMin = g_pHardwareConfig->Caps().m_flMinGammaControlPoint;
	float flMax = g_pHardwareConfig->Caps().m_flMaxGammaControlPoint;

	DXGI_GAMMA_CONTROL gammaControl;
	gammaControl.Scale.Red = gammaControl.Scale.Green = gammaControl.Scale.Blue = 1.0f;
	gammaControl.Offset.Red = gammaControl.Offset.Green = gammaControl.Offset.Blue = 0.0f;
	float flOOCount = 1.0f / ( nGammaPoints - 1 );
	for ( int i = 0; i < nGammaPoints; i++ )
	{
		float flGamma22 = i * flOOCount;
		float flCorrection = pow( flGamma22, fGamma / 2.2f );
		flCorrection = clamp( flCorrection, flMin, flMax );

		gammaControl.GammaCurve[i].Red = flCorrection;
		gammaControl.GammaCurve[i].Green = flCorrection;
		gammaControl.GammaCurve[i].Blue = flCorrection;
	}

	HRESULT hr = m_pOutput->SetGammaControl( &gammaControl );
	if ( FAILED( hr ) )
	{
		Warning( "shaderapidx11: SetGammaControl failed (hr=0x%08x)\n", hr );
	}
}


//-----------------------------------------------------------------------------
// Misc IShaderDevice plumbing
//-----------------------------------------------------------------------------
void CShaderDeviceDx11::EnableNonInteractiveMode( MaterialNonInteractiveMode_t mode, ShaderNonInteractiveInfo_t *pInfo )
{
}

void CShaderDeviceDx11::RefreshFrontBufferNonInteractive()
{
}

void CShaderDeviceDx11::HandleThreadEvent( uint32 threadEvent )
{
}

char *CShaderDeviceDx11::GetDisplayDeviceName()
{
	static char s_szName[MATERIAL_ADAPTER_NAME_LENGTH] = "";
	if ( !s_szName[0] && m_nAdapter >= 0 )
	{
		Q_strncpy( s_szName, g_ShaderDeviceMgrDx11.GetHardwareCaps( m_nAdapter ).m_pDriverName, sizeof( s_szName ) );
	}
	return s_szName;
}
