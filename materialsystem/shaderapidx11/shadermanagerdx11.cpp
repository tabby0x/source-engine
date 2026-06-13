//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX11 shader pack loading (.vcsx v1) + compile-from-source path +
// the M2 toolchain-spike debug triangle. See Plan.md M2 and
// devtools/build_shaders_dx11.py for the pack writer.
//
//===========================================================================//

#include <d3d11.h>
#include <d3dcompiler.h>

#include "shadermanagerdx11.h"
#include "shaderapidx11_global.h"
#include "shaderapi_global.h"
#include "filesystem.h"
#include "tier0/icommandline.h"
#include "tier1/strtools.h"

// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// .vcsx v1 reading (see build_shaders_dx11.py for the layout)
//-----------------------------------------------------------------------------
#pragma pack(push, 1)
struct VcsxHeader_t
{
	char m_Magic[4];
	uint32 m_nVersion;
	uint32 m_nFlags;
	uint32 m_nPermutationCount;
	uint64 m_nSourceHash;
};

struct VcsxRecord_t
{
	uint64 m_nKey;
	uint32 m_nOffset;
	uint32 m_nSize;
};
#pragma pack(pop)

bool ShaderPackDx11_LoadBlob( const char *pPackName, uint64 nKey, CUtlBuffer &blob )
{
	char szPath[MAX_PATH];
	Q_snprintf( szPath, sizeof( szPath ), "shaders/dx11/%s.vcsx", pPackName );

	CUtlBuffer file;
	if ( !g_pFullFileSystem || !g_pFullFileSystem->ReadFile( szPath, "GAME", file ) )
	{
		Warning( "shaderapidx11: missing shader pack %s\n", szPath );
		return false;
	}

	if ( file.TellPut() < (int)sizeof( VcsxHeader_t ) )
		return false;

	const VcsxHeader_t *pHeader = (const VcsxHeader_t *)file.Base();
	if ( Q_strncmp( pHeader->m_Magic, "VCSX", 4 ) != 0 || pHeader->m_nVersion != 1 )
	{
		Warning( "shaderapidx11: %s has bad magic/version\n", szPath );
		return false;
	}

	const VcsxRecord_t *pRecords = (const VcsxRecord_t *)( (const byte *)file.Base() + sizeof( VcsxHeader_t ) );
	for ( uint32 i = 0; i < pHeader->m_nPermutationCount; ++i )
	{
		if ( pRecords[i].m_nKey != nKey )
			continue;
		if ( (int)( pRecords[i].m_nOffset + pRecords[i].m_nSize ) > file.TellPut() )
			return false;
		blob.Clear();
		blob.Put( (const byte *)file.Base() + pRecords[i].m_nOffset, pRecords[i].m_nSize );
		return true;
	}

	Warning( "shaderapidx11: %s has no permutation key %llu\n", szPath, (unsigned long long)nKey );
	return false;
}


//-----------------------------------------------------------------------------
// Runtime HLSL compilation (hot-reload/dev path)
//-----------------------------------------------------------------------------
bool ShaderDx11_CompileFromSource( const char *pHlslPath, const char *pEntry, const char *pTarget, CUtlBuffer &blob,
	const char *pDefineName, int nDefineValue )
{
	CUtlBuffer source;
	if ( !g_pFullFileSystem || !g_pFullFileSystem->ReadFile( pHlslPath, NULL, source ) )
	{
		Warning( "shaderapidx11: cannot read shader source %s\n", pHlslPath );
		return false;
	}

	char szValue[32];
	D3D_SHADER_MACRO macros[2] = { { NULL, NULL }, { NULL, NULL } };
	if ( pDefineName )
	{
		V_snprintf( szValue, sizeof( szValue ), "%d", nDefineValue );
		macros[0].Name = pDefineName;
		macros[0].Definition = szValue;
	}

	ID3DBlob *pCode = NULL;
	ID3DBlob *pErrors = NULL;
	// Debug + no optimization during the migration: RenderDoc shows the exact
	// HLSL source and per-line stepping in captures (M8 revisits for perf).
	HRESULT hr = D3DCompile( source.Base(), source.TellPut(), pHlslPath,
		pDefineName ? macros : NULL, NULL,
		pEntry, pTarget, D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &pCode, &pErrors );

	if ( pErrors )
	{
		if ( FAILED( hr ) )
		{
			Warning( "shaderapidx11: compile failed for %s [%s %s]:\n%s\n",
				pHlslPath, pEntry, pTarget, (const char *)pErrors->GetBufferPointer() );
		}
		pErrors->Release();
	}
	if ( FAILED( hr ) )
		return false;

	blob.Clear();
	blob.Put( pCode->GetBufferPointer(), (int)pCode->GetBufferSize() );
	pCode->Release();
	return true;
}


//-----------------------------------------------------------------------------
// M2 debug triangle harness
//-----------------------------------------------------------------------------
struct DebugTriangleVertex_t
{
	float m_flPos[3];
	float m_flColor[4];
};

static struct DebugTriangleState_t
{
	bool m_bInitAttempted;
	bool m_bReady;
	bool m_bHotReload;
	int m_nFramesSinceMtimeCheck;
	FILETIME m_LastWriteTime;
	char m_szSourcePath[MAX_PATH];
	ID3D11VertexShader *m_pVS;
	ID3D11PixelShader *m_pPS;
	ID3D11InputLayout *m_pLayout;
	ID3D11Buffer *m_pVB;
	ID3D11Buffer *m_pTintCB;
} s_Tri;

static void DebugTriangle_ReleaseShaders()
{
	if ( s_Tri.m_pLayout ) { s_Tri.m_pLayout->Release(); s_Tri.m_pLayout = NULL; }
	if ( s_Tri.m_pVS ) { s_Tri.m_pVS->Release(); s_Tri.m_pVS = NULL; }
	if ( s_Tri.m_pPS ) { s_Tri.m_pPS->Release(); s_Tri.m_pPS = NULL; }
}

static bool DebugTriangle_GetSourceWriteTime( FILETIME &ft )
{
	WIN32_FILE_ATTRIBUTE_DATA data;
	if ( !GetFileAttributesExA( s_Tri.m_szSourcePath, GetFileExInfoStandard, &data ) )
		return false;
	ft = data.ftLastWriteTime;
	return true;
}

static bool DebugTriangle_CreateShaders( bool bFromSource )
{
	CUtlBuffer vsBlob, psBlob;
	if ( bFromSource )
	{
		if ( !ShaderDx11_CompileFromSource( s_Tri.m_szSourcePath, "MainVs", "vs_5_0", vsBlob ) ||
			 !ShaderDx11_CompileFromSource( s_Tri.m_szSourcePath, "MainPs", "ps_5_0", psBlob ) )
			return false;
	}
	else
	{
		if ( !ShaderPackDx11_LoadBlob( "debugtriangle_vs", 0, vsBlob ) ||
			 !ShaderPackDx11_LoadBlob( "debugtriangle_ps", 0, psBlob ) )
			return false;
	}

	ID3D11VertexShader *pVS = NULL;
	ID3D11PixelShader *pPS = NULL;
	ID3D11InputLayout *pLayout = NULL;

	if ( FAILED( D3D11Device()->CreateVertexShader( vsBlob.Base(), vsBlob.TellPut(), NULL, &pVS ) ) ||
		 FAILED( D3D11Device()->CreatePixelShader( psBlob.Base(), psBlob.TellPut(), NULL, &pPS ) ) )
	{
		if ( pVS ) pVS->Release();
		Warning( "shaderapidx11: debug triangle shader creation failed\n" );
		return false;
	}

	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	if ( FAILED( D3D11Device()->CreateInputLayout( layout, 2, vsBlob.Base(), vsBlob.TellPut(), &pLayout ) ) )
	{
		pVS->Release();
		pPS->Release();
		Warning( "shaderapidx11: debug triangle input layout creation failed\n" );
		return false;
	}

	DebugTriangle_ReleaseShaders();
	s_Tri.m_pVS = pVS;
	s_Tri.m_pPS = pPS;
	s_Tri.m_pLayout = pLayout;
	return true;
}

static bool DebugTriangle_Init()
{
	const char *pSrcDir = CommandLine()->ParmValue( "-shadersrcpath", "../materialsystem/stdshaders_dx11/hlsl" );
	Q_snprintf( s_Tri.m_szSourcePath, sizeof( s_Tri.m_szSourcePath ), "%s/debugtriangle.hlsl", pSrcDir );
	s_Tri.m_bHotReload = CommandLine()->FindParm( "-dx11hotreload" ) != 0;

	if ( !DebugTriangle_CreateShaders( s_Tri.m_bHotReload ) )
		return false;
	if ( s_Tri.m_bHotReload )
	{
		DebugTriangle_GetSourceWriteTime( s_Tri.m_LastWriteTime );
	}

	// Clip-space triangle (clockwise = front face for the default rasterizer)
	DebugTriangleVertex_t verts[3] =
	{
		{ {  0.0f,  0.6f, 0.5f }, { 1.0f, 0.2f, 0.2f, 1.0f } },
		{ {  0.6f, -0.6f, 0.5f }, { 0.2f, 1.0f, 0.2f, 1.0f } },
		{ { -0.6f, -0.6f, 0.5f }, { 0.2f, 0.2f, 1.0f, 1.0f } },
	};
	D3D11_BUFFER_DESC vbDesc = { sizeof( verts ), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
	D3D11_SUBRESOURCE_DATA vbData = { verts, 0, 0 };
	if ( FAILED( D3D11Device()->CreateBuffer( &vbDesc, &vbData, &s_Tri.m_pVB ) ) )
		return false;

	const float flTint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	D3D11_BUFFER_DESC cbDesc = { sizeof( flTint ), D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
	D3D11_SUBRESOURCE_DATA cbData = { flTint, 0, 0 };
	if ( FAILED( D3D11Device()->CreateBuffer( &cbDesc, &cbData, &s_Tri.m_pTintCB ) ) )
		return false;

	Msg( "shaderapidx11: debug triangle ready (%s, hot-reload %d)\n",
		s_Tri.m_bHotReload ? "compiled from source" : "loaded from .vcsx pack", s_Tri.m_bHotReload ? 1 : 0 );
	return true;
}

void DebugTriangleDx11_DrawIfEnabled()
{
	if ( !CommandLine()->FindParm( "-dx11triangle" ) )
		return;

	if ( !s_Tri.m_bInitAttempted )
	{
		s_Tri.m_bInitAttempted = true;
		s_Tri.m_bReady = DebugTriangle_Init();
	}
	if ( !s_Tri.m_bReady || !D3D11Context() )
		return;

	if ( s_Tri.m_bHotReload && ++s_Tri.m_nFramesSinceMtimeCheck >= 30 )
	{
		s_Tri.m_nFramesSinceMtimeCheck = 0;
		FILETIME ft;
		if ( DebugTriangle_GetSourceWriteTime( ft ) && CompareFileTime( &ft, &s_Tri.m_LastWriteTime ) != 0 )
		{
			s_Tri.m_LastWriteTime = ft;
			if ( DebugTriangle_CreateShaders( true ) )
			{
				Msg( "shaderapidx11: debug triangle hot-reloaded\n" );
			}
		}
	}

	ID3D11DeviceContext *pCtx = D3D11Context();
	UINT nStride = sizeof( DebugTriangleVertex_t ), nOffset = 0;
	pCtx->IASetInputLayout( s_Tri.m_pLayout );
	pCtx->IASetVertexBuffers( 0, 1, &s_Tri.m_pVB, &nStride, &nOffset );
	pCtx->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	pCtx->VSSetShader( s_Tri.m_pVS, NULL, 0 );
	pCtx->GSSetShader( NULL, NULL, 0 );
	pCtx->PSSetShader( s_Tri.m_pPS, NULL, 0 );
	pCtx->PSSetConstantBuffers( 0, 1, &s_Tri.m_pTintCB );
	pCtx->OMSetRenderTargets( 1, &g_pD3D11RTV, g_pD3D11DSV );
	pCtx->OMSetBlendState( NULL, NULL, 0xFFFFFFFF );
	pCtx->OMSetDepthStencilState( NULL, 0 );
	pCtx->RSSetState( NULL );
	pCtx->Draw( 3, 0 );
}

void DebugTriangleDx11_Shutdown()
{
	DebugTriangle_ReleaseShaders();
	if ( s_Tri.m_pVB ) { s_Tri.m_pVB->Release(); s_Tri.m_pVB = NULL; }
	if ( s_Tri.m_pTintCB ) { s_Tri.m_pTintCB->Release(); s_Tri.m_pTintCB = NULL; }
	s_Tri.m_bInitAttempted = false;
	s_Tri.m_bReady = false;
}
