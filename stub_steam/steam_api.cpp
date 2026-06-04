#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define STEAM_API_EXPORTS

#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <string.h>

#if defined __GNUC__
 #define S_API extern "C" __attribute__ ((visibility("default"))) 
#elif defined _MSC_VER
#define S_API extern "C" __declspec(dllexport)
#endif
#ifndef NULL
#define NULL 0
#endif

static char g_szMiniDumpComment[2048];
static unsigned int g_unBreakpadAppID;

static void GetMiniDumpDirectory( char *pchOut, size_t cubOut )
{
	DWORD cch = GetEnvironmentVariableA( "SOURCE_ENGINE_STUB_DUMP_DIR", pchOut, (DWORD)cubOut );
	if ( cch == 0 || cch >= cubOut )
	{
		GetCurrentDirectoryA( (DWORD)cubOut, pchOut );
	}
	CreateDirectoryA( pchOut, NULL );
}

static void AppendMiniDumpLog( const char *pchDumpDir, const char *pchMessage )
{
	char szLogPath[MAX_PATH];
	snprintf( szLogPath, sizeof( szLogPath ), "%s\\steam_api_minidump.log", pchDumpDir );

	FILE *fp = fopen( szLogPath, "ab" );
	if ( fp )
	{
		fprintf( fp, "%s\r\n", pchMessage );
		fclose( fp );
	}
}

static void WriteLocalMiniDump( unsigned int uStructuredExceptionCode, void *pvExceptionInfo, unsigned int uBuildID )
{
	char szDumpDir[MAX_PATH];
	GetMiniDumpDirectory( szDumpDir, sizeof( szDumpDir ) );

	SYSTEMTIME st;
	GetLocalTime( &st );

	char szDumpPath[MAX_PATH];
	snprintf(
		szDumpPath,
		sizeof( szDumpPath ),
		"%s\\hl2_launcher_%04u%02u%02u_%02u%02u%02u_%lu_%08x_%u.dmp",
		szDumpDir,
		st.wYear,
		st.wMonth,
		st.wDay,
		st.wHour,
		st.wMinute,
		st.wSecond,
		GetCurrentProcessId(),
		uStructuredExceptionCode,
		uBuildID );

	HMODULE hDbgHelp = LoadLibraryA( "DbgHelp.dll" );
	if ( !hDbgHelp )
	{
		AppendMiniDumpLog( szDumpDir, "SteamAPI_WriteMiniDump: failed to load DbgHelp.dll" );
		return;
	}

	typedef BOOL ( WINAPI *MiniDumpWriteDumpFn )( HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION );
	MiniDumpWriteDumpFn pfnMiniDumpWriteDump = (MiniDumpWriteDumpFn)GetProcAddress( hDbgHelp, "MiniDumpWriteDump" );
	if ( !pfnMiniDumpWriteDump )
	{
		AppendMiniDumpLog( szDumpDir, "SteamAPI_WriteMiniDump: failed to find MiniDumpWriteDump" );
		FreeLibrary( hDbgHelp );
		return;
	}

	HANDLE hFile = CreateFileA( szDumpPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hFile == INVALID_HANDLE_VALUE )
	{
		AppendMiniDumpLog( szDumpDir, "SteamAPI_WriteMiniDump: failed to create dump file" );
		FreeLibrary( hDbgHelp );
		return;
	}

	MINIDUMP_EXCEPTION_INFORMATION exInfo;
	exInfo.ThreadId = GetCurrentThreadId();
	exInfo.ExceptionPointers = (_EXCEPTION_POINTERS *)pvExceptionInfo;
	exInfo.ClientPointers = FALSE;

	MINIDUMP_USER_STREAM userStream;
	userStream.Type = CommentStreamA;
	userStream.Buffer = g_szMiniDumpComment;
	userStream.BufferSize = (ULONG)strlen( g_szMiniDumpComment ) + 1;

	MINIDUMP_USER_STREAM_INFORMATION userStreamInfo;
	userStreamInfo.UserStreamCount = g_szMiniDumpComment[0] ? 1 : 0;
	userStreamInfo.UserStreamArray = g_szMiniDumpComment[0] ? &userStream : NULL;

	MINIDUMP_TYPE dumpType = (MINIDUMP_TYPE)( MiniDumpNormal | MiniDumpWithDataSegs | MiniDumpWithUnloadedModules | MiniDumpWithProcessThreadData | MiniDumpWithThreadInfo );
	BOOL bWroteDump = pfnMiniDumpWriteDump(
		GetCurrentProcess(),
		GetCurrentProcessId(),
		hFile,
		dumpType,
		pvExceptionInfo ? &exInfo : NULL,
		userStreamInfo.UserStreamCount ? &userStreamInfo : NULL,
		NULL );

	CloseHandle( hFile );
	FreeLibrary( hDbgHelp );

	char szLogMessage[MAX_PATH + 128];
	snprintf( szLogMessage, sizeof( szLogMessage ), "SteamAPI_WriteMiniDump: wrote=%d path=%s", bWroteDump ? 1 : 0, szDumpPath );
	AppendMiniDumpLog( szDumpDir, szLogMessage );
}

S_API void *g_pSteamClientGameServer;
void *g_pSteamClientGameServer = NULL;

//steam_api.h
S_API bool SteamAPI_Init() {
	return true;
}

S_API bool SteamAPI_InitSafe() {
	return true;
}

S_API void SteamAPI_Shutdown() {

}

S_API bool SteamAPI_RestartAppIfNecessary() {
	return false;
}

S_API void SteamAPI_ReleaseCurrentThreadMemory() {

}

S_API void SteamAPI_WriteMiniDump( unsigned int uStructuredExceptionCode, void *pvExceptionInfo, unsigned int uBuildID ) {
	WriteLocalMiniDump( uStructuredExceptionCode, pvExceptionInfo, uBuildID );
}

S_API void SteamAPI_SetMiniDumpComment( const char *pchMsg ) {
	if ( pchMsg )
	{
		strncpy( g_szMiniDumpComment, pchMsg, sizeof( g_szMiniDumpComment ) - 1 );
		g_szMiniDumpComment[ sizeof( g_szMiniDumpComment ) - 1 ] = '\0';
	}
}

S_API void SteamAPI_RunCallbacks() {
}

S_API void SteamAPI_RegisterCallback() {

}

S_API void SteamAPI_UnregisterCallback() {

}

S_API void SteamAPI_RegisterCallResult() {

}

S_API void SteamAPI_UnregisterCallResult() {

}

S_API bool SteamAPI_IsSteamRunning() {
	return false;
}

S_API void Steam_RunCallbacks() {

}

S_API void Steam_RegisterInterfaceFuncs() {
}

S_API int Steam_GetHSteamUserCurrent() {
	return 0;
}

S_API const char *SteamAPI_GetSteamInstallPath() {
	return NULL;
}

S_API int SteamAPI_GetHSteamPipe() {
	return 0;
}

S_API void SteamAPI_SetTryCatchCallbacks() {

}

S_API void SteamAPI_SetBreakpadAppID( unsigned int unAppID ) {
	g_unBreakpadAppID = unAppID;
}

S_API void SteamAPI_UseBreakpadCrashHandler( const char *, const char *, const char *, bool, void *, void (*)( void * ) ) {
}

S_API int GetHSteamPipe() {
	return 0;
}

S_API int GetHSteamUser() {
	return 0;
}

S_API int SteamAPI_GetHSteamUser() {
	return 0;
}

S_API void *SteamInternal_ContextInit( void *pContextInitData ) {
	static void *s_pNullContext = NULL;

	void **ppContextInitData = (void **)pContextInitData;
	if ( !ppContextInitData )
	{
		return &s_pNullContext;
	}

	typedef void (*ContextInitFn)( void * );
	ContextInitFn pInitFn = (ContextInitFn)ppContextInitData[0];
	if ( pInitFn )
	{
		pInitFn( &ppContextInitData[2] );
	}

	return &ppContextInitData[2];
}

S_API void *SteamInternal_CreateInterface( const char * ) {
	return NULL;
}

S_API void *SteamInternal_FindOrCreateUserInterface( int hSteamUser, const char *pszVersion ) {
	return NULL;
}

S_API void *SteamInternal_FindOrCreateGameServerInterface( int hSteamUser, const char *pszVersion ) {
	return NULL;
}

S_API void *SteamApps() {
	return NULL;
}

S_API void *SteamClient() {
	return NULL;
}

S_API void *SteamFriends() {
	return NULL;
}

S_API void *SteamHTTP() {
	return NULL;
}

S_API void *SteamMatchmaking() {
	return NULL;
}

S_API void *SteamMatchmakingServers() {
	return NULL;
}

S_API void *SteamNetworking() {
	return NULL;
}

S_API void *SteamRemoteStorage() {
	return NULL;
}

S_API void *SteamScreenshots() {
	return NULL;
}

S_API void *SteamUser() {
	return NULL;
}

S_API void *SteamUserStats() {
	return NULL;
}

S_API void *SteamUtils() {
	return NULL;
}

S_API int SteamGameServer_GetHSteamPipe() {
	return 0;
}

S_API int SteamGameServer_GetHSteamUser() {
	return 0;
}

S_API int SteamGameServer_GetIPCCallCount() {
	return 0;
}

S_API int SteamGameServer_InitSafe() {
	return 0;
}

S_API bool SteamGameServer_BSecure() {
	return false;
}

S_API bool SteamInternal_GameServer_Init( unsigned int unIP, unsigned short usLegacySteamPort, unsigned short usGamePort, unsigned short usQueryPort, int eServerMode, const char *pchVersionString ) {
	return false;
}

S_API void SteamGameServer_RunCallbacks() {
}

S_API void SteamGameServer_Shutdown() {
}
