#define GPU_IRQ(n) Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { n }

DefinitionBlock ("", "DSDT", 2, "ONEPLS", "HUMMER  ", 2)
{
    Scope (\_SB)
    {
        OperationRegion (QCLK, SystemMemory, 0x00152008, 4)
        Field (QCLK, DWordAcc, NoLock, Preserve) { QVEN, 32 }
        Method (VOTE, 2, Serialized)
        {
            If (Arg1) { QVEN |= Arg0 }
            Else { QVEN &= ~Arg0 }
            Return (Zero)
        }

        Device (CPU0) { Name (_HID, "ACPI0007") Name (_UID, 0) }
        Device (CPU1) { Name (_HID, "ACPI0007") Name (_UID, 1) }
        Device (CPU2) { Name (_HID, "ACPI0007") Name (_UID, 2) }
        Device (CPU3) { Name (_HID, "ACPI0007") Name (_UID, 3) }
        Device (CPU4) { Name (_HID, "ACPI0007") Name (_UID, 4) }
        Device (CPU5) { Name (_HID, "ACPI0007") Name (_UID, 5) }
        Device (CPU6) { Name (_HID, "ACPI0007") Name (_UID, 6) }
        Device (CPU7) { Name (_HID, "ACPI0007") Name (_UID, 7) }

        Device (CAM0)
        {
            Name (_HID, "HMRC0906")
            Name (CQST, 0xFF)
            Method (QSST, 0, NotSerialized) { Return (CQST) }
            Name (_UID, 0)
            Name (_CCA, 0)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x0ADE0000, 0x00020000)
                Memory32Fixed (ReadWrite, 0x01760000, 0x00006000)
                Memory32Fixed (ReadWrite, 0x0AC7B000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x0ADB3000, 0x00002000)
                Memory32Fixed (ReadWrite, 0x0AD27000, 0x00003000)
                Memory32Fixed (ReadWrite, 0x0AC86000, 0x00010000)
                Memory32Fixed (ReadWrite, 0x0AC04000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x0AD90000, 0x00009000)
            })
        }

        Device (KEYS)
        {
            Name (_HID, "HMRK0001")
            Name (_UID, 0)
            Name (_CCA, 0)
            Name (_S0W, 0)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x0F150000, 0x00002000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, SharedAndWake) { 778 }
            })
            OperationRegion (KPDC, SystemMemory, 0x0B220000, 0x1004)
            Field (KPDC, DWordAcc, NoLock, Preserve)
            {
                Offset (0x10), KBEN, 32,
                Offset (0x114), KCFG, 32,
                Offset (0x1000), KVER, 32
            }
            Method (_PS0, 0, Serialized)
            {
                KCFG = (KCFG & 0xFFFFFFF8) | 4
                If ((KVER & 0xFFFFFF00) >= 0x30200) { KCFG |= 8 }
                Else { KBEN |= 2 }
            }
            Method (_PS3, 0, Serialized)
            {
                If ((KVER & 0xFFFFFF00) >= 0x30200) { KCFG &= 0xFFFFFFF7 }
                Else { KBEN &= 0xFFFFFFFD }
            }
        }

        Device (WLN0)
        {
            Name (_HID, "HMRW7861")
            Name (_UID, 0)
            Name (_CCA, 1)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x40400000, 0x00200000)
                Memory32Fixed (ReadWrite, 0x40100000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x40000000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x01C00000, 0x00003000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                {
                    173
                }
            })
        }

        Device (UFS0)
        {
            Name (_HID, "QCOM24A5")
            Name (_UID, 0)
            Name (_CCA, 1)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x01D84000, 0x1C000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                {
                    297
                }
            })

            Device (DEV0)
            {
                Name (_ADR, 8)
                Name (_RMV, 0)
            }
        }

        Device (TPD0)
        {
            Name (_HID, "SYNA3910")
            Name (_UID, 0)
            Name (_CCA, 0)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00A90000, 0x00004000)
                Memory32Fixed (ReadWrite, 0x00A14000, 0x00004000)
            })
            Method (QVOT, 1, NotSerialized) { Return (\_SB.VOTE (Arg0, One)) }
        }

        Device (SPK0)
        {
            Name (_HID, "HMRS9177")
            Name (_UID, 0)
            Name (_CCA, 0)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00990000, 0x00004000)
            })
            Method (QVOT, 1, NotSerialized) { Return (\_SB.VOTE (Arg0, One)) }
        }

        Device (IR0)
        {
            Name (_HID, "HMRI0001")
            Name (_UID, 0)
            Name (_CCA, 0)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00A94000, 0x00004000)
                Memory32Fixed (ReadWrite, 0x0F135000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x00A18000, 0x00004000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 313 }
            })
            OperationRegion (RCLK, SystemMemory, 0x00118508, 0x28)
            Field (RCLK, DWordAcc, NoLock, Preserve)
            {
                RHST, 32,
                Offset (0x10), RCMD, 32, RCFG, 32,
                Offset (0x24), RDFS, 32
            }
            Method (_PS0, 0, Serialized)
            {
                RDFS &= 0xFFFFFFFE
                \_SB.VOTE (0x083C0000, One)
                RCFG = 0x00100001
                RCMD |= One
                Local0 = 100
                While ((RCMD & One) && Local0) { Sleep (One) Local0-- }
                Local0 = 100
                While ((RHST & 0x80000000) && Local0) { Sleep (One) Local0-- }
            }
            Method (_PS3, 0, Serialized) { \_SB.VOTE (0x08000000, Zero) }
        }

        Device (NFC0)
        {
            Name (_HID, "NXP0220")
            Name (_UID, 0)
            Name (_CCA, 0)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00A80000, 0x00004000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 657 }
            })
            OperationRegion (GCC0, SystemMemory, 0x00118004, 0x18)
            Field (GCC0, DWordAcc, NoLock, Preserve)
            {
                GHST, 32,
                Offset (0x10), GCMD, 32, GCFG, 32
            }
            OperationRegion (GPIO, SystemMemory, 0x0F120000, 0x3008)
            Field (GPIO, DWordAcc, NoLock, Preserve)
            {
                SDAC, 32,
                Offset (0x1000), SCLC, 32,
                Offset (0x2000), VENC, 32, VENO, 32,
                Offset (0x3000), CLKC, 32
            }
            OperationRegion (GIRQ, SystemMemory, 0x0F14B000, 0x0C)
            Field (GIRQ, DWordAcc, NoLock, Preserve)
            {
                IRQC, 32,
                Offset (8), IRQE, 32
            }
            OperationRegion (PDC0, SystemMemory, 0x0B220000, 0x1004)
            Field (PDC0, DWordAcc, NoLock, Preserve)
            {
                Offset (0x1C), PBEN, 32,
                Offset (0x2D8), PCFG, 32,
                Offset (0x1000), PVER, 32
            }
            Method (_PS0, 0, Serialized)
            {
                \_SB.VOTE (0x00700000, One)
                GCFG = 0x00100001
                GCMD |= One
                Local0 = 100
                While ((GCMD & One) && Local0)
                {
                    Sleep (One)
                    Local0--
                }
                Local0 = 100
                While ((GHST & 0x80000000) && Local0)
                {
                    Sleep (One)
                    Local0--
                }
                SDAC = (SDAC & 0xFFFFDC00) | 0x3007
                SCLC = (SCLC & 0xFFFFDC00) | 0x3007
                CLKC &= 0xFFFFDC00
                IRQC = (IRQC & 0xFFFFDC00) | One
                VENO = Zero
                VENC = (VENC & 0xFFFFDC00) | 0x200
                Sleep (10)
                VENO = 2
                Sleep (10)
                IRQE = (IRQE & 0xFFFFFFFE) | 0x80
                PCFG = (PCFG & 0xFFFFFFF8) | 4
                If ((PVER & 0xFFFFFF00) >= 0x30200)
                {
                    PCFG |= 8
                }
                Else
                {
                    PBEN |= 0x00040000
                }
            }
            Method (_PS3, 0, Serialized)
            {
                If ((PVER & 0xFFFFFF00) >= 0x30200)
                {
                    PCFG &= 0xFFFFFFF7
                }
                Else
                {
                    PBEN &= 0xFFFBFFFF
                }
                VENO = Zero
                \_SB.VOTE (0x00400000, Zero)
            }
        }

        Device (BTH0)
        {
            Name (_HID, "HMRB7861")
            Name (_UID, 0)
            Name (_CCA, 0)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00898000, 0x00004000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                {
                    493
                }
            })
        }

        Device (USB0)
        {
            Name (_HID, "PNP0D10")
            Name (_UID, 0)
            Name (_CCA, 1)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x0A600000, 0x00008000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 165 }
            })

            OperationRegion (UCLK, SystemMemory, 0x00139000, 0x90)
            Field (UCLK, DWordAcc, NoLock, Preserve)
            {
                Offset (0x08), , 16, PWUP, 1,
                Offset (0x18), , 31, CHLT, 1,
                Offset (0x8C), , 31, AHLT, 1
            }
            OperationRegion (UCOR, SystemMemory, 0x0A60C100, 0x610)
            Field (UCOR, DWordAcc, NoLock, Preserve)
            {
                Offset (0x10), , 12, ROLE, 2,
                Offset (0x604), , 31, DRUN, 1,
                Offset (0x60C), , 22, DHLT, 1
            }

            Method (_STA, 0, NotSerialized)
            {
                If (LAnd (PWUP, LAnd (LNot (CHLT), LNot (AHLT))))
                {
                    If (LAnd (LNot (DRUN), DHLT)) { Return (0x0F) }
                }
                Return (Zero)
            }
            Method (_INI, 0, Serialized)
            {
                If (LEqual (_STA (), 0x0F)) { Store (One, ROLE) }
            }
        }

        Device (GPU0)
        {
            Name (_HID, "HMRG0830")
            Name (_UID, 0)
            Name (_CCA, 1)
            Name (ZPRD, Zero)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x03D00000, 0x000E0000)
                GPU_IRQ (332)
                GPU_IRQ (336)
                GPU_IRQ (337)
                GPU_IRQ (706)
                GPU_IRQ (710)
                GPU_IRQ (711)
            })
            Method (_STA, 0, NotSerialized)
            {
                Return (ZPRD * 0x0F)
            }
        }

        Device (BAT0)
        {
            Name (_HID, "HMRB0001")
            Name (_UID, 0)
            Name (_CCA, 0)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x0F142000, 0x00002000)
            })
        }
    }
}
