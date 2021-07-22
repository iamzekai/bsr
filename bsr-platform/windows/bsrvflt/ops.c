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

#include "../../../bsr/bsr_int.h"
#include <wdm.h>
#include "../../../bsr/bsr-kernel-compat/windows/bsr_windows.h"
#include "disp.h"
#include "proto.h"
#include "../../../bsr/bsr_idx_ring.h"
#include "../../../bsr/bsr_debugfs.h"

extern SIMULATION_DISK_IO_ERROR gSimulDiskIoError;

// BSR-764
extern SIMULATION_PERF_DEGR g_simul_perf;

CALLBACK_FUNCTION bsrCallbackFunc;
PCALLBACK_OBJECT g_pCallbackObj;
PVOID g_pCallbackReg;

NTSTATUS
IOCTL_GetAllVolumeInfo( PIRP Irp, PULONG ReturnLength )
{
	*ReturnLength = 0;
	NTSTATUS status = STATUS_SUCCESS;
	PROOT_EXTENSION	prext = mvolRootDeviceObject->DeviceExtension;

	MVOL_LOCK();
	ULONG count = prext->Count;
	if (count == 0) {
		goto out;
	}

	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
	ULONG outlen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
	if (outlen < (count * sizeof(BSR_VOLUME_ENTRY))) {
		//bsr_err(52, BSR_LC_ETC, NO_OBJECT,"IOCTL_GetAllVolumeInfo buffer too small outlen:%d required len:%d",outlen,(count * sizeof(BSR_VOLUME_ENTRY)) );
		*ReturnLength = count * sizeof(BSR_VOLUME_ENTRY);
		status = STATUS_BUFFER_TOO_SMALL;
		goto out;
	}

	PBSR_VOLUME_ENTRY pventry = (PBSR_VOLUME_ENTRY)Irp->AssociatedIrp.SystemBuffer;
	PVOLUME_EXTENSION pvext = prext->Head;
	for ( ; pvext; pvext = pvext->Next, pventry++) {
		RtlZeroMemory(pventry, sizeof(BSR_VOLUME_ENTRY));

		RtlCopyMemory(pventry->PhysicalDeviceName, pvext->PhysicalDeviceName, pvext->PhysicalDeviceNameLength);
		// BSR-109
		RtlCopyMemory(pventry->MountPoint, pvext->MountPoint, wcslen(pvext->MountPoint) * sizeof(WCHAR));
		RtlCopyMemory(pventry->VolumeGuid, pvext->VolumeGuid, wcslen(pvext->VolumeGuid) * sizeof(WCHAR));
		pventry->ExtensionActive = pvext->Active;
		pventry->Minor = pvext->Minor;
#ifndef _WIN_MULTIVOL_THREAD
		pventry->ThreadActive = pvext->WorkThreadInfo.Active;
		pventry->ThreadExit = pvext->WorkThreadInfo.exit_thread;
#endif
		if (pvext->dev) {
			if (pvext->Active)
				pventry->AgreedSize = pvext->dev->d_size;
			else
				pventry->AgreedSize = 0;
			if (pvext->dev->bd_contains) {
				// BSR-617 get real size
				unsigned long long d_size = get_targetdev_volsize(pvext);
				if (pvext->dev->bd_contains->d_size != d_size) {
					pvext->dev->bd_contains->d_size = d_size;
					pvext->dev->bd_disk->queue->max_hw_sectors = d_size ? (d_size >> 9) : BSR_MAX_BIO_SIZE;
				}
				pventry->Size = pvext->dev->bd_contains->d_size;
			}
		}
	}

	*ReturnLength = count * sizeof(BSR_VOLUME_ENTRY);
out:
	MVOL_UNLOCK();

	return status;
}

NTSTATUS
IOCTL_GetVolumeInfo( PDEVICE_OBJECT DeviceObject, PIRP Irp, PULONG ReturnLength )
{
	PIO_STACK_LOCATION	irpSp = IoGetCurrentIrpStackLocation(Irp);
	PVOLUME_EXTENSION	VolumeExtension = NULL;
	PMVOL_VOLUME_INFO	pOutBuffer = NULL;
	ULONG			outlen;

	if( DeviceObject == mvolRootDeviceObject ) {
		mvolLogError( DeviceObject, 211,
			MSG_ROOT_DEVICE_REQUEST, STATUS_INVALID_DEVICE_REQUEST );
		bsr_err(11, BSR_LC_DRIVER, NO_OBJECT, "Failed to get volume info due to this is root device");
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	VolumeExtension = DeviceObject->DeviceExtension;
	outlen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
	if( outlen < sizeof(MVOL_VOLUME_INFO) )	{
		mvolLogError( DeviceObject, 212, MSG_BUFFER_SMALL, STATUS_BUFFER_TOO_SMALL );
		bsr_err(12, BSR_LC_DRIVER, NO_OBJECT, "Failed to get volume info due to buffer too small out %d sizeof(MVOL_VOLUME_INFO) %d", outlen, sizeof(MVOL_VOLUME_INFO));
		*ReturnLength = sizeof(MVOL_VOLUME_INFO);
		return STATUS_BUFFER_TOO_SMALL;
	}

	pOutBuffer = (PMVOL_VOLUME_INFO) Irp->AssociatedIrp.SystemBuffer;
	RtlCopyMemory( pOutBuffer->PhysicalDeviceName, VolumeExtension->PhysicalDeviceName,
		MAXDEVICENAME * sizeof(WCHAR) );
	pOutBuffer->Active = VolumeExtension->Active;
	*ReturnLength = sizeof(MVOL_VOLUME_INFO);
	return STATUS_SUCCESS;
}

NTSTATUS
IOCTL_MountVolume(PDEVICE_OBJECT DeviceObject, PIRP Irp, PULONG ReturnLength)
{
	if (DeviceObject == mvolRootDeviceObject) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (!Irp->AssociatedIrp.SystemBuffer) {
		bsr_warn(85, BSR_LC_DRIVER, NO_OBJECT, 
			"SystemBuffer is NULL. Maybe older bsrcon was used or other access was tried");
		return STATUS_INVALID_PARAMETER;
	}

	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
	PVOLUME_EXTENSION pvext = DeviceObject->DeviceExtension;
	CHAR Message[128] = { 0, };
	*ReturnLength = 0;
	// DW-1300
	struct bsr_device *device = NULL;
    COUNT_LOCK(pvext);
	
    if (!pvext->Active) {
		_snprintf(Message, sizeof(Message) - 1, "Failed to mount volume due to %ws is not a replication volume", &pvext->MountPoint);
		*ReturnLength = (ULONG)strlen(Message);
		bsr_err(13, BSR_LC_DRIVER, NO_OBJECT, "%s", Message);
        //status = STATUS_INVALID_DEVICE_REQUEST;
        goto out;
    }

	// DW-1300 get device and get reference.
	device = get_device_with_vol_ext(pvext, TRUE);
#ifdef _WIN_MULTIVOL_THREAD
    if (device)
#else
	if (pvext->WorkThreadInfo.Active && device)
#endif
	{
		_snprintf(Message, sizeof(Message) - 1, "Failed to mount volume due to %ws volume is handling by bsr. Failed to release volume",
			&pvext->MountPoint);
		*ReturnLength = (ULONG)strlen(Message);
		bsr_err(14, BSR_LC_DRIVER, NO_OBJECT, "%s", Message);
        //status = STATUS_VOLUME_DISMOUNTED;
        goto out;
    }

    pvext->Active = FALSE;
	// DW-1327 to allow I/O by bsrlock.
	SetBsrlockIoBlock(pvext, FALSE);
#ifdef _WIN_MULTIVOL_THREAD
	pvext->WorkThreadInfo = NULL;
#else
	mvolTerminateThread(&pvext->WorkThreadInfo);
#endif

out:
    COUNT_UNLOCK(pvext);

	// DW-1300 put device reference count when no longer use.
	if (device)
		kref_put(&device->kref, bsr_destroy_device);

	if (*ReturnLength) {
		ULONG outlen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
		ULONG DecidedLength = ((*ReturnLength) >= outlen) ?
			outlen - 1 : *ReturnLength;
		memcpy((PCHAR)Irp->AssociatedIrp.SystemBuffer, Message, DecidedLength);
		*((PCHAR)Irp->AssociatedIrp.SystemBuffer + DecidedLength) = '\0';
	}

    return status;
}

NTSTATUS
IOCTL_GetVolumeSize( PDEVICE_OBJECT DeviceObject, PIRP Irp )
{
	NTSTATUS		status;
	ULONG			inlen, outlen;
	PIO_STACK_LOCATION	irpSp=IoGetCurrentIrpStackLocation(Irp);
	PVOLUME_EXTENSION	VolumeExtension = NULL;
	PMVOL_VOLUME_INFO	pVolumeInfo = NULL;
	PLARGE_INTEGER		pVolumeSize;

	inlen = irpSp->Parameters.DeviceIoControl.InputBufferLength;
	outlen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
	if( inlen < sizeof(MVOL_VOLUME_INFO) || outlen < sizeof(LARGE_INTEGER) ) {
		mvolLogError( DeviceObject, 321, MSG_BUFFER_SMALL, STATUS_BUFFER_TOO_SMALL );

		bsr_err(15, BSR_LC_DRIVER, NO_OBJECT, "Failed to get volume size due to buffer too small");
		return STATUS_BUFFER_TOO_SMALL;
	}

	pVolumeInfo = (PMVOL_VOLUME_INFO) Irp->AssociatedIrp.SystemBuffer;
	
	if( DeviceObject == mvolRootDeviceObject ) {
		bsr_debug(114, BSR_LC_DRIVER, NO_OBJECT, "Root Device IOCTL");

		MVOL_LOCK();
		VolumeExtension = mvolSearchDevice( pVolumeInfo->PhysicalDeviceName );
		MVOL_UNLOCK();

		if( VolumeExtension == NULL ) {
			mvolLogError( DeviceObject, 322, MSG_NO_DEVICE, STATUS_NO_SUCH_DEVICE );
			bsr_err(16, BSR_LC_DRIVER, NO_OBJECT, "Failed to get volume size due to cannot find volume, PD=%ws", pVolumeInfo->PhysicalDeviceName);
			return STATUS_NO_SUCH_DEVICE;
		}
	}
	else {
		VolumeExtension = DeviceObject->DeviceExtension;
	}

	pVolumeSize = (PLARGE_INTEGER) Irp->AssociatedIrp.SystemBuffer;
	status = mvolGetVolumeSize( VolumeExtension->TargetDeviceObject, pVolumeSize );
	if( !NT_SUCCESS(status) ) {
		mvolLogError( VolumeExtension->DeviceObject, 323, MSG_CALL_DRIVER_ERROR, status );
		bsr_err(17, BSR_LC_DRIVER, NO_OBJECT, "Failed to get volume size due to cannot get volume size, err=0x%x", status);
	}

	return status;
}

NTSTATUS
IOCTL_GetCountInfo( PDEVICE_OBJECT DeviceObject, PIRP Irp, PULONG ReturnLength )
{
	ULONG			inlen, outlen;
	PIO_STACK_LOCATION	irpSp=IoGetCurrentIrpStackLocation(Irp);
	PVOLUME_EXTENSION	VolumeExtension = NULL;
	PMVOL_VOLUME_INFO	pVolumeInfo = NULL;
	PMVOL_COUNT_INFO	pCountInfo = NULL;

	inlen = irpSp->Parameters.DeviceIoControl.InputBufferLength;
	outlen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
	if( inlen < sizeof(MVOL_VOLUME_INFO) || outlen < sizeof(MVOL_COUNT_INFO) ) {
		mvolLogError( DeviceObject, 351, MSG_BUFFER_SMALL, STATUS_BUFFER_TOO_SMALL );
		bsr_err(18, BSR_LC_DRIVER, NO_OBJECT, "Failed to get count info due to buffer too small");
		return STATUS_BUFFER_TOO_SMALL;
	}

	pVolumeInfo = (PMVOL_VOLUME_INFO) Irp->AssociatedIrp.SystemBuffer;
	if( DeviceObject == mvolRootDeviceObject ) {
		bsr_debug(115, BSR_LC_DRIVER, NO_OBJECT, "Root Device IOCTL");

		MVOL_LOCK();
		VolumeExtension = mvolSearchDevice( pVolumeInfo->PhysicalDeviceName );
		MVOL_UNLOCK();

		if( VolumeExtension == NULL ) {
			mvolLogError( DeviceObject, 352, MSG_NO_DEVICE, STATUS_NO_SUCH_DEVICE );
			bsr_err(19, BSR_LC_DRIVER, NO_OBJECT, "Failed to get count info due to cannot find volume, PD=%ws", pVolumeInfo->PhysicalDeviceName);
			return STATUS_NO_SUCH_DEVICE;
		}
	}
	else {
		VolumeExtension = DeviceObject->DeviceExtension;
	}

	pCountInfo = (PMVOL_COUNT_INFO) Irp->AssociatedIrp.SystemBuffer;
	pCountInfo->IrpCount = VolumeExtension->IrpCount;

	*ReturnLength = sizeof(MVOL_COUNT_INFO);
	return STATUS_SUCCESS;
}

// Simulate Disk I/O Error
// this function just copy pSDError(SIMULATION_DISK_IO_ERROR) param to gSimulDiskIoError variables
NTSTATUS
IOCTL_SetSimulDiskIoError( PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	ULONG			inlen, outlen;
	SIMULATION_DISK_IO_ERROR* pSDError = NULL;
	
	PIO_STACK_LOCATION	irpSp=IoGetCurrentIrpStackLocation(Irp);
	inlen = irpSp->Parameters.DeviceIoControl.InputBufferLength;
	outlen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
	
	if( inlen < sizeof(SIMULATION_DISK_IO_ERROR) || outlen < sizeof(SIMULATION_DISK_IO_ERROR) ) {
		mvolLogError( DeviceObject, 351, MSG_BUFFER_SMALL, STATUS_BUFFER_TOO_SMALL );
		bsr_err(20, BSR_LC_DRIVER, NO_OBJECT, "Failed to set disk error dule to buffer too small");
		return STATUS_BUFFER_TOO_SMALL;
	}
	if(Irp->AssociatedIrp.SystemBuffer) {
		pSDError = (SIMULATION_DISK_IO_ERROR*)Irp->AssociatedIrp.SystemBuffer;
		RtlCopyMemory(&gSimulDiskIoError, pSDError, sizeof(SIMULATION_DISK_IO_ERROR));
		bsr_info(21, BSR_LC_DRIVER, NO_OBJECT, "IOCTL_MVOL_SET_SIMUL_DISKIO_ERROR ErrorFlag:%d ErrorType:%d", gSimulDiskIoError.ErrorFlag, gSimulDiskIoError.ErrorType);
	} else {
		return STATUS_INVALID_PARAMETER;
	}
	
	return STATUS_SUCCESS;
}

NTSTATUS
IOCTL_SetSimulPerfDegr(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	ULONG			inlen, outlen;
	SIMULATION_PERF_DEGR* pSPTest = NULL;
	
	PIO_STACK_LOCATION	irpSp=IoGetCurrentIrpStackLocation(Irp);
	inlen = irpSp->Parameters.DeviceIoControl.InputBufferLength;
	outlen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
	
	if( inlen < sizeof(SIMULATION_PERF_DEGR) || outlen < sizeof(SIMULATION_PERF_DEGR) ) {
		mvolLogError( DeviceObject, 351, MSG_BUFFER_SMALL, STATUS_BUFFER_TOO_SMALL );
		bsr_err(149, BSR_LC_DRIVER, NO_OBJECT, "Failed to set SIMULATION_PERF_DEGR dule to buffer too small");
		return STATUS_BUFFER_TOO_SMALL;
	}
	if(Irp->AssociatedIrp.SystemBuffer) {
		pSPTest = (SIMULATION_PERF_DEGR*)Irp->AssociatedIrp.SystemBuffer;
		RtlCopyMemory(&g_simul_perf, pSPTest, sizeof(SIMULATION_PERF_DEGR));
		bsr_info(150, BSR_LC_DRIVER, NO_OBJECT, "IOCTL_MVOL_SET_SIMUL_PERF_DEGR flag:%d type:%d", g_simul_perf.flag, g_simul_perf.type);
	} else {
		return STATUS_INVALID_PARAMETER;
	}
	
	return STATUS_SUCCESS;
}

NTSTATUS
IOCTL_SetMinimumLogLevel(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	ULONG			inlen;
	PLOGGING_MIN_LV pLoggingMinLv = NULL;
	NTSTATUS	Status;

	// DW-2041
	int previous_lv_min = 0;
	PIO_STACK_LOCATION	irpSp = IoGetCurrentIrpStackLocation(Irp);
	inlen = irpSp->Parameters.DeviceIoControl.InputBufferLength;

	if (inlen < sizeof(LOGGING_MIN_LV)) {
		mvolLogError(DeviceObject, 355, MSG_BUFFER_SMALL, STATUS_BUFFER_TOO_SMALL);
		bsr_err(22, BSR_LC_DRIVER, NO_OBJECT, "Failed to set minmum log level due to buffer too small");
		return STATUS_BUFFER_TOO_SMALL;
	}
	if (Irp->AssociatedIrp.SystemBuffer) {
		pLoggingMinLv = (PLOGGING_MIN_LV)Irp->AssociatedIrp.SystemBuffer;

		if (pLoggingMinLv->nType == LOGGING_TYPE_SYSLOG) {
			previous_lv_min = atomic_read(&g_eventlog_lv_min);
			atomic_set(&g_eventlog_lv_min, pLoggingMinLv->nErrLvMin);
		}
		else if (pLoggingMinLv->nType == LOGGING_TYPE_DBGLOG) {
			previous_lv_min = atomic_read(&g_dbglog_lv_min);
			atomic_set(&g_dbglog_lv_min, pLoggingMinLv->nErrLvMin);
		}
		else {
			bsr_warn(86, BSR_LC_DRIVER, NO_OBJECT, "Invalidate logging type(%d)", pLoggingMinLv->nType);
		}

		// DW-1432 Modified to see if command was successful 
		Status = SaveCurrentValue(LOG_LV_REG_VALUE_NAME, Get_log_lv());
		// DW-2008
		bsr_info(24, BSR_LC_DRIVER, NO_OBJECT, "The log level has been updated, type : %s(%d), minumum level : %s(%d) => %s(%d), result : %lu",
					g_log_type_str[pLoggingMinLv->nType], pLoggingMinLv->nType, 
					g_default_lv_str[previous_lv_min], previous_lv_min, 
					g_default_lv_str[pLoggingMinLv->nErrLvMin], pLoggingMinLv->nErrLvMin,
					Status);

		if (Status != STATUS_SUCCESS) {
			return STATUS_UNSUCCESSFUL; 
		}
	}
	else {
		return STATUS_INVALID_PARAMETER;
	}

	return STATUS_SUCCESS;
}

// BSR-654 Sets which category is output during debug log.
NTSTATUS
IOCTL_SetDebugLogCategory(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	ULONG			inlen;
	PDEBUG_LOG_CATEGORY pDebugLogCategory = NULL;
	NTSTATUS	Status;

	unsigned int previous = 0;
	unsigned int categories = 0;
	PIO_STACK_LOCATION	irpSp = IoGetCurrentIrpStackLocation(Irp);
	inlen = irpSp->Parameters.DeviceIoControl.InputBufferLength;

	if (inlen < sizeof(DEBUG_LOG_CATEGORY)) {
		mvolLogError(DeviceObject, 355, MSG_BUFFER_SMALL, STATUS_BUFFER_TOO_SMALL);
		bsr_err(133, BSR_LC_DRIVER, NO_OBJECT, "Failed to set debug log category due to buffer too small");
		return STATUS_BUFFER_TOO_SMALL;
	}
	if (Irp->AssociatedIrp.SystemBuffer) {
		pDebugLogCategory = (PDEBUG_LOG_CATEGORY)Irp->AssociatedIrp.SystemBuffer;

		previous = atomic_read(&g_debug_output_category);
		categories = pDebugLogCategory->nCategory;

		if (pDebugLogCategory->nType == 0) {
			//enable
			categories = (previous | categories);
		}
		else {
			//disable
			categories = previous - (previous & categories);
		}

		atomic_set(&g_debug_output_category, categories);
		Status = SaveCurrentValue(DEBUG_LOG_CATEGORY_REG_VALUE_NAME, categories);

		bsr_info(134, BSR_LC_DRIVER, NO_OBJECT, "The debug log output has been updated, %u => %u, status(%x)", previous, atomic_read(&g_debug_output_category), Status);
		if (Status != STATUS_SUCCESS) {
			return STATUS_UNSUCCESSFUL;
		}
	}
	else {
		return STATUS_INVALID_PARAMETER;
	}

	return STATUS_SUCCESS;
}



// BSR-579
NTSTATUS
IOCTL_SetLogFileMaxCount(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	ULONG inlen;
	ULONG log_file_max_count = LOG_FILE_COUNT_DEFAULT;
	NTSTATUS status;

	// DW-2041
	PIO_STACK_LOCATION	irpSp = IoGetCurrentIrpStackLocation(Irp);
	inlen = irpSp->Parameters.DeviceIoControl.InputBufferLength;

	if (inlen < sizeof(ULONG)) {
		mvolLogError(DeviceObject, 355, MSG_BUFFER_SMALL, STATUS_BUFFER_TOO_SMALL);
		bsr_err(23, BSR_LC_DRIVER, NO_OBJECT, "Failed to set log file max count due to buffer too small");
		return STATUS_BUFFER_TOO_SMALL;
	}
	if (Irp->AssociatedIrp.SystemBuffer) {
		log_file_max_count = *(ULONG*)Irp->AssociatedIrp.SystemBuffer;

		status = SaveCurrentValue(LOG_FILE_MAX_REG_VALUE_NAME, log_file_max_count);
		bsr_info(25, BSR_LC_DRIVER, NO_OBJECT, "Change the maximum number of log files stored %lu => %lu", atomic_read(&g_log_file_max_count), log_file_max_count);
		atomic_set(&g_log_file_max_count, log_file_max_count);

		if (status != STATUS_SUCCESS) {
			return STATUS_UNSUCCESSFUL;
		}
	}
	else {
		return STATUS_INVALID_PARAMETER;
	}

	return STATUS_SUCCESS;
}

// BSR-740
NTSTATUS IOCTL_SetBsrmonRun(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	unsigned int inlen;
	unsigned int run = 1;
	NTSTATUS status;

	PIO_STACK_LOCATION	irpSp = IoGetCurrentIrpStackLocation(Irp);
	inlen = irpSp->Parameters.DeviceIoControl.InputBufferLength;

	if (inlen < sizeof(unsigned int))
		return STATUS_BUFFER_TOO_SMALL;
		
	if (Irp->AssociatedIrp.SystemBuffer) {
		run = *(unsigned int*)Irp->AssociatedIrp.SystemBuffer;
		status = SaveCurrentValue(L"bsrmon_run", run);
		if (status != STATUS_SUCCESS) {
			return STATUS_UNSUCCESSFUL;
		}

		bsr_debug(145, BSR_LC_DRIVER, NO_OBJECT, "IOCTL_MVOL_SET_BSRMON_RUN %u => %u", atomic_read(&g_bsrmon_run), run);
		atomic_set(&g_bsrmon_run, run);
	}
	else {
		return STATUS_INVALID_PARAMETER;
	}

	return STATUS_SUCCESS;
}

// BSR-740
NTSTATUS IOCTL_GetBsrmonRun(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	ULONG outlen;
	unsigned int run = 0;

	PIO_STACK_LOCATION    irpSp = IoGetCurrentIrpStackLocation(Irp);
	outlen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

	if (outlen < sizeof(unsigned int))
		return STATUS_BUFFER_TOO_SMALL;
		
	if (Irp->AssociatedIrp.SystemBuffer) {
		run = atomic_read(&g_bsrmon_run);
		RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &run, sizeof(unsigned int));
		Irp->IoStatus.Information = sizeof(unsigned int);

	}
	else {
		return STATUS_INVALID_PARAMETER;
	}

	return STATUS_SUCCESS;
}



NTSTATUS
IOCTL_GetBsrLog(PDEVICE_OBJECT DeviceObject, PIRP Irp, ULONG* size)
{
	ULONG			inlen, outlen;
	BSR_LOG* 		pBsrLog = NULL;
	PIO_STACK_LOCATION	irpSp = IoGetCurrentIrpStackLocation(Irp);
	inlen = irpSp->Parameters.DeviceIoControl.InputBufferLength;
	outlen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

	if(!size) {
		bsr_err(26, BSR_LC_DRIVER, NO_OBJECT, "Failed to get bsr log due to invalid parameter. size is not allocate");
		return STATUS_INVALID_PARAMETER;
	}
	*size = 0;	
	
	if (inlen < BSR_LOG_SIZE || outlen < BSR_LOG_SIZE) {
		mvolLogError(DeviceObject, 355, MSG_BUFFER_SMALL, STATUS_BUFFER_TOO_SMALL);
		bsr_err(27, BSR_LC_DRIVER, NO_OBJECT, "Failed to get bsr log due to buffer too small");
		return STATUS_BUFFER_TOO_SMALL;
	}
	if (Irp->AssociatedIrp.SystemBuffer) {
		pBsrLog = (BSR_LOG*)Irp->AssociatedIrp.SystemBuffer;
		pBsrLog->totalcnt = InterlockedCompareExchange64(&gLogBuf.h.total_count, 0, 0);
		if (pBsrLog->LogBuf) {
			RtlCopyMemory(pBsrLog->LogBuf, gLogBuf.b, MAX_BSRLOG_BUF*LOGBUF_MAXCNT);
				*size = BSR_LOG_SIZE;
		} else {
			bsr_err(28, BSR_LC_DRIVER, NO_OBJECT, "Failed to get bsr log due to invalid parameter. log buffer is not allocate");
			return STATUS_INVALID_PARAMETER;
		}
	}
	
	return STATUS_SUCCESS;
}

NTSTATUS
IOCTL_SetHandlerUse(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	ULONG			inlen;
	PHANDLER_INFO	pHandlerInfo = NULL;
	PIO_STACK_LOCATION	irpSp = IoGetCurrentIrpStackLocation(Irp);
	inlen = irpSp->Parameters.DeviceIoControl.InputBufferLength;

	if (inlen < sizeof(HANDLER_INFO)) {
		mvolLogError(DeviceObject, 356, MSG_BUFFER_SMALL, STATUS_BUFFER_TOO_SMALL);
		bsr_err(29, BSR_LC_DRIVER, NO_OBJECT, "Failed to set handle use due to buffer too small");
		return STATUS_BUFFER_TOO_SMALL;
	}
	
	if (Irp->AssociatedIrp.SystemBuffer) {
		pHandlerInfo = (PHANDLER_INFO)Irp->AssociatedIrp.SystemBuffer;
		g_handler_use = pHandlerInfo->use;

		SaveCurrentValue(L"handler_use", g_handler_use);

		bsr_debug(116, BSR_LC_DRIVER, NO_OBJECT, "IOCTL_MVOL_SET_HANDLER_USE : %d ", g_handler_use);
	}
	else {
		return STATUS_INVALID_PARAMETER;
	}

	return STATUS_SUCCESS;
}


NTSTATUS
IOCTL_GetDebugInfo(PIRP Irp, ULONG *size)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PBSR_DEBUG_INFO p = NULL;
	struct bsr_resource *resource = NULL;
	struct bsr_connection *connection = NULL;
	struct bsr_peer_device *peer_device = NULL;
	struct bsr_device * device = NULL;
	struct seq_file seq;
	ULONG inlen, outlen;
	PIO_STACK_LOCATION	irpSp = IoGetCurrentIrpStackLocation(Irp);

	if (!Irp->AssociatedIrp.SystemBuffer) {
		bsr_warn(138, BSR_LC_DRIVER, NO_OBJECT,
			"SystemBuffer is NULL. Maybe older bsrcon was used or other access was tried");
		return STATUS_INVALID_PARAMETER;
	}

	p = (PBSR_DEBUG_INFO)Irp->AssociatedIrp.SystemBuffer;

	inlen = irpSp->Parameters.DeviceIoControl.InputBufferLength;
	outlen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

	if (p->buf_size == 0 || 
		inlen < sizeof(BSR_DEBUG_INFO) + p->buf_size || outlen < sizeof(BSR_DEBUG_INFO) + p->buf_size) {
		bsr_err(139, BSR_LC_DRIVER, NO_OBJECT, "Failed to get bsr debug info due to buffer too small");
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (p->res_name && strlen(p->res_name)) {
		resource = bsr_find_resource(p->res_name);
		if (!resource) {
			status = STATUS_INVALID_PARAMETER;
			goto out;
		}
	}

	if (p->peer_node_id != -1) {
		connection = bsr_get_connection_by_node_id(resource, p->peer_node_id);
		if (!connection) {
			status = STATUS_INVALID_PARAMETER;
			goto out;
		}
		if (p->vnr != -1) {
			peer_device = conn_peer_device(connection, p->vnr);
			if (!peer_device) {
				status = STATUS_INVALID_PARAMETER;
				goto out;
			}
		}
	}
	else if (p->vnr != -1) {
		device = idr_find(&resource->devices, p->vnr);
		if (!device) {
			status = STATUS_INVALID_PARAMETER;
			goto out;
		}
	}

	seq_alloc(&seq, p->buf_size);

	switch (p->flags){
	case DBG_BSR_VERSION:
		bsr_version_show(&seq, 0);
		break;
	case DBG_RES_IN_FLIGHT_SUMMARY:
		seq.private = resource;
		resource_in_flight_summary_show(&seq, 0);
		break;
	case DBG_RES_STATE_TWOPC:
		seq.private = resource;
		resource_state_twopc_show(&seq, 0);
		break;
	case DBG_CONN_CALLBACK_HISTORY:
		seq.private = connection;
		connection_callback_history_show(&seq, 0);
		break;
	case DBG_CONN_DEBUG:
		seq.private = connection;
		connection_debug_show(&seq, 0);
		break;
	case DBG_CONN_OLDEST_REQUESTS:
		seq.private = connection;
		connection_oldest_requests_show(&seq, 0);
		break;
	case DBG_CONN_TRANSPORT:
		seq.private = connection;
		connection_transport_show(&seq, 0);
		break;
	case DBG_CONN_TRANSPORT_SPEED:
		seq.private = connection;
		connection_transport_speed_show(&seq, 0);
		break;
	case DBG_CONN_SEND_BUF:
		seq.private = connection;
		connection_send_buf_show(&seq, 0);
		break;
	case DBG_PEER_PROC_BSR:
		seq.private = peer_device;
		peer_device_proc_bsr_show(&seq, 0);
		break;
	case DBG_PEER_RESYNC_EXTENTS:
		seq.private = peer_device;
		peer_device_resync_extents_show(&seq, 0);
		break;
	case DBG_DEV_ACT_LOG_EXTENTS:
		seq.private = device;
		device_act_log_extents_show(&seq, 0);
		break;
	case DBG_DEV_ACT_LOG_STAT:
		seq.private = device;
		device_act_log_stat_show(&seq, 0);
		break;
	case DBG_DEV_DATA_GEN_ID:
		seq.private = device;
		device_data_gen_id_show(&seq, 0);
		break;
	case DBG_DEV_ED_GEN_ID:
		seq.private = device;
		device_ed_gen_id_show(&seq, 0);
		break;
	case DBG_DEV_IO_FROZEN:
		seq.private = device;
		device_io_frozen_show(&seq, 0);
		break;
	case DBG_DEV_OLDEST_REQUESTS:
		seq.private = device;
		device_oldest_requests_show(&seq, 0);
		break;
	case DBG_DEV_IO_STAT:
		seq.private = device;
		device_io_stat_show(&seq, 0);
		break;
	case DBG_DEV_IO_COMPLETE:
		seq.private = device;
		device_io_complete_show(&seq, 0);
		break;
	case DBG_DEV_REQ_TIMING:
		seq.private = device;
		device_req_timing_show(&seq, 0);
		break;
	case DBG_DEV_PEER_REQ_TIMING:
		seq.private = device;
		device_peer_req_timing_show(&seq, 0);
		break;

	default:
		break;
	}

	RtlCopyMemory(p->buf, seq.buf, seq.size);
	*size = seq.size;

	if (seq_has_overflowed(&seq)) {
		status = STATUS_BUFFER_OVERFLOW;
	}
	else {
		status = STATUS_SUCCESS;
	}

	seq_free(&seq);

out:
	if (resource)
		kref_put(&resource->kref, bsr_destroy_resource);
	if (connection)
		kref_put(&connection->kref, bsr_destroy_connection);

	return status;
}

VOID
bsrCallbackFunc(
	IN PVOID Context, 
	IN PVOID Argument1,	
	IN PVOID Argument2)
/*++

Routine Description:

	This routine is called whenever bsrlock driver notifies bsrlock's callback object.

Arguments:

	Context - not used.
	Argument1 - Pointer to the BSR_VOLUME_CONTROL data structure containing volume information to be resized.
	Argument2 - not used.

Return Value:

	None.

--*/	
{
	UNREFERENCED_PARAMETER(Context);
	UNREFERENCED_PARAMETER(Argument2);

	PBSR_VOLUME_CONTROL pVolume = (PBSR_VOLUME_CONTROL)Argument1;

	if (pVolume == NULL) {
		// invalid parameter.
		bsr_err(30, BSR_LC_DRIVER, NO_OBJECT, "Failed to call back function bsr due to volume control is not allocate");
		return;
	}

	PDEVICE_OBJECT pDeviceObject = pVolume->pVolumeObject;

	PVOLUME_EXTENSION VolumeExtension = mvolSearchVolExtention(pDeviceObject);
	
	if (VolumeExtension == NULL) {
		bsr_err(31, BSR_LC_DRIVER, NO_OBJECT, "Failed to call back function bsr due to cannot find volume, PDO(0x%p)", pDeviceObject);
		return;
	}

	bsr_info(32, BSR_LC_DRIVER, NO_OBJECT, "volume [%ws] is extended.", VolumeExtension->PhysicalDeviceName);

	unsigned long long new_size = get_targetdev_volsize(VolumeExtension);
	
	if (VolumeExtension->dev->bd_contains) {
		VolumeExtension->dev->bd_contains->d_size = new_size;
		VolumeExtension->dev->bd_disk->queue->max_hw_sectors = new_size ? (new_size >> 9) : BSR_MAX_BIO_SIZE;
	}
	
	if (VolumeExtension->Active) {	
		struct bsr_device *device = get_device_with_vol_ext(VolumeExtension, TRUE);

		if (device) {
			int err = 0;
			

			bsr_suspend_io(device, WRITE_ONLY);
			bsr_set_my_capacity(device, new_size >> 9);
			
			err = bsr_resize(device);

			if (err) {
				bsr_err(33, BSR_LC_DRIVER, NO_OBJECT, "Failed to call back function bsr due to bsr resize failed. err(%d)", err);
			}
			bsr_resume_io(device);

			kref_put(&device->kref, bsr_destroy_device);
		}
	}
}

NTSTATUS 
bsrStartupCallback(
)
/*++

Routine Description:

	Initializes callback object to be notified.

Arguments:

	None.

Return Value:

	NtStatus values.

--*/
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	OBJECT_ATTRIBUTES oa = { 0, };
	UNICODE_STRING usCallbackName;

	RtlInitUnicodeString(&usCallbackName, BSR_CALLBACK_NAME);
	InitializeObjectAttributes(&oa, &usCallbackName, OBJ_CASE_INSENSITIVE | OBJ_PERMANENT, 0, 0);

	status = ExCreateCallback(&g_pCallbackObj, &oa, TRUE, TRUE);
	if (!NT_SUCCESS(status)) {
		bsr_info(34, BSR_LC_DRIVER, NO_OBJECT, "ExCreateCallback failed, status : 0x%x", status);
		return status;
	}

	g_pCallbackReg = ExRegisterCallback(g_pCallbackObj, bsrCallbackFunc, NULL);

	return status;
}

VOID
bsrCleanupCallback(
	)
/*++

Routine Description:

	Cleans up callback object.

Arguments:

	None.

Return Value:

	None.

--*/
{
	if (g_pCallbackReg)
		ExUnregisterCallback(g_pCallbackReg);

	if (g_pCallbackObj)
		ObDereferenceObject(g_pCallbackObj);
}


