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

#include <Ntifs.h>
#include <wdm.h>
#include "../../../bsr/bsr-kernel-compat/windows/bsr_windows.h"
#include "proto.h"
#include "../../../bsr/bsr_int.h"

#ifdef _WIN_MULTIVOL_THREAD
NTSTATUS
mvolInitializeThread( PMVOL_THREAD pThreadInfo, PKSTART_ROUTINE ThreadRoutine )
#else
NTSTATUS
mvolInitializeThread( PVOLUME_EXTENSION VolumeExtension,
	PMVOL_THREAD pThreadInfo, PKSTART_ROUTINE ThreadRoutine )
#endif
{
	NTSTATUS					status;
	HANDLE						threadhandle;
	SECURITY_QUALITY_OF_SERVICE	se_quality_service;

#ifndef _WIN_MULTIVOL_THREAD
    if (pThreadInfo->Active) {
        return STATUS_DEVICE_ALREADY_ATTACHED;
    }
#endif

	pThreadInfo->exit_thread = FALSE;
#ifndef _WIN_MULTIVOL_THREAD
	pThreadInfo->DeviceObject = VolumeExtension->DeviceObject;
#endif

	RtlZeroMemory( &se_quality_service, sizeof(SECURITY_QUALITY_OF_SERVICE) );
	se_quality_service.Length = sizeof(SECURITY_QUALITY_OF_SERVICE);
	se_quality_service.ImpersonationLevel = SecurityImpersonation;
	se_quality_service.ContextTrackingMode = SECURITY_STATIC_TRACKING;
	se_quality_service.EffectiveOnly = FALSE;

	status = SeCreateClientSecurity( PsGetCurrentThread(), &se_quality_service,
		FALSE, (PSECURITY_CLIENT_CONTEXT)&pThreadInfo->se_client_context);
	if( !NT_SUCCESS(status) ) {
		bsr_err(NO_OBJECT,"cannot create client security, err=0x%x", status);
		return status;
	}

	KeInitializeEvent(&pThreadInfo->RequestEvent, SynchronizationEvent, FALSE);
	KeInitializeEvent(&pThreadInfo->SplitIoDoneEvent, SynchronizationEvent, FALSE);
	InitializeListHead(&pThreadInfo->ListHead);
	KeInitializeSpinLock(&pThreadInfo->ListLock);

	status = PsCreateSystemThread( &threadhandle, 0L, NULL, 0L, NULL,
		(PKSTART_ROUTINE)ThreadRoutine, (PVOID)pThreadInfo );
	if( !NT_SUCCESS(status) ) {
		bsr_err(NO_OBJECT,"cannot create Thread, err=0x%x", status);
		SeDeleteClientSecurity( &pThreadInfo->se_client_context );
		return status;
	}

	status = ObReferenceObjectByHandle( threadhandle, THREAD_ALL_ACCESS, NULL, KernelMode,
		&pThreadInfo->pThread, NULL );
	ZwClose( threadhandle );
	if( !NT_SUCCESS(status) ) {
		pThreadInfo->exit_thread = TRUE;
		IO_THREAD_SIG( pThreadInfo);
		SeDeleteClientSecurity( &pThreadInfo->se_client_context );
		return status;
	}
#ifndef _WIN_MULTIVOL_THREAD
	pThreadInfo->Active = TRUE;
#endif
	return STATUS_SUCCESS;
}

VOID
mvolTerminateThread( PMVOL_THREAD pThreadInfo )
{
    if( NULL == pThreadInfo )   return ;
#ifdef _WIN_MULTIVOL_THREAD
	if(!pThreadInfo->exit_thread)
#else
    if( TRUE == pThreadInfo->Active )
#endif
    {
        pThreadInfo->exit_thread = TRUE;
	    IO_THREAD_SIG( pThreadInfo );
        KeWaitForSingleObject( pThreadInfo->pThread, Executive, KernelMode, FALSE, NULL );
    }

    if( NULL != pThreadInfo->pThread ) {
	    ObDereferenceObject( pThreadInfo->pThread );
	    SeDeleteClientSecurity( &pThreadInfo->se_client_context );
        pThreadInfo->pThread = NULL;
    }
#ifndef _WIN_MULTIVOL_THREAD
	pThreadInfo->Active = FALSE;
#endif
}

VOID
mvolWorkThread(PVOID arg)
{
	NTSTATUS					status;
	PMVOL_THREAD				pThreadInfo;
	PDEVICE_OBJECT				DeviceObject;
	PVOLUME_EXTENSION			VolumeExtension = NULL;
	PLIST_ENTRY					request;
	PIRP						irp;
	PIO_STACK_LOCATION			irpSp;
	pThreadInfo = (PMVOL_THREAD) arg;
	int							high = 0;

#ifndef _WIN_MULTIVOL_THREAD
	DeviceObject = pThreadInfo->DeviceObject;
	VolumeExtension = DeviceObject->DeviceExtension;
	
    bsr_debug(NO_OBJECT,"WorkThread [%ws]: handle 0x%x start", VolumeExtension->PhysicalDeviceName, KeGetCurrentThread());
#endif

	for (;;) {
		int loop = 0;

		IO_THREAD_WAIT(pThreadInfo);
		if (pThreadInfo->exit_thread) {
#ifdef _WIN_MULTIVOL_THREAD
			bsr_info(NO_OBJECT,"Terminating mvolWorkThread");
#else
			bsr_debug(NO_OBJECT,"WorkThread [%ws]: Terminate Thread", VolumeExtension->PhysicalDeviceName);
#endif
			PsTerminateSystemThread(STATUS_SUCCESS);
		}

		while ((request = ExInterlockedRemoveHeadList(&pThreadInfo->ListHead, &pThreadInfo->ListLock)) != 0) {
#ifdef _WIN_MULTIVOL_THREAD		
			PMVOL_WORK_WRAPPER wr = CONTAINING_RECORD(request, struct _MVOL_WORK_WRAPPER, ListEntry);
			DeviceObject = wr->DeviceObject;	
			VolumeExtension = DeviceObject->DeviceExtension;		
			irp = wr->Irp;

#else
			irp = CONTAINING_RECORD(request, IRP, Tail.Overlay.ListEntry);
#endif
			irpSp = IoGetCurrentIrpStackLocation(irp);

#ifdef BSR_TRACE	
			DbgPrint("\n");
			bsr_debug(NO_OBJECT,"I/O Thread:IRQL(%d) start I/O(%s) loop(%d) .......................!", 
				KeGetCurrentIrql(), (irpSp->MajorFunction == IRP_MJ_WRITE)? "Write" : "Read", loop);
#endif

			switch (irpSp->MajorFunction) {
				case IRP_MJ_WRITE:
					status = mvolReadWriteDevice(VolumeExtension, irp, IRP_MJ_WRITE);
					if (status != STATUS_SUCCESS) {
						mvolLogError(VolumeExtension->DeviceObject, 111, MSG_WRITE_ERROR, status);

						irp->IoStatus.Information = 0;
						irp->IoStatus.Status = status;
						IoCompleteRequest(irp, (CCHAR)(NT_SUCCESS(irp->IoStatus.Status) ? IO_DISK_INCREMENT : IO_NO_INCREMENT));
					}
					break;

			case IRP_MJ_READ:
				if (g_read_filter) {
					status = mvolReadWriteDevice(VolumeExtension, irp, IRP_MJ_READ);
					if (status != STATUS_SUCCESS) {
						mvolLogError(VolumeExtension->DeviceObject, 111, MSG_WRITE_ERROR, status);
						irp->IoStatus.Information = 0;
						irp->IoStatus.Status = status;
						IoCompleteRequest(irp, (CCHAR)(NT_SUCCESS(irp->IoStatus.Status) ? IO_DISK_INCREMENT : IO_NO_INCREMENT));
					}
				}
				break;
			case IRP_MJ_FLUSH_BUFFERS:
				mvolSendToNextDriver(VolumeExtension->DeviceObject, irp);
				break;
			default:
				bsr_err(NO_OBJECT,"WorkThread: invalid IRP MJ=0x%x", irpSp->MajorFunction);
				irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
				IoCompleteRequest(irp, (CCHAR)(NT_SUCCESS(irp->IoStatus.Status) ? IO_DISK_INCREMENT : IO_NO_INCREMENT));
				break;
			}
#ifdef _WIN_MULTIVOL_THREAD
			kfree(wr);
#endif
			loop++;
		}

		if (loop > 1) {
			if (high < loop) {
				high = loop;
				bsr_info(NO_OBJECT,"hooker[%ws]: irp processing peek(%d)",
					VolumeExtension->PhysicalDeviceName, high);
			}
		}		
		loop = 0;
	}
}



#ifdef _WIN_MULTIVOL_THREAD
VOID mvolQueueWork (PMVOL_THREAD pThreadInfo, PDEVICE_OBJECT DeviceObject, PIRP irp)
{
    PMVOL_WORK_WRAPPER wr = kmalloc(sizeof(struct _MVOL_WORK_WRAPPER), 0, '76DW');

    if(!wr) {
        bsr_err(NO_OBJECT,"Could not allocate mvol work.");
        return;
    }
    
    wr->DeviceObject = DeviceObject;
    wr->Irp = irp;
    ExInterlockedInsertTailList(&pThreadInfo->ListHead, &wr->ListEntry, &pThreadInfo->ListLock);

    IO_THREAD_SIG(pThreadInfo);
}
#endif