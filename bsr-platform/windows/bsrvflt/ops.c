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

extern SIMULATION_DISK_IO_ERROR gSimulDiskIoError;

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
		//bsr_err(NO_OBJECT,"IOCTL_GetAllVolumeInfo buffer too small outlen:%d required len:%d\n",outlen,(count * sizeof(BSR_VOLUME_ENTRY)) );
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
			pventry->AgreedSize = pvext->dev->d_size;
			if (pvext->dev->bd_contains) {
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
		bsr_err(NO_OBJECT,"RootDevice\n");
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	VolumeExtension = DeviceObject->DeviceExtension;
	outlen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
	if( outlen < sizeof(MVOL_VOLUME_INFO) )	{
		mvolLogError( DeviceObject, 212, MSG_BUFFER_SMALL, STATUS_BUFFER_TOO_SMALL );
		bsr_err(NO_OBJECT,"buffer too small out %d sizeof(MVOL_VOLUME_INFO) %d\n", outlen, sizeof(MVOL_VOLUME_INFO));
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
		bsr_warn(BSR_LC_DRIVER, NO_OBJECT,"SystemBuffer is NULL. Maybe older bsrcon was used or other access was tried\n");
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
		_snprintf(Message, sizeof(Message) - 1, "%wZ is not a replication volume", &pvext->MountPoint);
		*ReturnLength = (ULONG)strlen(Message);
		bsr_err(NO_OBJECT,"%s\n", Message);
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
		_snprintf(Message, sizeof(Message) - 1, "%wZ volume is handling by bsr. Failed to release volume",
			&pvext->MountPoint);
		*ReturnLength = (ULONG)strlen(Message);
		bsr_err(NO_OBJECT,"%s\n", Message);
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

		bsr_err(NO_OBJECT,"buffer too small\n");
		return STATUS_BUFFER_TOO_SMALL;
	}

	pVolumeInfo = (PMVOL_VOLUME_INFO) Irp->AssociatedIrp.SystemBuffer;
	
	if( DeviceObject == mvolRootDeviceObject ) {
		bsr_debug(NO_OBJECT,"Root Device IOCTL\n");

		MVOL_LOCK();
		VolumeExtension = mvolSearchDevice( pVolumeInfo->PhysicalDeviceName );
		MVOL_UNLOCK();

		if( VolumeExtension == NULL ) {
			mvolLogError( DeviceObject, 322, MSG_NO_DEVICE, STATUS_NO_SUCH_DEVICE );
			bsr_err(NO_OBJECT,"cannot find volume, PD=%ws\n", pVolumeInfo->PhysicalDeviceName);
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
		bsr_err(NO_OBJECT,"cannot get volume size, err=0x%x\n", status);
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
		bsr_err(NO_OBJECT,"buffer too small\n");
		return STATUS_BUFFER_TOO_SMALL;
	}

	pVolumeInfo = (PMVOL_VOLUME_INFO) Irp->AssociatedIrp.SystemBuffer;
	if( DeviceObject == mvolRootDeviceObject ) {
		bsr_debug(NO_OBJECT,"Root Device IOCTL\n");

		MVOL_LOCK();
		VolumeExtension = mvolSearchDevice( pVolumeInfo->PhysicalDeviceName );
		MVOL_UNLOCK();

		if( VolumeExtension == NULL ) {
			mvolLogError( DeviceObject, 352, MSG_NO_DEVICE, STATUS_NO_SUCH_DEVICE );
			bsr_err(NO_OBJECT,"cannot find volume, PD=%ws\n", pVolumeInfo->PhysicalDeviceName);
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
		bsr_err(NO_OBJECT,"buffer too small\n");
		return STATUS_BUFFER_TOO_SMALL;
	}
	if(Irp->AssociatedIrp.SystemBuffer) {
		pSDError = (SIMULATION_DISK_IO_ERROR*)Irp->AssociatedIrp.SystemBuffer;
		RtlCopyMemory(&gSimulDiskIoError, pSDError, sizeof(SIMULATION_DISK_IO_ERROR));
		bsr_info(NO_OBJECT,"IOCTL_MVOL_SET_SIMUL_DISKIO_ERROR ErrorFlag:%d ErrorType:%d\n", gSimulDiskIoError.ErrorFlag, gSimulDiskIoError.ErrorType);
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
		bsr_err(NO_OBJECT,"buffer too small\n");
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
		else if (pLoggingMinLv->nType == LOGGING_TYPE_FEATURELOG) {
			previous_lv_min = atomic_read(&g_featurelog_flag);
			atomic_set(&g_featurelog_flag, pLoggingMinLv->nErrLvMin);
		}
		else {
			bsr_warn(BSR_LC_DRIVER, NO_OBJECT,"invalidate logging type(%d)\n", pLoggingMinLv->nType);
		}

		// DW-1432 Modified to see if command was successful 
		Status = SaveCurrentValue(LOG_LV_REG_VALUE_NAME, Get_log_lv());
		// DW-2008
		bsr_info(NO_OBJECT,"set minimum log level, type : %s(%d), minumum level : %s(%d) => %s(%d), result : %lu\n", 
					g_log_type_str[pLoggingMinLv->nType], pLoggingMinLv->nType, 
					// DW-2041
					((pLoggingMinLv->nType == LOGGING_TYPE_FEATURELOG) ? "" : g_default_lv_str[previous_lv_min]), previous_lv_min, 
					((pLoggingMinLv->nType == LOGGING_TYPE_FEATURELOG) ? "" : g_default_lv_str[pLoggingMinLv->nErrLvMin]), pLoggingMinLv->nErrLvMin,
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
		bsr_err(NO_OBJECT, "buffer too small\n");
		return STATUS_BUFFER_TOO_SMALL;
	}
	if (Irp->AssociatedIrp.SystemBuffer) {
		log_file_max_count = *(ULONG*)Irp->AssociatedIrp.SystemBuffer;

		status = SaveCurrentValue(LOG_FILE_MAX_REG_VALUE_NAME, log_file_max_count);
		bsr_info(NO_OBJECT, "set log file max count %lu => %lu\n", atomic_read(&g_log_file_max_count), log_file_max_count);
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

NTSTATUS
IOCTL_GetBsrLog(PDEVICE_OBJECT DeviceObject, PIRP Irp, ULONG* size)
{
	ULONG			inlen, outlen;
	BSR_LOG* 		pBsrLog = NULL;
	PIO_STACK_LOCATION	irpSp = IoGetCurrentIrpStackLocation(Irp);
	inlen = irpSp->Parameters.DeviceIoControl.InputBufferLength;
	outlen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

	if(!size) {
		bsr_err(NO_OBJECT,"GetBsrLog Invalid parameter. size is NULL\n");
		return STATUS_INVALID_PARAMETER;
	}
	*size = 0;	
	
	if (inlen < BSR_LOG_SIZE || outlen < BSR_LOG_SIZE) {
		mvolLogError(DeviceObject, 355, MSG_BUFFER_SMALL, STATUS_BUFFER_TOO_SMALL);
		bsr_err(NO_OBJECT,"GetBsrLog buffer too small\n");
		return STATUS_BUFFER_TOO_SMALL;
	}
	if (Irp->AssociatedIrp.SystemBuffer) {
		pBsrLog = (BSR_LOG*)Irp->AssociatedIrp.SystemBuffer;
		pBsrLog->totalcnt = InterlockedCompareExchange64(&gLogBuf.h.total_count, 0, 0);
		if (pBsrLog->LogBuf) {
			RtlCopyMemory(pBsrLog->LogBuf, gLogBuf.b, MAX_BSRLOG_BUF*LOGBUF_MAXCNT);
				*size = BSR_LOG_SIZE;
		} else {
			bsr_err(NO_OBJECT,"GetBsrLog Invalid parameter. pBsrLog->LogBuf is NULL\n");
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
		bsr_err(NO_OBJECT,"buffer too small\n");
		return STATUS_BUFFER_TOO_SMALL;
	}
	
	if (Irp->AssociatedIrp.SystemBuffer) {
		pHandlerInfo = (PHANDLER_INFO)Irp->AssociatedIrp.SystemBuffer;
		g_handler_use = pHandlerInfo->use;

		SaveCurrentValue(L"handler_use", g_handler_use);

		bsr_debug(NO_OBJECT,"IOCTL_MVOL_SET_HANDLER_USE : %d \n", g_handler_use);
	}
	else {
		return STATUS_INVALID_PARAMETER;
	}

	return STATUS_SUCCESS;
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
		bsr_err(NO_OBJECT,"pVolume is NULL\n");
		return;
	}

	PDEVICE_OBJECT pDeviceObject = pVolume->pVolumeObject;

	PVOLUME_EXTENSION VolumeExtension = mvolSearchVolExtention(pDeviceObject);
	
	if (VolumeExtension == NULL) {
		bsr_err(NO_OBJECT,"cannot find volume, PDO=0x%p\n", pDeviceObject);
		return;
	}

	bsr_info(NO_OBJECT,"volume [%ws] is extended.\n", VolumeExtension->PhysicalDeviceName);

	unsigned long long new_size = get_targetdev_volsize(VolumeExtension);
	
	if (VolumeExtension->dev->bd_contains) {
		VolumeExtension->dev->bd_contains->d_size = new_size;
	}	
	
	if (VolumeExtension->Active) {	
		struct bsr_device *device = get_device_with_vol_ext(VolumeExtension, TRUE);

		if (device) {
			int err = 0;
			

			bsr_suspend_io(device, WRITE_ONLY);
			bsr_set_my_capacity(device, new_size >> 9);
			
			err = bsr_resize(device);

			if (err) {
				bsr_err(NO_OBJECT,"bsr resize failed. (err=%d)\n", err);
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
		bsr_info(NO_OBJECT,"ExCreateCallback failed, status : 0x%x\n", status);
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


