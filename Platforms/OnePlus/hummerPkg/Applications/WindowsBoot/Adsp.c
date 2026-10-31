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
#include <Protocol/EFIScm.h>
#include <Protocol/scm_sip_interface.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include "Pcie0.h"

#define ADSP_FIRMWARE_DIR      L"\\ADSP\\"

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

STATIC EFI_HANDLE         *mAdspFileSystems;
STATIC UINTN              mAdspFileSystemCount;
STATIC QCOM_SCM_PROTOCOL  *mScm;

STATIC
BOOLEAN
AdspOpenRoot (
  IN  UINTN              Index,
  OUT EFI_FILE_PROTOCOL  **Root
  )
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FileSystem;
  EFI_DEVICE_PATH_PROTOCOL         *DevicePath;

  DevicePath = DevicePathFromHandle (mAdspFileSystems[Index]);
  return (DevicePath != NULL) && !MsBootPolicyLibIsDevicePathUsb (DevicePath) &&
         !EFI_ERROR (gBS->HandleProtocol (mAdspFileSystems[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&FileSystem)) &&
         !EFI_ERROR (FileSystem->OpenVolume (FileSystem, Root));
}

STATIC
EFI_STATUS
AdspReadFile (
  IN  CONST CHAR16  *Name,
  OUT UINT8         **Data,
  OUT UINTN         *Size
  )
{
  CHAR16             Path[64];
  EFI_FILE_PROTOCOL  *Root;
  EFI_FILE_PROTOCOL  *File;
  EFI_FILE_INFO      *Info;
  UINTN              InfoSize;
  UINTN              Index;
  EFI_STATUS         Status;

  UnicodeSPrint (Path, sizeof (Path), L"%s%s", ADSP_FIRMWARE_DIR, Name);
  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < mAdspFileSystemCount; Index++) {
    if (!AdspOpenRoot (Index, &Root)) {
      continue;
    }
    Status = Root->Open (Root, &File, Path, EFI_FILE_MODE_READ, 0);
    Root->Close (Root);
    if (EFI_ERROR (Status)) {
      continue;
    }
    InfoSize = 0;
    Info     = NULL;
    Status   = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, NULL);
    if (Status == EFI_BUFFER_TOO_SMALL) {
      Info   = AllocatePool (InfoSize);
      Status = (Info == NULL) ? EFI_OUT_OF_RESOURCES : File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info);
    }
    if (!EFI_ERROR (Status)) {
      *Size = (UINTN)Info->FileSize;
      *Data = AllocatePool (MAX (*Size, 1));
      Status = (*Data == NULL) ? EFI_OUT_OF_RESOURCES : File->Read (File, Size, *Data);
      if (EFI_ERROR (Status) && (*Data != NULL)) {
        FreePool (*Data);
        *Data = NULL;
      }
    }
    if (Info != NULL) {
      FreePool (Info);
    }
    File->Close (File);
    return Status;
  }
  return Status;
}

STATIC
EFI_STATUS
AdspScm4 (
  IN  UINT32       SmcId,
  IN  UINT32       ParamId,
  IN  UINT64       Arg0,
  IN  UINT64       Arg1,
  IN  UINT64       Arg2,
  IN  UINT64       Arg3,
  OUT UINT64       *Result1 OPTIONAL
  )
{
  UINT64      Parameters[SCM_MAX_NUM_PARAMETERS];
  UINT64      Results[SCM_MAX_NUM_RESULTS];
  EFI_STATUS  Status;

  ZeroMem (Parameters, sizeof (Parameters));
  ZeroMem (Results, sizeof (Results));
  Parameters[0] = Arg0;
  Parameters[1] = Arg1;
  Parameters[2] = Arg2;
  Parameters[3] = Arg3;
  Status        = mScm->ScmSipSysCall (mScm, SmcId, ParamId, Parameters, Results);
  if (Result1 != NULL) {
    *Result1 = Results[1];
  }
  if (!EFI_ERROR (Status) && (Results[1] != 0)) {
    Status = EFI_DEVICE_ERROR;
  }
  return Status;
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
  return AdspScm4 (SmcId, ParamId, Arg0, Arg1, Arg2, 0, NULL);
}

STATIC
VOID
AdspWriteRegion (
  IN UINTN        Destination,
  IN CONST UINT8  *Source,
  IN UINTN        FileSize,
  IN UINTN        MemSize
  )
{
  UINTN   Offset;
  UINT32  Word;

  for (Offset = 0; Offset < MemSize; Offset += 4) {
    Word = 0;
    if (Offset + 4 <= FileSize) {
      Word = ReadUnaligned32 ((CONST UINT32 *)(Source + Offset));
    } else if (Offset < FileSize) {
      CopyMem (&Word, Source + Offset, FileSize - Offset);
    }
    MmioWrite32 (Destination + Offset, Word);
  }
}

STATIC
EFI_STATUS
AdspLoadImage (
  IN  CONST CHAR16          *Name,
  IN  UINT32                PasId,
  IN  UINTN                 MemBase,
  IN  UINTN                 MemSize,
  OUT EFI_PHYSICAL_ADDRESS  *Metadata,
  OUT UINTN                 *MetadataPages
  )
{
  CHAR16                 FileName[32];
  UINT8                  *Mdt;
  UINTN                  MdtSize;
  UINT8                  *Segment;
  UINTN                  SegmentSize;
  CONST ADSP_ELF32_EHDR  *Ehdr;
  CONST ADSP_ELF32_PHDR  *Phdr;
  UINTN                  Index;
  UINTN                  Hash;
  UINT64                 MinAddr;
  UINT64                 MaxAddr;
  UINT64                 Reloc;
  BOOLEAN                Relocate;
  UINTN                  MetaSize;
  EFI_STATUS             Status;

  UnicodeSPrint (FileName, sizeof (FileName), L"%s.mdt", Name);
  Status = AdspReadFile (FileName, &Mdt, &MdtSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Ehdr = (CONST ADSP_ELF32_EHDR *)Mdt;
  if ((MdtSize < sizeof (*Ehdr)) || (ReadUnaligned32 ((CONST UINT32 *)Ehdr->Ident) != 0x464C457F) || (Ehdr->Ident[4] != 1) ||
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
  MetaSize = (UINTN)Phdr[0].FileSz + Phdr[Hash].FileSz;
  if ((Hash == 0) || (MetaSize != MdtSize)) {
    Status = EFI_UNSUPPORTED;
    goto done;
  }

  MinAddr  = MAX_UINT64;
  MaxAddr  = 0;
  Relocate = FALSE;
  for (Index = 0; Index < Ehdr->PhNum; Index++) {
    if ((Phdr[Index].Type != ELF_PT_LOAD) || ((Phdr[Index].Flags & MDT_TYPE_MASK) == MDT_TYPE_HASH) || (Phdr[Index].MemSz == 0)) {
      continue;
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

  *MetadataPages = EFI_SIZE_TO_PAGES (MdtSize);
  *Metadata      = 0xFFFFFFFF;
  Status         = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData, *MetadataPages, Metadata);
  if (EFI_ERROR (Status)) {
    *MetadataPages = 0;
    goto done;
  }
  CopyMem ((VOID *)(UINTN)*Metadata, Mdt, MdtSize);
  WriteBackDataCacheRange ((VOID *)(UINTN)*Metadata, MdtSize);

  Status = AdspScm (
             TZ_PIL_INIT_ID,
             TZ_PIL_INIT_ID_PARAM_ID,
             PasId,
             *Metadata,
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
    if ((Phdr[Index].FileSz > Phdr[Index].MemSz) || ((Phdr[Index].PAddr & 3) != 0)) {
      Status = EFI_COMPROMISED_DATA;
      goto done;
    }
    Segment     = NULL;
    SegmentSize = 0;
    if (Phdr[Index].FileSz != 0) {
      UnicodeSPrint (FileName, sizeof (FileName), L"%s.b%02u", Name, (UINT32)Index);
      Status = AdspReadFile (FileName, &Segment, &SegmentSize);
      if (EFI_ERROR (Status) || (SegmentSize != Phdr[Index].FileSz)) {
        if (Segment != NULL) {
          FreePool (Segment);
        }
        Status = EFI_ERROR (Status) ? Status : EFI_COMPROMISED_DATA;
        goto done;
      }
    }
    AdspWriteRegion (MemBase + (UINTN)(Phdr[Index].PAddr - Reloc), Segment, SegmentSize, Phdr[Index].MemSz);
    if (Segment != NULL) {
      FreePool (Segment);
    }
  }
  Status = EFI_SUCCESS;

done:
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
  EFI_PHYSICAL_ADDRESS  DtbMetadata;
  EFI_PHYSICAL_ADDRESS  Metadata;
  UINTN                 DtbPages;
  UINTN                 Pages;
  UINT32                Step;
  EFI_STATUS            Status;

  mAdspFileSystems     = FileSystems;
  mAdspFileSystemCount = FileSystemCount;
  DtbPages             = 0;
  Pages                = 0;

  Status = gBS->LocateProtocol (&gQcomScmProtocolGuid, NULL, (VOID **)&mScm);
  if (EFI_ERROR (Status)) {
    goto done;
  }

  Step = QmpOpen ();
  if (Step == 0) {
    QmpSend ("{class: image, res: load_state, name: adsp, val: on}");
  }
  AdspVoteRail ("lcx.lvl");
  AdspVoteRail ("lmx.lvl");
  AdspScm (TZ_PIL_IS_SUBSYS_SUPPORTED_ID, TZ_PIL_IS_SUBSYS_SUPPORTED_ID_PARAM_ID, ADSP_DTB_PAS_ID, 0, 0);
  AdspScm (TZ_PIL_IS_SUBSYS_SUPPORTED_ID, TZ_PIL_IS_SUBSYS_SUPPORTED_ID_PARAM_ID, ADSP_PAS_ID, 0, 0);

  Status = AdspLoadImage (L"adsp_dtb", ADSP_DTB_PAS_ID, ADSP_DTB_MEM_BASE, ADSP_DTB_MEM_SIZE, &DtbMetadata, &DtbPages);
  if (EFI_ERROR (Status)) {
    goto done;
  }
  Status = AdspScm (TZ_PIL_AUTH_RESET_ID, TZ_PIL_AUTH_RESET_ID_PARAM_ID, ADSP_DTB_PAS_ID, 0, 0);
  if (EFI_ERROR (Status)) {
    goto done;
  }

  Status = AdspLoadImage (L"adsp", ADSP_PAS_ID, ADSP_MEM_BASE, ADSP_MEM_SIZE, &Metadata, &Pages);
  if (EFI_ERROR (Status)) {
    goto done;
  }
  Status = AdspScm (TZ_PIL_AUTH_RESET_ID, TZ_PIL_AUTH_RESET_ID_PARAM_ID, ADSP_PAS_ID, 0, 0);

done:
  if (Pages != 0) {
    gBS->FreePages (Metadata, Pages);
  }
  if (DtbPages != 0) {
    gBS->FreePages (DtbMetadata, DtbPages);
  }
  return Status;
}
