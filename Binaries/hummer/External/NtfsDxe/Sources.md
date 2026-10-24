# NTFS firmware driver

Built from [EfiFs](https://github.com/pbatard/efifs). Only the NTFS driver is included.

- efifs: `0f1b63b92f61f23fe17697d24df7164f1d13fa1f`

- grub: `f10ad08fa2a02dce95774c1b5caee38254b30531`

- gnu-efi: `3cf072ace613e4459a0308b06309f4072cf4aa36`


Apply the upstream `0001-GRUB-fixes.patch` and `0001-gnu-efi-fixes.patch` to their respective submodules, then build with:

```sh
make ARCH=aa64 CROSS_COMPILE=aarch64-linux-gnu- -j12 FILESYSTEMS=ntfs
```
