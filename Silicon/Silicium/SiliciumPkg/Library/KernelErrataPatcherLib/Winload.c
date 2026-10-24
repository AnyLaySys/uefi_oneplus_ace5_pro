/**
  Based on https://github.com/SamuelTulach/rainbow

  Copyright (c) 2021 Samuel Tulach
  Copyright (c) 2022-2023 DuoWoA authors
  Copyright (c) 2026 Project Silicium

  SPDX-License-Identifier: MIT
**/

#include <Library/DebugLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/AssemblyUtilsLib.h>
#include <IndustryStandard/PeImage.h>

#include "Winload.h"

//
// Target Patch Locations
//
STATIC EFI_PHYSICAL_ADDRESS mTransferToKernelAddr  = 0;
STATIC EFI_PHYSICAL_ADDRESS mTargetInstructionAddr = 0;
STATIC EFI_PHYSICAL_ADDRESS mWinloadBase = 0;
STATIC UINTN mWinloadLength = 0;

BOOLEAN
IsWinloadMemory (
  IN EFI_PHYSICAL_ADDRESS Base,
  IN UINTN                Length)
{
  // Populate DOS Header
  EFI_IMAGE_DOS_HEADER *DosHeader = (EFI_IMAGE_DOS_HEADER *)Base;

  if (Length < sizeof (*DosHeader) + sizeof (EFI_IMAGE_NT_HEADERS64)) {
    return FALSE;
  }

  // Verify DOS Signature
  if (DosHeader->e_magic != EFI_IMAGE_DOS_SIGNATURE) {
    return FALSE;
  }

  if (DosHeader->e_lfanew < sizeof (*DosHeader) ||
      (UINTN)DosHeader->e_lfanew > Length - sizeof (EFI_IMAGE_NT_HEADERS64)) {
    return FALSE;
  }

  // Populate NT Header
  EFI_IMAGE_NT_HEADERS64 *NtHeader = (EFI_IMAGE_NT_HEADERS64 *)(Base + DosHeader->e_lfanew);

  // Verify NT Signature
  if (NtHeader->Signature != EFI_IMAGE_NT_SIGNATURE) {
    return FALSE;
  }

  if (NtHeader->FileHeader.Machine != IMAGE_FILE_MACHINE_ARM64) {
    return FALSE;
  }

  // Go thru each Codebase Semester
  for (UINT8 i = 0; i < ARRAY_SIZE (CodebaseSemester); i++) {
    // Get "TransferToKernel" Function Offset
    UINT16 TransferToKernelOffset = CodebaseSemester[i].TransferToKernelOffset;

    if (TransferToKernelOffset > Length || Length - TransferToKernelOffset < 8) {
      continue;
    }

    // Save TransferToKernel Function Location
    mTransferToKernelAddr = Base + TransferToKernelOffset;

    // Go thru TransferToKernel Memory Area
    for (EFI_PHYSICAL_ADDRESS Current = mTransferToKernelAddr; Current <= Base + Length - 8; Current += ARM64_INSTRUCTION_LENGTH) {
      // Verify Branch Instruction
      if (ARM64_INSTRUCTION (Current) != ARM64_BRANCH_LOCATION_INSTRUCTION (Current, mTransferToKernelAddr)) {
        continue;
      }

      // Verify Next Instruction
      if (ARM64_INSTRUCTION (Current + ARM64_TOTAL_INSTRUCTION_LENGTH (1)) != CodebaseSemester[i].TargetInstruction) {
        continue;
      }

      // Show Winload Details
      DEBUG ((EFI_D_WARN, "[KEP] Winload Memory Range      = 0x%p - 0x%llx\n", Base, Length));
      DEBUG ((EFI_D_WARN, "[KEP] Winload Codebase Semester = %a\n", CodebaseSemester[i].Name));

      // Save Target Patch Instruction
      mTargetInstructionAddr = Current;
      mWinloadBase = Base;
      mWinloadLength = Length;

      return TRUE;
    }
  }

  return FALSE;
}

EFI_STATUS
InstallTransferToKernelPatch (
  IN UINT8 *ShellCode,
  IN UINTN ShellCodeSize
  )
{
  EFI_PHYSICAL_ADDRESS Address = 0;
  EFI_IMAGE_DOS_HEADER *Dos = (VOID *)(UINTN)mWinloadBase;
  EFI_IMAGE_NT_HEADERS64 *Nt = (VOID *)(UINTN)(mWinloadBase + Dos->e_lfanew);
  EFI_IMAGE_SECTION_HEADER *Section = (VOID *)((UINT8 *)&Nt->OptionalHeader +
    Nt->FileHeader.SizeOfOptionalHeader);
  UINTN Offset;
  UINTN End;
  UINTN Index;

  for (Index = 0; Index < Nt->FileHeader.NumberOfSections; Index++, Section++) {
    Offset = ALIGN_VALUE (Section->Misc.VirtualSize, ARM64_INSTRUCTION_LENGTH);
    End = Section->SizeOfRawData;
    if ((Section->Characteristics & EFI_IMAGE_SCN_MEM_EXECUTE) == 0 ||
        Offset > End || ShellCodeSize > End - Offset ||
        Section->VirtualAddress > mWinloadLength ||
        End > mWinloadLength - Section->VirtualAddress) {
      continue;
    }
    Address = mWinloadBase + Section->VirtualAddress + Offset;
    if (IsZeroBuffer ((VOID *)(UINTN)Address, ShellCodeSize)) {
      break;
    }
    Address = 0;
  }
  if (Address == 0) {
    return EFI_NOT_FOUND;
  }
  CopyMem ((VOID *)(UINTN)Address, ShellCode, ShellCodeSize);
  WriteUnaligned64 ((UINT64 *)(UINTN)(Address + ShellCodeSize - sizeof (UINT64)),
    mTransferToKernelAddr);
  ARM64_INSTRUCTION (mTargetInstructionAddr) =
    ARM64_BRANCH_LOCATION_INSTRUCTION (mTargetInstructionAddr, Address);
  WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)Address, ShellCodeSize);
  InvalidateInstructionCacheRange ((VOID *)(UINTN)Address, ShellCodeSize);
  WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)mTargetInstructionAddr,
    ARM64_INSTRUCTION_LENGTH);
  InvalidateInstructionCacheRange ((VOID *)(UINTN)mTargetInstructionAddr,
    ARM64_INSTRUCTION_LENGTH);
  return EFI_SUCCESS;
}

VOID
PatchTransferToKernel (
  IN EFI_PHYSICAL_ADDRESS  Base,
  IN UINT64                Length,
  IN UINT8                *ShellCode,
  IN UINTN                 ShellCodeSize)
{
  // Set New TarnsferToKernel Address
  EFI_PHYSICAL_ADDRESS NewTransferToKernelAddr = mTransferToKernelAddr - ShellCodeSize;

  // Inject Jump Instruction
  ARM64_INSTRUCTION (mTargetInstructionAddr) = ARM64_BRANCH_LOCATION_INSTRUCTION (mTargetInstructionAddr, NewTransferToKernelAddr);

  // Copy Shell Code
  CopyMem ((VOID *)NewTransferToKernelAddr, (CONST VOID *)ShellCode, ShellCodeSize);

  // Flush Cache
  WriteBackInvalidateDataCacheRange ((VOID *)NewTransferToKernelAddr, ShellCodeSize);
  InvalidateInstructionCacheRange   ((VOID *)NewTransferToKernelAddr, ShellCodeSize);
  WriteBackInvalidateDataCacheRange ((VOID *)mTargetInstructionAddr,  ARM64_INSTRUCTION_LENGTH);
  InvalidateInstructionCacheRange   ((VOID *)mTargetInstructionAddr,  ARM64_INSTRUCTION_LENGTH);

  // Show Winload Details
  DEBUG ((EFI_D_WARN, "[KEP] Patched Branch Instruction at 0x%p\n", mTargetInstructionAddr));
  DEBUG ((EFI_D_WARN, "[KEP] Injected Shell Code at 0x%p\n",        NewTransferToKernelAddr));
}
