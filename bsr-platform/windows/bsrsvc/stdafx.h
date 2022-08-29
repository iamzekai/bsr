// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once
#include <winsock2.h>
#include <tchar.h>
#include <stdio.h>
#include "bsrservice.h"

DWORD RunProcess(
	__in WORD wExecMode,
	__in WORD wExecStyle,
	__in const wchar_t * pwszAppName,
	__in const wchar_t * pwszParameter,
	__in const wchar_t * pwszWorkingDirectory,
	__out volatile DWORD & dwPID,
	__in DWORD dwWait,
	LPDWORD lpdwExitCode,
	BOOL * bIsExist);


#pragma comment(lib, "Ws2_32.lib")

// TODO: reference additional headers your program requires here
#define EXEC_MODE_CMD	0
#define EXEC_MODE_WIN	1

#define BSR_DAEMON_TCP_PORT	5679

//#define _WIN32_LOGLINK	
#define BSR_EVENTLOG_LINK_PORT	5677

#define	LOGLINK_NOT_USED	0	// kernel level log with multi-line
#define	LOGLINK_DUAL		1	// kernel level log + user level log
#define	LOGLINK_OLNY		2	// user level log, eventname = application/bsr
#define	LOGLINK_NEW_NAME	3	// user level log, save bsr event only
#define	LOGLINK_2OUT		4	// user level log, save one event to two eventlog 