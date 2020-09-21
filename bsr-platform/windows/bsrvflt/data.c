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

#include <ntddk.h>
#include "disp.h"
#include "proto.h"

PDEVICE_OBJECT	mvolRootDeviceObject;
PDRIVER_OBJECT	mvolDriverObject;
KSPIN_LOCK		mvolVolumeLock;
KMUTEX			mvolMutex;
KMUTEX			eventlogMutex;
PETHREAD		g_NetlinkServerThread;



