#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PlatformBootOptionsLib.h>

STATIC EFI_GUID mInternalStorageMenuGuid = {
  0xB2F146C7, 0x34D6, 0x4CB0, { 0x91, 0x27, 0x61, 0x26, 0x3E, 0xAB, 0x70, 0x15 }
};

VOID
GetPlatformBootOptions (
  OUT EFI_PLATFORM_BOOT_OPTION **BootOption,
  OUT UINT8 *BootOptionCount)
{
  *BootOptionCount = 0;
  *BootOption = AllocateZeroPool (sizeof (**BootOption) * 2);
  if (*BootOption == NULL) {
    return;
  }
  (*BootOption)[0].AppName = L"Internal Storage";
  (*BootOption)[0].AppGuid = &mInternalStorageMenuGuid;
  (*BootOption)[0].LoadAttribute = LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP;
  (*BootOption)[1].AppName = L"Mass Storage";
  (*BootOption)[1].AppGuid = FixedPcdGetPtr (PcdMassStorageFile);
  (*BootOption)[1].LoadAttribute = LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP;
  *BootOptionCount = 2;
}
