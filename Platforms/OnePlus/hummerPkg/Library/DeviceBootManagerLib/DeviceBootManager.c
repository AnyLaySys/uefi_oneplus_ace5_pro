#include <Uefi.h>
#include <Library/DeviceBootManagerLib.h>
#include <Library/RfsProtectionLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

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
