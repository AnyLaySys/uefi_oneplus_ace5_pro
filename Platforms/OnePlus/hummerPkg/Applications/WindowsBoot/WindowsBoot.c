#include <Uefi.h>
#include <PiPei.h>
#include <Guid/Fdt.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MsBootPolicyLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/WindowsKernelPatchLib.h>
#include <Protocol/EFIUsbMsd.h>
#include <Protocol/BlockIo.h>
#include <Protocol/UsbFunctionIo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleFileSystem.h>
#include <BootOptions.h>
#include <LinuxPayload.h>
#include "Pcie0.h"

STATIC EFI_GUID mPayloadGuid = HUMMER_LINUX_PAYLOAD_HOB_GUID;

STATIC
EFI_STATUS
TryBootFile (
  IN EFI_HANDLE ImageHandle,
  IN EFI_HANDLE FileSystemHandle,
  IN CONST CHAR16 *FileName,
  IN BOOLEAN PatchWindows
  )
{
  EFI_STATUS Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE_PROTOCOL *Root;
  EFI_FILE_PROTOCOL *File;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_HANDLE Child;

  Status = gBS->HandleProtocol (FileSystemHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&FileSystem);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = FileSystem->OpenVolume (FileSystem, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = Root->Open (Root, &File, (CHAR16 *)FileName, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    return Status;
  }
  File->Close (File);

  Path = FileDevicePath (FileSystemHandle, FileName);
  if (Path == NULL) {
    Root->Close (Root);
    return EFI_OUT_OF_RESOURCES;
  }
  Status = gBS->LoadImage (TRUE, ImageHandle, Path, NULL, 0, &Child);
  FreePool (Path);
  if (!EFI_ERROR (Status)) {
    if (PatchWindows) {
      Status = InstallWindowsKernelPatch ();
    }
    if (!EFI_ERROR (Status)) {
      Status = gBS->StartImage (Child, NULL, NULL);
    }
    if (PatchWindows) {
      UninstallWindowsKernelPatch ();
    }
    gBS->UnloadImage (Child);
  }
  Root->Close (Root);
  return Status;
}

STATIC EFI_STATUS
ReservePlatformMemory (VOID)
{
  EFI_MEMORY_DESCRIPTOR *Map = NULL;
  EFI_MEMORY_DESCRIPTOR *Descriptor;
  EFI_PHYSICAL_ADDRESS Start;
  EFI_PHYSICAL_ADDRESS End;
  EFI_PHYSICAL_ADDRESS Limit;
  EFI_PHYSICAL_ADDRESS RamStart;
  EFI_PHYSICAL_ADDRESS RamEnd;
  EFI_HOB_GUID_TYPE *Hob;
  CONST HUMMER_LINUX_PAYLOAD_HOB *Payload;
  CONST VOID *Dtb;
  CONST UINT64 *Ranges;
  EFI_STATUS Status;
  UINTN MapSize = 0;
  UINTN BufferSize;
  UINTN DescriptorSize;
  UINTN MapKey;
  UINTN Offset;
  INT32 Node;
  INT32 Length;
  INT32 Index;
  UINT32 Version;

  Dtb = NULL;
  Hob = GetFirstGuidHob (&mPayloadGuid);
  if (Hob != NULL) {
    if (GET_GUID_HOB_DATA_SIZE (Hob) < sizeof (*Payload)) {
      return EFI_COMPROMISED_DATA;
    }
    Payload = GET_GUID_HOB_DATA (Hob);
    if (Payload->DtbOffset > Payload->SourceSize ||
        Payload->DtbSize > Payload->SourceSize - Payload->DtbOffset) {
      return EFI_COMPROMISED_DATA;
    }
    Dtb = (CONST VOID *)(UINTN)(Payload->SourceStart + Payload->DtbOffset);
    if (FdtCheckHeader (Dtb) != 0 || FdtTotalSize (Dtb) > Payload->DtbSize) {
      return EFI_COMPROMISED_DATA;
    }
  } else {
    Status = EfiGetSystemConfigurationTable (&gFdtTableGuid, (VOID **)&Dtb);
    if (EFI_ERROR (Status) || Dtb == NULL) {
      return EFI_SUCCESS;
    }
    if (FdtCheckHeader (Dtb) != 0) {
      return EFI_COMPROMISED_DATA;
    }
  }
  Node = FdtPathOffset (Dtb, "/memory@81960000");
  if (Node < 0) {
    return EFI_COMPROMISED_DATA;
  }
  Ranges = FdtGetProp (Dtb, Node, "reg", &Length);
  if (Ranges == NULL || Length <= 0 || Length % 16 != 0) {
    return EFI_COMPROMISED_DATA;
  }
  RamEnd = 0;
  for (Index = 0; Index < Length / 8; Index += 2) {
    RamStart = SwapBytes64 (ReadUnaligned64 (&Ranges[Index]));
    Limit = SwapBytes64 (ReadUnaligned64 (&Ranges[Index + 1]));
    if (RamStart < RamEnd || Limit == 0 || Limit > MAX_UINT64 - RamStart ||
        ((RamStart | Limit) & EFI_PAGE_MASK) != 0) {
      return EFI_COMPROMISED_DATA;
    }
    RamEnd = RamStart + Limit;
  }

  Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescriptorSize, &Version);
  while (Status == EFI_BUFFER_TOO_SMALL) {
    if (Map != NULL) {
      FreePool (Map);
    }
    BufferSize = MapSize + 2 * DescriptorSize;
    Map = AllocatePool (BufferSize);
    if (Map == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    MapSize = BufferSize;
    Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescriptorSize, &Version);
  }
  if (!EFI_ERROR (Status)) {
    for (Offset = 0; Offset < MapSize; Offset += DescriptorSize) {
      Descriptor = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Offset);
      if (Descriptor->Type != EfiConventionalMemory) {
        continue;
      }
      Start = Descriptor->PhysicalStart;
      End = Start + EFI_PAGES_TO_SIZE (Descriptor->NumberOfPages);
      for (Index = 0; Start < End; Index += 2) {
        Limit = End;
        RamEnd = End;
        if (Index < Length / 8) {
          RamStart = SwapBytes64 (ReadUnaligned64 (&Ranges[Index]));
          RamEnd = RamStart + SwapBytes64 (ReadUnaligned64 (&Ranges[Index + 1]));
          Limit = MIN (End, RamStart);
        }
        if (Start < Limit) {
          Status = gBS->AllocatePages (
            AllocateAddress, EfiReservedMemoryType, EFI_SIZE_TO_PAGES (Limit - Start), &Start);
          if (EFI_ERROR (Status)) {
            goto done;
          }
        }
        Start = MAX (Start, MIN (End, RamEnd));
      }
    }
  }
done:
  if (Map != NULL) {
    FreePool (Map);
  }
  return Status;
}

EFI_STATUS
EFIAPI
WindowsBootEntry (
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;
  EFI_USB_MSD_PROTOCOL *Msd;
  EFI_USBFN_IO_PROTOCOL *Usb;
  EFI_HANDLE *Handles;
  EFI_HANDLE *BlockHandles;
  CONST CHAR16 *WindowsBootFiles[] = {
    L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
    L"\\Windows\\Boot\\EFI\\bootmgfw.efi"
  };
  UINTN Count;
  UINTN BlockCount;
  UINTN Index;
  UINTN FileIndex;
  UINTN LoaderCount;
  CONST CHAR16 *LoaderName;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  BOOLEAN Generic;

  Generic = FALSE;
  LoadedImage = NULL;
  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  if (!EFI_ERROR (Status) && LoadedImage->LoadOptionsSize == sizeof (HUMMER_GENERIC_BOOT_OPTION) &&
      CompareMem (LoadedImage->LoadOptions, HUMMER_GENERIC_BOOT_OPTION, sizeof (HUMMER_GENERIC_BOOT_OPTION)) == 0) {
    Generic = TRUE;
  }

  Status = ReservePlatformMemory ();
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = gBS->LocateProtocol (&gEfiUsbMsdProtocolGuid, NULL, (VOID **)&Msd);
  if (!EFI_ERROR (Status) && Msd->StopDevice != NULL) {
    Status = Msd->StopDevice (Msd);
    if (EFI_ERROR (Status) && Status != EFI_NOT_READY) {
      return Status;
    }
  }

  Status = EfiEventGroupSignal (&gUsbControllerInitGuid);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  EfiBootManagerConnectAll ();

  Status = gBS->LocateHandleBuffer (
    ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &BlockCount, &BlockHandles);
  if (!EFI_ERROR (Status)) {
    for (Index = 0; Index < BlockCount; Index++) {
      gBS->ConnectController (BlockHandles[Index], NULL, NULL, TRUE);
    }
    FreePool (BlockHandles);
  }

  Status = gBS->LocateProtocol (&gEfiUsbfnIoProtocolGuid, NULL, (VOID **)&Usb);
  if (!EFI_ERROR (Status) && Usb != NULL) {
    Status = Usb->StartController (Usb);
    if (Status == EFI_ALREADY_STARTED) {
      Status = EFI_SUCCESS;
    }
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = gBS->LocateHandleBuffer (
    ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Pcie0Initialize (Handles, Count);
  AdspInitialize (Handles, Count);

  //
  // The Windows kernel compatibility patch is applied on both paths: the
  // generic path only differs in the loader file name, not in the patch.
  // A single call site keeps the patch argument out of the compiler's reach.
  //
  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    DevicePath = DevicePathFromHandle (Handles[Index]);
    if (DevicePath == NULL || MsBootPolicyLibIsDevicePathUsb (DevicePath)) {
      continue;
    }
    LoaderCount = Generic ? 1 : ARRAY_SIZE (WindowsBootFiles);
    for (FileIndex = 0; FileIndex < LoaderCount; FileIndex++) {
      LoaderName = Generic ? L"\\EFI\\BOOT\\BOOTAA64.EFI" : WindowsBootFiles[FileIndex];
      Status     = TryBootFile (ImageHandle, Handles[Index], LoaderName, TRUE);
      if (!EFI_ERROR (Status)) {
        break;
      }
    }
    if (!EFI_ERROR (Status)) {
      break;
    }
  }

  FreePool (Handles);
  return Status;
}
