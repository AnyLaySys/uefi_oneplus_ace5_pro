#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Guid/FileInfo.h>
#include "Registry.h"

#define REG_HEADER_SIZE  512
#define REG_HBINS_BASE   4096
#define REG_MAX_SIZE     SIZE_128MB
#define REG_PATH_SIZE    512
#define REG_NIL          MAX_UINT32

typedef struct {
  EFI_FILE_PROTOCOL *File;
  UINTN             Size;
  UINT8             Header[REG_HEADER_SIZE];
  UINT8             *Data;
  UINT32            Length;
  UINT32            Root;
} REG_HIVE;

STATIC UINT16 R16 (CONST VOID *Data) { return ReadUnaligned16 ((CONST UINT16 *)Data); }
STATIC UINT32 R32 (CONST VOID *Data) { return ReadUnaligned32 ((CONST UINT32 *)Data); }
STATIC UINT64 R64 (CONST VOID *Data) { return ReadUnaligned64 ((CONST UINT64 *)Data); }

STATIC EFI_STATUS
RegRead (REG_HIVE *Hive, UINTN Offset, UINTN Size, VOID *Data)
{
  EFI_STATUS Status;
  UINTN Read = Size;

  if (Offset > Hive->Size || Size > Hive->Size - Offset) {
    return EFI_COMPROMISED_DATA;
  }
  if (Hive->Data != NULL) {
    CopyMem (Data, Hive->Data + Offset, Size);
    return EFI_SUCCESS;
  }
  Status = Hive->File->SetPosition (Hive->File, Offset);
  if (!EFI_ERROR (Status)) {
    Status = Hive->File->Read (Hive->File, &Read, Data);
    if (!EFI_ERROR (Status) && Read != Size) {
      Status = EFI_COMPROMISED_DATA;
    }
  }
  return Status;
}

STATIC EFI_STATUS
RegOpen (EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Path, REG_HIVE *Hive)
{
  EFI_FILE_INFO *Info = NULL;
  UINTN Size = 0;
  EFI_STATUS Status;

  ZeroMem (Hive, sizeof (*Hive));
  Status = Root->Open (Root, &Hive->File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = Hive->File->GetInfo (Hive->File, &gEfiFileInfoGuid, &Size, NULL);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Info = AllocatePool (Size);
    Status = Info == NULL ? EFI_OUT_OF_RESOURCES : Hive->File->GetInfo (Hive->File, &gEfiFileInfoGuid, &Size, Info);
  }
  if (!EFI_ERROR (Status)) {
    if (Info->FileSize < REG_HEADER_SIZE || Info->FileSize > REG_MAX_SIZE || (Info->Attribute & EFI_FILE_DIRECTORY)) {
      Status = EFI_COMPROMISED_DATA;
    } else {
      Hive->Size = (UINTN)Info->FileSize;
      Status = RegRead (Hive, 0, sizeof (Hive->Header), Hive->Header);
    }
  }
  if (Info != NULL) {
    FreePool (Info);
  }
  if (EFI_ERROR (Status)) {
    Hive->File->Close (Hive->File);
    Hive->File = NULL;
  }
  return Status;
}

STATIC VOID
RegClose (REG_HIVE *Hive)
{
  if (Hive->File != NULL) {
    Hive->File->Close (Hive->File);
  }
  if (Hive->Data != NULL) {
    FreePool (Hive->Data);
  }
}

STATIC BOOLEAN
RegHeaderValid (CONST UINT8 *Header, UINT32 Type)
{
  UINT32 Checksum = 0;

  for (UINTN Offset = 0; Offset < 508; Offset += 4) {
    Checksum ^= R32 (Header + Offset);
  }
  if (Checksum == 0) { Checksum = 1; }
  if (Checksum == MAX_UINT32) { Checksum--; }
  return R32 (Header) == SIGNATURE_32 ('r', 'e', 'g', 'f') &&
         R32 (Header + 20) == 1 && R32 (Header + 24) >= 3 && R32 (Header + 24) <= 6 &&
         R32 (Header + 28) == Type && R32 (Header + 32) == 1 && R32 (Header + 44) == 1 &&
         R32 (Header + 40) >= REG_HBINS_BASE && (R32 (Header + 40) & (REG_HBINS_BASE - 1)) == 0 &&
         R32 (Header + 40) <= REG_MAX_SIZE - REG_HBINS_BASE &&
         R32 (Header + 36) < R32 (Header + 40) && Checksum == R32 (Header + 508);
}

STATIC EFI_STATUS
RegLoad (REG_HIVE *Hive)
{
  UINT8 *Data = AllocatePool (Hive->Size);
  EFI_STATUS Status;
  if (Data == NULL) { return EFI_OUT_OF_RESOURCES; }
  Status = RegRead (Hive, 0, Hive->Size, Data);
  if (EFI_ERROR (Status)) { FreePool (Data); }
  else { Hive->Data = Data; }
  return Status;
}

// HvLE hashes cover multiples of four bytes and use the registry's fixed seed.
STATIC UINT64
RegHash (CONST UINT8 *Data, UINTN Size)
{
  UINT32 Low = 0x7A4E55C5;
  UINT32 High = 0x82EF4D88;
  UINT32 Value;

  for (UINTN Offset = 0; Offset <= Size + 4; Offset += 4) {
    Value = Offset < Size ? R32 (Data + Offset) : Offset == Size ? 0x80 : 0;
    Low += Value;
    High ^= Low;
    Low = (Low << 20 | Low >> 12) + High;
    High = (High << 9 | High >> 23) ^ Low;
    Low = (Low << 27 | Low >> 5) + High;
    High = High << 19 | High >> 13;
  }
  return (UINT64)High << 32 | Low;
}

STATIC BOOLEAN
RegBefore (UINT32 Left, UINT32 Right)
{
  return (INT32)(Left - Right) < 0;
}

STATIC EFI_STATUS
RegReplay (REG_HIVE *Hive, REG_HIVE *Log, UINT32 *Sequence, BOOLEAN *Applied)
{
  UINT32 Expected = R32 (Log->Header + 4);
  UINTN EntrySize;
  UINTN Pages;
  UINTN Cursor;
  UINT32 Length;
  UINT32 Offset;
  UINT32 Size;
  UINT8 *Entry;
  UINT8 *Data;
  EFI_STATUS Status;

  if ((*Applied && Expected != *Sequence + 1) || (!*Applied && RegBefore (Expected, *Sequence))) {
    return EFI_NOT_FOUND;
  }
  for (UINTN Position = REG_HEADER_SIZE; Log->Size - Position >= 40; Position += EntrySize) {
    Entry = Log->Data + Position;
    EntrySize = R32 (Entry + 4);
    if (R32 (Entry) != SIGNATURE_32 ('H', 'v', 'L', 'E') || EntrySize < 512 || (EntrySize & 511) != 0 ||
        EntrySize > Log->Size - Position || R32 (Entry + 12) != Expected ||
        RegHash (Entry, 32) != R64 (Entry + 32) || RegHash (Entry + 40, EntrySize - 40) != R64 (Entry + 24)) {
      break; // A partially written tail is not a committed transaction.
    }
    Length = R32 (Entry + 16);
    Pages = R32 (Entry + 20);
    if (Length < REG_HBINS_BASE || (Length & (REG_HBINS_BASE - 1)) != 0 ||
        Length > REG_MAX_SIZE - REG_HBINS_BASE || Pages == 0 || Pages > (EntrySize - 40) / 8) {
      return EFI_COMPROMISED_DATA;
    }
    Cursor = 40 + Pages * 8;
    for (UINTN Page = 0; Page < Pages; Page++) {
      Offset = R32 (Entry + 40 + Page * 8);
      Size = R32 (Entry + 44 + Page * 8);
      if (Offset > Length || Size == 0 || Size > Length - Offset || Size > EntrySize - Cursor) {
        return EFI_COMPROMISED_DATA;
      }
      Cursor += Size;
    }
    if (Hive->Data == NULL) {
      Status = RegLoad (Hive);
      if (EFI_ERROR (Status)) { return Status; }
    }
    if ((UINTN)Length + REG_HBINS_BASE > Hive->Size) {
      Data = ReallocatePool (Hive->Size, (UINTN)Length + REG_HBINS_BASE, Hive->Data);
      if (Data == NULL) { return EFI_OUT_OF_RESOURCES; }
      Hive->Data = Data;
      Hive->Size = (UINTN)Length + REG_HBINS_BASE;
    }
    Cursor = 40 + Pages * 8;
    for (UINTN Page = 0; Page < Pages; Page++) {
      Offset = R32 (Entry + 40 + Page * 8);
      Size = R32 (Entry + 44 + Page * 8);
      CopyMem (Hive->Data + REG_HBINS_BASE + Offset, Entry + Cursor, Size);
      Cursor += Size;
    }
    Hive->Length = Length;
    Hive->Root = R32 (Log->Header + 36);
    *Sequence = Expected++;
    *Applied = TRUE;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
RegRecover (EFI_FILE_PROTOCOL *Root, REG_HIVE *Hive)
{
  STATIC CONST CHAR16 *CONST Names[] = {
    L"\\Windows\\System32\\config\\SYSTEM.LOG1", L"\\Windows\\System32\\config\\SYSTEM.LOG2"
  };
  REG_HIVE Logs[2];
  REG_HIVE *First;
  REG_HIVE *Second;
  BOOLEAN Valid = RegHeaderValid (Hive->Header, 0);
  BOOLEAN Applied = FALSE;
  UINT32 Sequence = R32 (Hive->Header + 8);
  EFI_STATUS Status = EFI_COMPROMISED_DATA;

  ZeroMem (Logs, sizeof (Logs));
  for (UINTN Index = 0; Index < ARRAY_SIZE (Logs); Index++) {
    if (!EFI_ERROR (RegOpen (Root, Names[Index], &Logs[Index])) &&
        RegHeaderValid (Logs[Index].Header, 6) && R32 (Logs[Index].Header + 4) == R32 (Logs[Index].Header + 8)) {
      Status = RegLoad (&Logs[Index]);
      if (EFI_ERROR (Status)) { goto done; }
    }
  }
  First = &Logs[0];
  Second = &Logs[1];
  if (First->Data == NULL || (Second->Data != NULL && RegBefore (R32 (Second->Header + 4), R32 (First->Header + 4)))) {
    First = &Logs[1];
    Second = &Logs[0];
  }
  if (!Valid && Second->Data != NULL) {
    First = Second; // A damaged base block is recovered from the newest log.
    Second = NULL;
  }
  if (!Valid && First->Data != NULL) {
    Sequence = R32 (First->Header + 4);
  }
  Status = EFI_COMPROMISED_DATA;
  if (First->Data != NULL) {
    Status = RegReplay (Hive, First, &Sequence, &Applied);
    if (EFI_ERROR (Status) && Status != EFI_NOT_FOUND) { goto done; }
  }
  if (Second != NULL && Second->Data != NULL) {
    Status = RegReplay (Hive, Second, &Sequence, &Applied);
    if (EFI_ERROR (Status) && Status != EFI_NOT_FOUND) { goto done; }
  }
  Status = Applied ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
done:
  for (UINTN Index = 0; Index < ARRAY_SIZE (Logs); Index++) { RegClose (&Logs[Index]); }
  return Status;
}

STATIC UINT8 *
RegCell (REG_HIVE *Hive, UINT32 Offset, UINTN Minimum, UINTN *Length OPTIONAL)
{
  UINT32 SignedSize;
  UINT32 Size;
  UINT8 *Data;

  if ((Offset & 7) != 0 || Offset > Hive->Length || Hive->Length - Offset < 4 ||
      EFI_ERROR (RegRead (Hive, REG_HBINS_BASE + (UINTN)Offset, 4, &SignedSize))) { return NULL; }
  Size = 0U - SignedSize;
  if ((SignedSize & BIT31) == 0 || (Size & 7) != 0 || Size < 4 || Minimum > Size - 4 ||
      Size > Hive->Length - Offset || Size > SIZE_1MB) { return NULL; }
  Data = AllocatePool (Size - 4);
  if (Data != NULL && EFI_ERROR (RegRead (Hive, REG_HBINS_BASE + (UINTN)Offset + 4, Size - 4, Data))) {
    FreePool (Data);
    Data = NULL;
  }
  if (Length != NULL) { *Length = Size - 4; }
  return Data;
}

STATIC BOOLEAN
RegName (CONST UINT8 *Data, UINTN Length, BOOLEAN Ascii, CONST CHAR8 *Name)
{
  UINTN Count = AsciiStrLen (Name);
  if (Length != Count * (Ascii ? 1U : 2U)) { return FALSE; }
  for (UINTN Index = 0; Index < Count; Index++) {
    UINT16 Character = Ascii ? Data[Index] : R16 (Data + Index * 2);
    if (Character > 127 || AsciiCharToUpper ((CHAR8)Character) != AsciiCharToUpper (Name[Index])) { return FALSE; }
  }
  return TRUE;
}

STATIC UINT8 *
RegNode (REG_HIVE *Hive, UINT32 Offset)
{
  UINTN Length;
  UINT8 *Node = RegCell (Hive, Offset, 76, &Length);
  if (Node != NULL && (R16 (Node) != SIGNATURE_16 ('n', 'k') || R16 (Node + 72) > Length - 76 || (R16 (Node + 2) & 0x10))) {
    FreePool (Node);
    Node = NULL;
  }
  return Node;
}

STATIC UINT32
RegFind (REG_HIVE *Hive, UINT32 Offset, CONST CHAR8 *Name, UINTN Depth)
{
  UINTN Length;
  UINTN Stride;
  UINT32 Hash = 0;
  UINT32 Found = REG_NIL;
  UINT8 *List;
  UINT8 *Node;
  UINT16 Kind;
  UINT16 Count;

  if (Depth == 8) { return REG_NIL; }
  List = RegCell (Hive, Offset, 4, &Length);
  if (List == NULL) { return REG_NIL; }
  Kind = R16 (List);
  Count = R16 (List + 2);
  Stride = Kind == SIGNATURE_16 ('l', 'h') || Kind == SIGNATURE_16 ('l', 'f') ? 8 : 4;
  if ((Stride == 4 && Kind != SIGNATURE_16 ('l', 'i') && Kind != SIGNATURE_16 ('r', 'i')) || Count > (Length - 4) / Stride) { goto done; }
  for (UINTN Index = 0; Name[Index] != 0; Index++) { Hash = Hash * 37 + (UINT8)AsciiCharToUpper (Name[Index]); }
  for (UINTN Index = 0; Index < Count && Found == REG_NIL; Index++) {
    Offset = R32 (List + 4 + Index * Stride);
    if (Kind == SIGNATURE_16 ('r', 'i')) {
      Found = RegFind (Hive, Offset, Name, Depth + 1);
    } else if (Kind != SIGNATURE_16 ('l', 'h') || R32 (List + 8 + Index * Stride) == Hash) {
      Node = RegNode (Hive, Offset);
      if (Node != NULL) {
        if (RegName (Node + 76, R16 (Node + 72), (R16 (Node + 2) & 0x20) != 0, Name)) { Found = Offset; }
        FreePool (Node);
      }
    }
  }
done:
  FreePool (List);
  return Found;
}

STATIC UINT32
RegKey (REG_HIVE *Hive, UINT32 Parent, CONST CHAR8 *Name)
{
  UINT8 *Node = RegNode (Hive, Parent);
  UINT32 Result = REG_NIL;
  if (Node != NULL) {
    if (R32 (Node + 20) != 0) { Result = RegFind (Hive, R32 (Node + 28), Name, 0); }
    FreePool (Node);
  }
  return Result;
}

STATIC UINT8 *
RegValue (REG_HIVE *Hive, UINT32 Key, CONST CHAR8 *Name, UINT32 *Type, UINTN *Size)
{
  UINT8 *Node = RegNode (Hive, Key);
  UINT8 *List;
  UINT8 *Value;
  UINT8 *Data = NULL;
  UINTN Length;
  UINT32 Count;
  UINT32 Offset;
  UINT32 Bytes;

  if (Node == NULL) { return NULL; }
  Count = R32 (Node + 36);
  Offset = R32 (Node + 40);
  FreePool (Node);
  List = RegCell (Hive, Offset, 0, &Length);
  if (List == NULL) { return NULL; }
  if (Count > Length / 4) { goto done; }
  for (UINTN Index = 0; Index < Count; Index++) {
    Value = RegCell (Hive, R32 (List + Index * 4), 20, &Length);
    if (Value == NULL) { break; }
    if (R16 (Value) == SIGNATURE_16 ('v', 'k') && R16 (Value + 2) <= Length - 20 &&
        RegName (Value + 20, R16 (Value + 2), (R16 (Value + 16) & 1) != 0, Name)) {
      Bytes = R32 (Value + 4);
      *Size = Bytes & ~BIT31;
      *Type = R32 (Value + 12);
      if (*Size != 0 && *Size < REG_PATH_SIZE * sizeof (CHAR16)) {
        if (Bytes & BIT31) {
          if (*Size <= 4) {
            Data = AllocateCopyPool (*Size, Value + 8);
          }
        } else {
          Data = RegCell (Hive, R32 (Value + 8), *Size, NULL);
        }
      }
      FreePool (Value);
      break;
    }
    FreePool (Value);
  }
done:
  FreePool (List);
  return Data;
}

STATIC UINT32
RegDword (REG_HIVE *Hive, UINT32 Key, CONST CHAR8 *Name)
{
  UINT32 Type;
  UINTN Size;
  UINT32 Result = REG_NIL;
  UINT8 *Data = RegValue (Hive, Key, Name, &Type, &Size);
  if (Data != NULL) {
    if (Type == 4 && Size == 4) { Result = R32 (Data); }
    FreePool (Data);
  }
  return Result;
}

STATIC BOOLEAN
RegPrefix (CONST CHAR16 *Path, CONST CHAR16 *Prefix)
{
  for (UINTN Index = 0; Prefix[Index] != 0; Index++) {
    if (CharToUpper (Path[Index]) != CharToUpper (Prefix[Index])) { return FALSE; }
  }
  return TRUE;
}

STATIC EFI_STATUS
RegPackage (REG_HIVE *Hive, UINT32 Services, CONST CHAR8 *Service, CHAR16 *Directory)
{
  CHAR16 Path[REG_PATH_SIZE];
  CHAR16 *Name = Path;
  CONST CHAR16 *Prefix = L"";
  UINT32 Type;
  UINTN Size;
  UINTN Length;
  UINTN Last = 0;
  UINT8 *Data = RegValue (Hive, RegKey (Hive, Services, Service), "ImagePath", &Type, &Size);

  if (Data == NULL) { return EFI_NOT_FOUND; }
  if ((Type != 1 && Type != 2) || (Size & 1) != 0) { FreePool (Data); return EFI_COMPROMISED_DATA; }
  Length = Size / 2;
  CopyMem (Path, Data, Size);
  FreePool (Data);
  while (Length != 0 && (Path[Length - 1] == 0 || Path[Length - 1] == ' ' || Path[Length - 1] == '\t')) { Length--; }
  Path[Length] = 0;
  for (UINTN Index = 0; Index < Length; Index++) { if (Path[Index] == 0) { return EFI_COMPROMISED_DATA; } }
  while (*Name == ' ' || *Name == '\t') { Name++; }
  if (*Name == '"') {
    if (Length == 0 || Path[Length - 1] != '"') { return EFI_COMPROMISED_DATA; }
    Path[Length - 1] = 0;
    Name++;
  }
  if (RegPrefix (Name, L"\\??\\") || RegPrefix (Name, L"\\\\?\\")) { Name += 4; }
  if (((Name[0] >= 'A' && Name[0] <= 'Z') || (Name[0] >= 'a' && Name[0] <= 'z')) && Name[1] == ':' && Name[2] == '\\') { Name += 2; }
  if (RegPrefix (Name, L"\\SystemRoot\\")) { Prefix = L"\\Windows"; Name += 11; }
  else if (RegPrefix (Name, L"%SystemRoot%\\")) { Prefix = L"\\Windows"; Name += 12; }
  else if (RegPrefix (Name, L"%windir%\\")) { Prefix = L"\\Windows"; Name += 8; }
  else if (*Name != '\\') { Prefix = L"\\Windows\\"; }
  if (StrLen (Prefix) + StrLen (Name) >= REG_PATH_SIZE) { return EFI_BAD_BUFFER_SIZE; }
  StrCpyS (Directory, REG_PATH_SIZE, Prefix);
  StrCatS (Directory, REG_PATH_SIZE, Name);
  if (!RegPrefix (Directory, L"\\Windows\\")) { return EFI_UNSUPPORTED; }
  for (UINTN Index = 0; Directory[Index] != 0; Index++) {
    if (Directory[Index] == '\\') {
      Last = Index;
      if (Directory[Index + 1] == '.' && (Directory[Index + 2] == '\\' || Directory[Index + 2] == 0 ||
          (Directory[Index + 2] == '.' && (Directory[Index + 3] == '\\' || Directory[Index + 3] == 0)))) { return EFI_COMPROMISED_DATA; }
    }
  }
  if (Last == 0 || Directory[Last + 1] == 0) { return EFI_COMPROMISED_DATA; }
  Directory[Last] = 0;
  return EFI_SUCCESS;
}

EFI_STATUS
RegistryOpenPackages (EFI_FILE_PROTOCOL *Root, EFI_FILE_PROTOCOL *Packages[2])
{
  STATIC CONST struct {
    CONST CHAR8  *Service;
    CONST CHAR16 *Required[2];
  } Drivers[] = {
    { "sia9177", { L"ADSP\\adsp.mdt", L"ADSP\\adsp_dtb.mdt" } },
    { "qcom_gpu", { L"gen80000_zap.mbn", NULL } }
  };
  REG_HIVE Hive;
  EFI_FILE_PROTOCOL *File;
  CHAR16 Path[REG_PATH_SIZE];
  CHAR8 ControlSet[24];
  UINT32 Select;
  UINT32 Control;
  UINT32 Services;
  EFI_STATUS Status;

  Packages[0] = Packages[1] = NULL;
  Status = RegOpen (Root, L"\\Windows\\System32\\config\\SYSTEM", &Hive);
  if (EFI_ERROR (Status)) { return Status; }
  if (Hive.Size < REG_HBINS_BASE * 2 || R32 (Hive.Header) != SIGNATURE_32 ('r', 'e', 'g', 'f') || R32 (Hive.Header + 28) != 0) {
    Status = EFI_COMPROMISED_DATA;
    goto done;
  }
  Hive.Length = R32 (Hive.Header + 40);
  Hive.Root = R32 (Hive.Header + 36);
  if (!RegHeaderValid (Hive.Header, 0) || R32 (Hive.Header + 4) != R32 (Hive.Header + 8)) {
    Status = RegRecover (Root, &Hive);
    if (EFI_ERROR (Status)) { goto done; }
  }
  if (Hive.Size < REG_HBINS_BASE || Hive.Length > Hive.Size - REG_HBINS_BASE) { Status = EFI_COMPROMISED_DATA; goto done; }
  Select = RegKey (&Hive, Hive.Root, "Select");
  // Default selects the next normal boot; Current describes the preceding boot.
  Control = RegDword (&Hive, Select, "Default");
  if (Control == REG_NIL) { Control = RegDword (&Hive, Select, "Current"); }
  if (Control == 0 || Control > 999) { Status = EFI_COMPROMISED_DATA; goto done; }
  AsciiSPrint (ControlSet, sizeof (ControlSet), "ControlSet%03u", Control);
  Services = RegKey (&Hive, RegKey (&Hive, Hive.Root, ControlSet), "Services");
  for (UINTN Index = 0; Index < ARRAY_SIZE (Drivers); Index++) {
    if (EFI_ERROR (RegPackage (&Hive, Services, Drivers[Index].Service, Path)) ||
        EFI_ERROR (Root->Open (Root, &Packages[Index], Path, EFI_FILE_MODE_READ, 0))) { continue; }
    for (UINTN Required = 0; Required < ARRAY_SIZE (Drivers[Index].Required); Required++) {
      if (Drivers[Index].Required[Required] == NULL) { break; }
      Status = Packages[Index]->Open (Packages[Index], &File, (CHAR16 *)Drivers[Index].Required[Required], EFI_FILE_MODE_READ, 0);
      if (EFI_ERROR (Status)) {
        Packages[Index]->Close (Packages[Index]);
        Packages[Index] = NULL;
        break;
      }
      File->Close (File);
    }
  }
  Status = Packages[0] != NULL || Packages[1] != NULL ? EFI_SUCCESS : EFI_NOT_FOUND;
done:
  RegClose (&Hive);
  return Status;
}
