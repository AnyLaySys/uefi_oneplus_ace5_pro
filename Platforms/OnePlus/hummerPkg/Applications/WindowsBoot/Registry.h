#ifndef HUMMER_REGISTRY_H
#define HUMMER_REGISTRY_H

#include <Uefi.h>
#include <Protocol/SimpleFileSystem.h>

EFI_STATUS
RegistryOpenPackages (
  IN  EFI_FILE_PROTOCOL  *Root,
  OUT EFI_FILE_PROTOCOL  *Packages[2]
  );

#endif
