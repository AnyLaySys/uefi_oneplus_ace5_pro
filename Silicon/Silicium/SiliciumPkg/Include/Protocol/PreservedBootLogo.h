#ifndef PRESERVED_BOOT_LOGO_H_
#define PRESERVED_BOOT_LOGO_H_

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

typedef struct {
  VOID (EFIAPI *Restore)(VOID);
  // Borrowed pixels for the next BGRT update; the consumer clears this pointer.
  CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BltBuffer;
  UINT32 X, Y, Width, Height, Stride;
} EFI_PRESERVED_BOOT_LOGO_PROTOCOL;

extern EFI_GUID gPreservedBootLogoProtocolGuid;

#endif
