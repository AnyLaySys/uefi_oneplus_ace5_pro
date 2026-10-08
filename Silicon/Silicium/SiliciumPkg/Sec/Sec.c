/**
  Copyright (c) 2011-2017, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/PcdLib.h>
#include <Library/DebugLib.h>
#include <Library/PrePiLib.h>
#include <Library/DebugAgentLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/SerialPortLib.h>
#include <Library/PlatformSecLib.h>
#include <Library/PrePiHobListPointerLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/TimerLib.h>
#include <Library/HobLib.h>
#include <Library/FdtLib.h>

#include "Sec.h"
#include <LinuxPayload.h>

STATIC EFI_GUID mHummerLinuxPayloadHobGuid = HUMMER_LINUX_PAYLOAD_HOB_GUID;
STATIC HUMMER_LINUX_PAYLOAD_CAPTURE mLinuxPayloadCapture;

STATIC
BOOLEAN
RangesOverlap (
  IN EFI_PHYSICAL_ADDRESS Base1,
  IN UINT64               Length1,
  IN EFI_PHYSICAL_ADDRESS Base2,
  IN UINT64               Length2
  )
{
  EFI_PHYSICAL_ADDRESS End1;
  EFI_PHYSICAL_ADDRESS End2;

  if ((Length1 == 0) || (Length2 == 0) ||
      (Base1 > MAX_UINT64 - Length1) || (Base2 > MAX_UINT64 - Length2)) {
    return TRUE;
  }
  End1 = Base1 + Length1;
  End2 = Base2 + Length2;
  return (Base1 < End2) && (Base2 < End1);
}

STATIC
EFI_STATUS
AcceptLinuxPayload (
  IN EFI_PHYSICAL_ADDRESS SourceStart,
  IN UINT64 SourceSize
  )
{
  EFI_MEMORY_REGION_DESCRIPTOR Region;
  CONST HUMMER_LINUX_PAYLOAD_HEADER *Header;

  if ((SourceStart > MAX_UINT64 - SourceSize) || (SourceSize == 0) || (SourceSize > 0x40000000ULL)) {
    return EFI_INVALID_PARAMETER;
  }
  LocateMemoryRegionByName ("UEFI_FD", &Region);
  if (RangesOverlap (SourceStart, SourceSize, Region.Address, Region.Length)) {
    return EFI_ACCESS_DENIED;
  }
  LocateMemoryRegionByName ("UEFI_Stack", &Region);
  if (RangesOverlap (SourceStart, SourceSize, Region.Address, Region.Length)) {
    return EFI_ACCESS_DENIED;
  }
  LocateMemoryRegionByName ("DXE_Heap", &Region);
  if (RangesOverlap (SourceStart, SourceSize, Region.Address, Region.Length)) {
    return EFI_ACCESS_DENIED;
  }
  Header = (CONST HUMMER_LINUX_PAYLOAD_HEADER *)(UINTN)SourceStart;
  if (!HummerLinuxPayloadHeaderValid (Header, SourceSize)) {
    return EFI_INVALID_PARAMETER;
  }
  CopyMem (&mLinuxPayloadCapture.Header, Header, sizeof (mLinuxPayloadCapture.Header));
  mLinuxPayloadCapture.SourceStart = SourceStart;
  mLinuxPayloadCapture.SourceSize = SourceSize;
  mLinuxPayloadCapture.Valid = TRUE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
CaptureLinuxPayload (
  IN UINTN BootFdt,
  IN UINTN BootPayload
  )
{
  INT32 Chosen;
  INT32 Length;
  CONST VOID *Property;
  UINT64 StartBe;
  UINT64 EndBe;
  EFI_PHYSICAL_ADDRESS Start;
  EFI_PHYSICAL_ADDRESS End;
  EFI_STATUS Status;
  UINT64 FdtSize;
  EFI_PHYSICAL_ADDRESS Tail;
  CONST HUMMER_LINUX_PAYLOAD_HEADER *TailHeader;

  ZeroMem (&mLinuxPayloadCapture, sizeof (mLinuxPayloadCapture));
  if (BootPayload != 0) {
    TailHeader = (CONST HUMMER_LINUX_PAYLOAD_HEADER *)(UINTN)BootPayload;
    if (HummerLinuxPayloadHeaderValid (TailHeader, TailHeader->TotalSize)) {
      Status = AcceptLinuxPayload (BootPayload, TailHeader->TotalSize);
      if (!EFI_ERROR (Status)) {
        return Status;
      }
    }
  }
  if (BootFdt == 0 || FdtCheckHeader ((CONST VOID *)(UINTN)BootFdt) != 0) {
    return EFI_NOT_FOUND;
  }
  Chosen = FdtPathOffset ((CONST VOID *)(UINTN)BootFdt, "/chosen");
  if (Chosen >= 0) {
    Property = FdtGetProp ((CONST VOID *)(UINTN)BootFdt, Chosen, "linux,initrd-start", &Length);
    if ((Property != NULL) && (Length == sizeof (UINT64))) {
      CopyMem (&StartBe, Property, sizeof (StartBe));
      Property = FdtGetProp ((CONST VOID *)(UINTN)BootFdt, Chosen, "linux,initrd-end", &Length);
      if ((Property != NULL) && (Length == sizeof (UINT64))) {
        CopyMem (&EndBe, Property, sizeof (EndBe));
        Start = Fdt64ToCpu (StartBe);
        End = Fdt64ToCpu (EndBe);
        if (End > Start) {
          Status = AcceptLinuxPayload (Start, End - Start);
          if (!EFI_ERROR (Status)) {
            return Status;
          }
        }
      }
    }
  }
  FdtSize = FdtTotalSize ((CONST VOID *)(UINTN)BootFdt);
  if ((FdtSize > MAX_UINT64 - BootFdt) || (FdtSize > 0x1000000ULL)) {
    return EFI_NOT_FOUND;
  }
  Tail = BootFdt + FdtSize;
  TailHeader = (CONST HUMMER_LINUX_PAYLOAD_HEADER *)(UINTN)Tail;
  if (!HummerLinuxPayloadHeaderValid (TailHeader, TailHeader->TotalSize)) {
    return EFI_NOT_FOUND;
  }
  return AcceptLinuxPayload (Tail, TailHeader->TotalSize);
}

#ifndef MDEPKG_NDEBUG
STATIC
VOID
ClearFrameBuffer (VOID)
{
  EFI_MEMORY_REGION_DESCRIPTOR FrameBufferRegion;

  // Locate "Display Reserved" Memory Region
  LocateMemoryRegionByName ("Display Reserved", &FrameBufferRegion);
  LocateMemoryRegionByName ("Display_Reserved", &FrameBufferRegion);

  // Clear Memory Region
  if (FrameBufferRegion.Address != 0 && FrameBufferRegion.Length != 0) {
    ZeroMem ((VOID *)FrameBufferRegion.Address, FrameBufferRegion.Length);
  }
}

STATIC
VOID
PrintFirmwareVersion (VOID)
{
  // Clear Frame Buffer
  ClearFrameBuffer ();

  // Print UEFI Version Message
  DEBUG ((EFI_D_WARN, "\n"));
  DEBUG ((EFI_D_WARN, "Project Silicium for %a %a\n",            FixedPcdGetPtr (PcdSmbiosSystemManufacturer), FixedPcdGetPtr (PcdSmbiosSystemModel)));
  DEBUG ((EFI_D_WARN, "Firmware Version %s Build at %a on %a\n", PcdGetPtr (PcdFirmwareVersionString), __TIME__, __DATE__));
  DEBUG ((EFI_D_WARN, "\n"));
}
#endif

STATIC
EFI_STATUS
InitializeMemory (VOID)
{
  EFI_STATUS                    Status;
  EFI_MEMORY_REGION_DESCRIPTOR  UefiMemoryRegion;
  EFI_HOB_HANDOFF_INFO_TABLE   *HobList;

  // Locate "DXE Heap" Memory Region
  LocateMemoryRegionByName ("DXE Heap", &UefiMemoryRegion);
  LocateMemoryRegionByName ("DXE_Heap", &UefiMemoryRegion);

  // Save UEFI Memory Details
  UINTN UefiMemoryBase = UefiMemoryRegion.Address;
  UINTN UefiMemorySize = UefiMemoryRegion.Length;

  // Verify UEFI Memory Base
  if (UefiMemoryBase == 0 && UefiMemorySize == 0) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate \"DXE Heap\" Memory Region!\n"));
    return EFI_NOT_FOUND;
  }

  // Initialize HOB List
  HobList = HobConstructor ((VOID *)UefiMemoryBase, UefiMemorySize, (VOID *)UefiMemoryBase, (VOID *)(UefiMemoryBase + UefiMemorySize));
  PrePeiSetHobList (HobList);

  // Initialize MMU & Memory HOBs
  Status = MemoryPeim (UefiMemoryBase, UefiMemorySize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (mLinuxPayloadCapture.Valid) {
    EFI_PHYSICAL_ADDRESS AllocationBase;
    EFI_PHYSICAL_ADDRESS AllocationEnd;
    HUMMER_LINUX_PAYLOAD_HOB PayloadHob;

    AllocationBase = mLinuxPayloadCapture.SourceStart & ~(EFI_PAGE_SIZE - 1);
    AllocationEnd = (mLinuxPayloadCapture.SourceStart + mLinuxPayloadCapture.SourceSize + EFI_PAGE_SIZE - 1) & ~(EFI_PAGE_SIZE - 1);
    BuildMemoryAllocationHob (AllocationBase, AllocationEnd - AllocationBase, EfiReservedMemoryType);
    ZeroMem (&PayloadHob, sizeof (PayloadHob));
    PayloadHob.SourceStart = mLinuxPayloadCapture.SourceStart;
    PayloadHob.SourceSize = mLinuxPayloadCapture.SourceSize;
    PayloadHob.ImageOffset = mLinuxPayloadCapture.Header.ImageOffset;
    PayloadHob.ImageSize = mLinuxPayloadCapture.Header.ImageSize;
    PayloadHob.DtbOffset = mLinuxPayloadCapture.Header.DtbOffset;
    PayloadHob.DtbSize = mLinuxPayloadCapture.Header.DtbSize;
    PayloadHob.InitrdOffset = mLinuxPayloadCapture.Header.InitrdOffset;
    PayloadHob.InitrdSize = mLinuxPayloadCapture.Header.InitrdSize;
    CopyMem (PayloadHob.BuildId, mLinuxPayloadCapture.Header.BuildId, sizeof (PayloadHob.BuildId));
    BuildGuidDataHob (&mHummerLinuxPayloadHobGuid, &PayloadHob, sizeof (PayloadHob));
  }

  // Build Stack HOB
  BuildStackHob (FixedPcdGet64 (PcdCPUCoresStackBase), FixedPcdGet32 (PcdCPUCorePrimaryStackSize));

  return EFI_SUCCESS;
}

STATIC
VOID
SecMain (
  IN UINTN BootFdt,
  IN UINTN BootPayload
  )
{
  EFI_STATUS Status;

  CaptureLinuxPayload (BootFdt, BootPayload);

  // Do Platform Specific Initialization
  PlatformInitialize ();

#ifndef MDEPKG_NDEBUG
  // Initialize Serial Port
  Status = SerialPortInitialize ();
  if (EFI_ERROR (Status)) {
    return;
  }

  // Print Firmware Version
  PrintFirmwareVersion ();

  // Initialize Debug Agent
  InitializeDebugAgent (DEBUG_AGENT_INIT_POSTMEM_SEC, NULL, NULL);
  SaveAndSetDebugTimerInterrupt (TRUE);
#endif

  // Initialize Memory
  Status = InitializeMemory ();
  if (EFI_ERROR (Status)) {
    return;
  }

  // Build CPU HOB
  BuildCpuHob (ArmGetPhysicalAddressBits (), PcdGet8 (PcdPrePiCpuIoSize));

  // Set the Boot Mode
  SetBootMode (BOOT_WITH_DEFAULT_SETTINGS);

  // Initialize Platform HOBs
  Status = PlatformPeim ();
  if (EFI_ERROR (Status)) {
    return;
  }

  // Run Library Cconstructors
  ProcessLibraryConstructorList ();

  // Decompress all FVs
  Status = DecompressFirstFv ();
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Decompress FV! Status = %r\n", Status));
    return;
  }

  // Load DXE Core
  Status = LoadDxeCoreFromFv (NULL, 0);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Load DXE Core! Status = %r\n", Status));
  }
}

VOID
SecEntry (
  IN UINTN BootFdt,
  IN UINTN BootPayload
  )
{
  // Disable Data Cache
  ArmDisableDataCache ();

  // Invalidate Instruction Cache
  ArmInvalidateInstructionCache ();

  // Enable Instruction Cache
  ArmEnableInstructionCache ();

  // Invalidate Stack & UEFI FD D-Cache
  InvalidateDataCacheRange ((VOID *)FixedPcdGet64 (PcdCPUCoresStackBase), FixedPcdGet32 (PcdCPUCorePrimaryStackSize));
  InvalidateDataCacheRange ((VOID *)FixedPcdGet64 (PcdFdBaseAddress),     FixedPcdGet32 (PcdFdSize));

  // Verify Exception Vector Table
  ASSERT (((UINTN)SecVectorTable & ARM_VECTOR_TABLE_ALIGNMENT) == 0);

  // Enable new Exception Vector Table
  ArmWriteVBar ((UINTN)SecVectorTable);

  // Enter Main SEC Function
  SecMain (BootFdt, BootPayload);
}
