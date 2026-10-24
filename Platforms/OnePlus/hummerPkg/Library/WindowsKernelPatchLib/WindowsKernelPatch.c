#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/WindowsKernelPatchLib.h>
#include "../../../../../Silicon/Silicium/SiliciumPkg/Library/KernelErrataPatcherLib/KernelErrataPatcherLib.h"

extern UINT8 HummerAmuPatchStart[];
extern UINT8 HummerAmuPatchEnd[];

STATIC EFI_EXIT_BOOT_SERVICES mExitBootServices;
STATIC UINT64 mMemoryMap[8192];

STATIC EFI_STATUS EFIAPI
ExitWindowsBootServices (
  IN EFI_HANDLE ImageHandle,
  IN UINTN MapKey
  )
{
  EFI_MEMORY_DESCRIPTOR *Descriptor;
  EFI_STATUS Status;
  UINTN Size = sizeof (mMemoryMap);
  UINTN Key;
  UINTN Stride;
  UINTN Offset;
  UINT32 Version;

  Status = gBS->GetMemoryMap (&Size, (EFI_MEMORY_DESCRIPTOR *)mMemoryMap,
    &Key, &Stride, &Version);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  for (Offset = 0; Offset < Size; Offset += Stride) {
    Descriptor = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)mMemoryMap + Offset);
    if (Descriptor->Type != EfiLoaderCode ||
        !IsWinloadMemory (Descriptor->PhysicalStart,
          EFI_PAGES_TO_SIZE (Descriptor->NumberOfPages))) {
      continue;
    }
    Status = SetWinloadProtection (Descriptor->PhysicalStart,
      EFI_PAGES_TO_SIZE (Descriptor->NumberOfPages), FALSE);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    Status = InstallTransferToKernelPatch (HummerAmuPatchStart,
      HummerAmuPatchEnd - HummerAmuPatchStart);
    if (EFI_ERROR (Status)) {
      SetWinloadProtection (Descriptor->PhysicalStart,
        EFI_PAGES_TO_SIZE (Descriptor->NumberOfPages), TRUE);
      return Status;
    }
    Status = SetWinloadProtection (Descriptor->PhysicalStart,
      EFI_PAGES_TO_SIZE (Descriptor->NumberOfPages), TRUE);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    UninstallWindowsKernelPatch ();
    return gBS->ExitBootServices (ImageHandle, MapKey);
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS
InstallWindowsKernelPatch (VOID)
{
  LocateMemoryAttributeProtocol ();
  mExitBootServices = gBS->ExitBootServices;
  gBS->ExitBootServices = ExitWindowsBootServices;
  gBS->Hdr.CRC32 = 0;
  return gBS->CalculateCrc32 (gBS, sizeof (*gBS), &gBS->Hdr.CRC32);
}

VOID
UninstallWindowsKernelPatch (VOID)
{
  gBS->ExitBootServices = mExitBootServices;
  gBS->Hdr.CRC32 = 0;
  gBS->CalculateCrc32 (gBS, sizeof (*gBS), &gBS->Hdr.CRC32);
}
