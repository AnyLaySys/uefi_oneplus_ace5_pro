# OnePlus Ace 5 Pro UEFI

Target: `hummer`, model `PKR110`, board `24811`, SM8750-AB, 16 GB RAM.
Source firmware: `PKR110_16.0.0.204(CN01)`.

Build after installing dependencies. Dependency source trees are included
as ordinary directories; no Git submodule initialization is required:

```bash
python3 build_uefi.py -d hummer -r DEBUG
```

The output is `Mu-hummer.img`. Use the matching device firmware in
`Binaries/hummer`; patch definitions and source hashes are recorded there.

## Boot options

- Mass Storage exposes only the physical UFS LU0 disk through the firmware
  USB mass-storage protocol, using USB LUN 0. It contains userdata and any
  Ubuntu partition created there. Other UFS LUNs are not exported. The
  standard console shows the disk capacity and errors. USB initialization
  uses the boot manager's console connection path, as on dodge; the
  application does not signal controller initialization a second time.
- Linux starts an EFI Linux Image with its OS DT and external initrd
  supplied in the BootShim-tail payload. It keeps payload validation,
  memory reservations and restoration of the previous DT on return.

Normal boot enters the Mu menu without the former R9 UFS diagnostic page,
extra GPT/reference reads or the diagnostic key wait. Essential error output
and firmware exception diagnostics remain available.

The Linux launcher applies one fixed command-line policy: `efi=noruntime`,
`retain_initrd`, and MCQ disabled (`ufshcd_core.use_mcq_mode=0`), with the
UFS left writable. The build ID is an informational timestamp appended as
`hummer.build_id=` and no longer selects boot policy.
USB reset and recovery behavior after Linux still require hardware testing;
a successful firmware build does not establish reset reliability.

## Hardware configuration

The inherited framebuffer is 1264 x 2780 BGRA. Memory and integer
configuration were derived from the device's post-DDR XBL configuration.

- Original firmware reservation starts at `0xA7000000`.
- Mu FD: `0xA7100000`, size `0x00300000`.
- UEFI stack: `0xA760D000`, size `0x00040000`.
- DXE heap: `0xBD930000`, size `0x1A6D0000`.
- Display reservation: `0xFC800000`, size `0x02B00000`.
- Android ramoops reservation: `0x880000000`, size `0x00240000`.

`Resources/DTBs/hummer.dtb` is the Android loader DT snapshot, with runtime
initrd pointers and entropy removed. The mainline Linux OS DT is a separate
payload input. This Mu image does not replace the phone's Android firmware
partitions.
