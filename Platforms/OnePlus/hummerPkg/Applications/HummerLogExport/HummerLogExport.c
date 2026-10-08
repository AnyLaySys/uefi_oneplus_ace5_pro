#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/EFIUsbMsd.h>

STATIC EFI_USB_MSD_PROTOCOL *mUsbMsd;
STATIC UINT8 mAssignedLuns;

STATIC VOID
AddLine (IN CONST CHAR8 *Format, ...)
{
  VA_LIST Args;
  CHAR8 Line[192];
  UINTN Length;

  VA_START (Args, Format);
  Length = AsciiVSPrint (Line, sizeof (Line) - 2, Format, Args);
  VA_END (Args);
  Line[Length++] = '\r';
  Line[Length++] = '\n';
  SerialPortWrite ((UINT8 *)Line, Length);
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

#include "LogDisk.h"

STATIC VOID
UnassignStorageHandles (VOID)
{
  UINT8 Lun;

  for (Lun = 0; Lun < mAssignedLuns; Lun++) {
    mUsbMsd->AssignBlkIoHandle (mUsbMsd, NULL, Lun);
  }
  mAssignedLuns = 0;
  ReleaseLogDisk ();
}

EFI_STATUS EFIAPI
HummerLogExportEntry (
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
  if (gST->ConIn != NULL) {
    while (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
    }
  }
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
  AddLine ("Linux logs ready. Connect USB; Volume Up: return.");
  LastEventError = EFI_SUCCESS;
  while (!ExitPressed ()) {
    EventStatus = mUsbMsd->EventHandler (mUsbMsd);
    if (EFI_ERROR (EventStatus) && (EventStatus != EFI_NOT_READY)) {
      if (EventStatus != LastEventError) {
        AddLine ("USB event error: %r", EventStatus);
        LastEventError = EventStatus;
      }
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
  return EFI_SUCCESS;
}
