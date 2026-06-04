//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "baseprojectile.h"
#include "basecombatweapon_shared.h"

#ifdef GAME_DLL
#include "iscorer.h"
#endif // GAME_DLL


IMPLEMENT_NETWORKCLASS_ALIASED( BaseProjectile, DT_BaseProjectile )

BEGIN_NETWORK_TABLE( CBaseProjectile, DT_BaseProjectile )
#if !defined( CLIENT_DLL )
	SendPropEHandle( SENDINFO( m_hOriginalLauncher ) ),
#else
	RecvPropEHandle( RECVINFO( m_hOriginalLauncher ) ),
#endif // CLIENT_DLL
END_NETWORK_TABLE()

#ifndef CLIENT_DLL
IMPLEMENT_AUTO_LIST( IBaseProjectileAutoList );
#endif // !CLIENT_DLL

#ifdef TF_DLL
CBaseEntity* GetAttackerEntity( CBaseProjectile* pProjectile )
{
	CBaseEntity *pAttacker = pProjectile->GetOriginalLauncher();
	IScorer *pScorerInterface = dynamic_cast<IScorer*>( pAttacker );
	if ( pScorerInterface )
	{
		pAttacker = pScorerInterface->GetScorer();
	}
	else if ( pAttacker && pAttacker->GetOwnerEntity() )
	{
		pAttacker = pAttacker->GetOwnerEntity();
	}

	return pAttacker;
}
#endif // TF_DLL


//-----------------------------------------------------------------------------
// Purpose: Constructor.
//-----------------------------------------------------------------------------
CBaseProjectile::CBaseProjectile()
{
#ifdef GAME_DLL
	m_iDestroyableHitCount = 0;
	m_bCanCollideWithTeammates = false;
#endif
	m_hOriginalLauncher = NULL;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CBaseProjectile::~CBaseProjectile()
{
#ifdef TF_DLL
	IGameEvent *event = gameeventmanager->CreateEvent( "projectile_removed" );
	if ( event )
	{
		item_definition_index_t ownerWeaponDefIndex = INVALID_ITEM_DEF_INDEX;

		CBaseCombatWeapon *pWeapon = dynamic_cast< CBaseCombatWeapon * >( GetOriginalLauncher() );
		if ( pWeapon )
		{
			ownerWeaponDefIndex = pWeapon->GetAttributeContainer()->GetItem()->GetItemDefIndex();
		}

		CBaseEntity *pAttacker = GetAttackerEntity( this );

		if ( !pAttacker || ownerWeaponDefIndex == INVALID_ITEM_DEF_INDEX )
		{
			delete event;
			return;
		}

		event->SetInt( "attacker", pAttacker->entindex() );
		event->SetInt( "weapon_def_index", ownerWeaponDefIndex );
		event->SetInt( "num_hit", m_vecEntsHit.Count() );
		event->SetInt( "num_direct_hit", m_vecEntsDirectHit.Count() );

		gameeventmanager->FireEvent( event, true );
	}
#endif // TF_DLL
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBaseProjectile::SetLauncher( CBaseEntity *pLauncher )
{
	if ( m_hOriginalLauncher == NULL )
	{
		m_hOriginalLauncher = pLauncher;
	}

#ifdef GAME_DLL
	ResetCollideWithTeammates();
#endif // GAME_DLL
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBaseProjectile::Spawn()
{
	BaseClass::Spawn();

#ifdef GAME_DLL
	ResetCollideWithTeammates();
#endif // GAME_DLL
}

#ifdef GAME_DLL

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBaseProjectile::CollideWithTeammatesThink()
{
	m_bCanCollideWithTeammates = true;
}

//-----------------------------------------------------------------------------
// Purpose: Lots of stuff uses this, so centralize here
//-----------------------------------------------------------------------------
bool CBaseProjectile::ShouldTouchNonWorldSolid( CBaseEntity *pOther, const trace_t *pTrace )
{
	if ( !pOther )
		return false;

	if ( !pTrace )
		return false;

	// Used when checking against things like FUNC_BRUSHES
	if ( !pOther->IsWorld() && pOther->GetSolid() == SOLID_VPHYSICS )
	{
		vcollide_t *pOtherVCollide = NULL;
		IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		int nPhysicsCount = 0;

		CPhysCollide *pTriggerCollide = ( modelinfo->GetVCollide( GetModelIndex() ) ) ? modelinfo->GetVCollide( GetModelIndex() )->solids[0] : NULL;
		Assert( pTriggerCollide );
		if ( pTriggerCollide )
		{
			nPhysicsCount = pOther->VPhysicsGetObjectList( pList, ARRAYSIZE( pList ) );
			pOtherVCollide = modelinfo->GetVCollide( pOther->GetModelIndex() );
		}

		struct projectilecollidelist_t
		{
			const CPhysCollide *pCollide;
			Vector origin;
			QAngle angles;
		};

		CUtlVector< projectilecollidelist_t > collideList;

		if ( nPhysicsCount )
		{
			for ( int i = 0; i < nPhysicsCount; i++ )
			{
				const CPhysCollide *pCollide = pList[i]->GetCollide();
				if ( pCollide )
				{
					projectilecollidelist_t element;
					element.pCollide = pCollide;
					pList[i]->GetPosition( &element.origin, &element.angles );
					collideList.AddToTail( element );
				}
			}
		}
		else if ( pOtherVCollide && pOtherVCollide->solidCount )
		{
			projectilecollidelist_t element;
			element.pCollide = pOtherVCollide->solids[0];
			element.origin = pOther->GetAbsOrigin();
			element.angles = pOther->GetAbsAngles();
			collideList.AddToTail( element );
		}
		else
		{
			return false;
		}

		for ( int i = collideList.Count() - 1; i >= 0; --i )
		{
			const projectilecollidelist_t &element = collideList[i];
			trace_t tr;
			physcollision->TraceCollide( pTrace->startpos, element.origin, element.pCollide, element.angles, pTriggerCollide, GetAbsOrigin(), GetAbsAngles(), &tr );
			if ( !tr.DidHit() )
				return false;
		}
	}

	return true;
}

extern float g_flServerCurTime;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBaseProjectile::ResetCollideWithTeammates()
{
	m_bCanCollideWithTeammates = false;

	if ( GetCollideWithTeammatesDelay() == 0.0f )
	{
		m_bCanCollideWithTeammates = true;
	}
	else
	{
		SetContextThink( &CBaseProjectile::CollideWithTeammatesThink, g_flServerCurTime + GetCollideWithTeammatesDelay(), "CollideWithTeammates" );
	}
}

#endif // GAME_DLL

#ifdef TF_DLL
//-----------------------------------------------------------------------------
// Purpose: Fire an event that we hit someone and tally hits over this projectile's life.
//-----------------------------------------------------------------------------
void CBaseProjectile::RecordEnemyPlayerHit( const CBaseEntity* pHitPlayer, bool bDirect )
{
	Assert( pHitPlayer->IsPlayer() );
	if ( pHitPlayer->GetTeamNumber() == GetTeamNumber() )
		return;

	if ( m_vecEntsHit.Find( pHitPlayer->entindex() ) == m_vecEntsHit.InvalidIndex() )
	{
		m_vecEntsHit.AddToTail( pHitPlayer->entindex() );
	}

	if ( bDirect )
	{
		if ( m_vecEntsDirectHit.Find( pHitPlayer->entindex() ) == m_vecEntsDirectHit.InvalidIndex() )
		{
			m_vecEntsDirectHit.AddToTail( pHitPlayer->entindex() );
		}

		IGameEvent *event = gameeventmanager->CreateEvent( "projectile_direct_hit" );
		if ( event )
		{
			item_definition_index_t ownerWeaponDefIndex = INVALID_ITEM_DEF_INDEX;
			CBaseCombatWeapon *pWeapon = dynamic_cast< CBaseCombatWeapon * >( GetOriginalLauncher() );
			if ( pWeapon )
			{
				ownerWeaponDefIndex = pWeapon->GetAttributeContainer()->GetItem()->GetItemDefIndex();
			}

			CBaseEntity *pAttacker = GetAttackerEntity( this );

			if ( !pAttacker )
			{
				delete event;
				return;
			}

			event->SetInt( "attacker", pAttacker->entindex() );
			event->SetInt( "victim", pHitPlayer->entindex() );
			event->SetInt( "weapon_def_index", ownerWeaponDefIndex );

			gameeventmanager->FireEvent( event, true );
		}
	}
}
#endif // TF_DLL
