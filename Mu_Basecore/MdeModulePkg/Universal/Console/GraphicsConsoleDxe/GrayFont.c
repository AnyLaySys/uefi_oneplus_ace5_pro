#include <PiDxe.h>
#include <Library/BaseMemoryLib.h>
#include <Library/ConsoleFont.h>
#include <Library/DxeServicesLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

typedef struct {
  CHAR8 Magic[8];
  UINT16 Width;
  UINT16 Height;
  UINT16 Count;
  UINT16 Reserved;
} GRAY_FONT_HEADER;

STATIC GRAY_FONT_HEADER *mFont;
STATIC UINT16 *mCodes;
STATIC UINT8 *mAlpha;

STATIC UINTN
FindGlyph (IN CHAR16 Character)
{
  UINTN Index;

  for (Index = 0; Index < mFont->Count; Index++) {
    if (mCodes[Index] == Character) {
      break;
    }
  }
  return Index;
}

STATIC BOOLEAN EFIAPI
HasGlyph (IN CHAR16 Character)
{
  return FindGlyph (Character) < mFont->Count;
}

STATIC EFI_STATUS EFIAPI
DrawText (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
  IN CONST CHAR16 *String,
  IN UINTN Count,
  IN UINTN X,
  IN UINTN Y,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL Foreground,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL Background
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Buffer;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Pixel;
  EFI_STATUS Status;
  UINTN Index;
  UINTN Glyph;
  UINTN Row;
  UINTN Column;
  UINTN Width;
  UINTN Height;
  UINTN Alpha;

  if (Gop == NULL || String == NULL || X >= Gop->Mode->Info->HorizontalResolution ||
      Y >= Gop->Mode->Info->VerticalResolution) {
    return EFI_INVALID_PARAMETER;
  }
  Count = MIN (Count, (Gop->Mode->Info->HorizontalResolution - X) / CONSOLE_FONT_WIDTH);
  Width = Count * CONSOLE_FONT_WIDTH;
  Height = MIN (CONSOLE_FONT_HEIGHT, Gop->Mode->Info->VerticalResolution - Y);
  if (Width == 0) {
    return EFI_SUCCESS;
  }
  Buffer = AllocatePool (Width * Height * sizeof (*Buffer));
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  for (Index = 0; Index < Count; Index++) {
    Glyph = FindGlyph (String[Index]);
    if (Glyph == mFont->Count) {
      Glyph = FindGlyph (L'?');
    }
    for (Row = 0; Row < Height; Row++) {
      for (Column = 0; Column < CONSOLE_FONT_WIDTH; Column++) {
        Alpha = mAlpha[(Glyph * CONSOLE_FONT_HEIGHT + Row) * CONSOLE_FONT_WIDTH + Column];
        Pixel = &Buffer[Row * Width + Index * CONSOLE_FONT_WIDTH + Column];
        Pixel->Blue = (UINT8)((Alpha * Foreground.Blue + (255 - Alpha) * Background.Blue + 127) / 255);
        Pixel->Green = (UINT8)((Alpha * Foreground.Green + (255 - Alpha) * Background.Green + 127) / 255);
        Pixel->Red = (UINT8)((Alpha * Foreground.Red + (255 - Alpha) * Background.Red + 127) / 255);
        Pixel->Reserved = 0;
      }
    }
  }
  Status = Gop->Blt (Gop, Buffer, EfiBltBufferToVideo, 0, 0, X, Y, Width, Height, 0);
  FreePool (Buffer);
  return Status;
}

STATIC CONSOLE_FONT_PROTOCOL mProtocol = {HasGlyph, DrawText};
CONSOLE_FONT_PROTOCOL *mConsoleFont = &mProtocol;

EFI_STATUS
InitializeBitmapFont (IN EFI_HANDLE ImageHandle)
{
  EFI_STATUS Status;
  UINTN Size;
  UINTN Index;

  Status = GetSectionFromFv (&gConsoleFontProtocolGuid, EFI_SECTION_RAW, 0, (VOID **)&mFont, &Size);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Size < sizeof (*mFont) || CompareMem (mFont->Magic, "ICFONT01", 8) != 0 ||
      mFont->Width != CONSOLE_FONT_WIDTH || mFont->Height != CONSOLE_FONT_HEIGHT ||
      mFont->Count == 0 || mFont->Reserved != 0 ||
      Size != sizeof (*mFont) + mFont->Count * (sizeof (*mCodes) + CONSOLE_FONT_WIDTH * CONSOLE_FONT_HEIGHT)) {
    Status = EFI_COMPROMISED_DATA;
    goto Error;
  }
  mCodes = (UINT16 *)(mFont + 1);
  mAlpha = (UINT8 *)(mCodes + mFont->Count);
  for (Index = 1; Index < mFont->Count; Index++) {
    if (mCodes[Index - 1] >= mCodes[Index]) {
      Status = EFI_COMPROMISED_DATA;
      goto Error;
    }
  }
  if (!HasGlyph (L'?')) {
    Status = EFI_COMPROMISED_DATA;
    goto Error;
  }
  Status = gBS->InstallProtocolInterface (&ImageHandle, &gConsoleFontProtocolGuid, EFI_NATIVE_INTERFACE, &mProtocol);
  if (!EFI_ERROR (Status)) {
    return Status;
  }
Error:
  FreePool (mFont);
  mFont = NULL;
  return Status;
}
