#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/DeviceBootManagerLib.h>
#include <Library/PcdLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/RfsProtectionLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/LoadedImage.h>

STATIC EFI_STATUS
StartLogExporter (VOID)
{
  EFI_GUID Guid = {
    0xA491D18E, 0x8267, 0x4AF4, { 0x99, 0x71, 0xE4, 0xD1, 0xFD, 0x83, 0x07, 0x31 }
  };
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_DEVICE_PATH_PROTOCOL *VolumePath;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH FileNode;
  EFI_HANDLE Handle;
  EFI_STATUS Status;

  Status = gBS->HandleProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  VolumePath = DevicePathFromHandle (Loaded->DeviceHandle);
  if (VolumePath == NULL) {
    return EFI_NOT_FOUND;
  }
  EfiInitializeFwVolDevicepathNode (&FileNode, &Guid);
  Path = AppendDevicePathNode (VolumePath, &FileNode.Header);
  if (Path == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  Status = gBS->LoadImage (FALSE, gImageHandle, Path, NULL, 0, &Handle);
  FreePool (Path);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = gBS->StartImage (Handle, NULL, NULL);
  gBS->UnloadImage (Handle);
  return Status;
}

STATIC VOID EFIAPI
DummyNotify (IN EFI_EVENT Event, IN VOID *Context)
{
}

EFI_HANDLE EFIAPI
DeviceBootManagerBeforeConsole (
  OUT EFI_DEVICE_PATH_PROTOCOL **DevicePath,
  OUT BDS_CONSOLE_CONNECT_ENTRY **PlatformConsoles)
{
  return NULL;
}

EFI_DEVICE_PATH_PROTOCOL ** EFIAPI
DeviceBootManagerAfterConsole (VOID)
{
  EFI_STATUS Status;
  EFI_EVENT InitEvent;

  if (FixedPcdGetBool (PcdHummerLogRecovery)) {
    Status = StartLogExporter ();
    DEBUG ((EFI_D_ERROR, "HUMMER log recovery application returned: %r\n", Status));
    return NULL;
  }
  Status = gBS->CreateEventEx (
    EVT_NOTIFY_SIGNAL, TPL_CALLBACK, DummyNotify, NULL,
    &gSdCardInitGuid, &InitEvent);
  if (!EFI_ERROR (Status)) {
    gBS->SignalEvent (InitEvent);
    gBS->CloseEvent (InitEvent);
  }

  gBS->SetWatchdogTimer (0, 0, 0, NULL);
  return NULL;
}

EFI_DEVICE_PATH_PROTOCOL ** EFIAPI
DeviceBootManagerOnDemandConInConnect (VOID)
{
  EFI_STATUS Status;
  EFI_EVENT InitEvent;

  Status = gBS->CreateEventEx (
    EVT_NOTIFY_SIGNAL, TPL_CALLBACK, DummyNotify, NULL,
    &gUsbControllerInitGuid, &InitEvent);
  if (!EFI_ERROR (Status)) {
    gBS->SignalEvent (InitEvent);
    gBS->CloseEvent (InitEvent);
  }
  return NULL;
}

VOID EFIAPI
DeviceBootManagerProcessBootCompletion (
  OUT EFI_BOOT_MANAGER_LOAD_OPTION *BootOption)
{
}

EFI_STATUS EFIAPI
DeviceBootManagerPriorityBoot (
  IN OUT EFI_BOOT_MANAGER_LOAD_OPTION *BootOption)
{
  return EFI_SUCCESS;
}

VOID EFIAPI
DeviceBootManagerUnableToBoot (VOID)
{
}

VOID EFIAPI
DeviceBootManagerBdsEntry (VOID)
{
  ProtectRfsSharedArea ();
}
