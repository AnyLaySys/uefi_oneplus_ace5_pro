#define CommonCExceptionHandler HummerNativeCommonCExceptionHandler
#include "../../../../../Mu_Basecore/UefiCpuPkg/Library/CpuExceptionHandlerLib/AArch64/CpuExceptionHandlerLib.c"
#undef CommonCExceptionHandler

#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/PcdLib.h>

#include "../../../../../Silicon/Silicium/SiliciumPkg/Library/FrameBufferSerialPortLib/Font.h"

STATIC CHAR8 mCrashLines[24][64];
STATIC UINTN mCrashLineCount;
STATIC BOOLEAN mCrashActive;
STATIC CONST UINT64 mCrashFont[] = GLYPH_FONT

STATIC VOID
CrashLine (IN CONST CHAR8 *Format, ...)
{
  VA_LIST Args;

  if (mCrashLineCount >= ARRAY_SIZE (mCrashLines)) {
    return;
  }
  VA_START (Args, Format);
  AsciiVSPrint (mCrashLines[mCrashLineCount], sizeof (mCrashLines[0]), Format, Args);
  VA_END (Args);
  mCrashLineCount++;
}

STATIC VOID
DrawCrash (VOID)
{
  EFI_MEMORY_REGION_DESCRIPTOR FrameBuffer;
  UINT32 *Pixels;
  UINT64 Glyph;
  UINT32 Bits;
  UINTN Width;
  UINTN Height;
  UINTN Scale;
  UINTN Length;
  UINTN Row;
  UINTN Column;
  UINTN Character;
  UINTN X;
  UINTN Y;
  UINTN Dx;
  UINTN Dy;
  UINTN Px;
  UINTN Py;

  ZeroMem (&FrameBuffer, sizeof (FrameBuffer));
  LocateMemoryRegionByName ("Display_Reserved", &FrameBuffer);
  Width = FixedPcdGet32 (PcdFrameBufferWidth);
  Height = FixedPcdGet32 (PcdFrameBufferHeight);
  if ((FrameBuffer.Address == 0) || (Width < 128) || (Height < 256) ||
      (Width > SIZE_64KB) || (Height > SIZE_64KB) ||
      (FrameBuffer.Length < Width * Height * sizeof (*Pixels))) {
    return;
  }
  Length = 1;
  for (Row = 0; Row < mCrashLineCount; Row++) {
    Length = MAX (Length, AsciiStrLen (mCrashLines[Row]));
  }
  Scale = MIN (6, MIN ((Width - 64) / (Length * 6),
    (Height - 128) / (MAX (mCrashLineCount, 1) * 16)));
  Scale = MAX (Scale, 1);
  Pixels = (UINT32 *)(UINTN)FrameBuffer.Address;
  ZeroMem (Pixels, Width * Height * sizeof (*Pixels));
  for (Row = 0; Row < mCrashLineCount; Row++) {
    for (Column = 0; mCrashLines[Row][Column] != '\0'; Column++) {
      Character = (UINT8)mCrashLines[Row][Column];
      if ((Character < 32) || (Character >= 32 + ARRAY_SIZE (mCrashFont))) {
        Character = '?';
      }
      Glyph = mCrashFont[Character - 32];
      for (Y = 0; Y < 12; Y++) {
        Bits = (UINT32)(Glyph >> ((Y < 6 ? 32 : 0) + (Y % 6) * 5)) & 31;
        for (X = 0; X < 5; X++) {
          if ((Bits & (1U << X)) == 0) {
            continue;
          }
          Px = 32 + (Column * 6 + X) * Scale;
          Py = 64 + (Row * 16 + Y) * Scale;
          for (Dy = 0; Dy < Scale; Dy++) {
            for (Dx = 0; Dx < Scale; Dx++) {
              if ((Px + Dx < Width) && (Py + Dy < Height)) {
                Pixels[(Py + Dy) * Width + Px + Dx] = 0x00FFFFFF;
              }
            }
          }
        }
      }
    }
  }
  WriteBackDataCacheRange (Pixels, Width * Height * sizeof (*Pixels));
}

STATIC VOID
CrashImage (IN CONST CHAR8 *Label, IN UINTN Address)
{
  CHAR8 *Name;
  UINTN Base;
  UINTN HeaderSize;

  Base = 0;
  HeaderSize = 0;
  Name = GetImageName (Address, &Base, &HeaderSize);
  if (Base == 0) {
    CrashLine ("%a: %016lx", Label, (UINT64)Address);
    return;
  }
  CrashLine ("%a: %.32a", Label, Name == NULL ? "unknown image" : BaseName (Name));
  CrashLine ("Base %08lx  offset %08lx", (UINT64)Base, (UINT64)(Address - Base));
}

VOID EFIAPI
CommonCExceptionHandler (
  IN EFI_EXCEPTION_TYPE ExceptionType,
  IN OUT EFI_SYSTEM_CONTEXT SystemContext)
{
  EFI_SYSTEM_CONTEXT_AARCH64 *Context;
  UINT64 *Frame;
  UINTN FrameAddress;
  UINTN StackBase;
  UINTN StackEnd;
  UINTN Index;

  if ((ExceptionType != EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS) &&
      (ExceptionType != EXCEPT_AARCH64_SERROR)) {
    HummerNativeCommonCExceptionHandler (ExceptionType, SystemContext);
    return;
  }
  if (mCrashActive) {
    CpuDeadLoop ();
  }
  mCrashActive = TRUE;
  ArmDisableInterrupts ();
  Context = SystemContext.SystemContextAArch64;
  mCrashLineCount = 0;
  CrashLine ("ONEPLUS ACE 5 PRO FIRMWARE EXCEPTION");
  CrashLine ("Type %u  ESR %08lx", ExceptionType, Context->ESR);
  CrashLine ("FAR %016lx", Context->FAR);
  CrashLine ("ELR %016lx", Context->ELR);
  CrashLine ("SP  %016lx", Context->SP);
  CrashLine (" ");
  DrawCrash ();
  CrashImage ("FAULT", (UINTN)Context->ELR);
  CrashImage ("CALLER", (UINTN)Context->LR);
  DrawCrash ();
  StackBase = (UINTN)FixedPcdGet64 (PcdCPUCoresStackBase);
  StackEnd = StackBase + FixedPcdGet32 (PcdCPUCorePrimaryStackSize);
  FrameAddress = (UINTN)Context->FP;
  for (Index = 0; Index < 3; Index++) {
    if ((FrameAddress < StackBase) || (FrameAddress > StackEnd - 16) ||
        ((FrameAddress & 15) != 0)) {
      break;
    }
    Frame = (UINT64 *)FrameAddress;
    CrashImage ("FRAME", (UINTN)Frame[1]);
    DrawCrash ();
    if ((Frame[0] <= FrameAddress) || (Frame[0] > StackEnd - 16)) {
      break;
    }
    FrameAddress = (UINTN)Frame[0];
  }
  CrashLine ("X0 %016lx  X1 %016lx", Context->X0, Context->X1);
  CrashLine ("X2 %016lx  X3 %016lx", Context->X2, Context->X3);
  CrashLine ("X4 %016lx  X5 %016lx", Context->X4, Context->X5);
  CrashLine ("X6 %016lx  X7 %016lx", Context->X6, Context->X7);
  DrawCrash ();
  CpuDeadLoop ();
}
