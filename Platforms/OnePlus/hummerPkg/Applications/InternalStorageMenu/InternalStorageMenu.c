#include <Uefi.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/LoadedImage.h>

STATIC
EFI_STATUS
BootApplication (
  IN EFI_HANDLE ImageHandle,
  IN EFI_GUID *FileGuid,
  IN CHAR16 *Description,
  IN CHAR8 *Parameter
  )
{
  EFI_BOOT_MANAGER_LOAD_OPTION Option;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH FileNode;
  EFI_STATUS Status;

  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  EfiInitializeFwVolDevicepathNode (&FileNode, FileGuid);
  DevicePath = AppendDevicePathNode (
                 DevicePathFromHandle (LoadedImage->DeviceHandle),
                 (EFI_DEVICE_PATH_PROTOCOL *)&FileNode
                 );
  if (DevicePath == NULL) {
    return EFI_NOT_FOUND;
  }
  Status = EfiBootManagerInitializeLoadOption (
             &Option,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP,
             Description,
             DevicePath,
             (UINT8 *)Parameter,
             AsciiStrSize (Parameter)
             );
  FreePool (DevicePath);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  EfiBootManagerBoot (&Option);
  Status = Option.Status;
  EfiBootManagerFreeLoadOption (&Option);
  return Status;
}

EFI_STATUS
EFIAPI
InternalStorageMenuEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  return BootApplication (
           ImageHandle,
           PcdGetPtr (PcdBootManagerMenuFile),
           L"Internal Storage",
           "INTERNAL_STORAGE"
           );
}
