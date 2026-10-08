# OnePlus Ace 5 Pro (hummer / PKR110)

Extracted from the connected device's active B-slot `uefi` partition:

- Android build: `PKR110_16.0.0.204(CN01)`
- Qualcomm build: `BOOT.MXF.2.5.1-00265-PAKALA-1.104700.16`
- Original partition SHA-256: `2e7c5cdb942d0ede02bf9a817736840eddef9e22ba14803fcb620b08f0e5a9ac`
- Extractor: WOA-Project UEFIReader, with duplicate PDB paths preserved in GUID subdirectories.

This is an experimental bring-up port. Framebuffer debug output was observed
on the first test. The UFS-enabled image was then temporarily booted on the
PKR110. A user photograph matches the Mu BootManagerMenuApp and its registered
boot options in the compiled R3 image. A separate return to Android exposed
UFS SCSI disks and system/data mounts, which validates Android storage only.
Mu-side UFS enumeration and reads are still unconfirmed.
The hummer platform includes the UFS, DiskIo, Partition and FAT driver chain.
USB drivers are not yet dispatched.

## Driver patches

`patches.json` records original and resulting SHA-256 hashes, expected bytes,
replacement bytes, offsets and reference locations. `python3 apply_patches.py`
reproduces them without modifying the original `.efi` files.

The reference is `pagani` at Device-Binaries commit
`a1501e0c2e642d6ac0d8f52c85af2a67b75a7799`:

- ButtonsDxe: map the power key to Enter. Original text sections match.
- ClockDxe: preserve display CESTA dependencies. Original text sections match.
- SPMIDxe: skip repeated SPMI PIC initialization. Original text sections match.
- TzDxeLA: mark the TZ applet initialized. Entire original binaries match.
- PmicDxeLa: port the reference PMIC and battery initialization patches.
  Relocated sites were identified by unique instruction sequences and checked
  against the disassembly; original module data remains from this device.
- UFSDxe: wake the device instead of sleeping it, force the link out of Hibern8
  before checking its gear, and preserve error propagation. The original code
  and relocation sections match pagani exactly; the device's data is preserved.
  A new executable section holds the two position-independent trampolines.
  Unlike the reference, a failed wakeup rejoins native error handling, and the
  original power-wakeup error check is retained. Success and failure paths were
  tested with an ARM64 emulator (seven scenarios). Mu-side hardware read
  validation is pending; the `ufs-hardware-*` logs in the local
  `ace5pro-uefi/analysis` directory contain Android observations only.

Credit for the reference fixes: Gustave Monce, Kancy Joe, Robotix22 and N1kroks,
as documented in `../pagani/README.md`. Static matching does not establish that
these patches work on this hardware; real-device testing is still required.
