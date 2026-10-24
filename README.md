# OnePlus Ace 5 Pro UEFI

Target: hummer, PKR110, board 24811, SM8750-AB, 16 GB RAM.

Install build dependencies, then build the firmware and Android boot image:

```sh
./setup_env.sh -p apt
python3 build_uefi.py -d hummer -r RELEASE
```

The output is `hummer.img`. The firmware keeps Linux, Windows and mass-storage boot paths. The matching device firmware files are required under `Binaries/hummer`.
