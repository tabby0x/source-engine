//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX11 dynamic mesh (M3). One shared dynamic vertex/index ring,
// locked with MAP_WRITE_DISCARD on wrap and NO_OVERWRITE for appends,
// mirroring the dx9 dynamicvb.h semantics. Every lock binds with its own
// byte offset so MeshBuilder's indices stay zero-based and 16-bit safe.
//
//===========================================================================//

#include <d3d11.h>

#include "meshdx11.h"
#include "statedx11.h"
#include "texturedx11.h"
#include "shaderapidx11_global.h"
#include "shaderapi_global.h"
#include "shaderapi/ishaderutil.h"
#include "materialsystem/imaterial.h"
#include "imaterialinternal.h"
#include "meshbase.h"
#include "tier1/convar.h"
#include "tier1/strtools.h"

// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"

#define DX11_DYNAMIC_VB_BYTES ( 2 * 1024 * 1024 )
#define DX11_DYNAMIC_IB_INDICES ( 256 * 1024 )

static unsigned short s_ScratchIndices[6];
static float s_ScratchVertices[64];


//-----------------------------------------------------------------------------
// Shared dynamic ring buffers
//-----------------------------------------------------------------------------
struct DynamicRingDx11_t
{
	ID3D11Buffer *m_pVB;
	ID3D11Buffer *m_pIB;
	int m_nVBOffsetBytes;
	int m_nIBOffsetIndices;

	bool Ensure()
	{
		if ( m_pVB || !D3D11Device() )
			return m_pVB && m_pIB;

		D3D11_BUFFER_DESC vb = { DX11_DYNAMIC_VB_BYTES, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
		D3D11_BUFFER_DESC ib = { DX11_DYNAMIC_IB_INDICES * 2, D3D11_USAGE_DYNAMIC, D3D11_BIND_INDEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
		if ( FAILED( D3D11Device()->CreateBuffer( &vb, NULL, &m_pVB ) ) ||
			 FAILED( D3D11Device()->CreateBuffer( &ib, NULL, &m_pIB ) ) )
		{
			Warning( "shaderapidx11: dynamic ring buffer creation failed\n" );
			Release();
			return false;
		}
		Dx11_SetDebugName( m_pVB, "dynamic_ring_vb" );
		Dx11_SetDebugName( m_pIB, "dynamic_ring_ib" );
		m_nVBOffsetBytes = 0;
		m_nIBOffsetIndices = 0;
		return true;
	}

	void Release()
	{
		if ( m_pVB ) { m_pVB->Release(); m_pVB = NULL; }
		if ( m_pIB ) { m_pIB->Release(); m_pIB = NULL; }
		m_nVBOffsetBytes = 0;
		m_nIBOffsetIndices = 0;
	}
};

static DynamicRingDx11_t s_Ring;

// Temporary M4 diagnostics (definitions + dev_mesh_stats below)
extern int g_nMeshDx11_GetDynOverride, g_nMeshDx11_BindBatchHit, g_nMeshDx11_BindBatchMissIdx,
	g_nMeshDx11_BindBatchMissVtx, g_nMeshDx11_LockIdxOnly, g_nMeshDx11_LockIdxOnlyFail,
	g_nMeshDx11_DrawOverride, g_nMeshDx11_DrawDynamic,
	g_nMeshDx11_LockVtxOnly, g_nMeshDx11_LockVtxOnlyFail, g_nMeshDx11_DrawIdxOverride,
	g_nMeshDx11_GetDynIdxOverride;


//-----------------------------------------------------------------------------
// The dynamic mesh
//-----------------------------------------------------------------------------
class CStaticMeshDx11;

class CDynamicMeshDx11 : public IMesh
{
public:
	CDynamicMeshDx11()
	{
		m_VertexFormat = 0;
		m_Type = MATERIAL_TRIANGLES;
		m_nLockedVerts = m_nLockedIndices = 0;
		m_nVBLockOffsetBytes = 0;
		m_nIBLockFirstIndex = 0;
		m_nVertexSize = 0;
		m_bLocked = false;
		m_pVertexOverride = NULL;
		m_pLockedOverride = NULL;
		m_pIndexOverride = NULL;
		m_pLockedIndexOverride = NULL;
	}

	void SetVertexFormat( VertexFormat_t fmt )
	{
		m_VertexFormat = fmt & ~VERTEX_FORMAT_COMPRESSED;
		m_nVertexSize = CVertexBufferBase::VertexFormatSize( m_VertexFormat );
	}

	// Index-only mode: vertices come from a static mesh (the world draw path:
	// engine builds dynamic indices over the world's static vertex buffers).
	void SetVertexOverride( CStaticMeshDx11 *pOverride )
	{
		m_pVertexOverride = pOverride;
	}

	// Vertex-only mode (the reverse): indices come from a static mesh while we
	// supply fresh vertices — the old-format studio paths (SW flex/skin meshes
	// and eyeballs) rebuild a group's VERTICES each frame and draw them through
	// the group's own strip index buffer (GetDynamicMesh[Ex] with ONLY
	// pIndexOverride set). HL2-era models hit this constantly; TF2's
	// delta-flexed models never do, which is how it stayed unimplemented
	// (faces/eyes simply vanished into the dummy stub mesh).
	void SetIndexOverride( CStaticMeshDx11 *pOverride )
	{
		m_pIndexOverride = pOverride;
	}

	// Batch mode (BeginBatch/BindBatch/DrawBatch): the engine built indices for
	// surfaces of MANY static meshes into our ring IB in one index-only lock,
	// and now re-binds each static mesh in turn to draw ranges of that same
	// window. Swap the vertex source but keep the ring-IB lock window intact
	// (dx9: "Overriding with the dynamic index buffer, preserve state!").
	void RetargetVertexOverride( CStaticMeshDx11 *pOverride )
	{
		m_pVertexOverride = pOverride;
		m_pLockedOverride = pOverride;
	}

	// ---- IVertexBuffer / IIndexBuffer ----
	virtual int VertexCount() const { return m_nLockedVerts; }
	virtual int IndexCount() const { return m_nLockedIndices; }
	virtual bool IsDynamic() const { return true; }
	virtual MaterialIndexFormat_t IndexFormat() const { return MATERIAL_INDEX_FORMAT_16BIT; }
	virtual void BeginCastBuffer( VertexFormat_t format ) {}
	virtual void BeginCastBuffer( MaterialIndexFormat_t format ) {}
	virtual void EndCastBuffer() {}
	virtual int GetRoomRemaining() const
	{
		return m_nVertexSize ? ( DX11_DYNAMIC_VB_BYTES - s_Ring.m_nVBOffsetBytes ) / m_nVertexSize : 0;
	}

	virtual bool Lock( int nMaxIndexCount, bool bAppend, IndexDesc_t &desc )
	{
		desc.m_pIndices = s_ScratchIndices;
		desc.m_nIndexSize = 0;
		desc.m_nFirstIndex = 0;
		desc.m_nOffset = 0;
		return false;
	}
	virtual void Unlock( int nWrittenIndexCount, IndexDesc_t &desc ) {}
	virtual void ModifyBegin( bool bReadOnly, int nFirstIndex, int nIndexCount, IndexDesc_t &desc ) {}
	virtual void ModifyEnd( IndexDesc_t &desc ) {}
	virtual void Spew( int nIndexCount, const IndexDesc_t &desc ) {}
	virtual void ValidateData( int nIndexCount, const IndexDesc_t &desc ) {}
	virtual bool Lock( int nVertexCount, bool bAppend, VertexDesc_t &desc )
	{
		CVertexBufferBase::ComputeVertexDescription( (unsigned char *)s_ScratchVertices, 0, desc );
		desc.m_nFirstVertex = 0;
		return false;
	}
	virtual void Unlock( int nVertexCount, VertexDesc_t &desc ) {}
	virtual void Spew( int nVertexCount, const VertexDesc_t &desc ) {}
	virtual void ValidateData( int nVertexCount, const VertexDesc_t &desc ) {}

	// ---- IMesh ----
	virtual void LockMesh( int numVerts, int numIndices, MeshDesc_t &desc )
	{
		if ( m_pVertexOverride )
		{
			LockIndexOnly( numIndices, desc );
			return;
		}
		if ( m_pIndexOverride )
		{
			LockVertexOnly( numVerts, desc );
			return;
		}

		int nVBBytes = numVerts * m_nVertexSize;
		bool bOk = s_Ring.Ensure() && m_nVertexSize && nVBBytes <= DX11_DYNAMIC_VB_BYTES && numIndices <= DX11_DYNAMIC_IB_INDICES;
		if ( !bOk )
		{
			// Suppressed path: hand out scratch memory the caller can write into
			CVertexBufferBase::ComputeVertexDescription( NULL, 0, desc );
			desc.m_nFirstVertex = 0;
			desc.m_pIndices = s_ScratchIndices;
			desc.m_nIndexSize = 0;
			m_bLocked = false;
			return;
		}

		ID3D11DeviceContext *pCtx = D3D11Context();

		bool bVBWrap = s_Ring.m_nVBOffsetBytes + nVBBytes > DX11_DYNAMIC_VB_BYTES;
		if ( bVBWrap )
			s_Ring.m_nVBOffsetBytes = 0;
		bool bIBWrap = s_Ring.m_nIBOffsetIndices + numIndices > DX11_DYNAMIC_IB_INDICES;
		if ( bIBWrap )
			s_Ring.m_nIBOffsetIndices = 0;

		D3D11_MAPPED_SUBRESOURCE vbMap, ibMap;
		if ( FAILED( pCtx->Map( s_Ring.m_pVB, 0, bVBWrap || s_Ring.m_nVBOffsetBytes == 0 ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &vbMap ) ) )
		{
			LockMeshFailed( desc );
			return;
		}
		if ( FAILED( pCtx->Map( s_Ring.m_pIB, 0, bIBWrap || s_Ring.m_nIBOffsetIndices == 0 ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &ibMap ) ) )
		{
			pCtx->Unmap( s_Ring.m_pVB, 0 );
			LockMeshFailed( desc );
			return;
		}

		m_nVBLockOffsetBytes = s_Ring.m_nVBOffsetBytes;
		m_nIBLockFirstIndex = s_Ring.m_nIBOffsetIndices;
		m_pLockedOverride = NULL;
		m_pLockedIndexOverride = NULL;

		CVertexBufferBase::ComputeVertexDescription( (unsigned char *)vbMap.pData + m_nVBLockOffsetBytes, m_VertexFormat, desc );
		desc.m_nFirstVertex = 0;
		desc.VertexDesc_t::m_nOffset = m_nVBLockOffsetBytes;
		desc.m_pIndices = (unsigned short *)ibMap.pData + m_nIBLockFirstIndex;
		desc.m_nIndexSize = 1;
		desc.m_nFirstIndex = 0;
		desc.IndexDesc_t::m_nOffset = 0;

		m_nLockedVerts = numVerts;
		m_nLockedIndices = numIndices;
		m_bLocked = true;
	}

	virtual void UnlockMesh( int numVerts, int numIndices, MeshDesc_t &desc )
	{
		if ( !m_bLocked )
			return;
		ID3D11DeviceContext *pCtx = D3D11Context();
		// Lock-time mode snapshots say which ring buffers were actually mapped:
		// vertex-override locks IB only, index-override locks VB only.
		bool bVBMapped = ( m_pLockedOverride == NULL );
		bool bIBMapped = ( m_pLockedIndexOverride == NULL );
		if ( bVBMapped )
			pCtx->Unmap( s_Ring.m_pVB, 0 );
		if ( bIBMapped )
			pCtx->Unmap( s_Ring.m_pIB, 0 );

		m_nLockedVerts = numVerts;
		m_nLockedIndices = numIndices;
		if ( bVBMapped )
		{
			s_Ring.m_nVBOffsetBytes = m_nVBLockOffsetBytes + numVerts * m_nVertexSize;
			// Keep vertex starts 4-byte aligned for the next lock
			s_Ring.m_nVBOffsetBytes = ( s_Ring.m_nVBOffsetBytes + 3 ) & ~3;
		}
		if ( bIBMapped )
			s_Ring.m_nIBOffsetIndices = m_nIBLockFirstIndex + numIndices;
		m_bLocked = false;
	}

	// Index-only lock for the vertex-override mode
	void LockIndexOnly( int numIndices, MeshDesc_t &desc )
	{
		++g_nMeshDx11_LockIdxOnly;
		bool bOk = s_Ring.Ensure() && numIndices <= DX11_DYNAMIC_IB_INDICES;
		ID3D11DeviceContext *pCtx = D3D11Context();
		D3D11_MAPPED_SUBRESOURCE ibMap;
		if ( bOk )
		{
			bool bIBWrap = s_Ring.m_nIBOffsetIndices + numIndices > DX11_DYNAMIC_IB_INDICES;
			if ( bIBWrap )
				s_Ring.m_nIBOffsetIndices = 0;
			if ( FAILED( pCtx->Map( s_Ring.m_pIB, 0, bIBWrap || s_Ring.m_nIBOffsetIndices == 0 ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &ibMap ) ) )
				bOk = false;
		}
		if ( !bOk )
		{
			++g_nMeshDx11_LockIdxOnlyFail;
			LockMeshFailed( desc );
			return;
		}

		m_nIBLockFirstIndex = s_Ring.m_nIBOffsetIndices;
		m_pLockedOverride = m_pVertexOverride;
		m_pLockedIndexOverride = NULL;

		// No vertex writes in this mode: size-0 scratch descriptors
		CVertexBufferBase::ComputeVertexDescription( (unsigned char *)s_ScratchVertices, 0, desc );
		desc.m_nFirstVertex = 0;
		desc.VertexDesc_t::m_nOffset = 0;
		desc.m_pIndices = (unsigned short *)ibMap.pData + m_nIBLockFirstIndex;
		desc.m_nIndexSize = 1;
		desc.m_nFirstIndex = 0;
		desc.IndexDesc_t::m_nOffset = 0;

		m_nLockedVerts = 0;
		m_nLockedIndices = numIndices;
		m_bLocked = true;
	}

	// Vertex-only lock for the index-override mode: fresh vertices into the
	// ring VB, indices stay the static mesh's own strip IB (drawn by absolute
	// firstIndex/count from IMesh::Draw — studiorender's per-strip loop).
	void LockVertexOnly( int numVerts, MeshDesc_t &desc )
	{
		++g_nMeshDx11_LockVtxOnly;
		int nVBBytes = numVerts * m_nVertexSize;
		bool bOk = s_Ring.Ensure() && m_nVertexSize && nVBBytes > 0 && nVBBytes <= DX11_DYNAMIC_VB_BYTES;
		ID3D11DeviceContext *pCtx = D3D11Context();
		D3D11_MAPPED_SUBRESOURCE vbMap;
		if ( bOk )
		{
			bool bVBWrap = s_Ring.m_nVBOffsetBytes + nVBBytes > DX11_DYNAMIC_VB_BYTES;
			if ( bVBWrap )
				s_Ring.m_nVBOffsetBytes = 0;
			if ( FAILED( pCtx->Map( s_Ring.m_pVB, 0, bVBWrap || s_Ring.m_nVBOffsetBytes == 0 ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &vbMap ) ) )
				bOk = false;
		}
		if ( !bOk )
		{
			++g_nMeshDx11_LockVtxOnlyFail;
			LockMeshFailed( desc );
			return;
		}

		m_nVBLockOffsetBytes = s_Ring.m_nVBOffsetBytes;
		m_pLockedOverride = NULL;
		m_pLockedIndexOverride = m_pIndexOverride;

		CVertexBufferBase::ComputeVertexDescription( (unsigned char *)vbMap.pData + m_nVBLockOffsetBytes, m_VertexFormat, desc );
		desc.m_nFirstVertex = 0;
		desc.VertexDesc_t::m_nOffset = m_nVBLockOffsetBytes;
		// No index writes in this mode
		desc.m_pIndices = s_ScratchIndices;
		desc.m_nIndexSize = 0;
		desc.m_nFirstIndex = 0;
		desc.IndexDesc_t::m_nOffset = 0;

		m_nLockedVerts = numVerts;
		m_nLockedIndices = 0;
		m_bLocked = true;
	}

	virtual void ModifyBeginEx( bool bReadOnly, int firstVertex, int numVerts, int firstIndex, int numIndices, MeshDesc_t &desc )
	{
		CVertexBufferBase::ComputeVertexDescription( NULL, 0, desc );
		desc.m_nFirstVertex = 0;
		desc.m_pIndices = s_ScratchIndices;
		desc.m_nIndexSize = 0;
	}
	virtual void ModifyBegin( int firstVertex, int numVerts, int firstIndex, int numIndices, MeshDesc_t &desc )
	{
		ModifyBeginEx( false, firstVertex, numVerts, firstIndex, numIndices, desc );
	}
	virtual void ModifyEnd( MeshDesc_t &desc ) {}

	virtual void SetPrimitiveType( MaterialPrimitiveType_t type ) { m_Type = type; }

	void DrawWithMaterialPasses( int firstIndex, int numIndices );

	virtual void Draw( int firstIndex, int numIndices )
	{
		if ( !ShaderUtil()->OnDrawMesh( this, firstIndex, numIndices ) )
		{
			MarkAsDrawn();
			return;
		}
		DrawWithMaterialPasses( firstIndex, numIndices );
	}

	virtual void Draw( CPrimList *pPrims, int nPrims )
	{
		if ( !ShaderUtil()->OnDrawMesh( this, pPrims, nPrims ) )
		{
			MarkAsDrawn();
			return;
		}
		for ( int i = 0; i < nPrims; ++i )
		{
			DrawWithMaterialPasses( pPrims[i].m_FirstIndex, pPrims[i].m_NumIndices );
		}
	}

	void DrawInternal( int firstIndex, int numIndices );	// defined below CStaticMeshDx11

	virtual void SetColorMesh( IMesh *pColorMesh, int nVertexOffset ) {}
	virtual void SetFlexMesh( IMesh *pMesh, int nVertexOffset ) {}
	virtual void DisableFlexMesh() {}
	virtual void MarkAsDrawn() {}
	virtual unsigned ComputeMemoryUsed() { return 0; }
	virtual VertexFormat_t GetVertexFormat() const { return m_VertexFormat; }
	virtual IMesh *GetMesh() { return this; }
	virtual void CopyToMeshBuilder( int iStartVert, int nVerts, int iStartIndex, int nIndices, int indexOffset, CMeshBuilder &builder ) {}
	virtual void Spew( int numVerts, int numIndices, const MeshDesc_t &desc ) {}
	virtual void ValidateData( int numVerts, int numIndices, const MeshDesc_t &desc ) {}
	virtual IMaterial *GetMaterial() { return NULL; }

private:
	void LockMeshFailed( MeshDesc_t &desc )
	{
		CVertexBufferBase::ComputeVertexDescription( NULL, 0, desc );
		desc.m_nFirstVertex = 0;
		desc.m_pIndices = s_ScratchIndices;
		desc.m_nIndexSize = 0;
		m_bLocked = false;
		m_nLockedVerts = m_nLockedIndices = 0;
		m_pLockedOverride = NULL;
		m_pLockedIndexOverride = NULL;
	}

	VertexFormat_t m_VertexFormat;
	MaterialPrimitiveType_t m_Type;
	int m_nLockedVerts, m_nLockedIndices;
	int m_nVBLockOffsetBytes;
	int m_nIBLockFirstIndex;
	int m_nVertexSize;
	bool m_bLocked;
	CStaticMeshDx11 *m_pVertexOverride;
	CStaticMeshDx11 *m_pIndexOverride;
	// Snapshots at lock time: a re-entrant GetDynamicMesh (this object is a
	// shared singleton) must not retarget an in-flight locked batch.
	CStaticMeshDx11 *m_pLockedOverride;
	CStaticMeshDx11 *m_pLockedIndexOverride;
};

static CDynamicMeshDx11 s_DynamicMesh;
static IMaterialInternal *s_pBoundMaterial;
static CDynamicMeshDx11 *s_pRenderMesh;
static int s_nRenderFirstIndex;
static int s_nRenderNumIndices;

class CStaticMeshDx11;
static CStaticMeshDx11 *s_pRenderStaticMesh;
static CUtlVector<CStaticMeshDx11 *> s_StaticMeshes;

// Runs the bound material's pass loop (shadow snapshots already taken); each
// pass's shader dynamic block ends in RenderPass(), which lands in
// MeshDx11_RenderPass below and issues the actual GPU draw.
void CDynamicMeshDx11::DrawWithMaterialPasses( int firstIndex, int numIndices )
{
	s_pRenderMesh = this;
	s_pRenderStaticMesh = NULL;
	s_nRenderFirstIndex = firstIndex;
	s_nRenderNumIndices = numIndices;

	if ( s_pBoundMaterial )
	{
		s_pBoundMaterial->DrawMesh( VERTEX_COMPRESSION_NONE );
	}
	else
	{
		DrawInternal( firstIndex, numIndices );
	}

	s_pRenderMesh = NULL;
}


//-----------------------------------------------------------------------------
// Flex mesh: studiorender CPU-morphs facial deltas each frame
// (R_StudioFlexMeshGroup) into this mesh, then attaches it to the studio mesh
// via SetFlexMesh and clears it after the draw via DisableFlexMesh
// (r_studiodraw.cpp:2378). Layout = dx9 stream 2 / meshdx8 GetFlexMesh:
// VERTEX_POSITION | VERTEX_NORMAL | VERTEX_WRINKLE = position delta (12) +
// wrinkle (4) + normal delta (12) = 28 bytes. A dedicated ring keeps
// interleaved dynamic draws (HUD, sprites) from stomping the deltas between
// the fill and the flexed draw (dx9 has its own flex VB for the same reason).
//-----------------------------------------------------------------------------
#define DX11_FLEX_VB_BYTES ( 512 * 1024 )

struct FlexRingDx11_t
{
	ID3D11Buffer *m_pVB;
	int m_nOffsetBytes;

	bool Ensure()
	{
		if ( m_pVB || !D3D11Device() )
			return m_pVB != NULL;
		D3D11_BUFFER_DESC vb = { DX11_FLEX_VB_BYTES, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
		if ( FAILED( D3D11Device()->CreateBuffer( &vb, NULL, &m_pVB ) ) )
			return false;
		Dx11_SetDebugName( m_pVB, "flex_ring_vb" );
		m_nOffsetBytes = 0;
		return true;
	}
	void Release()
	{
		if ( m_pVB ) { m_pVB->Release(); m_pVB = NULL; }
		m_nOffsetBytes = 0;
	}
};
static FlexRingDx11_t s_FlexRing;

class CFlexMeshDx11 : public CDynamicMeshDx11
{
public:
	CFlexMeshDx11()
	{
		m_nFlexVertexSize = 0;
		m_nFlexLockOffsetBytes = 0;
		m_nFlexLockedVerts = 0;
		m_bFlexLocked = false;
	}

	virtual void LockMesh( int numVerts, int numIndices, MeshDesc_t &desc )
	{
		const VertexFormat_t fmt = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_WRINKLE;
		if ( !m_nFlexVertexSize )
			m_nFlexVertexSize = CVertexBufferBase::VertexFormatSize( fmt );

		int nBytes = numVerts * m_nFlexVertexSize;
		if ( !s_FlexRing.Ensure() || nBytes <= 0 || nBytes > DX11_FLEX_VB_BYTES )
		{
			CVertexBufferBase::ComputeVertexDescription( NULL, 0, desc );
			desc.m_nFirstVertex = 0;
			desc.m_pIndices = s_ScratchIndices;
			desc.m_nIndexSize = 0;
			m_bFlexLocked = false;
			return;
		}

		bool bWrap = s_FlexRing.m_nOffsetBytes + nBytes > DX11_FLEX_VB_BYTES;
		if ( bWrap )
			s_FlexRing.m_nOffsetBytes = 0;
		D3D11_MAPPED_SUBRESOURCE map;
		if ( FAILED( D3D11Context()->Map( s_FlexRing.m_pVB, 0,
			( bWrap || s_FlexRing.m_nOffsetBytes == 0 ) ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &map ) ) )
		{
			CVertexBufferBase::ComputeVertexDescription( NULL, 0, desc );
			desc.m_nFirstVertex = 0;
			desc.m_pIndices = s_ScratchIndices;
			desc.m_nIndexSize = 0;
			m_bFlexLocked = false;
			return;
		}

		m_nFlexLockOffsetBytes = s_FlexRing.m_nOffsetBytes;
		CVertexBufferBase::ComputeVertexDescription(
			(unsigned char *)map.pData + m_nFlexLockOffsetBytes, fmt, desc );
		// CMeshBuilder::Begin's out-param is m_nFirstVertex * vertex size; the
		// studiorender flex path hands that product to SetFlexMesh as a BYTE
		// offset, so report the ring position in whole vertices.
		desc.m_nFirstVertex = m_nFlexLockOffsetBytes / m_nFlexVertexSize;
		desc.VertexDesc_t::m_nOffset = m_nFlexLockOffsetBytes;
		desc.m_pIndices = s_ScratchIndices;
		desc.m_nIndexSize = 0;
		desc.m_nFirstIndex = 0;
		desc.IndexDesc_t::m_nOffset = 0;
		m_nFlexLockedVerts = numVerts;
		m_bFlexLocked = true;
	}

	virtual void UnlockMesh( int numVerts, int numIndices, MeshDesc_t &desc )
	{
		if ( !m_bFlexLocked )
			return;
		D3D11Context()->Unmap( s_FlexRing.m_pVB, 0 );
		s_FlexRing.m_nOffsetBytes = m_nFlexLockOffsetBytes + numVerts * m_nFlexVertexSize;
		m_nFlexLockedVerts = numVerts;
		m_bFlexLocked = false;
	}

	virtual int VertexCount() const { return m_nFlexLockedVerts; }
	virtual void MarkAsDrawn() {}

	ID3D11Buffer *GetFlexVB() const { return s_FlexRing.m_pVB; }
	int GetFlexStride() const { return m_nFlexVertexSize; }

private:
	int m_nFlexVertexSize;
	int m_nFlexLockOffsetBytes;
	int m_nFlexLockedVerts;
	bool m_bFlexLocked;
};
static CFlexMeshDx11 s_FlexMesh;

IMesh *MeshDx11_GetFlexMesh()
{
	return &s_FlexMesh;
}


//-----------------------------------------------------------------------------
// Static mesh: owned DEFAULT-usage buffers filled once via a CPU staging copy
// (kept around so ModifyBegin/ModifyEnd can patch and re-upload ranges).
//-----------------------------------------------------------------------------
class CStaticMeshDx11 : public IMesh
{
public:
	CStaticMeshDx11( VertexFormat_t fmt, IMaterial *pMaterial )
	{
		m_VertexFormat = fmt & ~VERTEX_FORMAT_COMPRESSED;
		m_nVertexSize = CVertexBufferBase::VertexFormatSize( m_VertexFormat );
		m_pMaterial = pMaterial;
		m_pVB = m_pIB = NULL;
		m_Type = MATERIAL_TRIANGLES;
		m_nVertexCount = m_nIndexCount = 0;
		m_bBuffersDirty = false;
		m_pColorMesh = NULL;
		m_nColorMeshVertOffsetInBytes = 0;
		m_pFlexMesh = NULL;
		m_nFlexVertOffsetInBytes = 0;
		V_snprintf( m_szDebugName, sizeof( m_szDebugName ), "static:%s",
			pMaterial ? pMaterial->GetName() : "?" );
	}

	~CStaticMeshDx11()
	{
		ReleaseBuffers();
	}

	void ReleaseBuffers()
	{
		if ( m_pVB ) { m_pVB->Release(); m_pVB = NULL; }
		if ( m_pIB ) { m_pIB->Release(); m_pIB = NULL; }
		m_bBuffersDirty = true;
	}

	// ---- IVertexBuffer / IIndexBuffer ----
	virtual int VertexCount() const { return m_nVertexCount; }
	virtual int IndexCount() const { return m_nIndexCount; }
	virtual bool IsDynamic() const { return false; }
	virtual MaterialIndexFormat_t IndexFormat() const { return MATERIAL_INDEX_FORMAT_16BIT; }
	virtual void BeginCastBuffer( VertexFormat_t format ) {}
	virtual void BeginCastBuffer( MaterialIndexFormat_t format ) {}
	virtual void EndCastBuffer() {}
	virtual int GetRoomRemaining() const { return 0; }

	virtual bool Lock( int nMaxIndexCount, bool bAppend, IndexDesc_t &desc )
	{
		desc.m_pIndices = s_ScratchIndices;
		desc.m_nIndexSize = 0;
		desc.m_nFirstIndex = 0;
		desc.m_nOffset = 0;
		return false;
	}
	virtual void Unlock( int nWrittenIndexCount, IndexDesc_t &desc ) {}
	virtual void ModifyBegin( bool bReadOnly, int nFirstIndex, int nIndexCount, IndexDesc_t &desc ) {}
	virtual void ModifyEnd( IndexDesc_t &desc ) {}
	virtual void Spew( int nIndexCount, const IndexDesc_t &desc ) {}
	virtual void ValidateData( int nIndexCount, const IndexDesc_t &desc ) {}
	virtual bool Lock( int nVertexCount, bool bAppend, VertexDesc_t &desc )
	{
		CVertexBufferBase::ComputeVertexDescription( (unsigned char *)s_ScratchVertices, 0, desc );
		desc.m_nFirstVertex = 0;
		return false;
	}
	virtual void Unlock( int nVertexCount, VertexDesc_t &desc ) {}
	virtual void Spew( int nVertexCount, const VertexDesc_t &desc ) {}
	virtual void ValidateData( int nVertexCount, const VertexDesc_t &desc ) {}

	// ---- IMesh ----
	virtual void LockMesh( int numVerts, int numIndices, MeshDesc_t &desc )
	{
		if ( numVerts < 0 || numIndices < 0 || !m_nVertexSize )
		{
			CVertexBufferBase::ComputeVertexDescription( NULL, 0, desc );
			desc.m_nFirstVertex = 0;
			desc.m_pIndices = s_ScratchIndices;
			desc.m_nIndexSize = 0;
			return;
		}

		// Static locks append: world/prop builders lock per-batch.
		int nFirstVert = m_nVertexCount;
		int nFirstIndex = m_nIndexCount;
		m_VertexData.EnsureCount( ( nFirstVert + numVerts ) * m_nVertexSize );
		m_IndexData.EnsureCount( nFirstIndex + numIndices );

		CVertexBufferBase::ComputeVertexDescription(
			(unsigned char *)m_VertexData.Base() + nFirstVert * m_nVertexSize, m_VertexFormat, desc );
		desc.m_nFirstVertex = 0;
		desc.VertexDesc_t::m_nOffset = nFirstVert * m_nVertexSize;
		desc.m_pIndices = m_IndexData.Base() + nFirstIndex;
		desc.m_nIndexSize = 1;
		desc.m_nFirstIndex = 0;
		desc.IndexDesc_t::m_nOffset = nFirstIndex;

		m_nLockFirstVert = nFirstVert;
		m_nLockFirstIndex = nFirstIndex;
	}

	virtual void UnlockMesh( int numVerts, int numIndices, MeshDesc_t &desc )
	{
		m_nVertexCount = m_nLockFirstVert + numVerts;
		m_nIndexCount = m_nLockFirstIndex + numIndices;
		m_bBuffersDirty = true;
	}

	virtual void ModifyBeginEx( bool bReadOnly, int firstVertex, int numVerts, int firstIndex, int numIndices, MeshDesc_t &desc )
	{
		if ( firstVertex < 0 || firstVertex + numVerts > m_nVertexCount ||
			 firstIndex < 0 || firstIndex + numIndices > m_nIndexCount )
		{
			CVertexBufferBase::ComputeVertexDescription( NULL, 0, desc );
			desc.m_nFirstVertex = 0;
			desc.m_pIndices = s_ScratchIndices;
			desc.m_nIndexSize = 0;
			return;
		}
		CVertexBufferBase::ComputeVertexDescription(
			(unsigned char *)m_VertexData.Base() + firstVertex * m_nVertexSize, m_VertexFormat, desc );
		desc.m_nFirstVertex = 0;
		desc.VertexDesc_t::m_nOffset = firstVertex * m_nVertexSize;
		desc.m_pIndices = m_IndexData.Base() + firstIndex;
		desc.m_nIndexSize = 1;
		desc.m_nFirstIndex = 0;
		desc.IndexDesc_t::m_nOffset = firstIndex;
	}
	virtual void ModifyBegin( int firstVertex, int numVerts, int firstIndex, int numIndices, MeshDesc_t &desc )
	{
		ModifyBeginEx( false, firstVertex, numVerts, firstIndex, numIndices, desc );
	}
	virtual void ModifyEnd( MeshDesc_t &desc )
	{
		m_bBuffersDirty = true;
	}

	virtual void SetPrimitiveType( MaterialPrimitiveType_t type ) { m_Type = type; }

	void DrawWithMaterialPasses( int firstIndex, int numIndices )
	{
		s_pRenderStaticMesh = this;
		s_pRenderMesh = NULL;
		s_nRenderFirstIndex = firstIndex;
		s_nRenderNumIndices = numIndices;

		if ( s_pBoundMaterial )
		{
			s_pBoundMaterial->DrawMesh( VERTEX_COMPRESSION_NONE );
		}
		else
		{
			DrawInternal( firstIndex, numIndices );
		}

		s_pRenderStaticMesh = NULL;
	}

	virtual void Draw( int firstIndex, int numIndices )
	{
		if ( !ShaderUtil()->OnDrawMesh( this, firstIndex, numIndices ) )
		{
			MarkAsDrawn();
			return;
		}
		DrawWithMaterialPasses( firstIndex, numIndices );
	}

	virtual void Draw( CPrimList *pPrims, int nPrims )
	{
		if ( !ShaderUtil()->OnDrawMesh( this, pPrims, nPrims ) )
		{
			MarkAsDrawn();
			return;
		}
		for ( int i = 0; i < nPrims; ++i )
		{
			DrawWithMaterialPasses( pPrims[i].m_FirstIndex, pPrims[i].m_NumIndices );
		}
	}

	// Static meshes come in three shapes: full VB+IB (props/studio groups),
	// vertex-only (world batches — indices come from the dynamic ring via the
	// vertex-override path), and INDEX-only (old-format studio SW-skin/flex
	// groups — R_StudioBuildMeshGroup skips the vertex data because the SW
	// path streams fresh vertices every frame; only the strip IB persists).
	// VB and IB are therefore created independently.
	bool EnsureBuffers()
	{
		if ( !m_bBuffersDirty )
			return m_pVB != NULL || m_pIB != NULL;
		// Size the VB from the staging EXTENT, not the unlock-reported vertex
		// count: the pooled color-mesh allocator (l_studio.cpp:4862) locks the
		// whole pool once, unlocks having advanced ZERO vertices, then streams
		// async vhv data through the captured lock pointer with no further mesh
		// calls. dx9 managed VBs pick that up via lazy upload at first draw
		// use; we mirror it — color meshes are never bound before the engine
		// flips m_bColorMeshValid, so first-use creation sees the final bytes.
		int nVBBytes = m_VertexData.Count();
		if ( !D3D11Device() || ( !nVBBytes && !m_nIndexCount ) )
			return false;

		ReleaseBuffers();

		if ( nVBBytes )
		{
			D3D11_BUFFER_DESC vb = { (UINT)nVBBytes, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
			D3D11_SUBRESOURCE_DATA vbData = { m_VertexData.Base(), 0, 0 };
			if ( FAILED( D3D11Device()->CreateBuffer( &vb, &vbData, &m_pVB ) ) )
			{
				Warning( "shaderapidx11: static mesh VB creation failed (%d bytes)\n", nVBBytes );
				ReleaseBuffers();
				return false;
			}
			Dx11_SetDebugName( m_pVB, m_szDebugName );
		}
		if ( m_nIndexCount )
		{
			D3D11_BUFFER_DESC ib = { (UINT)( m_nIndexCount * 2 ), D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
			D3D11_SUBRESOURCE_DATA ibData = { m_IndexData.Base(), 0, 0 };
			if ( FAILED( D3D11Device()->CreateBuffer( &ib, &ibData, &m_pIB ) ) )
			{
				Warning( "shaderapidx11: static mesh IB creation failed (%d indices)\n", m_nIndexCount );
				ReleaseBuffers();
				return false;
			}
			Dx11_SetDebugName( m_pIB, m_szDebugName );
		}
		m_bBuffersDirty = false;
		return true;
	}

	void DrawInternal( int firstIndex, int numIndices )
	{
		// Direct draws need our own VB AND IB; vertex-only meshes draw via the
		// dynamic ring's vertex-override path, index-only meshes via its
		// index-override path.
		if ( !EnsureBuffers() || !m_pIB || !m_pVB )
			return;

		D3D11_PRIMITIVE_TOPOLOGY topology;
		switch ( m_Type )
		{
		case MATERIAL_POINTS: topology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST; break;
		case MATERIAL_LINES: topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST; break;
		case MATERIAL_LINE_STRIP: topology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP; break;
		case MATERIAL_TRIANGLE_STRIP: topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
		default: topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
		}

		// Static meshes with normals = studio geometry (props): lit model path
		StateDx11_SetDrawingStaticMesh( true );
		if ( m_pColorMesh )
		{
			// Baked vertex lighting: stream the prop's color mesh on IA slot 2
			// (dx9 stream 1). Offset is in bytes (ColorMeshInfo_t semantics).
			ID3D11Buffer *pColorVB = m_pColorMesh->GetOverrideVB();
			if ( pColorVB )
			{
				StateDx11_SetStaticColorMesh( pColorVB,
					m_pColorMesh->GetOverrideStride(), m_nColorMeshVertOffsetInBytes );
			}
		}
		if ( m_pFlexMesh && m_pFlexMesh->GetFlexVB() )
		{
			// Facial flex deltas on IA slot 3 (dx9 stream 2)
			StateDx11_SetFlexMesh( m_pFlexMesh->GetFlexVB(),
				m_pFlexMesh->GetFlexStride(), m_nFlexVertOffsetInBytes );
		}
		bool bSetup = StateDx11_SetupForDraw( m_VertexFormat );
		StateDx11_SetStaticColorMesh( NULL, 0, 0 );
		StateDx11_SetFlexMesh( NULL, 0, 0 );
		StateDx11_SetDrawingStaticMesh( false );
		if ( !bSetup )
			return;

		ID3D11DeviceContext *pCtx = D3D11Context();
		UINT nStride = m_nVertexSize;
		UINT nOffset = 0;
		pCtx->IASetVertexBuffers( 0, 1, &m_pVB, &nStride, &nOffset );
		pCtx->IASetIndexBuffer( m_pIB, DXGI_FORMAT_R16_UINT, 0 );
		pCtx->IASetPrimitiveTopology( topology );

		int nCount = numIndices > 0 ? numIndices : m_nIndexCount;
		int nFirst = firstIndex >= 0 ? firstIndex : 0;
		pCtx->DrawIndexed( nCount, nFirst, 0 );
	}

	// Vertex-override support: the dynamic index-only mesh binds our VB
	ID3D11Buffer *GetOverrideVB()
	{
		EnsureBuffers();
		return m_pVB;
	}
	// Index-override support: the dynamic vertex-only mesh binds our strip IB
	ID3D11Buffer *GetOverrideIB()
	{
		EnsureBuffers();
		return m_pIB;
	}
	int GetOverrideStride() const { return m_nVertexSize; }
	VertexFormat_t GetOverrideFormat() const { return m_VertexFormat; }

	// Baked static-prop lighting (dx9 meshdx8.cpp:2522): a VERTEX_SPECULAR
	// mesh whose colors pair 1:1 with our vertices from a byte offset
	// (studiorender passes ColorMeshInfo_t::m_nVertOffsetInBytes). Same
	// trusted downcast as dx9 — color meshes come from CreateStaticMesh.
	virtual void SetColorMesh( IMesh *pColorMesh, int nVertexOffset )
	{
		m_pColorMesh = ( CStaticMeshDx11 * )pColorMesh;
		m_nColorMeshVertOffsetInBytes = nVertexOffset;
	}

	bool HasColorMesh() const { return m_pColorMesh != NULL; }

	// Studio facial flex (dx9 stream 2): studiorender fills the flex mesh
	// (always our GetFlexMesh() instance) and hands it over with a BYTE offset;
	// DisableFlexMesh clears it after the flexed draw (r_studiodraw.cpp:2378).
	virtual void SetFlexMesh( IMesh *pMesh, int nVertexOffset )
	{
		m_pFlexMesh = ( CFlexMeshDx11 * )pMesh;
		m_nFlexVertOffsetInBytes = nVertexOffset;
	}
	virtual void DisableFlexMesh()
	{
		m_pFlexMesh = NULL;
		m_nFlexVertOffsetInBytes = 0;
	}
	bool HasFlexMesh() const { return m_pFlexMesh != NULL; }
	virtual void MarkAsDrawn() {}
	virtual unsigned ComputeMemoryUsed()
	{
		return m_nVertexCount * m_nVertexSize + m_nIndexCount * 2;
	}
	virtual VertexFormat_t GetVertexFormat() const { return m_VertexFormat; }
	virtual IMesh *GetMesh() { return this; }
	virtual void CopyToMeshBuilder( int iStartVert, int nVerts, int iStartIndex, int nIndices, int indexOffset, CMeshBuilder &builder ) {}
	virtual void Spew( int numVerts, int numIndices, const MeshDesc_t &desc ) {}
	virtual void ValidateData( int numVerts, int numIndices, const MeshDesc_t &desc ) {}
	virtual IMaterial *GetMaterial() { return m_pMaterial; }

private:
	VertexFormat_t m_VertexFormat;
	IMaterial *m_pMaterial;
	ID3D11Buffer *m_pVB;
	ID3D11Buffer *m_pIB;
	MaterialPrimitiveType_t m_Type;
	CUtlVector<unsigned char> m_VertexData;
	CUtlVector<unsigned short> m_IndexData;
	int m_nVertexCount, m_nIndexCount;
	int m_nLockFirstVert, m_nLockFirstIndex;
	int m_nVertexSize;
	bool m_bBuffersDirty;
	CStaticMeshDx11 *m_pColorMesh;	// baked prop lighting (drawn on IA slot 2)
	int m_nColorMeshVertOffsetInBytes;
	CFlexMeshDx11 *m_pFlexMesh;		// facial flex deltas (drawn on IA slot 3)
	int m_nFlexVertOffsetInBytes;
	char m_szDebugName[96];
};

// Defined here so the vertex-override mode can reach into CStaticMeshDx11
void CDynamicMeshDx11::DrawInternal( int firstIndex, int numIndices )
{
	if ( !s_Ring.m_pIB )
		return;
	if ( !m_pLockedOverride && ( !m_nLockedVerts || !s_Ring.m_pVB ) )
		return;

	D3D11_PRIMITIVE_TOPOLOGY topology;
	switch ( m_Type )
	{
	case MATERIAL_POINTS: topology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST; break;
	case MATERIAL_LINES: topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST; break;
	case MATERIAL_LINE_STRIP: topology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP; break;
	case MATERIAL_TRIANGLE_STRIP: topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
	// QUADS/POLYGON arrive pre-triangulated by CMeshBuilder's generated indices
	default: topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
	}

	ID3D11DeviceContext *pCtx = D3D11Context();
	UINT nStride, nOffset;
	if ( m_pLockedIndexOverride )
	{
		// Vertex-only draw through a static mesh's strip indices (old-format
		// studio flex/SW-skin + eyeball paths). firstIndex/numIndices are
		// absolute positions in the static IB (studiorender's per-strip loop);
		// the freshly written ring vertices ARE the group's vertex order, so
		// no base-vertex shift is needed. Only studiorender uses this pattern,
		// so it carries the same lit-model-path signal as a static studio
		// draw: SW-skinned/eyeball verts arrive with zero bones pushed and
		// must still pick the rigid MODEL perm (bone 0 = the MODEL stack top,
		// identity for pre-transformed world-space vertices), not vgui/unlit.
		ID3D11Buffer *pIB = m_pLockedIndexOverride->GetOverrideIB();
		StateDx11_SetDrawingStaticMesh( true );
		bool bSetup = pIB && StateDx11_SetupForDraw( m_VertexFormat );
		StateDx11_SetDrawingStaticMesh( false );
		if ( !bSetup )
			return;
		nStride = m_nVertexSize;
		nOffset = m_nVBLockOffsetBytes;
		pCtx->IASetVertexBuffers( 0, 1, &s_Ring.m_pVB, &nStride, &nOffset );
		pCtx->IASetIndexBuffer( pIB, DXGI_FORMAT_R16_UINT, 0 );
		pCtx->IASetPrimitiveTopology( topology );

		int nCount = numIndices > 0 ? numIndices : m_pLockedIndexOverride->IndexCount();
		int nFirst = firstIndex >= 0 ? firstIndex : 0;
		if ( nCount > 0 && m_nLockedVerts > 0 )
		{
			++g_nMeshDx11_DrawIdxOverride;
			pCtx->DrawIndexed( nCount, nFirst, 0 );
		}
		return;
	}
	if ( m_pLockedOverride )
	{
		// Index-only draw over a static mesh's vertices (world batches)
		ID3D11Buffer *pVB = m_pLockedOverride->GetOverrideVB();
		if ( !pVB || !StateDx11_SetupForDraw( m_pLockedOverride->GetOverrideFormat() ) )
			return;
		nStride = m_pLockedOverride->GetOverrideStride();
		nOffset = 0;
		pCtx->IASetVertexBuffers( 0, 1, &pVB, &nStride, &nOffset );
	}
	else
	{
		if ( !StateDx11_SetupForDraw( m_VertexFormat ) )
			return;
		nStride = m_nVertexSize;
		nOffset = m_nVBLockOffsetBytes;
		pCtx->IASetVertexBuffers( 0, 1, &s_Ring.m_pVB, &nStride, &nOffset );
	}
	pCtx->IASetIndexBuffer( s_Ring.m_pIB, DXGI_FORMAT_R16_UINT, 0 );
	pCtx->IASetPrimitiveTopology( topology );

	int nCount = numIndices > 0 ? numIndices : m_nLockedIndices;
	int nFirst = firstIndex >= 0 ? firstIndex : 0;
	if ( m_nLockedIndices > 0 )
	{
		if ( m_pLockedOverride )
			++g_nMeshDx11_DrawOverride;
		else
			++g_nMeshDx11_DrawDynamic;
		pCtx->DrawIndexed( nCount, m_nIBLockFirstIndex + nFirst, 0 );
	}
	else if ( m_nLockedVerts > 0 )
	{
		pCtx->Draw( m_nLockedVerts, 0 );
	}
}

void MeshDx11_BindMaterial( IMaterial *pMaterial )
{
	s_pBoundMaterial = static_cast<IMaterialInternal *>( pMaterial );
	StateDx11_BindMaterialTint( pMaterial );
}

void MeshDx11_RenderPass()
{
	if ( s_pRenderMesh )
	{
		s_pRenderMesh->DrawInternal( s_nRenderFirstIndex, s_nRenderNumIndices );
	}
	else if ( s_pRenderStaticMesh )
	{
		s_pRenderStaticMesh->DrawInternal( s_nRenderFirstIndex, s_nRenderNumIndices );
	}
}

// dx9 GetDX9LightState reads m_pRenderMesh->HasColorMesh() — the mesh being
// drawn by the current material pass loop.
bool MeshDx11_RenderMeshHasColorMesh()
{
	return s_pRenderStaticMesh && s_pRenderStaticMesh->HasColorMesh();
}

IMesh *MeshDx11_GetDynamic( IMaterial *pMaterial, VertexFormat_t fmtOverride )
{
	VertexFormat_t fmt = fmtOverride;
	if ( !fmt && pMaterial )
		fmt = pMaterial->GetVertexFormat();
	s_DynamicMesh.SetVertexFormat( fmt );
	s_DynamicMesh.SetVertexOverride( NULL );
	s_DynamicMesh.SetIndexOverride( NULL );
	return &s_DynamicMesh;
}

IMesh *MeshDx11_GetDynamicWithVertexOverride( IMesh *pVertexOverride )
{
	// Only our own static meshes can act as vertex sources
	CStaticMeshDx11 *pStatic = static_cast<CStaticMeshDx11 *>( pVertexOverride );
	if ( !pStatic || s_StaticMeshes.Find( pStatic ) < 0 )
		return NULL;
	++g_nMeshDx11_GetDynOverride;
	s_DynamicMesh.SetVertexFormat( pStatic->GetOverrideFormat() );
	s_DynamicMesh.SetVertexOverride( pStatic );
	s_DynamicMesh.SetIndexOverride( NULL );
	return &s_DynamicMesh;
}

IMesh *MeshDx11_GetDynamicWithIndexOverride( IMaterial *pMaterial, VertexFormat_t fmtOverride, IMesh *pIndexOverride )
{
	// Only our own static meshes can act as index sources (the old-format
	// studio flex/SW-skin/eyeball paths pass the group's static mesh)
	CStaticMeshDx11 *pStatic = static_cast<CStaticMeshDx11 *>( pIndexOverride );
	if ( !pStatic || s_StaticMeshes.Find( pStatic ) < 0 )
		return NULL;
	++g_nMeshDx11_GetDynIdxOverride;
	VertexFormat_t fmt = fmtOverride;
	if ( !fmt && pMaterial )
		fmt = pMaterial->GetVertexFormat();
	s_DynamicMesh.SetVertexFormat( fmt );
	s_DynamicMesh.SetVertexOverride( NULL );
	s_DynamicMesh.SetIndexOverride( pStatic );
	return &s_DynamicMesh;
}

// Temporary M4 diagnostics: dump per-frame path counters via dev_mesh_stats
int g_nMeshDx11_GetDynOverride, g_nMeshDx11_BindBatchHit, g_nMeshDx11_BindBatchMissIdx,
	g_nMeshDx11_BindBatchMissVtx, g_nMeshDx11_LockIdxOnly, g_nMeshDx11_LockIdxOnlyFail,
	g_nMeshDx11_DrawOverride, g_nMeshDx11_DrawDynamic,
	g_nMeshDx11_LockVtxOnly, g_nMeshDx11_LockVtxOnlyFail, g_nMeshDx11_DrawIdxOverride,
	g_nMeshDx11_GetDynIdxOverride;

IMesh *MeshDx11_BindBatch( IMesh *pVertexOverride, IMesh *pIndexOverride )
{
	// World static chains pass the dynamic mesh itself as the index override:
	// its ring IB already holds the indices from the preceding build lock.
	if ( pIndexOverride != &s_DynamicMesh )
	{
		++g_nMeshDx11_BindBatchMissIdx;
		return NULL;
	}
	CStaticMeshDx11 *pStatic = static_cast<CStaticMeshDx11 *>( pVertexOverride );
	if ( !pStatic || s_StaticMeshes.Find( pStatic ) < 0 )
	{
		++g_nMeshDx11_BindBatchMissVtx;
		return NULL;
	}
	++g_nMeshDx11_BindBatchHit;
	s_DynamicMesh.SetVertexFormat( pStatic->GetOverrideFormat() );
	s_DynamicMesh.RetargetVertexOverride( pStatic );
	s_DynamicMesh.SetIndexOverride( NULL );
	return &s_DynamicMesh;
}

CON_COMMAND( dev_mesh_stats, "Dump DX11 dynamic-mesh path counters (since last call)" )
{
	Msg( "dx11 mesh: getDynOverride=%d bindBatch hit=%d missIdx=%d missVtx=%d lockIdxOnly=%d (fail %d) drawOverride=%d drawDynamic=%d getDynIdxOverride=%d lockVtxOnly=%d (fail %d) drawIdxOverride=%d\n",
		g_nMeshDx11_GetDynOverride, g_nMeshDx11_BindBatchHit, g_nMeshDx11_BindBatchMissIdx,
		g_nMeshDx11_BindBatchMissVtx, g_nMeshDx11_LockIdxOnly, g_nMeshDx11_LockIdxOnlyFail,
		g_nMeshDx11_DrawOverride, g_nMeshDx11_DrawDynamic,
		g_nMeshDx11_GetDynIdxOverride, g_nMeshDx11_LockVtxOnly, g_nMeshDx11_LockVtxOnlyFail,
		g_nMeshDx11_DrawIdxOverride );
	g_nMeshDx11_GetDynOverride = g_nMeshDx11_BindBatchHit = g_nMeshDx11_BindBatchMissIdx =
		g_nMeshDx11_BindBatchMissVtx = g_nMeshDx11_LockIdxOnly = g_nMeshDx11_LockIdxOnlyFail =
		g_nMeshDx11_DrawOverride = g_nMeshDx11_DrawDynamic =
		g_nMeshDx11_GetDynIdxOverride = g_nMeshDx11_LockVtxOnly = g_nMeshDx11_LockVtxOnlyFail =
		g_nMeshDx11_DrawIdxOverride = 0;
}

IMesh *MeshDx11_CreateStatic( VertexFormat_t fmt, IMaterial *pMaterial )
{
	if ( !fmt && pMaterial )
		fmt = pMaterial->GetVertexFormat();
	CStaticMeshDx11 *pMesh = new CStaticMeshDx11( fmt, pMaterial );
	s_StaticMeshes.AddToTail( pMesh );
	return pMesh;
}

void MeshDx11_DestroyStatic( IMesh *pMesh )
{
	if ( !pMesh )
		return;
	CStaticMeshDx11 *pStatic = static_cast<CStaticMeshDx11 *>( pMesh );
	s_StaticMeshes.FindAndRemove( pStatic );
	delete pStatic;
}

int MeshDx11_DynamicVBSize()
{
	return DX11_DYNAMIC_VB_BYTES;
}

void MeshDx11_ReleaseDevice()
{
	s_Ring.Release();
	s_FlexRing.Release();
	for ( int i = 0; i < s_StaticMeshes.Count(); ++i )
	{
		s_StaticMeshes[i]->ReleaseBuffers();
	}
}
