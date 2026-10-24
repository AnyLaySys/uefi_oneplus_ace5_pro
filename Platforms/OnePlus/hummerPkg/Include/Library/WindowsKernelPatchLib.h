#ifndef HUMMER_WINDOWS_KERNEL_PATCH_LIB_H
#define HUMMER_WINDOWS_KERNEL_PATCH_LIB_H

#include <Uefi.h>

EFI_STATUS
InstallWindowsKernelPatch (VOID);

VOID
UninstallWindowsKernelPatch (VOID);

#endif
