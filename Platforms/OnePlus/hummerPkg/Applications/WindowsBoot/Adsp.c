#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MsBootPolicyLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/AcpiSystemDescriptionTable.h>
#include <Protocol/EFIScm.h>
#include <Protocol/scm_sip_interface.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include "Pcie0.h"
#include "Registry.h"

#define ADSP_PAS_ID            1
#define ADSP_MEM_BASE          0x9EE00000
#define ADSP_MEM_SIZE          0x05080000
#define ADSP_DTB_PAS_ID        0x24
#define ADSP_DTB_MEM_BASE      0x9ED80000
#define ADSP_DTB_MEM_SIZE      0x00080000

#define MDT_TYPE_MASK          (7U << 24)
#define MDT_TYPE_HASH          (2U << 24)
#define MDT_RELOCATABLE        BIT27
#define ELF_PT_LOAD            1

typedef struct {
  UINT8   Ident[16];
  UINT16  Type;
  UINT16  Machine;
  UINT32  Version;
  UINT32  Entry;
  UINT32  PhOff;
  UINT32  ShOff;
  UINT32  Flags;
  UINT16  EhSize;
  UINT16  PhEntSize;
  UINT16  PhNum;
  UINT16  ShEntSize;
  UINT16  ShNum;
  UINT16  ShStrNdx;
} ADSP_ELF32_EHDR;

typedef struct {
  UINT32  Type;
  UINT32  Offset;
  UINT32  VAddr;
  UINT32  PAddr;
  UINT32  FileSz;
  UINT32  MemSz;
  UINT32  Flags;
  UINT32  Align;
} ADSP_ELF32_PHDR;

STATIC QCOM_SCM_PROTOCOL  *mScm;
STATIC EFI_FILE_PROTOCOL  *mPilPackages[2];

STATIC
BOOLEAN
AdspOpenRoot (
  IN  EFI_HANDLE         Handle,
  OUT EFI_FILE_PROTOCOL  **Root
  )
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FileSystem;
  EFI_DEVICE_PATH_PROTOCOL         *DevicePath;

  DevicePath = DevicePathFromHandle (Handle);
  return (DevicePath != NULL) && !MsBootPolicyLibIsDevicePathUsb (DevicePath) &&
         !EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&FileSystem)) &&
         !EFI_ERROR (FileSystem->OpenVolume (FileSystem, Root));
}

STATIC
EFI_STATUS
PilReadFile (
  IN  EFI_FILE_PROTOCOL *Directory,
  IN  CONST CHAR16  *Name,
  OUT UINT8         **Data,
  OUT UINTN         *Size
  )
{
  EFI_FILE_PROTOCOL *File;
  EFI_FILE_INFO *Info = NULL;
  UINTN InfoSize = 0;
  EFI_STATUS Status;

  *Data = NULL;
  Status = Directory->Open (Directory, &File, (CHAR16 *)Name, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) { return Status; }
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, NULL);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Info = AllocatePool (InfoSize);
    Status = Info == NULL ? EFI_OUT_OF_RESOURCES : File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info);
  }
  if (!EFI_ERROR (Status)) {
    InfoSize = (UINTN)Info->FileSize;
    *Size = InfoSize;
    *Data = AllocatePool (MAX (*Size, 1));
    Status = *Data == NULL ? EFI_OUT_OF_RESOURCES : File->Read (File, Size, *Data);
    if (!EFI_ERROR (Status) && *Size != InfoSize) { Status = EFI_COMPROMISED_DATA; }
    if (EFI_ERROR (Status) && *Data != NULL) { FreePool (*Data); *Data = NULL; }
  }
  if (Info != NULL) { FreePool (Info); }
  File->Close (File);
  return Status;
}

STATIC VOID
PilFindPackages (EFI_HANDLE *FileSystems, UINTN Count)
{
  EFI_FILE_PROTOCOL *Root;
  EFI_STATUS Status;

  for (UINTN Index = 0; Index < Count; Index++) {
    if (!AdspOpenRoot (FileSystems[Index], &Root)) { continue; }
    Status = RegistryOpenPackages (Root, mPilPackages);
    Root->Close (Root);
    if (!EFI_ERROR (Status)) { break; }
  }
}

STATIC
EFI_STATUS
AdspScm (
  IN  UINT32       SmcId,
  IN  UINT32       ParamId,
  IN  UINT64       Arg0,
  IN  UINT64       Arg1,
  IN  UINT64       Arg2
  )
{
  UINT64      Parameters[SCM_MAX_NUM_PARAMETERS] = { 0 };
  UINT64      Results[SCM_MAX_NUM_RESULTS] = { 0 };
  EFI_STATUS  Status;

  Parameters[0] = Arg0;
  Parameters[1] = Arg1;
  Parameters[2] = Arg2;
  Status        = mScm->ScmSipSysCall (mScm, SmcId, ParamId, Parameters, Results);
  return EFI_ERROR (Status) || Results[1] == 0 ? Status : EFI_DEVICE_ERROR;
}

STATIC
EFI_STATUS
PilReadSegment (
  IN EFI_FILE_PROTOCOL *Directory,
  IN CONST CHAR16      *Name,
  IN VOID              *Destination,
  IN UINTN             Size
  )
{
  EFI_FILE_PROTOCOL *File;
  EFI_STATUS Status;
  UINTN Length = Size;
  UINT8 Extra;

  Status = Directory->Open (Directory, &File, (CHAR16 *)Name, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = File->Read (File, &Length, Destination);
  if (!EFI_ERROR (Status) && Length != Size) {
    Status = EFI_COMPROMISED_DATA;
  }
  if (!EFI_ERROR (Status)) {
    Length = 1;
    Status = File->Read (File, &Length, &Extra);
    if (!EFI_ERROR (Status) && Length != 0) {
      Status = EFI_COMPROMISED_DATA;
    }
  }
  File->Close (File);
  return Status;
}

STATIC
EFI_STATUS
PilStartImage (
  IN  EFI_FILE_PROTOCOL     *Directory OPTIONAL,
  IN  CONST CHAR16          *Name,
  IN  BOOLEAN               Split,
  IN  UINT32                PasId,
  IN  UINTN                 MemBase,
  IN  UINTN                 MemSize,
  OUT EFI_PHYSICAL_ADDRESS  *RetainedMetadata OPTIONAL,
  OUT UINTN                 *RetainedPages OPTIONAL
  )
{
  CHAR16                 FileName[32];
  UINT8                  *Mdt;
  UINTN                  MdtSize;
  UINT8                  *Segment;
  CONST ADSP_ELF32_EHDR  *Ehdr;
  CONST ADSP_ELF32_PHDR  *Phdr;
  UINTN                  Index;
  UINTN                  Hash;
  UINT64                 MinAddr;
  UINT64                 MaxAddr;
  UINT64                 Reloc;
  BOOLEAN                Relocate;
  UINTN                  MetaSize;
  UINTN                  HashOffset;
  EFI_PHYSICAL_ADDRESS   Metadata = 0;
  UINTN                  Pages = 0;
  EFI_STATUS             Status;

  if (RetainedMetadata != NULL) {
    *RetainedMetadata = 0;
    *RetainedPages = 0;
  }

  UnicodeSPrint (FileName, sizeof (FileName), Split ? L"%s.mdt" : L"%s.mbn", Name);
  Status = PilReadFile (Directory, FileName, &Mdt, &MdtSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Ehdr = (CONST ADSP_ELF32_EHDR *)Mdt;
  if ((MdtSize < sizeof (*Ehdr)) || (ReadUnaligned32 ((CONST UINT32 *)Ehdr->Ident) != 0x464C457F) ||
      (Ehdr->Ident[4] != 1) || (Ehdr->Ident[5] != 1) || ((Ehdr->PhOff & 3) != 0) ||
      (Ehdr->PhEntSize != sizeof (ADSP_ELF32_PHDR)) || (Ehdr->PhNum < 2) ||
      ((UINT64)Ehdr->PhOff + (UINT64)Ehdr->PhNum * sizeof (ADSP_ELF32_PHDR) > MdtSize))
  {
    Status = EFI_COMPROMISED_DATA;
    goto done;
  }
  Phdr = (CONST ADSP_ELF32_PHDR *)(Mdt + Ehdr->PhOff);
  if (Phdr[0].Type == ELF_PT_LOAD) {
    Status = EFI_COMPROMISED_DATA;
    goto done;
  }

  Hash = 0;
  for (Index = 1; Index < Ehdr->PhNum; Index++) {
    if ((Phdr[Index].Flags & MDT_TYPE_MASK) == MDT_TYPE_HASH) {
      Hash = Index;
      break;
    }
  }
  if ((Hash == 0) || (Phdr[0].FileSz < (UINT64)Ehdr->PhOff + (UINT64)Ehdr->PhNum * sizeof (*Phdr))) {
    Status = EFI_COMPROMISED_DATA;
    goto done;
  }
  MetaSize = (UINTN)Phdr[0].FileSz + Phdr[Hash].FileSz;
  HashOffset = Split ? Phdr[0].FileSz : Phdr[Hash].Offset;
  if ((HashOffset < Phdr[0].FileSz) || (HashOffset > MdtSize) ||
      (Phdr[Hash].FileSz > MdtSize - HashOffset) || (Split && MetaSize != MdtSize)) {
    Status = EFI_COMPROMISED_DATA;
    goto done;
  }

  MinAddr  = MAX_UINT64;
  MaxAddr  = 0;
  Relocate = FALSE;
  for (Index = 0; Index < Ehdr->PhNum; Index++) {
    if ((Phdr[Index].Type != ELF_PT_LOAD) || ((Phdr[Index].Flags & MDT_TYPE_MASK) == MDT_TYPE_HASH) || (Phdr[Index].MemSz == 0)) {
      continue;
    }
    if ((Phdr[Index].FileSz > Phdr[Index].MemSz) || (Phdr[Index].PAddr & 3) != 0 ||
        (!Split && ((Phdr[Index].Offset > MdtSize) || (Phdr[Index].FileSz > MdtSize - Phdr[Index].Offset)))) {
      Status = EFI_COMPROMISED_DATA;
      goto done;
    }
    if ((Phdr[Index].Flags & MDT_RELOCATABLE) != 0) {
      Relocate = TRUE;
    }
    MinAddr = MIN (MinAddr, Phdr[Index].PAddr);
    MaxAddr = MAX (MaxAddr, ALIGN_VALUE ((UINT64)Phdr[Index].PAddr + Phdr[Index].MemSz, SIZE_4KB));
  }
  Reloc = Relocate ? MinAddr : MemBase;
  if ((MaxAddr <= MinAddr) || (MaxAddr - Reloc > MemSize) || (MinAddr < Reloc)) {
    Status = EFI_BAD_BUFFER_SIZE;
    goto done;
  }

  Pages    = EFI_SIZE_TO_PAGES (MetaSize);
  Metadata = 0xFFFFFFFF;
  Status   = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData, Pages, &Metadata);
  if (EFI_ERROR (Status)) {
    Pages = 0;
    goto done;
  }
  CopyMem ((VOID *)(UINTN)Metadata, Mdt, Phdr[0].FileSz);
  CopyMem ((UINT8 *)(UINTN)Metadata + Phdr[0].FileSz, Mdt + HashOffset, Phdr[Hash].FileSz);
  WriteBackDataCacheRange ((VOID *)(UINTN)Metadata, MetaSize);

  Status = AdspScm (
             TZ_PIL_INIT_ID,
             TZ_PIL_INIT_ID_PARAM_ID,
             PasId,
             Metadata,
             0
             );
  if (EFI_ERROR (Status)) {
    goto done;
  }
  if (Relocate) {
    Status = AdspScm (TZ_PIL_MEM_ID, TZ_PIL_MEM_ID_PARAM_ID, PasId, MemBase, MaxAddr - MinAddr);
    if (EFI_ERROR (Status)) {
      goto done;
    }
  }

  for (Index = 0; Index < Ehdr->PhNum; Index++) {
    if ((Phdr[Index].Type != ELF_PT_LOAD) || ((Phdr[Index].Flags & MDT_TYPE_MASK) == MDT_TYPE_HASH) || (Phdr[Index].MemSz == 0)) {
      continue;
    }
    Segment = (UINT8 *)(MemBase + (UINTN)(Phdr[Index].PAddr - Reloc));
    if (Phdr[Index].FileSz != 0) {
      if (Split) {
        UnicodeSPrint (FileName, sizeof (FileName), L"%s.b%02u", Name, (UINT32)Index);
        Status = PilReadSegment (Directory, FileName, Segment, Phdr[Index].FileSz);
        if (EFI_ERROR (Status)) {
          goto done;
        }
      } else {
        CopyMem (Segment, Mdt + Phdr[Index].Offset, Phdr[Index].FileSz);
      }
    }
    ZeroMem (Segment + Phdr[Index].FileSz, Phdr[Index].MemSz - Phdr[Index].FileSz);
  }
  MemoryFence ();
  Status = AdspScm (TZ_PIL_AUTH_RESET_ID, TZ_PIL_AUTH_RESET_ID_PARAM_ID, PasId, 0, 0);

done:
  if (!EFI_ERROR (Status) && RetainedMetadata != NULL) {
    *RetainedMetadata = Metadata;
    *RetainedPages = Pages;
    Pages = 0;
  }
  if (Pages != 0) {
    gBS->FreePages (Metadata, Pages);
  }
  FreePool (Mdt);
  return Status;
}

STATIC
EFI_STATUS
AdspVoteRail (
  IN CONST CHAR8  *Name
  )
{
  UINT32      Address;
  UINT32      Offset;
  UINT32      Level;
  UINTN       Count;
  EFI_STATUS  Status;

  Address = CmdDbAddress (Name);
  Count   = MIN (CmdDbAuxLength (Name) / 2, 16);
  for (Level = 1; Level < Count; Level++) {
    if (CmdDbAuxWord (Name, Level) == 0) {
      Count = Level;
      break;
    }
  }
  if ((Address == 0) || (Count == 0)) {
    return EFI_NOT_FOUND;
  }
  Offset = 0;
  Level  = (UINT32)(Count - 1);
  Status = RpmhWrite (Address, 1, &Offset, &Level);
  return Status;
}

EFI_STATUS
AdspInitialize (
  IN EFI_HANDLE  *FileSystems,
  IN UINTN       FileSystemCount
  )
{
  UINT32                Step;
  EFI_STATUS            Status;
  EFI_FILE_PROTOCOL     *Directory;
  EFI_FILE_PROTOCOL     *Package;
  EFI_PHYSICAL_ADDRESS  Metadata[2] = { 0 };
  UINTN                 Pages[2] = { 0 };

  Status = gBS->LocateProtocol (&gQcomScmProtocolGuid, NULL, (VOID **)&mScm);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  PilFindPackages (FileSystems, FileSystemCount);
  Package = mPilPackages[0];
  mPilPackages[0] = NULL;
  if (Package == NULL) {
    return EFI_NOT_FOUND;
  }
  Status = Package->Open (Package, &Directory, L"ADSP", EFI_FILE_MODE_READ, 0);
  Package->Close (Package);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Step = QmpOpen ();
  if (Step == 0) {
    QmpSend ("{class: image, res: load_state, name: adsp, val: on}");
  }
  AdspVoteRail ("lcx.lvl");
  AdspVoteRail ("lmx.lvl");
  Status = PilStartImage (Directory, L"adsp_dtb", TRUE, ADSP_DTB_PAS_ID, ADSP_DTB_MEM_BASE, ADSP_DTB_MEM_SIZE, &Metadata[0], &Pages[0]);
  if (!EFI_ERROR (Status)) {
    Status = PilStartImage (Directory, L"adsp", TRUE, ADSP_PAS_ID, ADSP_MEM_BASE, ADSP_MEM_SIZE, &Metadata[1], &Pages[1]);
  }
  for (UINTN Image = 0; Image < ARRAY_SIZE (Pages); Image++) {
    if (Pages[Image] != 0) {
      gBS->FreePages (Metadata[Image], Pages[Image]);
    }
  }
  Directory->Close (Directory);
  return Status;
}

EFI_STATUS
GpuInitialize (
  VOID
  )
{
  EFI_ACPI_SDT_PROTOCOL  *Sdt;
  EFI_ACPI_SDT_HEADER    *Table;
  EFI_ACPI_TABLE_VERSION Version;
  EFI_ACPI_HANDLE       Root = NULL;
  EFI_ACPI_HANDLE       Name = NULL;
  EFI_ACPI_DATA_TYPE    Type;
  CONST VOID            *Data;
  UINTN                 Size;
  UINTN                 Index;
  UINTN                 Key;
  UINT8                 Ready = 1;
  EFI_STATUS            Status;
  EFI_STATUS            CloseStatus;
  EFI_FILE_PROTOCOL     *Package;
  Package = mPilPackages[1];
  mPilPackages[1] = NULL;
  Status = gBS->LocateProtocol (&gEfiAcpiSdtProtocolGuid, NULL, (VOID **)&Sdt);
  if (EFI_ERROR (Status)) {
    goto done;
  }
  for (Index = 0; !EFI_ERROR (Status = Sdt->GetAcpiTable (Index, &Table, &Version, &Key)); Index++) {
    if (Table->Signature == SIGNATURE_32 ('D', 'S', 'D', 'T')) {
      Status = Sdt->OpenSdt (Key, &Root);
      break;
    }
  }
  if (!EFI_ERROR (Status)) {
    Status = Sdt->FindPath (Root, "\\_SB_.GPU0.ZPRD", &Name);
  }
  if (!EFI_ERROR (Status)) {
    Status = Name == NULL ? EFI_NOT_FOUND : Sdt->GetOption (Name, 2, &Type, &Data, &Size);
  }
  if (!EFI_ERROR (Status)) {
    if (Type != EFI_ACPI_DATA_TYPE_CHILD || Size != 1 || *(CONST UINT8 *)Data > 1) {
      Status = EFI_COMPROMISED_DATA;
    } else if (*(CONST UINT8 *)Data == 0) {
      Status = mScm == NULL ? EFI_NOT_READY : Package == NULL ? EFI_NOT_FOUND :
        PilStartImage (Package, L"gen80000_zap", FALSE, 13, 0x9B09A000, 0x2000, NULL, NULL);
      if (!EFI_ERROR (Status)) {
        Status = Sdt->SetOption (Name, 2, &Ready, sizeof (Ready));
      }
    }
  }
done:
  if (Package != NULL) {
    Package->Close (Package);
  }
  if (Name != NULL) {
    CloseStatus = Sdt->Close (Name);
    if (!EFI_ERROR (Status)) {
      Status = CloseStatus;
    }
  }
  if (Root != NULL) {
    Sdt->Close (Root);
  }
  return Status;
}
