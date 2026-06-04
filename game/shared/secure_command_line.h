//====== Copyright  Valve Corporation, All rights reserved. =================
//
// Secure Command Line parsing to be able to safely launch with untrusted launch parameters.
// 
//=============================================================================
#ifndef SECURE_COMMAND_LINE_H
#define SECURE_COMMAND_LINE_H
#if defined( COMPILER_MSVC )
#pragma once
#endif

#include "steam/steam_api.h"

// Should parse the command line and do whatever handling it wants. The command line coming in
// should be considered actively hostile, from an untrusted source. Note that this is not the 
// complete command line--only the options that are from an untrusted source (the others were
// passed directly to us on the command line already).
using FnUnsafeCmdLineProcessor = void( const char *pchUnsafeCmdLine, int cubSize, CSteamID srcSteamID );

class CSecureLaunchSystem : public CAutoGameSystem
{
public:
	CSecureLaunchSystem( FnUnsafeCmdLineProcessor *pfnUnsafeCmdLineProcessor );
	virtual ~CSecureLaunchSystem();

	STEAM_CALLBACK( CSecureLaunchSystem, OnNewUrlLaunchParameters, NewUrlLaunchParameters_t );
	STEAM_CALLBACK( CSecureLaunchSystem, OnGameRichPresenceJoinRequested, GameRichPresenceJoinRequested_t );

	void ProcessCommandLine();

private:
	FnUnsafeCmdLineProcessor *m_pfnUnsafeCmdLineProcessor;
};

void RegisterSecureLaunchProcessFunc( FnUnsafeCmdLineProcessor *pfnUnsafeCmdLineProcessor );
void ProcessInsecureLaunchParameters();

#ifndef TF_CLIENT_DLL
void UnsafeCmdLineProcessor( const char *pchUnsafeCmdLine, int cubSize, CSteamID srcSteamID );
#else
void TFUnsafeCmdLineProcessor( const char *pchUnsafeCmdLine, int cubSize, CSteamID srcSteamID );
#endif

#endif // SECURE_COMMAND_LINE_H
