/**
  Copyright (C) Microsoft Corporation. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DevicePathLib.h>

#include <Protocol/SimpleTextInEx.h>

#include "BootManager.h"

//
// Global Variables
//
STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *mSimpleInExProtocol  = NULL;
STATIC VOID                              *mNotifyHandle        = NULL;

EFI_STATUS
EFIAPI
PowerOffNotify (IN EFI_KEY_DATA *KeyData)
{
  gRT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL);

  CpuDeadLoop ();

  return EFI_SUCCESS;
}

EFI_STATUS
SetupKeypad (IN EFI_DEVICE_PATH_PROTOCOL *DevicePath)
{
  EFI_STATUS  Status;
  EFI_HANDLE  StiHandle;
  EFI_HANDLE *KeypadBuffer;
  UINTN       KeypadCount;

  // Locate Keypad Controller Protocols
  Status = gBS->LocateHandleBuffer (ByProtocol, &gKeypadDeviceProtocolGuid, NULL, &KeypadCount, &KeypadBuffer);
  if (!EFI_ERROR (Status)) {
    // Start all Keypad Controllers
    for (UINTN i = 0; i < KeypadCount; i++) {
      gBS->ConnectController (KeypadBuffer[i], NULL, NULL, TRUE);
    }

    // Free Buffer
    FreePool (KeypadBuffer);
  }

  // Locate STI Protocol Handle
  Status = gBS->LocateDevicePath (&gEfiSimpleTextInputExProtocolGuid, &DevicePath, &StiHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Locate STI Device Path! Status = %r\n", __FUNCTION__, Status));
    return Status;
  }

  // Get STI Protocol from Handle
  Status = gBS->HandleProtocol (StiHandle, &gEfiSimpleTextInputExProtocolGuid, (VOID *)&mSimpleInExProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to get STI Protocol from Handle! Status = %r\n", __FUNCTION__, Status));
    return Status;
  }

  // Reset STI Device
  Status = mSimpleInExProtocol->Reset (mSimpleInExProtocol, TRUE);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Reset STI Device! Status = %r\n", __FUNCTION__, Status));
  }

  return EFI_SUCCESS;
}

EFI_STATUS
RegisterKeyNotify ()
{
  EFI_STATUS   Status;
  EFI_KEY_DATA KeyData;

  // Set Key Data
  KeyData.KeyState.KeyToggleState = 0;
  KeyData.KeyState.KeyShiftState  = 0;
  KeyData.Key.UnicodeChar         = CHAR_NULL;
  KeyData.Key.ScanCode            = SCAN_DOWN;

  // Register Volume Down Key Notify
  Status = mSimpleInExProtocol->RegisterKeyNotify (mSimpleInExProtocol, &KeyData, &PowerOffNotify, &mNotifyHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Register Volume Down Key Notify! Status = %r\n", __FUNCTION__, Status));
    return Status;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
RegisterKeyCallback (IN EFI_DEVICE_PATH_PROTOCOL *DevicePath)
{
  EFI_STATUS Status;

  // Setup Keypad
  Status = SetupKeypad (DevicePath);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Register Key Notify
  Status = RegisterKeyNotify ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}

VOID
UnregisterKeyCallback ()
{
  EFI_STATUS Status;

  // Verify STI Protocol Presense & Key Notify
  if (mSimpleInExProtocol == NULL || mNotifyHandle == NULL) {
    return;
  }
  // Unregister Volume Down Key Notify
  Status = mSimpleInExProtocol->UnregisterKeyNotify (mSimpleInExProtocol, mNotifyHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Unregister Volume Down Key Notify! Status = %r\n", __FUNCTION__, Status));
  }
}
