#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/EFIUsbMsd.h>
#include <Protocol/GraphicsOutput.h>

STATIC VOID
ShowStatus (VOID)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Graphics;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black;
  UINTN Columns = 80;
  UINTN Rows = 25;

  ZeroMem (&Black, sizeof (Black));
  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Graphics))) {
    Graphics->Blt (
      Graphics,
      &Black,
      EfiBltVideoFill,
      0,
      0,
      0,
      0,
      Graphics->Mode->Info->HorizontalResolution,
      Graphics->Mode->Info->VerticalResolution,
      0
      );
  }

  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->QueryMode (gST->ConOut, gST->ConOut->Mode->Mode, &Columns, &Rows);
  gST->ConOut->SetAttribute (gST->ConOut, EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK));
  gST->ConOut->SetCursorPosition (gST->ConOut, (Columns - (sizeof (L"Mass Storage") / sizeof (CHAR16) - 1)) / 2, Rows / 2);
  gST->ConOut->OutputString (gST->ConOut, L"Mass Storage");
}

STATIC BOOLEAN
ExitPressed (VOID)
{
  EFI_INPUT_KEY Key;
  EFI_STATUS Status;

  if (gST->ConIn == NULL) {
    return FALSE;
  }
  ZeroMem (&Key, sizeof (Key));
  Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  return !EFI_ERROR (Status) && (Key.ScanCode == SCAN_UP);
}

STATIC BOOLEAN
IsUfsLun0 (IN EFI_HANDLE Handle)
{
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_STATUS Status;

  Status = gBS->HandleProtocol (
    Handle, &gEfiDevicePathProtocolGuid, (VOID **)&Path);
  if (EFI_ERROR (Status) || !IsDevicePathValid (Path, SIZE_4KB)) {
    return FALSE;
  }
  while (!IsDevicePathEnd (Path)) {
    if ((DevicePathType (Path) == HARDWARE_DEVICE_PATH) &&
        (DevicePathSubType (Path) == HW_VENDOR_DP) &&
        (DevicePathNodeLength (Path) >= sizeof (VENDOR_DEVICE_PATH)) &&
        CompareGuid (&((VENDOR_DEVICE_PATH *)Path)->Guid, &gEfiUfsLU0Guid)) {
      return TRUE;
    }
    if ((DevicePathType (Path) == MESSAGING_DEVICE_PATH) &&
        (DevicePathSubType (Path) == MSG_UFS_DP) &&
        (DevicePathNodeLength (Path) >= sizeof (UFS_DEVICE_PATH))) {
      return ((UFS_DEVICE_PATH *)Path)->Lun == 0;
    }
    Path = NextDevicePathNode (Path);
  }
  return FALSE;
}

STATIC EFI_STATUS
AssignStorageHandle (IN EFI_USB_MSD_PROTOCOL *UsbMsd)
{
  EFI_BLOCK_IO_PROTOCOL *Storage;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  EFI_HANDLE *Handles;
  EFI_STATUS Status;
  UINTN HandleCount;
  UINTN Index;

  Storage = NULL;
  Status = gBS->LocateHandleBuffer (
    ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (
      Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo);
    if (EFI_ERROR (Status) || (BlockIo->Media == NULL) ||
        !BlockIo->Media->MediaPresent || BlockIo->Media->LogicalPartition ||
        (BlockIo->Media->BlockSize == 0)) {
      continue;
    }
    if (IsUfsLun0 (Handles[Index])) {
      Storage = BlockIo;
      break;
    }
  }
  FreePool (Handles);
  if (Storage == NULL) {
    return EFI_NOT_FOUND;
  }
  Status = UsbMsd->AssignBlkIoHandle (UsbMsd, Storage, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI
MassStorageEntry (
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_USB_MSD_PROTOCOL *UsbMsd;
  EFI_STATUS Status;
  EFI_STATUS EventStatus;
  EFI_STATUS StopStatus;
  EFI_INPUT_KEY Key;

  gBS->SetWatchdogTimer (0, 0, 0, NULL);
  if (gST->ConOut != NULL) {
    gST->ConOut->ClearScreen (gST->ConOut);
    gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  }
  if (gST->ConIn != NULL) {
    while (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
    }
  }
  ShowStatus ();
  Status = gBS->LocateProtocol (
    &gEfiUsbMsdProtocolGuid, NULL, (VOID **)&UsbMsd);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = AssignStorageHandle (UsbMsd);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = UsbMsd->StartDevice (UsbMsd);
  if (EFI_ERROR (Status)) {
    UsbMsd->AssignBlkIoHandle (UsbMsd, NULL, 0);
    return Status;
  }
  Status = EFI_SUCCESS;
  while (!ExitPressed ()) {
    EventStatus = UsbMsd->EventHandler (UsbMsd);
    if (EFI_ERROR (EventStatus) && (EventStatus != EFI_NOT_READY)) {
      Status = EventStatus;
      break;
    }
    gBS->Stall (100);
  }
  StopStatus = UsbMsd->StopDevice (UsbMsd);
  UsbMsd->AssignBlkIoHandle (UsbMsd, NULL, 0);
  if (gST->ConOut != NULL) {
    gST->ConOut->ClearScreen (gST->ConOut);
  }
  return EFI_ERROR (Status) ? Status : StopStatus;
}
