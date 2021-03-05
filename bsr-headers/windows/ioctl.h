/*
	Copyright(C) 2007-2016, ManTechnology Co., LTD.
	Copyright(C) 2007-2016, bsr@mantech.co.kr

	Windows BSR is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2, or (at your option)
	any later version.

	Windows BSR is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Windows BSR; see the file COPYING. If not, write to
	the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __MVF_IOCTL_H__
#define __MVF_IOCTL_H__

#include "bsr_log.h"

// BSR-674 change device for distinguishing
#define	MVOL_DEVICE		"\\\\.\\mvolBsrCtrl"

//
// IOCTL
//
#define	MVOL_TYPE		0x9800

#define	IOCTL_MVOL_GET_VOLUME_COUNT			CTL_CODE(MVOL_TYPE, 1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	IOCTL_MVOL_GET_VOLUMES_INFO			CTL_CODE(MVOL_TYPE, 2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	IOCTL_MVOL_GET_VOLUME_INFO			CTL_CODE(MVOL_TYPE, 3, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define	IOCTL_MVOL_MOUNT_VOLUME             CTL_CODE(MVOL_TYPE, 15, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define	IOCTL_MVOL_GET_VOLUME_SIZE			CTL_CODE(MVOL_TYPE, 21, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	IOCTL_MVOL_VOLUME_READ_OFF			CTL_CODE(MVOL_TYPE, 22, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	IOCTL_MVOL_VOLUME_READ_ON			CTL_CODE(MVOL_TYPE, 23, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	IOCTL_MVOL_VOLUME_WRITE_OFF			CTL_CODE(MVOL_TYPE, 24, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	IOCTL_MVOL_VOLUME_WRITE_ON			CTL_CODE(MVOL_TYPE, 25, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define	IOCTL_MVOL_GET_COUNT_INFO			CTL_CODE(MVOL_TYPE, 30, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define	IOCTL_MVOL_GET_PROC_BSR			CTL_CODE(MVOL_TYPE, 38, METHOD_BUFFERED, FILE_ANY_ACCESS)

// BSR-37 debugfs porting
#define	IOCTL_MVOL_GET_DEBUG_INFO			CTL_CODE(MVOL_TYPE, 39, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_MVOL_SET_SIMUL_DISKIO_ERROR	CTL_CODE(MVOL_TYPE, 40, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_MVOL_SET_LOGLV_MIN			CTL_CODE(MVOL_TYPE, 46, METHOD_BUFFERED, FILE_ANY_ACCESS)

// BSR-579
#define IOCTL_MVOL_SET_LOG_FILE_MAX_COUNT	CTL_CODE(MVOL_TYPE, 47, METHOD_BUFFERED, FILE_ANY_ACCESS)
// BSR-649
#define IOCTL_MVOL_SET_DEBUG_LOG_CATEGORY		CTL_CODE(MVOL_TYPE, 48, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_MVOL_GET_BSR_LOG				CTL_CODE(MVOL_TYPE, 50, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_MVOL_SET_HANDLER_USE			CTL_CODE(MVOL_TYPE, 52, METHOD_BUFFERED, FILE_ANY_ACCESS)
// BSR-740
#define IOCTL_MVOL_SET_BSRMON_RUN			CTL_CODE(MVOL_TYPE, 53, METHOD_BUFFERED, FILE_ANY_ACCESS)


#define	MAXDEVICENAME			256     //  1024 -> 256
#define MAX_SEQ_BUF			4096	


//
// Structure
//
typedef struct _MVOL_VOLUME_INFO
{
	BOOLEAN				Active;
	WCHAR				PhysicalDeviceName[MAXDEVICENAME];		// src device
	ULONG				PeerIp;
	USHORT				PeerPort;
	CHAR				Seq[MAX_SEQ_BUF];
} MVOL_VOLUME_INFO, *PMVOL_VOLUME_INFO;

typedef struct _MVOL_COUNT_INFO
{
	ULONG				IrpCount;
} MVOL_COUNT_INFO, *PMVOL_COUNT_INFO;

typedef struct _MVOL_SYNC_REQ
{
	WCHAR				PhysicalDeviceName[MAXDEVICENAME];
	LARGE_INTEGER		Offset;
	ULONG				BlockSize;
	ULONG				Count;
} MVOL_SYNC_REQ, *PMVOL_SYNC_REQ;

#define _WIN_MULTIVOL_THREAD
typedef struct _BSR_VOLUME_ENTRY
{
	WCHAR		PhysicalDeviceName[MAXDEVICENAME];
	WCHAR		MountPoint[MAXDEVICENAME];
	WCHAR		VolumeGuid[MAXDEVICENAME];

	ULONGLONG	Size;
	ULONGLONG	AgreedSize;

	UCHAR		Minor;
	BOOLEAN		ExtensionActive;
#ifndef _WIN_MULTIVOL_THREAD
	BOOLEAN		ThreadActive;
	BOOLEAN		ThreadExit;
#endif
} BSR_VOLUME_ENTRY, *PBSR_VOLUME_ENTRY;


// BSR-37 debugfs porting
enum BSR_DEBUG_FLAGS
{
	DBG_NO_FLAGS = 0,
	DBG_BSR_VERSION,
	DBG_RES_IN_FLIGHT_SUMMARY,
	DBG_RES_STATE_TWOPC,
	DBG_RES_CONNECTIONS,
	DBG_RES_VOLUMES,
	DBG_CONN_CALLBACK_HISTORY,
	DBG_CONN_DEBUG,
	DBG_CONN_OLDEST_REQUESTS,
	DBG_CONN_TRANSPORT,
	DBG_CONN_TRANSPORT_SPEED,
	DBG_CONN_SEND_BUF,
	DBG_PEER_PROC_BSR,
	DBG_PEER_RESYNC_EXTENTS,
	DBG_DEV_ACT_LOG_EXTENTS,
	DBG_DEV_DATA_GEN_ID,
	DBG_DEV_ED_GEN_ID,
	DBG_DEV_IO_FROZEN,
	DBG_DEV_OLDEST_REQUESTS,
	DBG_DEV_IO_STAT,
	DBG_DEV_IO_COMPLETE,
	DBG_DEV_REQ_TIMING, // BSR-682
};

typedef struct _BSR_DEBUG_INFO
{
	CHAR					res_name[64];
	int						peer_node_id;
	int						vnr;
	enum BSR_DEBUG_FLAGS	flags;
	int						buf_size;
	char					buf[1];
} BSR_DEBUG_INFO, *PBSR_DEBUG_INFO;

#define SIMUL_DISK_IO_ERROR_FLAG0		0 // No Disk Error 
#define SIMUL_DISK_IO_ERROR_FLAG1		1 // Continuous Disk Error Flag
#define SIMUL_DISK_IO_ERROR_FLAG2		2 // Temporary Disk Error Flag

#define SIMUL_DISK_IO_ERROR_TYPE0		0 // generic_make_request fail
#define SIMUL_DISK_IO_ERROR_TYPE1		1 // Local I/O Completed with Error
#define SIMUL_DISK_IO_ERROR_TYPE2		2 // Peer Request I/O Completed with Error
#define SIMUL_DISK_IO_ERROR_TYPE3		3 // Meta I/O Completed with Error
#define SIMUL_DISK_IO_ERROR_TYPE4		4 // Bitmap I/O Completed with Error

typedef struct _SIMULATION_DISK_IO_ERROR {
	ULONG 		ErrorFlag;		// Global Disk Error Flag
	ULONG		ErrorType;		// Global Disk Error Type
	ULONG		ErrorCount;		// Global Disk Error Count when Disk Error Flag is 2(Temporary Disk Error)
}SIMULATION_DISK_IO_ERROR, *PSIMULATION_DISK_IO_ERROR;



typedef struct _HANDLER_INFO
{
	BOOLEAN				use;
} HANDLER_INFO, *PHANDLER_INFO;

#endif // __MVF_IOCTL_H__