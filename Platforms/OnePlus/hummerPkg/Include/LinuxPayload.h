#ifndef HUMMER_LINUX_PAYLOAD_H
#define HUMMER_LINUX_PAYLOAD_H

#include <Uefi.h>

#define HUMMER_LINUX_PAYLOAD_MAGIC 0x5941504C
#define HUMMER_LINUX_PAYLOAD_VERSION 1
#define HUMMER_LINUX_PAYLOAD_HEADER_SIZE 128
#define HUMMER_LINUX_PAYLOAD_BUILD_ID_SIZE 64
#define HUMMER_LINUX_PAYLOAD_HOB_GUID { 0x4A89A5D0, 0x4B10, 0x4A3B, { 0xA1, 0x5B, 0x29, 0xA1, 0xD3, 0x5B, 0x7C, 0x11 } }

#pragma pack(1)
typedef struct {
  UINT32 Magic;
  UINT32 Version;
  UINT32 HeaderSize;
  UINT32 TotalSize;
  UINT32 ImageOffset;
  UINT32 ImageSize;
  UINT32 DtbOffset;
  UINT32 DtbSize;
  UINT32 InitrdOffset;
  UINT32 InitrdSize;
  CHAR8 BuildId[HUMMER_LINUX_PAYLOAD_BUILD_ID_SIZE];
  UINT32 Reserved[6];
} HUMMER_LINUX_PAYLOAD_HEADER;
#pragma pack()

typedef struct {
  EFI_PHYSICAL_ADDRESS SourceStart;
  UINT64 SourceSize;
  UINT32 ImageOffset;
  UINT32 ImageSize;
  UINT32 DtbOffset;
  UINT32 DtbSize;
  UINT32 InitrdOffset;
  UINT32 InitrdSize;
  CHAR8 BuildId[HUMMER_LINUX_PAYLOAD_BUILD_ID_SIZE];
} HUMMER_LINUX_PAYLOAD_HOB;

typedef struct {
  BOOLEAN Valid;
  EFI_PHYSICAL_ADDRESS SourceStart;
  UINT64 SourceSize;
  HUMMER_LINUX_PAYLOAD_HEADER Header;
} HUMMER_LINUX_PAYLOAD_CAPTURE;

STATIC inline BOOLEAN
HummerLinuxPayloadRangeValid (
  IN UINT32 Offset,
  IN UINT32 Size,
  IN UINT32 HeaderSize,
  IN UINT32 TotalSize
  )
{
  return (Offset >= HeaderSize) && (Offset <= TotalSize) && (Size <= TotalSize - Offset);
}

STATIC inline BOOLEAN
HummerLinuxPayloadHeaderValid (
  IN CONST VOID *Data,
  IN UINTN AvailableSize
  )
{
  CONST HUMMER_LINUX_PAYLOAD_HEADER *Header;
  UINT32 Starts[3];
  UINT32 Ends[3];
  UINTN Index;
  UINTN Next;

  if ((Data == NULL) || (AvailableSize < HUMMER_LINUX_PAYLOAD_HEADER_SIZE)) {
    return FALSE;
  }
  Header = (CONST HUMMER_LINUX_PAYLOAD_HEADER *)Data;
  if ((Header->Magic != HUMMER_LINUX_PAYLOAD_MAGIC) ||
      (Header->Version != HUMMER_LINUX_PAYLOAD_VERSION) ||
      (Header->HeaderSize < HUMMER_LINUX_PAYLOAD_HEADER_SIZE) ||
      (Header->HeaderSize > Header->TotalSize) ||
      ((UINT64)Header->TotalSize > AvailableSize)) {
    return FALSE;
  }
  if (!HummerLinuxPayloadRangeValid (Header->ImageOffset, Header->ImageSize, Header->HeaderSize, Header->TotalSize) ||
      !HummerLinuxPayloadRangeValid (Header->DtbOffset, Header->DtbSize, Header->HeaderSize, Header->TotalSize) ||
      !HummerLinuxPayloadRangeValid (Header->InitrdOffset, Header->InitrdSize, Header->HeaderSize, Header->TotalSize)) {
    return FALSE;
  }
  Starts[0] = Header->ImageOffset;
  Starts[1] = Header->DtbOffset;
  Starts[2] = Header->InitrdOffset;
  Ends[0] = Header->ImageOffset + Header->ImageSize;
  Ends[1] = Header->DtbOffset + Header->DtbSize;
  Ends[2] = Header->InitrdOffset + Header->InitrdSize;
  for (Index = 0; Index < 3; Index++) {
    for (Next = Index + 1; Next < 3; Next++) {
      if ((Starts[Index] < Ends[Next]) && (Starts[Next] < Ends[Index])) {
        return FALSE;
      }
    }
  }
  return TRUE;
}

#endif
