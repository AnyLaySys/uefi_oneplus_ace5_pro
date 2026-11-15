#ifndef CONSOLE_FONT_H_
#define CONSOLE_FONT_H_

#include <Protocol/GraphicsOutput.h>

#define CONSOLE_FONT_WIDTH 12
#define CONSOLE_FONT_HEIGHT 22

typedef struct {
  BOOLEAN (EFIAPI *HasGlyph)(IN CHAR16 Character);
  EFI_STATUS (EFIAPI *DrawText)(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
    IN CONST CHAR16 *String,
    IN UINTN Count,
    IN UINTN X,
    IN UINTN Y,
    IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL Foreground,
    IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL Background);
} CONSOLE_FONT_PROTOCOL;

extern EFI_GUID gConsoleFontProtocolGuid;

#endif
