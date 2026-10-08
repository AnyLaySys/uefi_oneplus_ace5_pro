#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PlatformBootOptionsLib.h>

STATIC EFI_GUID mHummerLinuxBootGuid = {
  0x7B2D0E54, 0x5B4F, 0x4D86, { 0x9D, 0x4C, 0xB2, 0x9A, 0x7E, 0x0D, 0x1A, 0x39 }
};

STATIC EFI_GUID mHummerLogExportGuid = {
  0xA491D18E, 0x8267, 0x4AF4, { 0x99, 0x71, 0xE4, 0xD1, 0xFD, 0x83, 0x07, 0x31 }
};

VOID
GetPlatformBootOptions (
  OUT EFI_PLATFORM_BOOT_OPTION **BootOption,
  OUT UINT8 *BootOptionCount)
{
  *BootOptionCount = 0;
  *BootOption = AllocateZeroPool (sizeof (**BootOption) * 3);
  if (*BootOption == NULL) {
    return;
  }
  if (FixedPcdGetBool (PcdHummerLogRecovery)) {
    (*BootOption)[0].AppName = L"Linux Logs";
    (*BootOption)[0].AppGuid = &mHummerLogExportGuid;
    (*BootOption)[0].LoadAttribute = LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP;
    *BootOptionCount = 1;
    return;
  }
  (*BootOption)[0].AppName = L"Mass Storage";
  (*BootOption)[0].AppGuid = FixedPcdGetPtr (PcdMassStorageFile);
  (*BootOption)[0].LoadAttribute = LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP;
  (*BootOption)[1].AppName = L"Linux Test";
  (*BootOption)[1].AppGuid = &mHummerLinuxBootGuid;
  (*BootOption)[1].LoadAttribute = LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP;
  (*BootOption)[2].AppName = L"Linux Logs";
  (*BootOption)[2].AppGuid = &mHummerLogExportGuid;
  (*BootOption)[2].LoadAttribute = LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP;
  *BootOptionCount = 3;
}
