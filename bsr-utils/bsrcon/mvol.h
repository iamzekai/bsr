#ifndef __MVOL_H__
#define __MVOL_H__

#ifdef _WIN
#include "bsr_ioctl.h"
#else // _LIN
#include "../../bsr-headers/bsr_ioctl.h"
#endif

#ifdef _WIN
DWORD MVOL_GetVolumeInfo( CHAR DriveLetter, PMVOL_VOLUME_INFO pVolumeInfo );
extern DWORD MVOL_GetVolumesInfo(BOOLEAN verbose);

DWORD MVOL_GetVolumeSize( PWCHAR PhysicalVolume, PLARGE_INTEGER pVolumeSize );
/**
 *      @brief  get bsr's status inforamtion 
 *      @param  VolumeInfo [out]    status Info buffer 
 *      @return if it success, return ERROR_SUCCESS, if failed, return value is GetLastError()'s return
 */
DWORD MVOL_GetStatus( PMVOL_VOLUME_INFO VolumeInfo );
DWORD MVOL_set_ioctl(PWCHAR PhysicalVolume, DWORD code, MVOL_VOLUME_INFO *volumeInfo);
/**
 *      @brief  change regisrty settings about nagle 
 *      @param  ResourceName [in]   resource name 
 *      @param  arg [in]            disable or enable
 *      @return if it success, return ERROR_SUCCESS, if failed, return value is GetLastError()'s return
 */
DWORD MVOL_SetDelayedAck(CHAR *addr, CHAR *arg);


/**
*      @brief  mount a volume
*      @param  driveletter [in] Local drive letter(ex C, D, E, ...)
*      @return if it success, return ERROR_SUCCESS, if failed, return value is GetLastError()'s return
*/
DWORD MVOL_MountVolume(char drive_letter);

/**
 *      @brief  dismount a volume
 *      @param  driveletter [in] Local drive letter(ex C, D, E, ...)
 *      @param  Force [in] true or false
 *              true -  FSCTL_DISMOUNT_VOLUME
 *              false - FSCTL_LOCK_VOLUME -> FSCTL_DISMOUNT_VOLUME -> FSCTL_UNLOCK_VOLUME
 *      @return if it success, return ERROR_SUCCESS, if failed, return value is GetLastError()'s return
 */
DWORD MVOL_DismountVolume(CHAR DriveLetter, int Force);

/**
*      @brief  Simulate Disk I/O Error
*      @param  SIMULATION_DISK_IO_ERROR structure's pointer
*			ErrorFlag : Error Simulation flag
*				#define SIMUL_DISK_IO_ERROR_FLAG0		0 // No Disk Error 
*				#define SIMUL_DISK_IO_ERROR_FLAG1		1 // Continuous Disk Error Flag
*				#define SIMUL_DISK_IO_ERROR_FLAG2		2 // Temporary Disk Error Flag
*			ErrorType : Type of Disk I/O Error
*				0 - generic_make_request fail
*				1 - Local Disk I/O complete with error
*				2 - Peer Request I/O complete with error
*				3 - Meta Data I/O complete with error
*				4 - Bitmap I/O complete with error
* 			ErrorCount : Error Count when Disk Error Flag is 2(Temporary Disk Error). If Error Flag is 0 or 1, this filed is Ignored.
*      @return if it success, return ERROR_SUCCESS, if failed, return value is GetLastError()'s return
*/
DWORD MVOL_SimulDiskIoError(SIMULATION_DISK_IO_ERROR* pSdie);

DWORD GetBsrlockStatus();

// BSR-71
DWORD MVOL_BsrlockUse(int bBsrlock);


#endif

DWORD MVOL_SetHandlerUse(PHANDLER_INFO pHandler);

DWORD MVOL_SetMinimumLogLevel(PLOGGING_MIN_LV pLml);
// BSR-579
DWORD MVOL_SetLogFileMaxCount(ULONG limit);

// BSR-1052
// BSR-973
DWORD MVOL_GetBsrLog(char* pszProviderName, char* resourceName, BOOLEAN oosTrace);

// BSR-1048
DWORD MVOL_WriteBsrKernelLog(int level, char* message);

// BSR-1072
DWORD MVOL_BsrPanic(int panic_enable, int occurrence_time, int force, char *cert);

// BSR-1039
DWORD MVOL_HoldState(int type, int state);

// BSR-654
DWORD MVOL_SetDebugLogCategory(PDEBUG_LOG_CATEGORY pDlcE);

#ifdef _DEBUG_OOS
DWORD MVOL_ConvertOosLog(LPCTSTR pSrcFilePath);
DWORD MVOL_SearchOosLog(LPCTSTR pSrcFilePath, LPCTSTR szSector);
#endif

#endif // __MVOL_H__