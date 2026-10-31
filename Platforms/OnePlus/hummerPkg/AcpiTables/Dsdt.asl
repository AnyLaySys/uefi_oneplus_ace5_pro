/*
 * hummer (OnePlus Ace 5 Pro, SM8750) DSDT -- touch controller and WCN7861 bring-up.
 *
 * The touch node deliberately does NOT use SpiSerialBus/GpioInt resources:
 * none of the three kernel drivers those would require (Qualcomm GENI SPI,
 * TLMM GPIO, GIC SPI interrupt) exist for Windows on this platform. The node
 * therefore hands the driver the raw register windows it needs and lets it
 * drive the serial engine, the GPI DMA rings, the pin mux and the PMIC rails
 * itself. Addresses and pin numbers come from the board DT:
 *   sm8750.dtsi spi4                 @0x00A90000 (+0x4000)
 *   sm8750.dtsi gpi_dma1             @0x00A00000 (+0x60000)
 *   sm8750.dtsi pinctrl@f100000      (+0x102000)
 *   sm8750-oneplus-hummer-bringup.dtsi touchscreen@0:
 *       spi-max-frequency 19000000, reset tlmm 161, irq tlmm 162,
 *       vddio vreg_l4b_1p8 (1.8V), avdd vreg_l16b_2p8 (3.104V)
 * Panel geometry 1264 x 2780 (TouchComm mx12639 / my27799 in 0.1 px units).
 */
DefinitionBlock ("", "DSDT", 2, "ONEPLS", "HUMMER  ", 2)
{
    Scope (\_SB)
    {
        Device (CPU0) { Name (_HID, "ACPI0007") Name (_UID, 0) }
        Device (CPU1) { Name (_HID, "ACPI0007") Name (_UID, 1) }
        Device (CPU2) { Name (_HID, "ACPI0007") Name (_UID, 2) }
        Device (CPU3) { Name (_HID, "ACPI0007") Name (_UID, 3) }
        Device (CPU4) { Name (_HID, "ACPI0007") Name (_UID, 4) }
        Device (CPU5) { Name (_HID, "ACPI0007") Name (_UID, 5) }
        Device (CPU6) { Name (_HID, "ACPI0007") Name (_UID, 6) }
        Device (CPU7) { Name (_HID, "ACPI0007") Name (_UID, 7) }

        //
        // WCN7861 (PEACH, 17cb:110e) on PCIe0. Windows cannot enumerate the
        // DWC root complex through ECAM, so WindowsBoot (Pcie0.c) trains the
        // link, assigns BAR0 and sets up the DWC internal MSI controller, and
        // this node hands the driver the raw windows:
        //   BAR0 (Pcie0.c BAR0_BASE), endpoint config (iATU CFG0 to 01:00.0),
        //   root port DBI (iMSI-RX at +0x820), PARF.
        // Interrupt is msi0 (SPI 141) from sm8750.dtsi pcie@1c00000.
        //
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

            Name (_DSD, Package ()
            {
                ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
                Package ()
                {
                    Package () { "pci-id", 0x110E17CB },
                    Package () { "msi-vectors", 32 },
                    Package () { "gpio-wlan-en", 16 },
                    Package () { "gpio-perst", 102 }
                }
            })

            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
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

        Device (QUP4)
        {
            Name (_HID, "QCOMA900")
            Name (_UID, 4)
            Name (_CCA, 1)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00A90000, 0x00004000)
                Memory32Fixed (ReadWrite, 0x00A00000, 0x00060000)
                Memory32Fixed (ReadWrite, 0x00118000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x00152000, 0x00001000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                {
                    389
                }
            })
        }

        Device (TPD0)
        {
            Name (_HID, "SYNA3910")
            Name (_UID, 0)
            Name (_CCA, 0)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00A90000, 0x00004000)
                Memory32Fixed (ReadWrite, 0x00A00000, 0x00060000)
                Memory32Fixed (ReadWrite, 0x0F100000, 0x00102000)
                Memory32Fixed (ReadWrite, 0x00100000, 0x001F4200)
                Memory32Fixed (ReadWrite, 0x16520000, 0x00010000)
                Memory32Fixed (ReadWrite, 0x81C60000, 0x00020000)
                Memory32Fixed (ReadWrite, 0x15000000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x00160000, 0x00010000)
                Memory32Fixed (ReadWrite, 0x00160800, 0x00200000)
            })

            Name (_DSD, Package ()
            {
                ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
                Package ()
                {
                    Package () { "panel-width", 1264 },
                    Package () { "panel-height", 2780 },
                    Package () { "panel-max-x", 12639 },
                    Package () { "panel-max-y", 27799 },
                    Package () { "max-contacts", 10 },
                    Package () { "spi-frequency", 19000000 },
                    Package () { "spi-cpol", 0 },
                    Package () { "spi-cpha", 0 },
                    Package () { "serial-engine-id", 4 },
                    Package () { "gpi-instance-mask", 0x1E },
                    Package () { "gpio-spi-miso", 48 },
                    Package () { "gpio-spi-mosi", 49 },
                    Package () { "gpio-spi-clk", 50 },
                    Package () { "gpio-spi-cs", 51 },
                    Package () { "gpio-reset", 161 },
                    Package () { "gpio-irq", 162 },
                    Package () { "interrupt-spi", 357 },
                    Package () { "vddio-vrm", 0x40800 },
                    Package () { "vddio-mv", 1800 },
                    Package () { "avdd-vrm", 0x41900 },
                    Package () { "avdd-mv", 3104 }
                }
            })

            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
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

            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }
    }
}
