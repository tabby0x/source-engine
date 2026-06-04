//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Holds the CGCClient class
//
//=============================================================================

#include "stdafx.h"
#include "gcclient.h"
#include "steam/isteamgamecoordinator.h"
#include "gcsdk_gcmessages.pb.h"
#include "tier0/icommandline.h"

#include <stdlib.h>

namespace GCSDK
{

//#define SOCDebug(...) Msg( __VA_ARGS__ )
#define SOCDebug(...) ((void)0)

static bool BReadOnlyInventoryModeEnabled()
{
	const char *pszEnv = getenv( "TF_READONLY_INVENTORY" );
	if ( pszEnv && pszEnv[0] && pszEnv[0] != '0' )
		return true;

	return CommandLine() && CommandLine()->FindParm( "-tf_readonly_inventory" );
}

static bool BReadOnlyInventoryMessageBlocked( uint32 unMsgType )
{
	switch ( unMsgType )
	{
	case 1001:  // k_EMsgGCSetSingleItemPosition
	case 1002:  // k_EMsgGCCraft
	case 1004:  // k_EMsgGCDelete
	case 1006:  // k_EMsgGCNameItem
	case 1007:  // k_EMsgGCUnlockCrate
	case 1009:  // k_EMsgGCPaintItem
	case 1019:  // k_EMsgGCNameBaseItem
	case 1023:  // k_EMsgGCCustomizeItemTexture
	case 1025:  // k_EMsgGCUseItemRequest
	case 1030:  // k_EMsgGCRemoveItemName
	case 1032:  // k_EMsgGCGiftWrapItem
	case 1034:  // k_EMsgGCDeliverGift
	case 1037:  // k_EMsgGCUnwrapGiftRequest
	case 1039:  // k_EMsgGCSetItemStyle
	case 1041:  // k_EMsgGCSortItems
	case 1059:  // k_EMsgGCAdjustItemEquippedState
	case 1062:  // k_EMsgGCItemAcknowledged
	case 1063:  // k_EMsgGCPresets_SelectPresetForClass
	case 1070:  // k_EMsgGCApplyStrangePart
	case 1077:  // k_EMsgGCApplyUpgradeCard
	case 1079:  // k_EMsgGCApplyStrangeRestriction
	case 1082:  // k_EMsgGCApplyXifier
	case 1085:  // k_EMsgGCFulfillDynamicRecipeComponent
	case 1087:  // k_EMsgGCSetItemEffectVerticalOffset
	case 1088:  // k_EMsgGCSetHatEffectUseHeadOrigin
	case 1089:  // k_EMsgGCItemEaterRecharger
	case 1091:  // k_EMsgGCApplyBaseItemXifier
	case 1092:  // k_EMsgGCApplyClassTransmogrifier
	case 1093:  // k_EMsgGCApplyHalloweenSpellbookPage
	case 1100:  // k_EMsgGCSetItemPositions
	case 1501:  // k_EMsgGCTrading_InitiateTradeRequest
	case 1502:  // k_EMsgGCTrading_InitiateTradeResponse
	case 1510:  // k_EMsgGCTrading_CancelSession
	case 1703:  // k_EMsgGCItemPreviewRequest
	case 1705:  // k_EMsgGCItemPreviewExpire
	case 2001:  // k_EMsgGCDev_NewItemRequest
	case 2003:  // k_EMsgGCDev_DebugRollLootRequest
	case 2523:  // k_EMsgGCApplyAutograph
	case 2527:  // k_EMsgGCRequestPassportItemGrant
	case 2531:  // k_EMsgGCItemPurgatory_FinalizePurchase
	case 2533:  // k_EMsgGCItemPurgatory_RefundPurchase
	case 2557:  // k_EMsgGCShuffleCrateContents
	case 2558:  // k_EMsgGCQuestObjective_Progress
	case 2560:  // k_EMsgGCApplyDuckToken
	case 2562:  // k_EMsgGCQuestObjective_PointsChange
	case 2564:  // k_EMsgGCQuestObjective_RequestLoanerItems
	case 2566:  // k_EMsgGCApplyStrangeCountTransfer
	case 2567:  // k_EMsgGCCraftCollectionUpgrade
	case 2568:  // k_EMsgGCCraftHalloweenOffering
	case 2569:  // k_EMsgGCQuestDiscard_Request
	case 2574:  // k_EMsgGCCraftCommonStatClock
	case 5001:  // k_EMsgGCReportWarKill
	case 5018:  // k_EMsgGCVoteKickBanPlayer
	case 5019:  // k_EMsgGCVoteKickBanPlayerResult
	case 5022:  // k_EMsgGCFreeTrial_ChooseMostHelpfulFriend
	case 5030:  // k_EMsgGCFreeTrial_ThankedSomeone
	case 5200:  // k_EMsgGCCoaching_AddToCoaches
	case 5202:  // k_EMsgGCCoaching_RemoveFromCoaches
	case 5206:  // k_EMsgGCCoaching_AskCoach
	case 5211:  // k_EMsgGCCoaching_LikeCurrentCoach
	case 5212:  // k_EMsgGCCoaching_RemoveCurrentCoach
	case 5500:  // k_EMsgGC_Duel_Request
	case 5501:  // k_EMsgGC_Duel_Response
	case 5502:  // k_EMsgGC_Duel_Results
	case 5601:  // k_EMsgGC_Halloween_GrantItem_DEPRECATED
	case 5608:  // k_EMsgGC_Halloween_GrantItem
	case 5612:  // k_EMsgGC_Halloween_ServerBossEvent
	case 5613:  // k_EMsgGC_Halloween_Merasmus2012
	case 5614:  // k_EMsgGC_Halloween_UpdateMerasmusLootLevel
	case 5710:  // k_EMsgGC_Client_UseServerModificationItem
	case 5711:  // k_EMsgGC_Client_UseServerModificationItem_Response
	case 5712:  // k_EMsgGC_GameServer_UseServerModificationItem
	case 5713:  // k_EMsgGC_GameServer_UseServerModificationItem_Response
	case 5714:  // k_EMsgGC_GameServer_ServerModificationItemExpired
	case 6100:  // k_EMsgGC_IncrementKillCountAttribute_DEPRECATED
	case 6235:  // k_EMsgGCAbandonCurrentGame
	case 6270:  // k_EMsgGCReadyUp
	case 6289:  // k_EMsgGCExitMatchmaking
	case 6400:  // k_EMsgGC_UpdatePeriodicEvent
	case 6401:  // k_EMsgGC_DuckLeaderboard_IndividualUpdate
	case 6503:  // k_EMsgGC_ClientSetItemSlotAttribute
	case 6505:  // k_EMsgGC_War_IndividualUpdate
	case 6506:  // k_EMsgGC_War_JoinWar
	case 6512:  // k_EMsgGC_Match_Result
	case 6513:  // k_EMsgGCVoteKickPlayerRequest
	case 6516:  // k_EMsgGC_DailyCompetitiveStatsRollup
	case 6519:  // k_EMsgGC_ReportPlayer
	case 6522:  // k_EMsgGCPlayerLeftMatch
	case 6527:  // k_EMsgGC_AcknowledgeXP
	case 6528:  // k_EMsgGCDataCenterPing_Update
	case 6529:  // k_EMsgGC_NotificationAcknowledge
	case 6535:  // k_EMsgGC_SurveyQuestionResponse
	case 6537:  // k_EMsgGC_NewMatchForLobbyRequest
	case 6539:  // k_EMsgGC_ChangeMatchPlayerTeamsRequest
	case 6541:  // k_EMsgGC_QuestIdentify
	case 6542:  // k_EMsgGC_QuestDevGive
	case 6544:  // k_EMsgGCQuestComplete_Debug
	case 6545:  // k_EMsgGC_QuestMapDebug
	case 6547:  // k_EMsgGC_QuestMapUnlockNode
	case 6549:  // k_EMsgGC_QuestMapPurchaseReward
	case 6550:  // k_EMsgGC_SetDisablePartyQuestProgress
	case 6553:  // k_EMsgGCQuestProgressReport
	case 6554:  // k_EMsgGCParty_SetOptions
	case 6556:  // k_EMsgGCParty_QueueForMatch
	case 6558:  // k_EMsgGCParty_RemoveFromQueue
	case 6560:  // k_EMsgGCParty_InvitePlayer
	case 6561:  // k_EMsgGCParty_RequestJoinPlayer
	case 6562:  // k_EMsgGCParty_SendChat
	case 6564:  // k_EMsgGCQuestNodeTurnIn
	case 6565:  // k_EMsgGCConsumePaintKit
	case 6566:  // k_EMsgGC_Painkit_DevGrant
	case 6567:  // k_EMsgGCParty_QueueForStandby
	case 6569:  // k_EMsgGCParty_RemoveFromStandbyQueue
	case 6571:  // k_EMsgGCParty_ClearPendingPlayer
	case 6573:  // k_EMsgGCParty_ClearOtherPartyRequest
	case 6575:  // k_EMsgGCParty_PromoteToLeader
	case 6576:  // k_EMsgGCParty_KickMember
	case 6577:  // k_EMsgGCQuestStrangeEvent
	case 6578:  // k_EMsgGC_AcceptLobbyInvite
	case 6581:  // k_EMsgGC_ProcessMatchVoteKick
	case 10001: // k_EMsgGCDev_GrantWarKill
		return true;
	default:
		return false;
	}
}

//------------------------------------------------------------------------------
// Purpose: Constructor
//------------------------------------------------------------------------------
CGCClient::CGCClient( ISteamGameCoordinator *pSteamGameCoordinator, bool bGameserver )
: m_pSteamGameCoordinator( NULL ),
	m_memMsg( 0, 1024 ),
#ifndef STEAM
	m_callbackGCMessageAvailable( NULL, NULL ),
#endif
	m_mapSOCache( DefLessFunc(CSteamID) )
{
#ifndef STEAM
	if( bGameserver )
	{
		m_callbackGCMessageAvailable.SetGameserverFlag();
	}
#endif
	if( pSteamGameCoordinator )
	{
		DbgVerify( BInit( pSteamGameCoordinator ) );
	}
}


//------------------------------------------------------------------------------
// Purpose: Constructor
//------------------------------------------------------------------------------
CGCClient::~CGCClient( )
{
	Uninit();

	FOR_EACH_MAP_FAST( m_mapSOCache, i )
	{
		delete m_mapSOCache[i];
	}
	m_mapSOCache.RemoveAll();
}


//------------------------------------------------------------------------------
// Purpose: Performs the every-frame work required by the GC Client. Mostly that
//			means running yielding jobs.
// Inputs:  ulLimitMicroseconds - The target number of microseconds worth of 
//			work to do this time through the loop.
// Outputs: Returns true if there is still work to do that was skipped because
//			time ran out.
//------------------------------------------------------------------------------
bool CGCClient::BMainLoop( uint64 ulLimitMicroseconds, uint64 ulFrameTimeMicroseconds )
{
	// Don't do any work if not initialized
	if ( !m_pSteamGameCoordinator )
		return false;

	CLimitTimer limitTimer;
	limitTimer.SetLimit( ulLimitMicroseconds );
	CJobTime::UpdateJobTime( ulFrameTimeMicroseconds ? ulFrameTimeMicroseconds : k_cMicroSecPerShellFrame );

	bool bWorkRemaining = m_JobMgr.BFrameFuncRunSleepingJobs( limitTimer );
	bWorkRemaining |= m_JobMgr.BFrameFuncRunYieldingJobs( limitTimer );
	return bWorkRemaining;
}


//------------------------------------------------------------------------------
// Purpose: Sends a message to the GC
// Inputs:  unMsgType - the type ID of the message to send
//			pubData - The data for the message we're sending
//			cubData - The number of bytes of data in this message including any
//				variable-lengthed data.
// Outputs: Returns false if the send failed. A return value of true doesn't 
//			mean that the message was necessarily received by the GC just that
//			it didn't fail in obvious ways on the client.
//------------------------------------------------------------------------------
bool CGCClient::BSendMessage( uint32 unMsgType, const uint8 *pubData, uint32 cubData )
{
	uint32 unMsgTypeWithoutProtoFlag = unMsgType & ~k_EMsgProtoBufFlag;
	if ( BReadOnlyInventoryModeEnabled() && BReadOnlyInventoryMessageBlocked( unMsgTypeWithoutProtoFlag ) )
	{
		Warning( "TF read-only inventory mode blocked GC message %u\n", unMsgTypeWithoutProtoFlag );
		return false;
	}

	if( m_pSteamGameCoordinator )
		return m_pSteamGameCoordinator->SendMessage( unMsgType, pubData, cubData ) == k_EGCResultOK;
	else
		return false;
}


//------------------------------------------------------------------------------
// Purpose: Sends a message to the GC
// Inputs:  msg		- The message to send
// Outputs: Returns false if the send failed. A return value of true doesn't 
//			mean that the message was necessarily received by the GC just that
//			it didn't fail in obvious ways on the client.
//------------------------------------------------------------------------------
bool CGCClient::BSendMessage( const CGCMsgBase& msg )
{
	return BSendMessage( msg.Hdr().m_eMsg, msg.PubPkt() + sizeof(GCMsgHdr_t), msg.CubPkt() - sizeof(GCMsgHdr_t) );	
}


//-----------------------------------------------------------------------------
// Purpose: Used to send protobuf messages to the GC
//-----------------------------------------------------------------------------
class CProtoBufGCClientSendHandler : public CProtoBufMsgBase::IProtoBufSendHandler
{
public:
	CProtoBufGCClientSendHandler( CGCClient *pGCClient ) 
		: m_pClient( pGCClient ) {}
	virtual bool BAsyncSend( MsgType_t eMsg, const uint8 *pubMsgBytes, uint32 cubSize ) 
	{	
		g_theMessageList.TallySendMessage( eMsg & ~k_EMsgProtoBufFlag, cubSize );
		VPROF_BUDGET( "CGCClient", VPROF_BUDGETGROUP_STEAM );
		{
			VPROF_BUDGET( "CGCClient - BSendGCMsgToClient (ProtoBuf)", VPROF_BUDGETGROUP_STEAM );
			return m_pClient->BSendMessage( eMsg | k_EMsgProtoBufFlag, pubMsgBytes, cubSize );
		}
	}

private:
	CGCClient *m_pClient;
};


//-----------------------------------------------------------------------------
// Purpose: Sends a message to the given SteamID
//-----------------------------------------------------------------------------
bool CGCClient::BSendMessage( const CProtoBufMsgBase& msg )
{
	CProtoBufGCClientSendHandler sender( this );
	return msg.BAsyncSend( sender );
}


//------------------------------------------------------------------------------
// Purpose: Callback handler for the GCMessageAvailable_t callback. Handles 
//			incoming messages.
// Inputs:	pCallback - the callback from Steam
//------------------------------------------------------------------------------
void CGCClient::OnGCMessageAvailable( GCMessageAvailable_t *pCallback )
{
	uint32 cubData;
	uint32 unMsgType;
	while( m_pSteamGameCoordinator && m_pSteamGameCoordinator->IsMessageAvailable( &cubData ) )
	{
		// Get the size of the full message. sizeof( GCMsgHdr_t ) was not sent in the binary data
		uint32 unFullSize = cubData + sizeof( GCMsgHdr_t );
		m_memMsg.EnsureCapacity( unFullSize );
		uint8 *pFullPacket = m_memMsg.Base();
		uint8 *pPacketFromGC = pFullPacket+sizeof(GCMsgHdr_t);

		EGCResults eResult = m_pSteamGameCoordinator->RetrieveMessage( &unMsgType, pPacketFromGC, m_memMsg.Count() - sizeof( GCMsgHdr_t ), &cubData );
		Assert( eResult == k_EGCResultOK );
		if( eResult == k_EGCResultOK )
		{
			if( unMsgType & k_EMsgProtoBufFlag )
			{
				CNetPacket *pGCPacket = CNetPacketPool::AllocNetPacket();
				pGCPacket->Init( cubData, pPacketFromGC );
				CIMsgNetPacketAutoRelease pMsgNetPacket( pGCPacket );

				// Safety check against malformed packet
				if ( pMsgNetPacket.Get() != NULL )
				{

					// dispatch the packet
					GetJobMgr().BRouteMsgToJob( this, pMsgNetPacket.Get(), JobMsgInfo_t( pMsgNetPacket->GetEMsg(), pMsgNetPacket->GetSourceJobID(), pMsgNetPacket->GetTargetJobID(), k_EServerTypeGCClient ) );

					// keep track of how much we've sent/received this message
					g_theMessageList.TallySendMessage( pMsgNetPacket->GetEMsg(), cubData );
				}

				// release the packet
				pGCPacket->Release();
			}
			else
			{
				Assert( 0 == (unMsgType & k_EMsgProtoBufFlag ) );

				// get the header so we can fix it up
				GCMsgHdrEx_t *pHdr = (GCMsgHdrEx_t *)pFullPacket;
				pHdr->m_eMsg = unMsgType;
				pHdr->m_ulSteamID = CSteamID().ConvertToUint64();

				// make a new packet for the message so we can dispatch it
				// The CNetPacket takes ownership of the buffer allocated above
				CNetPacket *pGCPacket = CNetPacketPool::AllocNetPacket();
				pGCPacket->Init( unFullSize, pFullPacket );
				CIMsgNetPacketAutoRelease pMsgNetPacket( pGCPacket );

				// Safety check against malformed packet
				if ( pMsgNetPacket.Get() != NULL )
				{

					// dispatch the packet
					GetJobMgr().BRouteMsgToJob( this, pMsgNetPacket.Get(), JobMsgInfo_t( pMsgNetPacket->GetEMsg(), pMsgNetPacket->GetSourceJobID(), pMsgNetPacket->GetTargetJobID(), k_EServerTypeGCClient ) );

					// keep track of how much we've sent/received this message
					g_theMessageList.TallySendMessage( pMsgNetPacket->GetEMsg(), cubData );
				}

				// release the packet
				pGCPacket->Release();
			}
		}
	}
}


//------------------------------------------------------------------------------
// Purpose: Performs all the initialization for the GC Client instance
// Outputs: Returns false if the initialization failed
//------------------------------------------------------------------------------
bool CGCClient::BInit( ISteamGameCoordinator *pSteamGameCoordinator )
{
	// Set the job pool size. Threads get lazily created so if no code
	// is using the thread pool, no threads will be created.
	m_JobMgr.SetThreadPoolSize( GetCPUInformation()->m_nLogicalProcessors - 1 );

	MsgRegistrationFromEnumDescriptor( EGCSystemMsg_descriptor(), GCSDK::MT_GC );

	m_pSteamGameCoordinator = pSteamGameCoordinator;
#ifndef STEAM
	m_callbackGCMessageAvailable.Register( this, &CGCClient::OnGCMessageAvailable );
#endif	

	// process any messages that are already waiting
	if( m_pSteamGameCoordinator )
	{
		OnGCMessageAvailable( NULL );
	}
	
	return true;
}


//------------------------------------------------------------------------------
// Purpose: Performs all the uninitialization for the GC Client instance
//------------------------------------------------------------------------------
void CGCClient::Uninit( )
{
#ifndef STEAM
	m_callbackGCMessageAvailable.Unregister();
#endif
	m_pSteamGameCoordinator = NULL;

	// Clear and remove the SO caches
	unsigned short nMapIndex = m_mapSOCache.FirstInorder();
	while ( m_mapSOCache.IsValidIndex( nMapIndex ) )
	{
		unsigned short nNextMapIndex = m_mapSOCache.NextInorder( nMapIndex );

		CGCClientSharedObjectCache *pSOCache = m_mapSOCache[nMapIndex];
		Assert( pSOCache );
		if ( pSOCache )
		{
			// Send notifications, but only if we were actually subscribed
			if ( pSOCache->BIsSubscribed() )
			{
				pSOCache->NotifyUnsubscribe();
			}

			// Delete the entry
			delete pSOCache;
			m_mapSOCache.RemoveAt( nMapIndex );
		}

		nMapIndex = nNextMapIndex;
	}
}


//------------------------------------------------------------------------------
// Purpose: Finds the SO cache for this steam ID. If bCreateIfMissing is false,
//			NULL will be returned if the cache can't be found
//------------------------------------------------------------------------------
CGCClientSharedObjectCache *CGCClient::FindSOCache( const CSteamID & steamID, bool bCreateIfMissing )
{
	CUtlMap< CSteamID, CGCClientSharedObjectCache * >::IndexType_t nCache = m_mapSOCache.Find( steamID );
	if( m_mapSOCache.IsValidIndex( nCache ) )
		return m_mapSOCache[nCache];
	else
	{
		if( bCreateIfMissing )
		{
			Assert( steamID.IsValid() );
			if ( !steamID.IsValid() )
				return NULL;

			CGCClientSharedObjectCache *pCache = new CGCClientSharedObjectCache( steamID );
			m_mapSOCache.Insert( steamID, pCache );
			return pCache;
		}
		else
		{
			return NULL;
		}
	}
}

//------------------------------------------------------------------------------
// Purpose: Add a serialized local SO cache subscription.
//------------------------------------------------------------------------------
CGCClientSharedObjectCache *CGCClient::AddLocalSOCache( const CSteamID &ownerID, void *pubData, uint32 cubData )
{
	Assert( ownerID.IsValid() );
	if ( !ownerID.IsValid() || !pubData || cubData == 0 )
	{
		Warning( "Local SO cache rejected invalid input for owner %s (%u bytes)\n", ownerID.Render(), cubData );
		return NULL;
	}

	CMsgSOCacheSubscribed msg;
	if ( !msg.ParseFromArray( pubData, cubData ) )
	{
		Warning( "Local SO cache protobuf parse failed for owner %s (%u bytes)\n", ownerID.Render(), cubData );
		return NULL;
	}

	if ( msg.owner() == 0 )
	{
		msg.set_owner( ownerID.ConvertToUint64() );
	}

	CGCClientSharedObjectCache *pSOCache = FindSOCache( ownerID, true );
	if ( !pSOCache )
	{
		Warning( "Local SO cache could not create cache for owner %s\n", ownerID.Render() );
		return NULL;
	}

	if ( !pSOCache->BParseCacheSubscribedMsg( msg, true ) )
	{
		Warning( "Local SO cache failed after protobuf parse for owner %s (%d object types)\n", ownerID.Render(), msg.objects_size() );
		return NULL;
	}

	Test_CacheSubscribed( pSOCache->GetOwner() );
	return pSOCache;
}

//------------------------------------------------------------------------------
// Purpose: Remove a locally-loaded SO cache subscription.
//------------------------------------------------------------------------------
void CGCClient::RemoveLocalSOCache( CGCClientSharedObjectCache *pSOCache )
{
	if ( !pSOCache )
		return;

	const CSteamID ownerID = pSOCache->GetOwner();
	CUtlMap< CSteamID, CGCClientSharedObjectCache * >::IndexType_t nCache = m_mapSOCache.Find( ownerID );
	if ( !m_mapSOCache.IsValidIndex( nCache ) || m_mapSOCache[nCache] != pSOCache )
		return;

	if ( pSOCache->BIsSubscribed() )
	{
		pSOCache->NotifyUnsubscribe();
	}

	delete pSOCache;
	m_mapSOCache.RemoveAt( nCache );
}

//------------------------------------------------------------------------------
// Purpose: Add a listener to the SO cache, creating it if necessary
//------------------------------------------------------------------------------
void CGCClient::AddSOCacheListener( const CSteamID &ownerID, ISharedObjectListener *pListener )
{
	Assert( ownerID.IsValid() );
	CGCClientSharedObjectCache *pCache = FindSOCache( ownerID, true );
	Assert( pCache );
	pCache->AddListener( pListener );
}

//------------------------------------------------------------------------------
// Purpose: Remove listener from the SO cache, if he is listening
//------------------------------------------------------------------------------
bool CGCClient::RemoveSOCacheListener( const CSteamID &ownerID, ISharedObjectListener *pListener )
{
	Assert ( this != NULL );		// Damn people - check your pointers before calling!
	Assert( ownerID.IsValid() );
	CGCClientSharedObjectCache *pCache = FindSOCache( ownerID, false );
	if ( pCache == NULL )
		return false; // cache doesn't exist, so we could't have ben listening
	return pCache->RemoveListener( pListener );
}

//------------------------------------------------------------------------------
// Purpose: Notify that the given SO cache has been unsubscribed
//------------------------------------------------------------------------------
void CGCClient::NotifySOCacheUnsubscribed( const CSteamID & ownerID )
{

	CUtlMap< CSteamID, CGCClientSharedObjectCache * >::IndexType_t nCache = m_mapSOCache.Find( ownerID );
	if( m_mapSOCache.IsValidIndex( nCache ) )
	{

		CGCClientSharedObjectCache *pSOCache = m_mapSOCache[nCache];

		// Ignore requests to remove caches that were never subscribed
		if ( pSOCache->BIsSubscribed() )
		{
			SOCDebug( "NotifySOCacheUnsubscribed(%s) [in cache, subscribed]\n", ownerID.Render()  );
			pSOCache->NotifyUnsubscribe();
		}
		else
		{
			SOCDebug( "NotifySOCacheUnsubscribed(%s) [in cache, not subscribed]\n", ownerID.Render()  );
		}
	}
	else
	{
		SOCDebug( "NotifySOCacheUnsubscribed(%s) [not in cache]\n", ownerID.Render()  );
	}
}

//------------------------------------------------------------------------------
// Purpose: Dump everything about everyone
//------------------------------------------------------------------------------
void CGCClient::Dump()
{
	FOR_EACH_MAP( m_mapSOCache, idx )
	{
		m_mapSOCache[ idx ]->Dump();
	}
}

//------------------------------------------------------------------------------
// Purpose: Finds the shared object for this steam ID and key object
//------------------------------------------------------------------------------
CSharedObject *CGCClient::FindSharedObject( const CSteamID & ownerID, const CSharedObject & soIndex ) 
{ 
	CGCClientSharedObjectCache *pCache = FindSOCache( ownerID, false );
	if( pCache )
		return pCache->FindSharedObject( soIndex ); 
	else
		return NULL;
}


//------------------------------------------------------------------------------
// Purpose: Validates all the statics in the GCSDKLib that need to be validated
//			when linked directly into the steam servers.
//------------------------------------------------------------------------------
#ifdef DBGFLAG_VALIDATE
void CGCClient::ValidateStatics( CValidator &validator )
{
	// Validate the global message list
	g_theMessageList.Validate( validator, "g_theMessageList" );

	// Validate the network global memory pool
	g_MemPoolMsg.Validate( validator, "g_MemPoolMsg" );

	CNetPacketPool::ValidateGlobals( validator );

	CJobMgr::ValidateStatics( validator, "CJobMgr" );
	CJob::ValidateStatics( validator, "CJob" );
	ValidateTempTextBuffers( validator );
	CSharedObject::ValidateStatics( validator );

	// validate the SQL access layer
	CRecordBase::ValidateStatics( validator, "CRecordBase" );
	GSchemaFull().Validate( validator, "GSchemaFull" );
	CRecordInfo::ValidateStatics( validator, "CRecordInfo" );
}
#endif // DBGFLAG_VALIDATE


class CGCSOCreateJob : public CGCClientJob
{
public:
	CGCSOCreateJob( CGCClient *pClient ) : CGCClientJob( pClient ) {}

	virtual bool BYieldingRunGCJob( GCSDK::IMsgNetPacket *pNetPacket )
	{
		CProtoBufMsg<CMsgSOSingleObject> msg( pNetPacket );
		SOCDebug( "CGCSOCreateJob(owner=%s, type=%d)\n", CSteamID( msg.Body().owner() ).Render(), msg.Body().type_id() );
		CGCClientSharedObjectCache *pSOCache = m_pGCClient->FindSOCache( msg.Body().owner() );
		if ( pSOCache )
		{
			pSOCache->BCreateFromMsg( msg.Body().type_id(), msg.Body().object_data().data(), msg.Body().object_data().size() );
			Assert( msg.Body().has_version() );
			pSOCache->SetVersion( msg.Body().version() );
		}
		return true;
	}

};

GC_REG_JOB( CGCClient, CGCSOCreateJob, "CGCSOCreateJob", k_ESOMsg_Create, GCSDK::k_EServerTypeGCClient );

class CGCSODestroyJob : public CGCClientJob
{
public:
	CGCSODestroyJob( CGCClient *pClient ) : CGCClientJob( pClient ) {}

	virtual bool BYieldingRunGCJob( GCSDK::IMsgNetPacket *pNetPacket )
	{
		CProtoBufMsg<CMsgSOSingleObject> msg( pNetPacket );
		SOCDebug( "CGCSODestroyJob(owner=%s, type=%d)\n", CSteamID( msg.Body().owner() ).Render(), msg.Body().type_id() );
		CGCClientSharedObjectCache *pCache = m_pGCClient->FindSOCache( msg.Body().owner(), false );
		if( pCache )
		{
			pCache->BDestroyFromMsg( msg.Body().type_id(), msg.Body().object_data().data(), msg.Body().object_data().size() );
			Assert( msg.Body().has_version() );
			pCache->SetVersion( msg.Body().version() );
		}
		return true;
	}

};

GC_REG_JOB( CGCClient, CGCSODestroyJob, "CGCSODestroyJob", k_ESOMsg_Destroy, GCSDK::k_EServerTypeGCClient );

class CGCSOUpdateJob : public CGCClientJob
{
public:
	CGCSOUpdateJob( CGCClient *pClient ) : CGCClientJob( pClient ) {}

	virtual bool BYieldingRunGCJob( GCSDK::IMsgNetPacket *pNetPacket )
	{
		CProtoBufMsg<CMsgSOSingleObject> msg( pNetPacket );
		SOCDebug( "CGCSOUpdateJob(owner=%s, type=%d)\n", CSteamID( msg.Body().owner() ).Render(), msg.Body().type_id() );
		CGCClientSharedObjectCache *pSOCache = m_pGCClient->FindSOCache( msg.Body().owner() );
		if ( pSOCache )
		{
			pSOCache->BUpdateFromMsg( msg.Body().type_id(), msg.Body().object_data().data(), msg.Body().object_data().size() );
			Assert( msg.Body().has_version() );
			pSOCache->SetVersion( msg.Body().version() );
		}
		return true;
	}

};

GC_REG_JOB( CGCClient, CGCSOUpdateJob, "CGCSOUpdateJob", k_ESOMsg_Update, GCSDK::k_EServerTypeGCClient );

class CGCSOUpdateMultipleJob : public CGCClientJob
{
public:
	CGCSOUpdateMultipleJob( CGCClient *pClient ) : CGCClientJob( pClient ) {}

	virtual bool BYieldingRunGCJob( GCSDK::IMsgNetPacket *pNetPacket )
	{
		CProtoBufMsg<CMsgSOMultipleObjects> msg( pNetPacket );
		SOCDebug( "CGCSOUpdateJob(owner=%s)\n", CSteamID( msg.Body().owner() ).Render() );
		CGCClientSharedObjectCache *pSOCache = m_pGCClient->FindSOCache( msg.Body().owner() );
		if ( pSOCache )
		{
			pSOCache->m_context.PreSOUpdate( eSOCacheEvent_Incremental );

			for ( int i = 0; i < msg.Body().objects_size(); ++i )
			{
				const CMsgSOMultipleObjects_SingleObject &objMessage = msg.Body().objects( i );
				SOCDebug( "     type %d\n", objMessage.type_id() );
				pSOCache->BUpdateFromMsg( objMessage.type_id(), objMessage.object_data().data(), objMessage.object_data().size() );
			}

			pSOCache->m_context.PostSOUpdate( eSOCacheEvent_Incremental );
			pSOCache->SetVersion( msg.Body().version() );
		}
		return true;
	}

};

GC_REG_JOB( CGCClient, CGCSOUpdateMultipleJob, "CGCSOUpdateMultipleJob", k_ESOMsg_UpdateMultiple, GCSDK::k_EServerTypeGCClient );

class CGCSOCacheSubscribedJob : public CGCClientJob
{
public:
	CGCSOCacheSubscribedJob( CGCClient *pClient ) : CGCClientJob( pClient ) {}

	virtual bool BYieldingRunGCJob( GCSDK::IMsgNetPacket *pNetPacket )
	{
		CProtoBufMsg< CMsgSOCacheSubscribed > msg ( pNetPacket );
		CGCClientSharedObjectCache *pSOCache = m_pGCClient->FindSOCache( msg.Body().owner(), true );

		Assert( pSOCache );
		if( pSOCache )
		{
			SOCDebug( "CGCSOCacheSubscribedJob(owner=%s) [in cache]\n", CSteamID( msg.Body().owner() ).Render() );
			DbgVerify( pSOCache->BParseCacheSubscribedMsg( msg.Body() ) );
		}
		else
		{
			SOCDebug( "CGCSOCacheSubscribedJob(owner=%s) [not in cache]\n", CSteamID( msg.Body().owner() ).Render() );
		}

		m_pGCClient->Test_CacheSubscribed( pSOCache->GetOwner() );

		return true;
	}

};

GC_REG_JOB( CGCClient, CGCSOCacheSubscribedJob, "CGCSOCacheSubscribedJob", k_ESOMsg_CacheSubscribed, GCSDK::k_EServerTypeGCClient );

class CGCSOCacheUnsubscribedJob : public CGCClientJob
{
public:
	CGCSOCacheUnsubscribedJob( CGCClient *pClient ) : CGCClientJob( pClient ) {}

	virtual bool BYieldingRunGCJob( GCSDK::IMsgNetPacket *pNetPacket )
	{
		CProtoBufMsg< CMsgSOCacheUnsubscribed > msg( pNetPacket );
		SOCDebug( "CGCSOCacheUnsubscribedJob(owner=%s)\n", CSteamID( msg.Body().owner() ).Render() );
		m_pGCClient->NotifySOCacheUnsubscribed( msg.Body().owner() );

		return true;
	}

};

GC_REG_JOB( CGCClient, CGCSOCacheUnsubscribedJob, "CGCSOCacheUnsubscribedJob", k_ESOMsg_CacheUnsubscribed, GCSDK::k_EServerTypeGCClient );

class CGCSOCacheSubscriptionCheck : public CGCClientJob
{
public:
	CGCSOCacheSubscriptionCheck( CGCClient *pClient ) : CGCClientJob( pClient ) {}

	virtual bool BYieldingRunGCJob( GCSDK::IMsgNetPacket *pNetPacket )
	{
		CProtoBufMsg< CMsgSOCacheSubscriptionCheck > msg ( pNetPacket );
		CGCClientSharedObjectCache *pSOCache = m_pGCClient->FindSOCache( msg.Body().owner(), false );

		// if we do not have the cache or it is out-of-date, request a refresh
		if ( pSOCache == NULL || !pSOCache->BIsInitialized() || pSOCache->GetVersion() != msg.Body().version() )
		{
			SOCDebug( "CGCSOCacheSubscriptionCheck(owner=%s) -- need refresh\n", CSteamID( msg.Body().owner() ).Render() );
			CProtoBufMsg< CMsgSOCacheSubscriptionRefresh > msg_response( k_ESOMsg_CacheSubscriptionRefresh );
			msg_response.Body().set_owner( msg.Body().owner() );
			m_pGCClient->BSendMessage( msg_response );
		}
		else
		{
			SOCDebug( "CGCSOCacheSubscriptionCheck(owner=%s) -- up-to-date, no refresh needed\n", CSteamID( msg.Body().owner() ).Render() );

			// This is one method by which the GC notifies us that we are subscribed.
			if ( !pSOCache->BIsSubscribed() )
			{
				pSOCache->NotifyResubscribedUpToDate();
				Assert( pSOCache->BIsSubscribed() );
			}
		}
		return true;
	}

};

GC_REG_JOB( CGCClient, CGCSOCacheSubscriptionCheck, "CGCSOCacheSubscriptionCheck", k_ESOMsg_CacheSubscriptionCheck, GCSDK::k_EServerTypeGCClient );

} // namespace GCSDK
