#include <Uefi.h>
#include <IndustryStandard/SmBios.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/Smbios.h>

typedef struct {
  UINT32 Count;
  UINT32 Words[120];
} CPU_REPLY;

STATIC UINT32 mCpuToken;

STATIC BOOLEAN
CpuGet (UINT32 Command, UINT32 Domain, UINT32 Index, CPU_REPLY *Reply)
{
  CONST UINTN Mailbox = 0x17B4E000;
  UINT32 Bytes;
  UINT32 Header;
  UINT32 State;
  UINT32 Length;
  UINT32 Poll;
  UINT32 Word;

  // Read-only SCMI performance queries; this path has no level/limit setters.
  if (Domain > 1) { return FALSE; }
  switch (Command) {
    case 0: case 1: Bytes = 0; break;
    case 8: Bytes = 4; break;
    case 4: if (Index > 15) { return FALSE; } Bytes = 8; break;
    default: return FALSE;
  }
  ZeroMem (Reply, sizeof (*Reply));
  State = MmioRead32 (Mailbox + 4);
  if ((State & 1) == 0 || (State & 2) != 0) { return FALSE; }
  Header = Command | (0x13U << 10) | ((++mCpuToken & 1023U) << 18);
  MmioWrite32 (Mailbox + 4, 0);
  MmioWrite32 (Mailbox + 16, 0);
  MmioWrite32 (Mailbox + 20, 4 + Bytes);
  MmioWrite32 (Mailbox + 24, Header);
  if (Bytes >= 4) { MmioWrite32 (Mailbox + 28, Domain); }
  if (Bytes == 8) { MmioWrite32 (Mailbox + 32, Index); }
  MemoryFence ();
  MmioWrite32 (0x17830104, Header);
  for (Poll = 0; Poll < 1000; Poll++) {
    State = MmioRead32 (Mailbox + 4);
    if ((State & 3) != 0) { break; }
    gBS->Stall (100);
  }
  MemoryFence ();
  Length = MmioRead32 (Mailbox + 20);
  if (Poll == 1000 || (State & 2) != 0 || MmioRead32 (Mailbox + 24) != Header ||
      Length < 8 || Length > 488 || (Length & 3) != 0 || MmioRead32 (Mailbox + 28) != 0) {
    return FALSE;
  }
  Reply->Count = (Length - 8) / 4;
  for (Word = 0; Word < Reply->Count; Word++) {
    Reply->Words[Word] = MmioRead32 (Mailbox + 32 + 4 * Word);
  }
  return TRUE;
}

VOID
HummerUpdateCpuSmbios (EFI_HANDLE ImageHandle)
{
  EFI_SMBIOS_PROTOCOL *Smbios;
  EFI_SMBIOS_HANDLE Handle;
  EFI_SMBIOS_TYPE Type;
  EFI_SMBIOS_TABLE_HEADER *Record;
  SMBIOS_TABLE_TYPE4 *Updated;
  VOID *Original;
  UINT8 *End;
  UINTN Size;
  UINT32 Maximum[2];
  UINT32 Current;
  CPU_REPLY Reply;
  EFI_STATUS Status;

  if (!CpuGet (0, 0, 0, &Reply) || Reply.Count < 1 || Reply.Words[0] != 0x40000 ||
      !CpuGet (1, 0, 0, &Reply) || Reply.Count < 1 || (Reply.Words[0] & 0xFFFF) != 2) {
    return;
  }
  if (!CpuGet (4, 0, 15, &Reply) || Reply.Count < 6 || Reply.Words[5] != 15) { return; }
  Maximum[0] = Reply.Words[4];
  if (!CpuGet (4, 1, 15, &Reply) || Reply.Count < 6 || Reply.Words[5] != 15) { return; }
  Maximum[1] = Reply.Words[4];
  if (Maximum[0] != 3532800 || Maximum[1] != 4320000) { return; }

  // A package-wide maximum is the maximum of the two actual factory tables.
  // CurrentSpeed remains a boot-time observation, separate from that maximum.
  Current = 0;
  if (CpuGet (8, 0, 0, &Reply) && Reply.Count >= 1 && Reply.Words[0] <= 15) {
    UINT32 Index = Reply.Words[0];
    if (CpuGet (4, 0, Index, &Reply) && Reply.Count >= 6 && Reply.Words[5] == Index) {
      Current = Reply.Words[4];
    }
  }
  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios);
  if (EFI_ERROR (Status)) { return; }
  Handle = SMBIOS_HANDLE_PI_RESERVED;
  Type = SMBIOS_TYPE_PROCESSOR_INFORMATION;
  Status = Smbios->GetNext (Smbios, &Handle, &Type, &Record, NULL);
  if (EFI_ERROR (Status) || Record->Length < 24) { return; }
  End = (UINT8 *)Record + Record->Length;
  for (Size = Record->Length; Size < 4096; Size++, End++) {
    if (End[0] == 0 && End[1] == 0) { Size += 2; break; }
  }
  if (Size >= 4096) { return; }
  Original = AllocateCopyPool (Size, Record);
  Updated = AllocateCopyPool (Size, Record);
  if (Original == NULL || Updated == NULL) {
    if (Original != NULL) { FreePool (Original); }
    if (Updated != NULL) { FreePool (Updated); }
    return;
  }
  Updated->MaxSpeed = (UINT16)((MAX (Maximum[0], Maximum[1]) + 500) / 1000);
  Updated->CurrentSpeed = (UINT16)((Current + 500) / 1000);
  // Re-publish through the protocol so the configuration table copy updates.
  Status = Smbios->Remove (Smbios, Handle);
  if (!EFI_ERROR (Status)) {
    Status = Smbios->Add (Smbios, ImageHandle, &Handle, (EFI_SMBIOS_TABLE_HEADER *)Updated);
    if (EFI_ERROR (Status)) {
      Smbios->Add (Smbios, ImageHandle, &Handle, (EFI_SMBIOS_TABLE_HEADER *)Original);
    }
  }
  FreePool (Updated);
  FreePool (Original);
}
