# OnePlus Ace 5 Pro UEFI

Target: hummer, PKR110, board 24811, SM8750-AB, 16 GB RAM.

Install build dependencies, then build the firmware and Android boot image:

```sh
./setup_env.sh -p apt
python3 build_uefi.py -d hummer -r RELEASE
```

The output is `hummer.img`. The firmware keeps Linux, Windows and mass-storage boot paths. The matching device firmware files are required under `Binaries/hummer`.

## Windows ADSP firmware and quiet boot

WindowsBoot reads the Windows SYSTEM registry hive to locate the audio and GPU packages registered for the next normal boot. It uses `Select\Default` (or `Select\Current` when Default is absent), then the `ImagePath` values of the `sia9177` and `qcom_gpu` services. Package paths are resolved on the same Windows volume; DriverStore is not enumerated and INF versions are not compared. Install the complete driver packages before booting.

The reader supports modern hive cells and HvLE transactions from `SYSTEM.LOG1` and `SYSTEM.LOG2`. Clean hives are read by cell; dirty hives are recovered in memory after log hash and sequence validation. Windows files are never modified. A damaged hive without usable logs or a missing service path leaves the corresponding firmware unavailable. Registry tests run with `python3 Platforms/OnePlus/hummerPkg/Applications/WindowsBoot/tests/run-registry.py`.

ADSP firmware is loaded from the registered audio package's `ADSP` subdirectory. Both DTB and main-image authentication metadata remain allocated until main-image authentication completes.

The ADSP file logger, empty logging functions, log calls and logging-only counters/labels have been removed. PCIe/network diagnostics are retained, and the RELEASE configuration disables generic debug printing. The Boot Menu footer is `Vol Move`; button handling and the Power/Select hint are unchanged.

After rebuilding UEFI, create the combined boot image without regenerating rootfs:

```sh
cd /home/fuck/uefi_oneplus_ace5_pro
python3 build_uefi.py -d hummer -r RELEASE
cd /home/fuck/linux_oneplus_ace5_pro
python3 build.py -nr
```

The second command packages the already-built UEFI. Running only `build.py -nr` does not compile changed UEFI source.

WindowsBoot preserves the splash image already displayed by the Android bootloader. SimpleFbDxe saves the framebuffer before registering the console, and WindowsBoot restores that screen before starting the EFI loader and publishes its visible image through BGRT. The screen copy lives in RAM; no splash bitmap is embedded in the firmware. If the incoming framebuffer is black, Windows keeps its default boot logo. The behavior still needs verification on the phone.

## Windows GPU firmware

The Windows GPU driver package installs the device-matched `gen80000_zap.mbn` through its INF into DriverStore. WindowsBoot reads ZAP from the directory containing the `qcom_gpu` service's registered driver binary. Install the complete GPU package with DISM or PnPUtil before booting this UEFI.

WindowsBoot uses the shared PIL loader to authenticate ZAP through PAS 13 in the reserved `0x9B09A000` / `0x2000` region. Only successful authentication enables `ACPI\HMRG0830`; a failed or missing image leaves the GPU node hidden and Windows boot continues. The node delivers the GPU/GMU/GPUCC/SMMU aperture and all GPU, GMU and SMMU interrupts. Repeat Windows boot attempts reuse successful authentication within the same UEFI session.

## NTFS driver

`Binaries/hummer/External/NtfsDxe/NtfsDxe.efi` is built from [EfiFs](https://github.com/pbatard/efifs) at `0f1b63b92f61f23fe17697d24df7164f1d13fa1f`, GRUB at `f10ad08fa2a02dce95774c1b5caee38254b30531`, and gnu-efi at `3cf072ace613e4459a0308b06309f4072cf4aa36`. Apply EfiFs's `0001-GRUB-fixes.patch` and `0001-gnu-efi-fixes.patch` to their submodules, then apply `Binaries/hummer/External/NtfsDxe/ntfs-minimal.patch` with `patch -p1` from the EfiFs root. Build from a clean tree with:

```sh
make ARCH=aa64 CROSS_COMPILE=aarch64-linux-gnu- -j12 FILESYSTEMS=ntfs
```

Copy `src/ntfs_aa64.efi` to `Binaries/hummer/External/NtfsDxe/NtfsDxe.efi` before building UEFI. The patch retains the extension MFT record fix and NTFS compression support. It removes logging, progress callbacks, cache statistics and unused generic helpers, and links only the functions needed by this ARM64 NTFS driver.

Boot Menu and the graphics console share Inconsolata Regular from the AOSP Recovery 12 × 22 font atlas. The build embeds only the required 8-bit grayscale glyphs. A shared bitmap-font service blends their alpha values with text foreground/background colors, preserving antialiasing without a TTF or outline-font engine. Source attribution and the SIL OFL license are under `Resources/Fonts/Inconsolata`.

Keep Markdown documentation at the repository root.
