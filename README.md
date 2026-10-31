# OnePlus Ace 5 Pro UEFI

Target: hummer, PKR110, board 24811, SM8750-AB, 16 GB RAM.

Install build dependencies, then build the firmware and Android boot image:

```sh
./setup_env.sh -p apt
python3 build_uefi.py -d hummer -r RELEASE
```

The output is `hummer.img`. The firmware keeps Linux, Windows and mass-storage boot paths. The matching device firmware files are required under `Binaries/hummer`.

## Windows ADSP firmware and quiet boot

WindowsBoot now loads the device-matched ADSP firmware from `\ADSP\` at the root of the als partition. Use uppercase `ADSP` for the directory; keep the original lowercase `adsp*` firmware filenames unchanged. The former `\hummer\adsp\` location is no longer used by new builds. Keep the complete firmware set; this directory is not a log or cache.

The ADSP file logger, empty logging functions, log calls and logging-only counters/labels have been removed. PCIe/network diagnostics are retained, and the RELEASE configuration disables generic debug printing. The Boot Menu footer is `Vol Move`; button handling and the Power/Select hint are unchanged.

After rebuilding UEFI, create the combined boot image without regenerating rootfs:

```sh
cd /home/fuck/uefi_oneplus_ace5_pro
python3 build_uefi.py -d hummer -r RELEASE
cd /home/fuck/ace5pro-ubuntu
python3 build.py -nr
```

The second command packages the already-built UEFI. Running only `build.py -nr` does not compile changed UEFI source. The directory change must be paired with the updated boot image; old images still expect the old firmware location.
