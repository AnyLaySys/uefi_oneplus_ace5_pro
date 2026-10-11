#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PlatformBootOptionsLib.h>

STATIC EFI_GUID mHummerLinuxBootGuid = {
  0x7B2D0E54, 0x5B4F, 0x4D86, { 0x9D, 0x4C, 0xB2, 0x9A, 0x7E, 0x0D, 0x1A, 0x39 }
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
  (*BootOption)[0].AppName = L"Linux";
  (*BootOption)[0].AppGuid = &mHummerLinuxBootGuid;
  (*BootOption)[0].LoadAttribute = LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP;
  (*BootOption)[1].AppName = L"Mass Storage";
  (*BootOption)[1].AppGuid = FixedPcdGetPtr (PcdMassStorageFile);
  (*BootOption)[1].LoadAttribute = LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP;
  *BootOptionCount = 2;
}
