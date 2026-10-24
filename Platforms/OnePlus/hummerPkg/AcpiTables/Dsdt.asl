DefinitionBlock ("", "DSDT", 2, "ONEPLS", "HUMMER  ", 1)
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
    }
}
