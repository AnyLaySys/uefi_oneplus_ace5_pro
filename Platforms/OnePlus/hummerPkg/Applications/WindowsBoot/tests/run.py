from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
header = r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
#define STATIC static
#define IN
#define VOID void
#define TRUE 1
#define FALSE 0
#define BIT0 1U
#define BIT31 0x80000000U
#define SIZE_4KB 4096U
#define SIZE_1MB 1048576U
#define MAX_UINT32 UINT32_MAX
#define MIN(a,b) ((a)<(b)?(a):(b))
#define EFI_SUCCESS 0
#define EFI_UNSUPPORTED 1
#define EFI_ACCESS_DENIED 2
#define EFI_OUT_OF_RESOURCES 3
#define EFI_NOT_READY 4
#define EFI_DEVICE_ERROR 5
#define EFI_NOT_FOUND 6
#define EFI_ERROR(s) ((s)!=EFI_SUCCESS)
#define SIGNATURE_32(a,b,c,d) ((uint32_t)(a)|((uint32_t)(b)<<8)|((uint32_t)(c)<<16)|((uint32_t)(d)<<24))
#define DEBUG_INFO 0
#define DEBUG(args) HostDebug args
typedef uint8_t BOOLEAN;
typedef uint32_t UINT32;
typedef uintptr_t UINTN;
typedef int EFI_STATUS;
typedef int EFI_GUID;
typedef uint32_t EFI_ACPI_TABLE_VERSION;
typedef struct { UINT32 Signature; } EFI_ACPI_SDT_HEADER;
typedef struct { EFI_STATUS (*GetAcpiTable)(UINTN,EFI_ACPI_SDT_HEADER **,EFI_ACPI_TABLE_VERSION *,UINTN *); } EFI_ACPI_SDT_PROTOCOL;
typedef struct { EFI_STATUS (*LocateProtocol)(EFI_GUID *,VOID *,VOID **); } EFI_BOOT_SERVICES;
extern EFI_BOOT_SERVICES *gBS;
extern EFI_GUID gEfiAcpiSdtProtocolGuid;
UINT32 MmioRead32(UINTN address);
UINT32 MmioWrite32(UINTN address,UINT32 value);
void HostDebug(int level,const char *format,...);
EFI_STATUS HummerCameraDmaInitialize(void);
'''
with tempfile.TemporaryDirectory(prefix='hummer-camera-dma-') as directory:
    work = Path(directory)
    (work / 'Uefi.h').write_text(header)
    for name in ['IndustryStandard/Acpi.h', 'Library/BaseLib.h', 'Library/DebugLib.h',
                 'Library/IoLib.h', 'Library/UefiBootServicesTableLib.h',
                 'Protocol/AcpiSystemDescriptionTable.h']:
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include <Uefi.h>\n')
    subprocess.run(['cc', '-std=c17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-g', '-I', str(work),
                    str(root.parent / 'CameraDma.c'), str(root / 'host.c'),
                    '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
