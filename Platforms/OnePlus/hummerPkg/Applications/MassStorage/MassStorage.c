#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/EFIUsbMsd.h>

STATIC EFI_USB_MSD_PROTOCOL *mUsbMsd;
STATIC UINT8 mAssignedLuns;

STATIC EFI_GUID *mUfsGuids[] = {
  &gEfiUfsLU0Guid, &gEfiUfsLU1Guid, &gEfiUfsLU2Guid, &gEfiUfsLU3Guid,
  &gEfiUfsLU4Guid, &gEfiUfsLU5Guid, &gEfiUfsLU6Guid, &gEfiUfsLU7Guid
};

STATIC VOID
AddLine (IN CONST CHAR8 *Format, ...)
{
  VA_LIST Args;
  CHAR8 Line[192];

  VA_START (Args, Format);
  AsciiVSPrint (Line, sizeof (Line), Format, Args);
  VA_END (Args);
  if (gST->ConOut != NULL) {
    AsciiPrint ("%a\r\n", Line);
  }
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

STATIC VOID
WaitForExit (VOID)
{
  EFI_STATUS Status;
  UINTN EventIndex;

  AddLine ("Volume Up: return to Mu menu");
  if (gST->ConIn == NULL) {
    return;
  }
  while (!ExitPressed ()) {
    Status = gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &EventIndex);
    if (EFI_ERROR (Status)) {
      return;
    }
  }
}

STATIC VOID EFIAPI
DummyNotify (IN EFI_EVENT Event, IN VOID *Context)
{
}

STATIC VOID
InitializeUsb (VOID)
{
  EFI_STATUS Status;
  EFI_EVENT InitEvent;

  Status = gBS->CreateEventEx (
    EVT_NOTIFY_SIGNAL, TPL_CALLBACK, DummyNotify, NULL,
    &gUsbControllerInitGuid, &InitEvent);
  if (!EFI_ERROR (Status)) {
    Status = gBS->SignalEvent (InitEvent);
    gBS->CloseEvent (InitEvent);
  }
  if (EFI_ERROR (Status)) {
    AddLine ("USB initialization failed: %r", Status);
  }
}

STATIC INTN
GetUfsLun (IN EFI_HANDLE Handle)
{
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_STATUS Status;
  UINTN Lun;

  Status = gBS->HandleProtocol (
    Handle, &gEfiDevicePathProtocolGuid, (VOID **)&Path);
  if (EFI_ERROR (Status) || (Path == NULL) ||
      !IsDevicePathValid (Path, SIZE_4KB)) {
    return -1;
  }
  while (!IsDevicePathEnd (Path)) {
    if ((DevicePathType (Path) == HARDWARE_DEVICE_PATH) &&
        (DevicePathSubType (Path) == HW_VENDOR_DP) &&
        (DevicePathNodeLength (Path) >= sizeof (VENDOR_DEVICE_PATH))) {
      for (Lun = 0; Lun < ARRAY_SIZE (mUfsGuids); Lun++) {
        if (CompareGuid (&((VENDOR_DEVICE_PATH *)Path)->Guid, mUfsGuids[Lun])) {
          return (INTN)Lun;
        }
      }
    }
    if ((DevicePathType (Path) == MESSAGING_DEVICE_PATH) &&
        (DevicePathSubType (Path) == MSG_UFS_DP) &&
        (DevicePathNodeLength (Path) >= sizeof (UFS_DEVICE_PATH))) {
      Lun = ((UFS_DEVICE_PATH *)Path)->Lun;
      return (Lun < ARRAY_SIZE (mUfsGuids)) ? (INTN)Lun : -1;
    }
    Path = NextDevicePathNode (Path);
  }
  return -1;
}

STATIC EFI_STATUS
AssignStorageHandles (VOID)
{
  EFI_BLOCK_IO_PROTOCOL *Storage[ARRAY_SIZE (mUfsGuids)];
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  EFI_HANDLE *Handles;
  EFI_STATUS Status;
  EFI_STATUS AssignmentError;
  UINTN HandleCount;
  UINTN Index;
  UINTN Lun;
  INTN StorageLun;
  UINT8 Capacity;
  UINT64 SizeMiB;

  ZeroMem (Storage, sizeof (Storage));
  Capacity = 0;
  Status = mUsbMsd->QueryMaxLun (mUsbMsd, &Capacity);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Capacity == 0) {
    return EFI_UNSUPPORTED;
  }
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
    StorageLun = GetUfsLun (Handles[Index]);
    if ((StorageLun >= 0) && (Storage[StorageLun] == NULL)) {
      Storage[StorageLun] = BlockIo;
    }
  }
  FreePool (Handles);
  AssignmentError = EFI_SUCCESS;
  for (Lun = 0; Lun < ARRAY_SIZE (Storage); Lun++) {
    if (Storage[Lun] == NULL) {
      continue;
    }
    if (mAssignedLuns >= Capacity) {
      AddLine ("USB LUN capacity reached");
      AssignmentError = EFI_OUT_OF_RESOURCES;
      break;
    }
    BlockIo = Storage[Lun];
    Status = mUsbMsd->AssignBlkIoHandle (mUsbMsd, BlockIo, mAssignedLuns);
    if (EFI_ERROR (Status)) {
      AddLine ("LU%u assign: %r", Lun, Status);
      AssignmentError = Status;
      continue;
    }
    SizeMiB = ((BlockIo->Media->LastBlock + 1) *
      BlockIo->Media->BlockSize) >> 20;
    AddLine ("USB%u = UFS LU%u  %Lu MiB", mAssignedLuns, Lun, SizeMiB);
    mAssignedLuns++;
  }
  if (mAssignedLuns == 0) {
    return EFI_ERROR (AssignmentError) ? AssignmentError : EFI_NOT_FOUND;
  }
  return AssignmentError;
}

STATIC VOID
UnassignStorageHandles (VOID)
{
  UINT8 Lun;

  for (Lun = 0; Lun < mAssignedLuns; Lun++) {
    mUsbMsd->AssignBlkIoHandle (mUsbMsd, NULL, Lun);
  }
  mAssignedLuns = 0;
}

EFI_STATUS EFIAPI
MassStorageEntry (
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_STATUS EventStatus;
  EFI_STATUS LastEventError;
  EFI_INPUT_KEY Key;

  mUsbMsd = NULL;
  mAssignedLuns = 0;
  gBS->SetWatchdogTimer (0, 0, 0, NULL);
  if (gST->ConOut != NULL) {
    gST->ConOut->ClearScreen (gST->ConOut);
    gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  }
  if (gST->ConIn != NULL) {
    while (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
    }
  }
  AddLine ("USB Mass Storage");
  InitializeUsb ();
  Status = gBS->LocateProtocol (
    &gEfiUsbMsdProtocolGuid, NULL, (VOID **)&mUsbMsd);
  if (EFI_ERROR (Status)) {
    AddLine ("USB MSD unavailable: %r", Status);
    WaitForExit ();
    return Status;
  }
  Status = AssignStorageHandles ();
  if (EFI_ERROR (Status)) {
    AddLine ("Storage assignment: %r", Status);
    UnassignStorageHandles ();
    WaitForExit ();
    return Status;
  }
  Status = mUsbMsd->StartDevice (mUsbMsd);
  if (EFI_ERROR (Status)) {
    AddLine ("StartDevice: %r", Status);
    UnassignStorageHandles ();
    WaitForExit ();
    return Status;
  }
  AddLine ("Connect USB to the computer. Volume Up: return.");
  LastEventError = EFI_SUCCESS;
  while (!ExitPressed ()) {
    EventStatus = mUsbMsd->EventHandler (mUsbMsd);
    if (EFI_ERROR (EventStatus) && (EventStatus != EFI_NOT_READY) &&
        (EventStatus != LastEventError)) {
      AddLine ("USB event error: %r", EventStatus);
      LastEventError = EventStatus;
    }
    gBS->Stall (100);
  }
  Status = mUsbMsd->StopDevice (mUsbMsd);
  if (EFI_ERROR (Status)) {
    AddLine ("StopDevice: %r", Status);
    WaitForExit ();
    return Status;
  }
  UnassignStorageHandles ();
  if (gST->ConOut != NULL) {
    gST->ConOut->ClearScreen (gST->ConOut);
  }
  return EFI_SUCCESS;
}
