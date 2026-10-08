  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf

  INF Binaries/hummer/QcomPkg/Drivers/EnvDxe/EnvDxeEnhanced.inf

  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf

  INF Binaries/hummer/QcomPkg/Drivers/ShmBridgeDxe/ShmBridgeDxeLA.inf
  INF Binaries/hummer/QcomPkg/Drivers/TzDxe/ScmDxeCompat.inf
  INF Binaries/hummer/QcomPkg/Drivers/TzDxe/TzDxeLA.inf

  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf

  INF Binaries/hummer/QcomPkg/Drivers/ChipInfoDxe/ChipInfoDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/PlatformInfoDxe/PlatformInfoDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/DALSYSDxe/DALSYSDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/HALIOMMUDxe/HALIOMMUDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/HWIODxe/HWIODxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/I2CDxe/I2CDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/TLMMDxe/TLMMDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/QupDxe/QupDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/PmicDxe/PmicDxeLa.inf

  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf

  INF Binaries/hummer/MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF Binaries/hummer/MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  INF FatPkg/EnhancedFatDxe/Fat.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
!if $(HUMMER_LOG_RECOVERY) != TRUE
  INF Binaries/hummer/QcomPkg/Drivers/UFSDxe/UFSDxe.inf
!endif

  INF Binaries/hummer/QcomPkg/Drivers/CmdDbDxe/CmdDbDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/PwrUtilsDxe/PwrUtilsDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/RpmhDxe/RpmhDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/NpaDxe/NpaDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/ULogDxe/ULogDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/MailboxDxe/MailboxDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/QcomScmiDxe/QcomScmiDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/VcsDxe/VcsDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/ClockDxe/ClockDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/SPMIDxe/SPMIDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/ICBDxe/ICBDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/SmemDxe/SmemDxe.inf
  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/FeatureEnablerDxe/FeatureEnablerDxe.inf

  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf

!if $(HUMMER_LOG_RECOVERY) != TRUE
  INF Binaries/hummer/QcomPkg/Drivers/ButtonsDxe/ButtonsDxe.inf
!endif
  INF Binaries/hummer/QcomPkg/Drivers/IPCCDxe/IPCCDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/DDRInfoDxe/DDRInfoDxe.inf
!if $(HUMMER_LOG_RECOVERY) != TRUE
  INF Binaries/hummer/QcomPkg/Drivers/OcdtDxe/OplusProject.inf
!endif

  INF Binaries/hummer/QcomPkg/Drivers/RNGDxe/RngDxe.inf

  INF MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/TsensDxe/TsensDxe.inf
!if $(HUMMER_LOG_RECOVERY) != TRUE
  INF Binaries/hummer/QcomPkg/Drivers/PILProxyDxe/PILProxyDxe.inf
!endif
!if $(HUMMER_LOG_RECOVERY) != TRUE
  INF Binaries/hummer/QcomPkg/Drivers/PILDxe/PILDxe.inf
!endif
!if $(HUMMER_LOG_RECOVERY) != TRUE
  INF Binaries/hummer/QcomPkg/Drivers/SPSSDxe/SPSSDxe.inf
!endif
  INF Binaries/hummer/QcomPkg/Drivers/GLinkDxe/GLinkDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/SPIDxe/SPIDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/PmicGlinkDxe/PmicGlinkDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/UsbPwrCtrlDxe/UsbPwrCtrlDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/QcomChargerDxe/QcomChargerDxeLA.inf
  INF Binaries/hummer/QcomPkg/Drivers/ChargerExDxe/ChargerExDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Dxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/XhciPciEmulationDxe/XhciPciEmulationDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/XhciDxe/XhciDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/UsbMsdDxe/UsbMsdDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/UsbDeviceDxe/UsbDeviceDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/UsbConfigDxe/UsbConfigDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/UsbInitDxe/UsbInitDxe.inf
  INF Binaries/hummer/QcomPkg/Drivers/UCDxe/UCDxe.inf
