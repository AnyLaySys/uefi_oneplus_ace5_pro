#ifndef HUMMER_PCIE0_H
#define HUMMER_PCIE0_H

#include <Uefi.h>

EFI_STATUS
Pcie0Initialize (
  IN EFI_HANDLE  *FileSystems,
  IN UINTN       FileSystemCount
  );

UINTN
CmdDbAuxLength (
  IN CONST CHAR8  *Name
  );

UINT32
CmdDbAddress (
  IN CONST CHAR8  *Name
  );

UINT16
CmdDbAuxWord (
  IN CONST CHAR8  *Name,
  IN UINTN        Index
  );

EFI_STATUS
RpmhWrite (
  IN UINT32        Address,
  IN UINTN         Count,
  IN CONST UINT32  *Offsets,
  IN CONST UINT32  *Values
  );

UINT32
QmpOpen (
  VOID
  );

BOOLEAN
QmpSend (
  IN CONST CHAR8  *Message
  );

EFI_STATUS
AdspInitialize (
  IN EFI_HANDLE  *FileSystems,
  IN UINTN       FileSystemCount
  );

#endif
