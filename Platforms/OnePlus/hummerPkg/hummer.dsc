##
#
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

[Defines]
  PLATFORM_NAME                  = hummer
  PLATFORM_GUID                  = 1AD35A35-104A-4B3F-8BAF-2BA3C7ACD9B5
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/hummerPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = hummerPkg/hummer.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0

  SOC_TYPE                       = 0

!include PakalaPkg/PakalaPkg.dsc.inc

[PcdsFixedAtBuild]
  gHummerPkgTokenSpaceGuid.PcdHummerLogRecovery|$(HUMMER_LOG_RECOVERY)
  gQcomPkgTokenSpaceGuid.PcdMassStorageFile|{ 0x35, 0x1F, 0xC9, 0x72, 0x94, 0x18, 0x2C, 0x4E, 0x84, 0x76, 0x66, 0xC7, 0x27, 0x51, 0x48, 0xD5 }

  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xA760D000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"OnePlus"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Ace 5 Pro"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"hummer"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"PKR110"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"24811"

  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1264
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2780
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"

[LibraryClasses]

  MemoryMapLib|hummerPkg/Library/MemoryMapLib/MemoryMapLib.inf

  ConfigurationMapLib|hummerPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf
  DeviceBootManagerLib|hummerPkg/Library/DeviceBootManagerLib/DeviceBootManagerLib.inf
  PlatformBootOptionsLib|hummerPkg/Library/PlatformBootOptionsLib/PlatformBootOptionsLib.inf

[Components]
  ArmPkg/Drivers/CpuDxe/CpuDxe.inf {
    <LibraryClasses>
      CpuExceptionHandlerLib|hummerPkg/Library/CpuExceptionHandlerLib/CpuExceptionHandlerLib.inf
  }
  ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf {
    <BuildOptions>
      *_CLANGPDB_AARCH64_CC_FLAGS = -DHUMMER_GIC_MASK_UNHANDLED
  }
  hummerPkg/Applications/MassStorage/MassStorage.inf
  hummerPkg/Applications/HummerLinuxBoot/HummerLinuxBoot.inf
  hummerPkg/Applications/HummerLogExport/HummerLogExport.inf

