#include <Uefi.h>
#include <IndustryStandard/Acpi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/AcpiSystemDescriptionTable.h>

#define CAMERA_SMMU_BASE 0x15000000
#define CAMERA_STREAM_ID 0x1C00
#define CAMERA_SMR (BIT31 | CAMERA_STREAM_ID)

STATIC
BOOLEAN
CameraIdentityRoute (
  IN UINT32 Control,
  IN UINT32 Id1
  )
{
  UINT32 Bank;
  UINT32 Context;
  UINT32 Type;

  Type = (Control >> 16) & 3;
  if (Type == 1) {
    return TRUE;
  }
  Bank = Control & 0xFF;
  if (Type != 0 || Bank >= (Id1 & 0xFF) || (Id1 & BIT31) != 0) {
    return FALSE;
  }
  Context = (1U << (((Id1 >> 28) & 7) + 1)) * SIZE_4KB;
  if (Context + Bank * SIZE_4KB >= SIZE_1MB) {
    return FALSE;
  }
  return ((MmioRead32 (CAMERA_SMMU_BASE + SIZE_4KB + Bank * 4) >> 16) & 3) == 1 &&
         (MmioRead32 (CAMERA_SMMU_BASE + Context + Bank * SIZE_4KB) & BIT0) == 0;
}

EFI_STATUS
HummerCameraDmaInitialize (VOID)
{
  EFI_ACPI_SDT_PROTOCOL *Sdt;
  EFI_ACPI_SDT_HEADER *Table;
  EFI_ACPI_TABLE_VERSION Version;
  EFI_STATUS Status;
  UINTN Key;
  UINTN Index;
  UINT32 Id0;
  UINT32 Id1;
  UINT32 Count;
  UINT32 Slot;
  UINT32 Smr;
  UINT32 Control;
  UINT32 Free;
  UINT32 Existing;
  UINT32 Identity;

  Status = gBS->LocateProtocol (&gEfiAcpiSdtProtocolGuid, NULL, (VOID **)&Sdt);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  for (Index = 0; !EFI_ERROR (Sdt->GetAcpiTable (Index, &Table, &Version, &Key)); Index++) {
    if (Table->Signature == SIGNATURE_32 ('I', 'O', 'R', 'T')) {
      return EFI_UNSUPPORTED;
    }
  }
  Id0 = MmioRead32 (CAMERA_SMMU_BASE + 0x20);
  Id1 = MmioRead32 (CAMERA_SMMU_BASE + 0x24);
  if (Id0 != 0x4C017E84 || Id1 != 0x60000053 ||
      (MmioRead32 (CAMERA_SMMU_BASE) & BIT0) != 0) {
    return EFI_UNSUPPORTED;
  }
  Count = MIN (Id0 & 0xFF, 128U);
  Free = MAX_UINT32;
  Existing = MAX_UINT32;
  Identity = MAX_UINT32;
  for (Slot = 0; Slot < Count; Slot++) {
    Smr = MmioRead32 (CAMERA_SMMU_BASE + 0x800 + Slot * 4);
    if ((Smr & BIT31) == 0) {
      if (Smr == 0 && Free == MAX_UINT32) {
        Free = Slot;
      }
      continue;
    }
    Control = MmioRead32 (CAMERA_SMMU_BASE + 0xC00 + Slot * 4);
    if (((Smr ^ CAMERA_STREAM_ID) & ~((Smr >> 16) & 0x7FFF) & 0x7FFF) == 0) {
      if (Smr != CAMERA_SMR || Existing != MAX_UINT32 || !CameraIdentityRoute (Control, Id1)) {
        return EFI_ACCESS_DENIED;
      }
      Existing = Slot;
    }
    if (Identity == MAX_UINT32 && CameraIdentityRoute (Control, Id1)) {
      Identity = Control;
    }
  }
  if (Existing != MAX_UINT32) {
    return EFI_SUCCESS;
  }
  if (Free == MAX_UINT32) {
    return EFI_OUT_OF_RESOURCES;
  }
  if (MmioRead32 (CAMERA_SMMU_BASE + 0x800 + Free * 4) != 0) {
    return EFI_NOT_READY;
  }
  Control = Identity == MAX_UINT32 ? 0x10000 : Identity;
  MmioWrite32 (CAMERA_SMMU_BASE + 0xC00 + Free * 4, Control);
  if (MmioRead32 (CAMERA_SMMU_BASE + 0xC00 + Free * 4) != Control) {
    return EFI_DEVICE_ERROR;
  }
  MmioWrite32 (CAMERA_SMMU_BASE + 0x800 + Free * 4, CAMERA_SMR);
  if (MmioRead32 (CAMERA_SMMU_BASE + 0x800 + Free * 4) != CAMERA_SMR ||
      MmioRead32 (CAMERA_SMMU_BASE + 0xC00 + Free * 4) != Control ||
      !CameraIdentityRoute (Control, Id1)) {
    MmioWrite32 (CAMERA_SMMU_BASE + 0x800 + Free * 4, 0);
    return EFI_DEVICE_ERROR;
  }
  DEBUG ((DEBUG_INFO, "Camera DMA stream 0x%x slot %u control 0x%x\n", CAMERA_STREAM_ID, Free, Control));
  return EFI_SUCCESS;
}
