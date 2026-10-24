#ifndef HUMMER_ACPI_TABLES_H
#define HUMMER_ACPI_TABLES_H

#include <IndustryStandard/Acpi64.h>

#define HUMMER_ACPI_HEADER(Signature, Type, Revision) \
  { Signature, sizeof (Type), Revision, 0, {'O', 'N', 'E', 'P', 'L', 'S'}, \
    SIGNATURE_64 ('H', 'U', 'M', 'M', 'E', 'R', ' ', ' '), 1, \
    SIGNATURE_32 ('S', 'I', 'L', 'C'), 1 }

#endif
