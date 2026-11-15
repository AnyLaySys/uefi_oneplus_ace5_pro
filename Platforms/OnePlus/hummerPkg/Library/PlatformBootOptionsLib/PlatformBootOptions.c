#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PlatformBootOptionsLib.h>
#include <BootOptions.h>

STATIC EFI_GUID mInternalStorageMenuGuid = HUMMER_INTERNAL_MENU_FILE_GUID;

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
  (*BootOption)[0].AppName = L"Internal";
  (*BootOption)[0].AppGuid = &mInternalStorageMenuGuid;
  (*BootOption)[0].LoadAttribute = LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP;
  (*BootOption)[1].AppName = L"Mass";
  (*BootOption)[1].AppGuid = FixedPcdGetPtr (PcdMassStorageFile);
  (*BootOption)[1].LoadAttribute = LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP;
  *BootOptionCount = 2;
}
