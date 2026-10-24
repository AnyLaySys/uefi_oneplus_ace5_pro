# Windows PE

Build the firmware with:

```sh
.build-venv/bin/python build_uefi.py -d hummer
```

The firmware publishes DSDT, FADT, MADT, GTDT, PPTT and DBG2 for the eight SM8750
processors, GICv3, architectural timers and PSCI through SMC. Peripheral ACPI
support remains incomplete. UFS is described as `QCOM24A5`; its Windows
driver binding and storage access still need device verification. Touch has
not been adapted.

The `Internal Storage` menu opens Linux and Windows choices. Linux continues to
use the kernel, DTB and initramfs embedded in `boot.img`. Windows first tries
the standard ARM64 fallback path `\EFI\BOOT\BOOTAA64.EFI` on non-USB filesystems,
then tries `\EFI\Microsoft\Boot\bootmgfw.efi` and
`\Windows\Boot\EFI\bootmgfw.efi` with the Windows kernel compatibility patch.
For the initial PE attempt, the target partition must contain the ARM64 PE media
files, including its EFI boot files, BCD, `Boot\boot.sdi` and `sources\boot.wim`.
An ISO or WIM alone is not a FAT32 partition image.

Create the media directory from the Windows Deployment and Imaging Tools
Environment:

```bat
copype arm64 C:\WoA\WinPE_ARM64
```

Use the contents of `C:\WoA\WinPE_ARM64\media` when preparing the FAT32 image.
Replacing `als` overwrites its existing Ubuntu filesystem. This firmware build
does not write to the phone's partitions.

Select Windows from the firmware boot menu, opened with Volume Up. Windows PE
26100 has reached its command prompt on the phone with a live kernel compatibility
patch. The bundled ARM64 NTFS driver also permits loading the Windows entry from
NTFS on `als`; NTFS boot and Windows storage access still need verification.

## USB debugging

The DBG2 table describes the Synopsys controller at `0x0A600000` as Qualcomm
KDNET device `8003:5143`, at index `0`. The Windows entry initializes the
controller through the existing UEFI USB function driver. Windows then uses its
inbox `kd_8003_5143.dll` for USB Ethernet Emulation Mode. This is a NET debugger
transport, not the xHCI Debug Capability USB transport.

The Windows entry reads the board's usable RAM ranges from the payload HOB or
the UEFI FDT configuration table and reserves conventional EFI memory outside
those ranges. If neither DTB source is present, it continues with the existing
UEFI memory map. The Linux entry is not involved in this operation.

The debug BCD enables kernel debugging with `busparams=0`,
`hostip=169.254.255.255`, DHCP disabled and UDP port `50005`. Boot Manager and
Winload boot debugging must remain disabled: they wait for a debugger before
the kernel starts the USB EEM transport, creating a connection deadlock when
the host has no EEM adapter yet. Use the matching key with WinDbg's NET
transport and connect a USB 3.0 data cable. The host should enumerate the KDNET
USB EEM adapter after the kernel starts.

Reference: https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/setting-up-kernel-mode-debugging-over-usb-eem-arm-kdnet

## Kernel compatibility

The Windows entry installs a hook for its own ExitBootServices call. It finds
Winload only in EFI loader-code allocations after the platform RAM exclusions
have been applied, then redirects its transfer through unused, zero-filled
space after an executable PE section within the existing mapped image. It
does not allocate a transfer buffer or overwrite an existing function. The stub
replaces `MRS X8, AMCNTENSET0_EL0` with `MOV X8, #0` in loaded Windows modules
and performs the required cache maintenance before entering the kernel. The
original boot-service entry is restored after installing the transfer patch or
if the Windows boot application returns. The PE image and Linux entry are not
modified by this patch.

Windows PE build 26100 reached its command prompt with the same instruction
replacement applied live through WinDbg. The persistent firmware patch still
requires confirmation after flashing. The full ARM64 Windows image has been prepared on the host with Explorer and
original OOBE. Booting it still requires working Windows storage support;
the touch driver is not yet available.
