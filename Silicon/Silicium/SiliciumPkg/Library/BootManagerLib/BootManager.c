/**
  Copyright (C) DuoWoA authors. All rights reserved.
  Copyright (C) Microsoft Corporation. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/DebugLib.h>
#include <Library/AcpiTableUpdateLib.h>
#include <Library/DeviceBootManagerLib.h>
#include <Library/PlatformBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/SecureBootHandlerLib.h>
#include <Library/MsBootOptionsLib.h>

#include <Configuration/BootDevices.h>

#include <Protocol/SimpleTextInEx.h>

STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *mKeypad;
STATIC VOID *mKeyNotify;
STATIC BOOLEAN mBootMenuRequested;

STATIC EFI_STATUS EFIAPI
BootMenuNotify (IN EFI_KEY_DATA *KeyData)
{
  mBootMenuRequested = TRUE;
  return EFI_SUCCESS;
}

VOID
EFIAPI
PlatformBootManagerBeforeConsole ()
{
  // Signal End Of DXE
  EfiEventGroupSignal (&gEfiEndOfDxeEventGroupGuid);

  // Dispatch Deferred Images
  EfiBootManagerDispatchDeferredImages ();

  // Register Boot Options
  MsBootOptionsLibRegisterDefaultBootOptions ();

  // Register Console Variables
  EfiBootManagerUpdateConsoleVariable (ConIn,  (EFI_DEVICE_PATH_PROTOCOL *)&KeypadDevicePath,  NULL);
  EfiBootManagerUpdateConsoleVariable (ConOut, (EFI_DEVICE_PATH_PROTOCOL *)&DisplayDevicePath, NULL);
  EfiBootManagerUpdateConsoleVariable (ErrOut, (EFI_DEVICE_PATH_PROTOCOL *)&DisplayDevicePath, NULL);

  // Execute Secondary Before Console
  DeviceBootManagerBeforeConsole (NULL, NULL);
}

VOID
EFIAPI
PlatformBootManagerAfterConsole ()
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_KEY_DATA Key = {0};

  if (gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid,
        (VOID **)&Gop) == EFI_UNSUPPORTED &&
      !EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop))) {
    gBS->InstallProtocolInterface (&gST->ConsoleOutHandle,
      &gEfiGraphicsOutputProtocolGuid, EFI_NATIVE_INTERFACE, Gop);
  }
  if (!EFI_ERROR (gBS->HandleProtocol (gST->ConsoleInHandle,
        &gEfiSimpleTextInputExProtocolGuid, (VOID **)&mKeypad))) {
    Key.Key.ScanCode = SCAN_DOWN;
    mKeypad->RegisterKeyNotify (mKeypad, &Key, BootMenuNotify, &mKeyNotify);
  }
  DeviceBootManagerAfterConsole ();
}

VOID
EFIAPI
PlatformBootManagerOnDemandConInConnect ()
{
  // Execute Secondary On Demand
  DeviceBootManagerOnDemandConInConnect ();
}

VOID
EFIAPI
PlatformBootManagerProcessBootCompletion (OUT EFI_BOOT_MANAGER_LOAD_OPTION *BootOption)
{
  return;
}

VOID
EFIAPI
PlatformBootManagerPriorityBoot (IN OUT UINT16 **BootNext)
{
  EFI_KEY_DATA Key;

  if (mKeypad != NULL) {
    if (!EFI_ERROR (mKeypad->ReadKeyStrokeEx (mKeypad, &Key)) && Key.Key.ScanCode == SCAN_DOWN) {
      mBootMenuRequested = TRUE;
    }
    if (mKeyNotify != NULL) {
      mKeypad->UnregisterKeyNotify (mKeypad, mKeyNotify);
    }
  }
  if (mBootMenuRequested) {
    PlatformBootManagerUnableToBoot ();
  }
}

VOID
EFIAPI
PlatformBootManagerWaitCallback (IN UINT16 TimeoutRemain)
{
}

VOID
EFIAPI
PlatformBootManagerUnableToBoot ()
{
  EFI_STATUS                   Status;
  EFI_BOOT_MANAGER_LOAD_OPTION LoadOption;

  Status = MsBootOptionsLibGetBootManagerMenu (&LoadOption, NULL);
  if (EFI_ERROR (Status)) {
    return;
  }

  EfiBootManagerBoot (&LoadOption);
  EfiBootManagerFreeLoadOption (&LoadOption);
}

VOID
EFIAPI
PlatformBootManagerBdsEntry ()
{
  EFI_STATUS Status;

  // Setup Secure Boot
  Status = SetupSecureBoot ();
  ASSERT_EFI_ERROR (Status);

  // Update ACPI Tables
  UpdateAcpiTables ();

  // Execute Secondary BDS Entry
  DeviceBootManagerBdsEntry ();
}
