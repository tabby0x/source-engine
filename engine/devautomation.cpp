//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Dev-automation channel for driving the engine from test tooling
// (devtools/tf2ctl.py). Opt-in via -devautomation. Commands arrive over a
// local named pipe (\\.\pipe\tf2devctl) - named pipes are host-local, so
// unlike rcon nothing is exposed on the network - and are executed on the
// main thread through the regular command buffer. Console output reaches the
// tool through console.log (-condebug).
//
//===========================================================================//

#include "devautomation.h"

#if defined( _WIN32 )

#include "winlite.h"
#include "tier0/icommandline.h"
#include "tier0/threadtools.h"
#include "tier1/utlvector.h"
#include "tier1/utlstring.h"
#include "cmd.h"

// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"

#define DEVAUTOMATION_PIPE "\\\\.\\pipe\\tf2devctl"

static CThreadFastMutex s_DevAutoMutex;
static CUtlVector<CUtlString> s_DevAutoQueue;
static ThreadHandle_t s_hDevAutoThread;
static volatile bool s_bDevAutoRun;

static void DevAutomation_HandleLine( HANDLE hPipe, char *pLine )
{
	// Strip trailing CR
	int nLen = V_strlen( pLine );
	while ( nLen > 0 && ( pLine[nLen - 1] == '\r' || pLine[nLen - 1] == ' ' ) )
		pLine[--nLen] = '\0';
	if ( !nLen )
		return;

	const char *pReply = "ok\n";
	if ( !V_stricmp( pLine, "ping" ) )
	{
		pReply = "pong\n";
	}
	else
	{
		AUTO_LOCK( s_DevAutoMutex );
		s_DevAutoQueue.AddToTail( CUtlString( pLine ) );
	}

	DWORD nWritten = 0;
	WriteFile( hPipe, pReply, (DWORD)V_strlen( pReply ), &nWritten, NULL );
}

static uintp DevAutomation_ThreadProc( void *pParam )
{
	char szBuf[4096];
	char szLine[4096];
	int nLinePos = 0;

	while ( s_bDevAutoRun )
	{
		HANDLE hPipe = CreateNamedPipeA( DEVAUTOMATION_PIPE,
			PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
			1, 4096, 4096, 0, NULL );
		if ( hPipe == INVALID_HANDLE_VALUE )
		{
			ThreadSleep( 500 );
			continue;
		}

		BOOL bConnected = ConnectNamedPipe( hPipe, NULL ) ||
			GetLastError() == ERROR_PIPE_CONNECTED;
		nLinePos = 0;
		while ( bConnected && s_bDevAutoRun )
		{
			DWORD nRead = 0;
			if ( !ReadFile( hPipe, szBuf, sizeof( szBuf ), &nRead, NULL ) || !nRead )
				break;
			for ( DWORD i = 0; i < nRead; ++i )
			{
				if ( szBuf[i] == '\n' )
				{
					szLine[nLinePos] = '\0';
					DevAutomation_HandleLine( hPipe, szLine );
					nLinePos = 0;
				}
				else if ( nLinePos < (int)sizeof( szLine ) - 1 )
				{
					szLine[nLinePos++] = szBuf[i];
				}
			}
		}

		DisconnectNamedPipe( hPipe );
		CloseHandle( hPipe );
	}
	return 0;
}

void DevAutomation_Init()
{
	if ( !CommandLine()->FindParm( "-devautomation" ) )
		return;
	s_bDevAutoRun = true;
	s_hDevAutoThread = CreateSimpleThread( DevAutomation_ThreadProc, NULL );
	Msg( "devautomation: listening on " DEVAUTOMATION_PIPE "\n" );
}

void DevAutomation_Frame()
{
	if ( !s_hDevAutoThread )
		return;

	CUtlVector<CUtlString> queued;
	{
		AUTO_LOCK( s_DevAutoMutex );
		queued.Swap( s_DevAutoQueue );
	}
	for ( int i = 0; i < queued.Count(); ++i )
	{
		Cbuf_AddText( queued[i].Get() );
		Cbuf_AddText( "\n" );
	}
}

#else // !_WIN32

void DevAutomation_Init() {}
void DevAutomation_Frame() {}

#endif
