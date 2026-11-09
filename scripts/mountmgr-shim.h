#pragma once
// Minimal compatibility declaration for the Mount Manager definitions LEP uses.
// The Windows Driver Kit normally supplies <mountmgr.h>; this project only needs
// the public IOCTL and two variable-length structures.
#ifndef CLAUDE_PROTECT_MOUNTMGR_SHIM_H
#define CLAUDE_PROTECT_MOUNTMGR_SHIM_H

#define MOUNTMGR_DEVICE_NAME L"\\Device\\MountPointManager"
#define MOUNTMGR_DOS_DEVICE_NAME L"\\\\.\\MountPointManager"
#define MOUNTMGRCONTROLTYPE ((ULONG)'m')

#define IOCTL_MOUNTMGR_QUERY_DOS_VOLUME_PATH \
    CTL_CODE(MOUNTMGRCONTROLTYPE, 12, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _MOUNTMGR_TARGET_NAME {
    USHORT DeviceNameLength;
    WCHAR DeviceName[1];
} MOUNTMGR_TARGET_NAME, *PMOUNTMGR_TARGET_NAME;

typedef struct _MOUNTMGR_VOLUME_PATHS {
    ULONG MultiSzLength;
    WCHAR MultiSz[1];
} MOUNTMGR_VOLUME_PATHS, *PMOUNTMGR_VOLUME_PATHS;

#endif
