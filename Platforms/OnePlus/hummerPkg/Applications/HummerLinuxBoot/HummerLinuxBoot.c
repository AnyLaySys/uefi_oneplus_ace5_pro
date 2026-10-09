#include <Uefi.h>
#include <PiPei.h>
#include <Guid/Fdt.h>
#include <Guid/LinuxEfiInitrdMedia.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadFile2.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/EFIUsbMsd.h>

#include <LinuxPayload.h>

STATIC EFI_GUID mHummerLinuxPayloadHobGuid = HUMMER_LINUX_PAYLOAD_HOB_GUID;
STATIC VOID *mInitrdData;
STATIC UINTN mInitrdSize;
STATIC EFI_HANDLE mInitrdHandle;
STATIC CONST CHAR8 mLinuxCommandLine[] = "console=tty0 rdinit=/init acpi=off loglevel=7 log_buf_len=1M panic=0 cpuidle.off=1 clk_ignore_unused pd_ignore_unused regulator_ignore_unused pstore.backend=ramoops hummer.linux_test=1";

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH Vendor;
  EFI_DEVICE_PATH_PROTOCOL End;
} INITRD_DEVICE_PATH;

typedef struct {
  MEMMAP_DEVICE_PATH Memory;
  EFI_DEVICE_PATH_PROTOCOL End;
} IMAGE_DEVICE_PATH;
#pragma pack()

STATIC CONST INITRD_DEVICE_PATH mInitrdPath = {
  {
    {
      MEDIA_DEVICE_PATH,
      MEDIA_VENDOR_DP,
      { sizeof (VENDOR_DEVICE_PATH), 0 }
    },
    LINUX_EFI_INITRD_MEDIA_GUID
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 } }
};

STATIC
EFI_STATUS
EFIAPI
LoadInitrd (
  IN EFI_LOAD_FILE2_PROTOCOL *This,
  IN EFI_DEVICE_PATH_PROTOCOL *FilePath,
  IN BOOLEAN BootPolicy,
  IN OUT UINTN *BufferSize,
  IN VOID *Buffer OPTIONAL
  )
{
  if ((BufferSize == NULL) || BootPolicy || (mInitrdData == NULL) || (mInitrdSize == 0)) {
    return EFI_INVALID_PARAMETER;
  }
  if ((Buffer == NULL) || (*BufferSize < mInitrdSize)) {
    *BufferSize = mInitrdSize;
    return EFI_BUFFER_TOO_SMALL;
  }
  CopyMem (Buffer, mInitrdData, mInitrdSize);
  *BufferSize = mInitrdSize;
  return EFI_SUCCESS;
}

STATIC EFI_LOAD_FILE2_PROTOCOL mLoadFile2 = { LoadInitrd };

STATIC
EFI_STATUS
StopMassStorage (
  VOID
  )
{
  EFI_STATUS Status;
  EFI_USB_MSD_PROTOCOL *Msd;

  Msd = NULL;
  Status = gBS->LocateProtocol (&gEfiUsbMsdProtocolGuid, NULL, (VOID **)&Msd);
  if (EFI_ERROR (Status) || (Msd == NULL) || (Msd->StopDevice == NULL)) {
    return EFI_SUCCESS;
  }
  return Msd->StopDevice (Msd);
}

STATIC
EFI_STATUS
GetPayload (
  OUT CONST HUMMER_LINUX_PAYLOAD_HOB **Payload
  )
{
  EFI_HOB_GUID_TYPE *Hob;

  Hob = GetFirstGuidHob (&mHummerLinuxPayloadHobGuid);
  if (Hob == NULL) {
    return EFI_NOT_FOUND;
  }
  if (GET_GUID_HOB_DATA_SIZE (Hob) < sizeof (HUMMER_LINUX_PAYLOAD_HOB)) {
    return EFI_BAD_BUFFER_SIZE;
  }
  *Payload = (CONST HUMMER_LINUX_PAYLOAD_HOB *)GET_GUID_HOB_DATA (Hob);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
PrepareDtb (
  IN CONST VOID *Source,
  IN UINTN SourceSize,
  OUT VOID **Dtb,
  OUT EFI_PHYSICAL_ADDRESS *DtbAddress,
  OUT UINTN *DtbPages,
  IN CONST CHAR8 *BootArgs
  )
{
  EFI_STATUS Status;
  EFI_PHYSICAL_ADDRESS Address;
  UINTN AllocationSize;
  INT32 Chosen;
  INT32 Err;
  UINT64 ThreadPointer;
  UINT64 ThreadPointerBe;

  if ((Source == NULL) || (SourceSize < sizeof (FDT_HEADER)) ||
      (FdtCheckHeader (Source) != 0) ||
      (FdtTotalSize (Source) > SourceSize) ||
      (FdtTotalSize (Source) > MAX_UINT32 - 4096)) {
    return EFI_INVALID_PARAMETER;
  }
  AllocationSize = FdtTotalSize (Source) + 4096;
  Status = gBS->AllocatePages (
             AllocateAnyPages,
             EfiLoaderData,
             EFI_SIZE_TO_PAGES (AllocationSize),
             &Address
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Err = FdtOpenInto (Source, (VOID *)(UINTN)Address, AllocationSize);
  if (Err != 0) {
    gBS->FreePages (Address, EFI_SIZE_TO_PAGES (AllocationSize));
    return EFI_INVALID_PARAMETER;
  }
  Chosen = FdtPathOffset ((VOID *)(UINTN)Address, "/chosen");
  if (Chosen < 0) {
    Chosen = FdtAddSubnode ((VOID *)(UINTN)Address, 0, "chosen");
  }
  if (Chosen < 0) {
    gBS->FreePages (Address, EFI_SIZE_TO_PAGES (AllocationSize));
    return EFI_INVALID_PARAMETER;
  }
  FdtDelProp ((VOID *)(UINTN)Address, Chosen, "linux,initrd-start");
  FdtDelProp ((VOID *)(UINTN)Address, Chosen, "linux,initrd-end");
  FdtDelProp ((VOID *)(UINTN)Address, Chosen, "kaslr-seed");
  FdtDelProp ((VOID *)(UINTN)Address, Chosen, "rng-seed");
  FdtDelProp ((VOID *)(UINTN)Address, Chosen, "bootargs");
  __asm__ __volatile__ ("mrs %0, tpidr_el1" : "=r" (ThreadPointer));
  ThreadPointerBe = SwapBytes64 (ThreadPointer);
  if (FdtSetProp ((VOID *)(UINTN)Address, Chosen, "hummer,uefi-tpidr-el1",
      &ThreadPointerBe, sizeof (ThreadPointerBe)) != 0) {
    gBS->FreePages (Address, EFI_SIZE_TO_PAGES (AllocationSize));
    return EFI_BUFFER_TOO_SMALL;
  }
  if (FdtSetProp ((VOID *)(UINTN)Address, Chosen, "bootargs", BootArgs, AsciiStrLen (BootArgs) + 1) != 0) {
    gBS->FreePages (Address, EFI_SIZE_TO_PAGES (AllocationSize));
    return EFI_INVALID_PARAMETER;
  }
  *Dtb = (VOID *)(UINTN)Address;
  *DtbAddress = Address;
  *DtbPages = EFI_SIZE_TO_PAGES (AllocationSize);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
InstallInitrd (
  IN CONST VOID *Data,
  IN UINTN Size
  )
{
  EFI_STATUS Status;

  mInitrdData = (VOID *)Data;
  mInitrdSize = Size;
  mInitrdHandle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
             &mInitrdHandle,
             &gEfiLoadFile2ProtocolGuid,
             &mLoadFile2,
             &gEfiDevicePathProtocolGuid,
             &mInitrdPath,
             NULL
             );
  if (EFI_ERROR (Status)) {
    mInitrdData = NULL;
    mInitrdSize = 0;
  }
  return Status;
}

STATIC
VOID
UninstallInitrd (
  VOID
  )
{
  if (mInitrdHandle != NULL) {
    gBS->UninstallMultipleProtocolInterfaces (
      mInitrdHandle,
      &gEfiLoadFile2ProtocolGuid,
      &mLoadFile2,
      &gEfiDevicePathProtocolGuid,
      &mInitrdPath,
      NULL
      );
  }
  mInitrdHandle = NULL;
  mInitrdData = NULL;
  mInitrdSize = 0;
}

STATIC
EFI_STATUS
StartLinuxImage (
  IN EFI_HANDLE Parent,
  IN CONST VOID *Image,
  IN UINTN ImageSize,
  IN CONST CHAR8 *BootArgs
  )
{
  EFI_STATUS Status;
  EFI_HANDLE ImageHandle;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  IMAGE_DEVICE_PATH Path;
  CHAR16 Options[1024];

  ZeroMem (&Path, sizeof (Path));
  if ((Image == NULL) || (ImageSize == 0) || ((EFI_PHYSICAL_ADDRESS)(UINTN)Image > MAX_UINT64 - ImageSize)) {
    return EFI_INVALID_PARAMETER;
  }
  Path.Memory.Header.Type = HARDWARE_DEVICE_PATH;
  Path.Memory.Header.SubType = HW_MEMMAP_DP;
  Path.Memory.Header.Length[0] = sizeof (MEMMAP_DEVICE_PATH);
  Path.Memory.StartingAddress = (EFI_PHYSICAL_ADDRESS)(UINTN)Image;
  Path.Memory.EndingAddress = (EFI_PHYSICAL_ADDRESS)(UINTN)Image + ImageSize - 1;
  Path.End.Type = END_DEVICE_PATH_TYPE;
  Path.End.SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  Path.End.Length[0] = sizeof (EFI_DEVICE_PATH_PROTOCOL);
  ImageHandle = NULL;
  Status = gBS->LoadImage (TRUE, Parent, (EFI_DEVICE_PATH_PROTOCOL *)&Path, (VOID *)Image, ImageSize, &ImageHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  UnicodeSPrint (Options, sizeof (Options), L"%a", BootArgs);
  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  if (!EFI_ERROR (Status)) {
    LoadedImage->LoadOptions = Options;
    LoadedImage->LoadOptionsSize = (UINT32)StrSize (Options);
    Status = gBS->StartImage (ImageHandle, NULL, NULL);
  }
  gBS->UnloadImage (ImageHandle);
  return Status;
}

EFI_STATUS
EFIAPI
HummerLinuxBootEntry (
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;
  CONST HUMMER_LINUX_PAYLOAD_HOB *Payload;
  CONST UINT8 *Source;
  CONST VOID *Image;
  CONST VOID *DtbSource;
  CONST VOID *Initrd;
  VOID *Dtb;
  EFI_PHYSICAL_ADDRESS DtbAddress;
  UINTN DtbPages;
  VOID *PreviousDtb;
  CHAR8 BootArgs[1024];

  Status = StopMassStorage ();
  if (EFI_ERROR (Status)) {
    Print (L"Mass Storage stop failed: %r\n", Status);
  }
  Status = GetPayload (&Payload);
  if (EFI_ERROR (Status)) {
    Print (L"PAYLOAD HOB %r\n", Status);
    return Status;
  }
  Source = (CONST UINT8 *)(UINTN)Payload->SourceStart;
  if (!HummerLinuxPayloadHeaderValid (Source, Payload->SourceSize) ||
      (ScanMem8 (Payload->BuildId, sizeof (Payload->BuildId), 0) == NULL)) {
    Print (L"PAYLOAD HEADER INVALID\n");
    return EFI_INVALID_PARAMETER;
  }
  Image = Source + Payload->ImageOffset;
  DtbSource = Source + Payload->DtbOffset;
  Initrd = Source + Payload->InitrdOffset;
  Print (L"Starting Linux (%a)...\n", Payload->BuildId);
  AsciiSPrint (
    BootArgs,
    sizeof (BootArgs),
    "%a%a hummer.build_id=%a",
    mLinuxCommandLine,
    (AsciiStrnCmp (Payload->BuildId, "M7-RW-", 6) == 0) ?
      " efi=noruntime retain_initrd sd_mod.hummer_read_only=0 ufshcd_core.hummer_read_only=0 ufshcd_core.use_mcq_mode=0" :
    ((AsciiStrnCmp (Payload->BuildId, "M6-", 3) == 0) ||
     (AsciiStrnCmp (Payload->BuildId, "M7-", 3) == 0)) ?
      " efi=noruntime retain_initrd sd_mod.hummer_read_only=1 ufshcd_core.hummer_read_only=1 ufshcd_core.use_mcq_mode=0" :
      ((AsciiStrnCmp (Payload->BuildId, "M4-", 3) == 0) ? " efi=noruntime" : ""),
    Payload->BuildId
    );
  PreviousDtb = NULL;
  EfiGetSystemConfigurationTable (&gFdtTableGuid, &PreviousDtb);
  Status = PrepareDtb (DtbSource, Payload->DtbSize, &Dtb, &DtbAddress, &DtbPages, BootArgs);
  if (EFI_ERROR (Status)) {
    Print (L"DTB PREPARE %r\n", Status);
    return Status;
  }
  Status = gBS->InstallConfigurationTable (&gFdtTableGuid, Dtb);
  if (EFI_ERROR (Status)) {
    gBS->FreePages (DtbAddress, DtbPages);
    Print (L"DTB INSTALL %r\n", Status);
    return Status;
  }
  Status = InstallInitrd (Initrd, Payload->InitrdSize);
  if (EFI_ERROR (Status)) {
    gBS->InstallConfigurationTable (&gFdtTableGuid, PreviousDtb);
    gBS->FreePages (DtbAddress, DtbPages);
    Print (L"INITRD INSTALL %r\n", Status);
    return Status;
  }
  Status = StartLinuxImage (ImageHandle, Image, Payload->ImageSize, BootArgs);
  UninstallInitrd ();
  gBS->InstallConfigurationTable (&gFdtTableGuid, PreviousDtb);
  gBS->FreePages (DtbAddress, DtbPages);
  if (EFI_ERROR (Status)) {
    Print (L"Linux boot failed: %r\n", Status);
  }
  return Status;
}
