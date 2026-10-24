.text
.balign 4
.global HummerAmuPatchStart
.global HummerAmuPatchEnd

HummerAmuPatchStart:
  add x8, x0, #0x10
  ldr x6, [x8]
  movz w5, #0xd2a8
  movk w5, #0xd53b, lsl #16
  movz w7, #0x0008
  movk w7, #0xd280, lsl #16
1:
  cmp x6, x8
  b.eq 5f
  ldr x2, [x6, #0x30]
  ldr w4, [x6, #0x40]
  add x4, x2, x4
2:
  cmp x2, x4
  b.hs 4f
  ldr w3, [x2]
  cmp w3, w5
  b.ne 3f
  str w7, [x2]
  dc cvau, x2
  dsb ish
  ic ivau, x2
  dsb ish
  isb
3:
  add x2, x2, #4
  b 2b
4:
  ldr x6, [x6]
  b 1b
5:
  ldr x9, 6f
  br x9
6:
  .quad 0
HummerAmuPatchEnd:
