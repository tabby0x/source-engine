//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: D3D11 device manager + device (migration milestone M1).
//
// Modeled on the abandoned DX10 scaffold (shaderapidx9/shaderdevicedx10.*)
// but targeting feature level 11_0 with a real swapchain/present path.
//
//===========================================================================//

#ifndef SHADERDEVICEDX11_H
#define SHADERDEVICEDX11_H

#ifdef _WIN32
#pragma once
#endif

#include "shaderdevicebase.h"

struct IDXGIFactory1;
struct IDXGIAdapter;
struct IDXGIAdapter1;
struct IDXGIOutput;
struct IDXGISwapChain;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11Texture2D;


//-----------------------------------------------------------------------------
// The DX11 implementation of the shader device manager
//-----------------------------------------------------------------------------
class CShaderDeviceMgrDx11 : public CShaderDeviceMgrBase
{
	typedef CShaderDeviceMgrBase BaseClass;

public:
	CShaderDeviceMgrDx11();
	virtual ~CShaderDeviceMgrDx11();

	// Methods of IAppSystem
	virtual bool Connect( CreateInterfaceFn factory );
	virtual void Disconnect();
	virtual InitReturnVal_t Init();
	virtual void Shutdown();

	// Methods of IShaderDeviceMgr
	virtual int	 GetAdapterCount() const;
	virtual void GetAdapterInfo( int nAdapter, MaterialAdapterInfo_t& info ) const;
	virtual int	 GetModeCount( int nAdapter ) const;
	virtual void GetModeInfo( ShaderDisplayMode_t* pInfo, int nAdapter, int nMode ) const;
	virtual void GetCurrentModeInfo( ShaderDisplayMode_t* pInfo, int nAdapter ) const;
	virtual bool SetAdapter( int nAdapter, int nFlags );
	virtual CreateInterfaceFn SetMode( void *hWnd, int nAdapter, const ShaderDeviceInfo_t& mode );

	// Returns the adapter/output interfaces (caller does not own the adapter ref)
	IDXGIAdapter* GetAdapter( int nAdapter ) const;
	IDXGIOutput* GetAdapterOutput( int nAdapter ) const;

private:
	// Initialize adapter information
	void InitAdapterInfo();

	// Determines hardware caps from DXGI + our FL 11_0 baseline
	bool ComputeCapsFromD3D11( HardwareCaps_t *pCaps, IDXGIAdapter *pAdapter, IDXGIOutput *pOutput );

	// Returns the amount of video memory in bytes for a particular adapter
	virtual int GetVidMemBytes( int nAdapter ) const;

	IDXGIFactory1 *m_pDXGIFactory;
	bool m_bObeyDxCommandlineOverride : 1;
};


//-----------------------------------------------------------------------------
// The DX11 implementation of the shader device
//-----------------------------------------------------------------------------
class CShaderDeviceDx11 : public CShaderDeviceBase
{
public:
	CShaderDeviceDx11();
	virtual ~CShaderDeviceDx11();

	// Methods of IShaderDevice
	virtual int  GetCurrentAdapter() const;
	virtual bool IsUsingGraphics() const;
	virtual void SpewDriverInfo() const;
	virtual ImageFormat GetBackBufferFormat() const;
	virtual void GetBackBufferDimensions( int& width, int& height ) const;
	virtual int  StencilBufferBits() const;
	virtual bool IsAAEnabled() const;
	virtual void Present();
	virtual void ReleaseResources();
	virtual void ReacquireResources();
	virtual IMesh* CreateStaticMesh( VertexFormat_t format, const char *pTextureBudgetGroup, IMaterial * pMaterial );
	virtual void DestroyStaticMesh( IMesh* pMesh );
	virtual IShaderBuffer* CompileShader( const char *pProgram, size_t nBufLen, const char *pShaderVersion );
	virtual VertexShaderHandle_t CreateVertexShader( IShaderBuffer* pShaderBuffer );
	virtual void DestroyVertexShader( VertexShaderHandle_t hShader );
	virtual GeometryShaderHandle_t CreateGeometryShader( IShaderBuffer* pShaderBuffer );
	virtual void DestroyGeometryShader( GeometryShaderHandle_t hShader );
	virtual PixelShaderHandle_t CreatePixelShader( IShaderBuffer* pShaderBuffer );
	virtual void DestroyPixelShader( PixelShaderHandle_t hShader );
	virtual IVertexBuffer *CreateVertexBuffer( ShaderBufferType_t type, VertexFormat_t fmt, int nVertexCount, const char *pBudgetGroup );
	virtual void DestroyVertexBuffer( IVertexBuffer *pVertexBuffer );
	virtual IIndexBuffer *CreateIndexBuffer( ShaderBufferType_t bufferType, MaterialIndexFormat_t fmt, int nIndexCount, const char *pBudgetGroup );
	virtual void DestroyIndexBuffer( IIndexBuffer *pIndexBuffer );
	virtual IVertexBuffer *GetDynamicVertexBuffer( int nStreamID, VertexFormat_t vertexFormat, bool bBuffered );
	virtual IIndexBuffer *GetDynamicIndexBuffer( MaterialIndexFormat_t fmt, bool bBuffered );
	virtual void SetHardwareGammaRamp( float fGamma, float fGammaTVRangeMin, float fGammaTVRangeMax, float fGammaTVExponent, bool bTVEnabled );
	virtual void EnableNonInteractiveMode( MaterialNonInteractiveMode_t mode, ShaderNonInteractiveInfo_t *pInfo );
	virtual void RefreshFrontBufferNonInteractive();
	virtual void HandleThreadEvent( uint32 threadEvent );
	virtual char *GetDisplayDeviceName();

	// Methods of CShaderDeviceBase
	virtual bool InitDevice( void *hWnd, int nAdapter, const ShaderDeviceInfo_t& mode );
	virtual void ShutdownDevice();
	virtual bool IsDeactivated() const;

	// Called by the shader API stub to execute clears against the backbuffer
	void ClearViews( bool bClearColor, bool bClearDepth, bool bClearStencil, const float pColor[4] );

	// A clear was issued this frame (suppresses the debug clear in Present)
	void OnFrameCleared() { m_bFrameCleared = true; }

	// mat_antialias support query (video options dropdown + engine clamp)
	bool SupportsMSAAMode( int nSamples );
	int GetAASamples() const { return m_nAASamples; }

	// In-place swapchain recreation for runtime mat_antialias / mode edits
	bool ChangeVideoMode( const ShaderDeviceInfo_t &mode );

private:
	bool CreateBackBufferViews( int nWidth, int nHeight, int nAASamples, int nAAQuality );
	void ReleaseBackBufferViews();
	void CheckRenderDocCapture();

	ID3D11Device *m_pDevice;
	ID3D11DeviceContext *m_pContext;
	IDXGISwapChain *m_pSwapChain;
	IDXGIOutput *m_pOutput;
	ID3D11RenderTargetView *m_pRenderTargetView;
	ID3D11Texture2D *m_pDepthStencilTexture;
	ID3D11DepthStencilView *m_pDepthStencilView;

	int m_nBackBufferWidth;
	int m_nBackBufferHeight;
	int m_nAASamples;
	int m_nFrameCount;
	int m_nRenderDocCaptureFrame;
	bool m_bVSync : 1;
	bool m_bFrameCleared : 1;
	bool m_bUsingDebugLayer : 1;
};


//-----------------------------------------------------------------------------
// Singleton
//-----------------------------------------------------------------------------
extern CShaderDeviceDx11 *g_pShaderDeviceDx11;
extern CShaderDeviceMgrDx11 *g_pShaderDeviceMgrDx11;

#endif // SHADERDEVICEDX11_H
