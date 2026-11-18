#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/EFIClock.h>
#include "Pcie0.h"

#define GCC_BASE               0x00100000
#define GCC_SIZE               0x00070000
#define GCC_BRANCH_VOTE        (GCC_BASE + 0x52008)
#define GCC_PCIE0_BCR          (GCC_BASE + 0x6B000)
#define GCC_PCIE0_PIPE_MUX     (GCC_BASE + 0x6B080)
#define GCC_PCIE0_PHY_BCR      (GCC_BASE + 0x6C01C)

#define PARF_BASE              0x01C00000
#define PHY_BASE               0x01C06000
#define PARF_PHY_SIZE          0x00008000
#define DBI_BASE               0x40000000
#define ELBI_BASE              0x40000F20
#define ATU_BASE               0x40001000
#define CONF_BASE              0x40100000
#define AXI_SIZE               0x00200000

//
// BAR0 address and size must match the WLN0 _CRS in Dsdt.asl. The MEM window
// is the DT "ranges" entry, identity mapped.
//
#define BAR0_BASE              0x40400000
#define BAR0_MAX_SIZE          0x00200000
#define MEM_WINDOW_BASE        0x40300000
#define MEM_WINDOW_LIMIT       0x43FFFFFF

#define DBI_MSI_ADDR_LO        0x0820
#define DBI_MSI_ADDR_HI        0x0824
#define DBI_MSI_INTR0_ENABLE   0x0828
#define DBI_MSI_INTR0_MASK     0x082C
#define DBI_MSI_INTR0_STATUS   0x0830

#define PARF_SYS_CTRL          0x0000
#define PARF_PM_CTRL           0x0020
#define PARF_PHY_CTRL          0x0040
#define PARF_MHI_CLOCK_RESET   0x0174
#define PARF_AXI_MSTR_WR_HALT  0x01A8
#define PARF_LTSSM             0x01B0
#define PARF_DBI_BASE_V2       0x0350
#define PARF_SLV_SIZE_V2       0x0358
#define PARF_ATU_BASE          0x0634
#define PARF_DEVICE_TYPE       0x1000

#define PCS_SW_RESET           0x0400
#define PCS_STATUS1            0x0414
#define PCS_POWER_DOWN         0x0440
#define PCS_START              0x0444

#define TLMM_BASE              0x0F100000
#define GPIO_PULL_DOWN         1
#define GPIO_PULL_UP           3
#define GPIO_WLAN_EN           16
#define GPIO_PERST             102
#define GPIO_CLKREQ            103
#define GPIO_WAKE              104

#define CMD_DB_BASE            0x81C60000
#define CMD_DB_MAGIC           0x0C0330DB

#define RSC_BASE               0x16520000
#define RSC_TCS_BASE           (RSC_BASE + 0xD00)
#define RSC_TCS_STRIDE         0x2A0
#define RSC_ACTIVE_TCS         3
#define RSC_IRQ_ENABLE         0x00
#define RSC_IRQ_STATUS         0x04
#define RSC_IRQ_CLEAR          0x08
#define RSC_AMC_ENABLE         BIT16
#define RSC_AMC_TRIGGER        BIT24
#define RSC_MSGID_WRITE_ACK    (8 | BIT8 | BIT16)
#define RSC_CMD_DONE           (BIT8 | BIT16)

#define QMP_BASE               0x0C300000
#define QMP_SIZE               0x400
#define QMP_MAGIC              0x4D41494C
#define QMP_STATE_UP           0x0000FFFF
#define QMP_MSG_LEN            64
#define IPCC_SEND_ID           0x0040600C

#define PCIE0_ENDPOINT_ID      0x110E17CB

typedef struct {
  CONST CHAR8  *Name;
  UINT32       Value;
} PCIE0_NAMED_VALUE;

typedef struct {
  UINT16  Offset;
  UINT8   Value;
} PCIE0_PHY_WRITE;

typedef struct {
  UINT32  Wait;
  UINT32  Control;
  UINT32  Status;
  UINT32  Enable;
  UINT32  MsgId;
  UINT32  CmdStride;
} RSC_LAYOUT;

STATIC CONST UINT64 mPcie0Mmio[][2] = {
  { GCC_BASE,  GCC_SIZE      },
  { PARF_BASE, PARF_PHY_SIZE },
  { DBI_BASE,  AXI_SIZE      },
  { BAR0_BASE, BAR0_MAX_SIZE }
};

//
// cmd-db names follow qcom-rpmh-regulator ("smp"/"ldo" + PMIC id + index);
// voltages are the wcn7861 pwrseq and pcie0_phy supplies. A zero voltage
// marks a VRM clock that only takes an enable.
//
STATIC CONST PCIE0_NAMED_VALUE mPcie0Rails[] = {
  { "ldof1",   880  },  // PHY vdda-phy
  { "ldog3",   1200 },  // PHY vdda-pll
  { "smpf5",   876  },  // vdd, vdddig
  { "ldof3",   1800 },  // vddio
  { "ldof2",   1200 },  // vddio1p2
  { "smpd4",   876  },  // vddaon
  { "smpi7",   1312 },  // vddrfa1p2
  { "smpg3",   1860 },  // vddrfa1p8
  { "ldob8",   2800 },  // vdd-wlan-ant-share (hummer overlay, RF front end)
  { "rfclka1", 0    }   // RPMH_RF_CLK1
};

//
// Interconnect for the WCN7861 DMA path (sm8750 icc): pcie_anoc xm_pcie3 ->
// qns_pcie_mem_noc (BCM SN4) -> gem_noc qnm_pcie (BCM SH1) -> LLCC/DDR
// (SH0/MC0, keepalive). SN4 has no keepalive and nothing votes it before
// Linux's pcie-qcom "pcie-mem" path does; SH1 is an enable-mask BCM.
//
STATIC CONST PCIE0_NAMED_VALUE mPcie0Bcms[] = {
  { "SN4", 0x80 },
  { "SH1", 1    }
};

#define BCM_TCS_CMD(X, Y)  (BIT30 | BIT29 | (((X) & 0x3FFF) << 14) | ((Y) & 0x3FFF))

STATIC CONST CHAR8 *mPcie0PowerDomains[] = {
  "gcc_pcie_0_gdsc",
  "gcc_pcie_0_phy_gdsc"
};

STATIC CONST PCIE0_NAMED_VALUE mPcie0Clocks[] = {
  { "gcc_pcie_0_aux_clk",         0         },
  { "gcc_pcie_0_cfg_ahb_clk",     0         },
  { "gcc_pcie_0_mstr_axi_clk",    0         },
  { "gcc_pcie_0_slv_axi_clk",     0         },
  { "gcc_pcie_0_slv_q2a_axi_clk", 0         },
  { "gcc_ddrss_pcie_sf_qtb_clk",  0         },
  { "gcc_aggre_noc_pcie_axi_clk", 0         },
  { "gcc_cnoc_pcie_sf_axi_clk",   0         },
  { "tcsr_pcie_0_clkref_en",      0         },
  { "gcc_pcie_0_phy_rchng_clk",   100000000 }
};

STATIC CONST CHAR8 *mPcie0PdcInit[] = {
  "{class: wlan_pdc, ss: rf, res: s5f.m, enable: 1}",
  "{class: wlan_pdc, ss: rf, res: s5f.v, enable: 1}",
  "{class: wlan_pdc, ss: rf, res: s5f.v, upval: 876}",
  "{class: wlan_pdc, ss: rf, res: s5f.v, dwnval: 876}"
};

STATIC CONST PCIE0_PHY_WRITE mPcie0PhyInit[] = {
  // sm8750_qmp_gen3x2_pcie_serdes_tbl
  { 0x00C0, 0x01 }, { 0x00CC, 0x62 }, { 0x00D0, 0x02 }, { 0x0060, 0xF8 },
  { 0x0064, 0x01 }, { 0x0000, 0x93 }, { 0x0004, 0x01 }, { 0x00E0, 0x90 },
  { 0x00E4, 0x82 }, { 0x00F4, 0x07 }, { 0x0070, 0x02 }, { 0x0010, 0x02 },
  { 0x0074, 0x16 }, { 0x0014, 0x16 }, { 0x0078, 0x36 }, { 0x0018, 0x36 },
  { 0x0110, 0x08 }, { 0x00BC, 0x0A }, { 0x0120, 0x42 }, { 0x0080, 0x04 },
  { 0x0084, 0x0D }, { 0x0020, 0x0A }, { 0x0024, 0x1A }, { 0x0088, 0x41 },
  { 0x0028, 0x34 }, { 0x0090, 0xAB }, { 0x0094, 0xAA }, { 0x0098, 0x01 },
  { 0x0030, 0x55 }, { 0x0034, 0x55 }, { 0x0038, 0x01 }, { 0x0140, 0x14 },
  { 0x0164, 0x34 }, { 0x003C, 0x01 }, { 0x001C, 0x04 }, { 0x0174, 0x16 },
  { 0x01BC, 0x0F }, { 0x0170, 0xA0 },
  // tx_tbl, lane 1
  { 0x1084, 0x35 }, { 0x108C, 0x10 }, { 0x1090, 0x31 }, { 0x1094, 0x7F },
  { 0x10E4, 0x02 }, { 0x1040, 0x08 }, { 0x103C, 0x14 },
  // rx_tbl, lane 1
  { 0x13A4, 0x38 }, { 0x12DC, 0x11 }, { 0x1360, 0xBF }, { 0x1364, 0xBF },
  { 0x1368, 0xB7 }, { 0x136C, 0xEA }, { 0x135C, 0x3F }, { 0x1374, 0x09 },
  { 0x1378, 0x49 }, { 0x137C, 0x1B }, { 0x1380, 0x9C }, { 0x1370, 0xD1 },
  { 0x1388, 0x09 }, { 0x138C, 0x49 }, { 0x1390, 0x1B }, { 0x1394, 0x9C },
  { 0x1384, 0xD1 }, { 0x12C4, 0x3E }, { 0x12C8, 0x1E }, { 0x12CC, 0xD2 },
  { 0x1208, 0x09 }, { 0x1214, 0x05 }, { 0x124C, 0x08 }, { 0x1250, 0x08 },
  { 0x12D8, 0x09 }, { 0x1318, 0x1C }, { 0x131C, 0x60 }, { 0x12F8, 0x07 },
  { 0x13F8, 0x08 },
  // tx_tbl, lane 2
  { 0x1884, 0x35 }, { 0x188C, 0x10 }, { 0x1890, 0x31 }, { 0x1894, 0x7F },
  { 0x18E4, 0x02 }, { 0x1840, 0x08 }, { 0x183C, 0x14 },
  // rx_tbl, lane 2
  { 0x1BA4, 0x38 }, { 0x1ADC, 0x11 }, { 0x1B60, 0xBF }, { 0x1B64, 0xBF },
  { 0x1B68, 0xB7 }, { 0x1B6C, 0xEA }, { 0x1B5C, 0x3F }, { 0x1B74, 0x09 },
  { 0x1B78, 0x49 }, { 0x1B7C, 0x1B }, { 0x1B80, 0x9C }, { 0x1B70, 0xD1 },
  { 0x1B88, 0x09 }, { 0x1B8C, 0x49 }, { 0x1B90, 0x1B }, { 0x1B94, 0x9C },
  { 0x1B84, 0xD1 }, { 0x1AC4, 0x3E }, { 0x1AC8, 0x1E }, { 0x1ACC, 0xD2 },
  { 0x1A08, 0x09 }, { 0x1A14, 0x05 }, { 0x1A4C, 0x08 }, { 0x1A50, 0x08 },
  { 0x1AD8, 0x09 }, { 0x1B18, 0x1C }, { 0x1B1C, 0x60 }, { 0x1AF8, 0x07 },
  { 0x1BF8, 0x08 },
  // pcs_tbl
  { 0x04DC, 0x05 }, { 0x0588, 0x77 }, { 0x0598, 0x0B }, { 0x05E0, 0x0F },
  { 0x05D0, 0x8C }, { 0x0568, 0x17 }, { 0x0570, 0x2E },
  // pcs_misc_tbl
  { 0x08A4, 0x1E }, { 0x08F4, 0x27 }, { 0x080C, 0x1D }, { 0x0814, 0x07 },
  { 0x0820, 0xC1 }, { 0x0894, 0x00 },
};

STATIC CONST RSC_LAYOUT mRscV2 = { 0x10, 0x14, 0x18, 0x1C, 0x30, 20 };
STATIC CONST RSC_LAYOUT mRscV3 = { 0x20, 0x24, 0x28, 0x2C, 0x34, 24 };

STATIC BOOLEAN          mQmpOpen;

STATIC
EFI_STATUS
Pcie0MapMmio (
  IN EFI_PHYSICAL_ADDRESS  Base,
  IN UINT64                Length
  )
{
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  Descriptor;
  EFI_STATUS                       Status;

  Status = gDS->GetMemorySpaceDescriptor (Base, &Descriptor);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if ((Descriptor.GcdMemoryType == EfiGcdMemoryTypeMemoryMappedIo) &&
      (Descriptor.BaseAddress + Descriptor.Length >= Base + Length) &&
      ((Descriptor.Attributes & EFI_MEMORY_UC) != 0))
  {
    return EFI_SUCCESS;
  }
  if (Descriptor.GcdMemoryType == EfiGcdMemoryTypeNonExistent) {
    Status = gDS->AddMemorySpace (EfiGcdMemoryTypeMemoryMappedIo, Base, Length, EFI_MEMORY_UC);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  return gDS->SetMemorySpaceAttributes (Base, Length, EFI_MEMORY_UC);
}

STATIC
VOID
Pcie0ConfigureGpio (
  IN UINT32   Pin,
  IN UINT32   Function,
  IN UINT32   Pull,
  IN BOOLEAN  Output,
  IN BOOLEAN  High
  )
{
  UINTN   Control;
  UINT32  Value;

  Control = TLMM_BASE + Pin * SIZE_4KB;
  if (Output) {
    MmioWrite32 (Control + 4, High ? BIT1 : 0);
  }
  Value = MmioRead32 (Control) & ~(UINT32)(0x3 | (0xF << 2) | (0x7 << 6) | BIT9);
  MmioWrite32 (Control, Value | Pull | (Function << 2) | (Output ? BIT9 : 0));
}

STATIC
UINTN
CmdDbEntry (
  IN  CONST CHAR8  *Name,
  OUT UINTN        *Header OPTIONAL
  )
{
  UINT32  Id[2];
  UINTN   Slave;
  UINTN   Slv;
  UINTN   Entry;
  UINTN   Count;

  if (MmioRead32 (CMD_DB_BASE + 4) != CMD_DB_MAGIC) {
    return 0;
  }
  ZeroMem (Id, sizeof (Id));
  CopyMem (Id, Name, MIN (AsciiStrLen (Name), sizeof (Id)));
  for (Slave = 0; Slave < 8; Slave++) {
    Slv = CMD_DB_BASE + 8 + Slave * 16;
    if (MmioRead16 (Slv) == 0) {
      break;
    }
    Entry = CMD_DB_BASE + 144 + MmioRead16 (Slv + 2);
    for (Count = MmioRead16 (Slv + 6); Count > 0; Count--, Entry += 24) {
      if ((MmioRead32 (Entry) == Id[0]) && (MmioRead32 (Entry + 4) == Id[1])) {
        if (Header != NULL) {
          *Header = Slv;
        }
        return Entry;
      }
    }
  }
  return 0;
}

UINTN
CmdDbAuxLength (
  IN CONST CHAR8  *Name
  )
{
  UINTN  Entry;

  Entry = CmdDbEntry (Name, NULL);
  return (Entry == 0) ? 0 : MmioRead16 (Entry + 20);
}

UINT32
CmdDbAddress (
  IN CONST CHAR8  *Name
  )
{
  UINTN  Entry;

  Entry = CmdDbEntry (Name, NULL);
  return (Entry == 0) ? 0 : MmioRead32 (Entry + 16);
}

UINT16
CmdDbAuxWord (
  IN CONST CHAR8  *Name,
  IN UINTN        Index
  )
{
  UINTN  Entry;
  UINTN  Header;

  Entry = CmdDbEntry (Name, &Header);
  if ((Entry == 0) || ((Index + 1) * 2 > MmioRead16 (Entry + 20))) {
    return 0;
  }
  return MmioRead16 (CMD_DB_BASE + 144 + MmioRead16 (Header + 4) + MmioRead16 (Entry + 22) + Index * 2);
}

STATIC
EFI_STATUS
RscWriteSync (
  IN UINTN   Address,
  IN UINT32  Value
  )
{
  UINTN  Retry;

  MmioWrite32 (Address, Value);
  for (Retry = 0; Retry < 1000; Retry++) {
    if (MmioRead32 (Address) == Value) {
      return EFI_SUCCESS;
    }
    gBS->Stall (1);
  }
  return EFI_DEVICE_ERROR;
}

//
// One active-mode request on the apps RSC DRV2, polled to completion, as the
// S3910 touch driver already does for its rails (layout from rpmh-rsc.c).
//
EFI_STATUS
RpmhWrite (
  IN UINT32        Address,
  IN UINTN         Count,
  IN CONST UINT32  *Offsets,
  IN CONST UINT32  *Values
  )
{
  CONST RSC_LAYOUT  *Layout;
  EFI_STATUS        Status;
  EFI_TPL           Tpl;
  UINTN             Pass;
  UINTN             Tcs;
  UINTN             Base;
  UINTN             Cmd;
  UINTN             Retry;
  UINT32            TcsStatus;
  UINT32            IrqEnable;
  UINT32            Mask;
  BOOLEAN           Done;

  Layout = (((MmioRead32 (RSC_BASE) >> 16) & 0xFF) >= 3) ? &mRscV3 : &mRscV2;
  Mask   = (UINT32)((1U << Count) - 1);
  Tpl    = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  Base   = 0;
  Tcs    = RSC_ACTIVE_TCS;
  //
  // DRV_STATUS != 0 is the hardware idle indication. Completed AMC requests
  // leave AMC_ENABLE and the enable/wait masks behind, so a clean TCS is only
  // preferred, not required.
  //
  for (Pass = 0; (Pass < 2) && (Tcs == RSC_ACTIVE_TCS); Pass++) {
    for (Tcs = 0; Tcs < RSC_ACTIVE_TCS; Tcs++) {
      Base      = RSC_TCS_BASE + Tcs * RSC_TCS_STRIDE;
      TcsStatus = MmioRead32 (Base + Layout->Status);
      if ((TcsStatus != 0) && (TcsStatus != MAX_UINT32) &&
          ((Pass == 1) ||
           ((MmioRead32 (Base + Layout->Control) == 0) && (MmioRead32 (Base + Layout->Enable) == 0) &&
            (MmioRead32 (Base + Layout->Wait) == 0) &&
            ((MmioRead32 (RSC_TCS_BASE + RSC_IRQ_STATUS) & (1U << Tcs)) == 0))))
      {
        break;
      }
    }
  }
  if (Tcs == RSC_ACTIVE_TCS) {
    gBS->RestoreTPL (Tpl);
    return EFI_NOT_READY;
  }

  IrqEnable = MmioRead32 (RSC_TCS_BASE + RSC_IRQ_ENABLE);
  Status    = RscWriteSync (RSC_TCS_BASE + RSC_IRQ_ENABLE, IrqEnable & ~(1U << Tcs));
  MmioWrite32 (RSC_TCS_BASE + RSC_IRQ_CLEAR, 1U << Tcs);
  MmioWrite32 (Base + Layout->Control, MmioRead32 (Base + Layout->Control) & ~RSC_AMC_TRIGGER);
  if (!EFI_ERROR (Status)) {
    Status = RscWriteSync (Base + Layout->Control, 0);
  }
  if (!EFI_ERROR (Status)) {
    Status = RscWriteSync (Base + Layout->Enable, 0);
  }
  for (Cmd = 0; !EFI_ERROR (Status) && (Cmd < Count); Cmd++) {
    Status = RscWriteSync (Base + Layout->MsgId + Cmd * Layout->CmdStride, RSC_MSGID_WRITE_ACK);
    if (!EFI_ERROR (Status)) {
      Status = RscWriteSync (Base + Layout->MsgId + 4 + Cmd * Layout->CmdStride, Address + Offsets[Cmd]);
    }
    if (!EFI_ERROR (Status)) {
      Status = RscWriteSync (Base + Layout->MsgId + 8 + Cmd * Layout->CmdStride, Values[Cmd]);
    }
  }
  if (!EFI_ERROR (Status)) {
    Status = RscWriteSync (Base + Layout->Wait, Mask);
  }
  if (!EFI_ERROR (Status)) {
    Status = RscWriteSync (Base + Layout->Enable, Mask);
  }
  if (!EFI_ERROR (Status)) {
    Status = RscWriteSync (Base + Layout->Control, RSC_AMC_ENABLE);
  }
  if (!EFI_ERROR (Status)) {
    MmioWrite32 (Base + Layout->Control, RSC_AMC_ENABLE | RSC_AMC_TRIGGER);
    Done = FALSE;
    for (Retry = 0; (Retry < 100) && !Done; Retry++) {
      gBS->Stall (1000);
      Done = MmioRead32 (Base + Layout->Status) != 0;
      for (Cmd = 0; Cmd < Count; Cmd++) {
        if ((MmioRead32 (Base + Layout->MsgId + 12 + Cmd * Layout->CmdStride) & RSC_CMD_DONE) != RSC_CMD_DONE) {
          Done = FALSE;
        }
      }
    }
    Status = Done ? EFI_SUCCESS : EFI_TIMEOUT;
    }

  RscWriteSync (Base + Layout->Control, RSC_AMC_ENABLE);
  RscWriteSync (Base + Layout->Control, 0);
  RscWriteSync (Base + Layout->Enable, 0);
  RscWriteSync (Base + Layout->Wait, 0);
  MmioWrite32 (RSC_TCS_BASE + RSC_IRQ_CLEAR, 1U << Tcs);
  RscWriteSync (RSC_TCS_BASE + RSC_IRQ_ENABLE, IrqEnable);
  gBS->RestoreTPL (Tpl);
  return Status;
}

STATIC
BOOLEAN
QmpWait (
  IN UINTN   Address,
  IN UINT32  Value
  )
{
  UINTN  Retry;

  for (Retry = 0; Retry < 1000; Retry++) {
    if (MmioRead32 (Address) == Value) {
      return TRUE;
    }
    gBS->Stall (1000);
  }
  return FALSE;
}

UINT32
QmpOpen (
  VOID
  )
{
  if (mQmpOpen) {
    return 0;
  }
  if ((MmioRead32 (QMP_BASE) != QMP_MAGIC) || (MmioRead32 (QMP_BASE + 0x04) != 1)) {
    return 1;
  }
  if ((MmioRead32 (QMP_BASE + 0x34) < sizeof (UINT32) + QMP_MSG_LEN) ||
      (QMP_BASE + MmioRead32 (QMP_BASE + 0x38) + sizeof (UINT32) + QMP_MSG_LEN > QMP_BASE + QMP_SIZE))
  {
    return 2;
  }
  MmioWrite32 (QMP_BASE + 0x10, MmioRead32 (QMP_BASE + 0x0C));
  MmioWrite32 (QMP_BASE + 0x24, QMP_STATE_UP);
  MmioWrite32 (IPCC_SEND_ID, 0);
  if (!QmpWait (QMP_BASE + 0x28, QMP_STATE_UP)) {
    return 3;
  }
  MmioWrite32 (QMP_BASE + 0x2C, QMP_STATE_UP);
  MmioWrite32 (IPCC_SEND_ID, 0);
  if (!QmpWait (QMP_BASE + 0x14, QMP_STATE_UP)) {
    return 4;
  }
  MmioWrite32 (QMP_BASE + 0x18, QMP_STATE_UP);
  MmioWrite32 (IPCC_SEND_ID, 0);
  if (!QmpWait (QMP_BASE + 0x30, QMP_STATE_UP)) {
    return 5;
  }
  mQmpOpen = TRUE;
  return 0;
}

BOOLEAN
QmpSend (
  IN CONST CHAR8  *Message
  )
{
  UINT32  Buffer[QMP_MSG_LEN / sizeof (UINT32)];
  UINTN   Mailbox;
  UINTN   Word;

  if (!mQmpOpen) {
    return FALSE;
  }
  Mailbox = QMP_BASE + MmioRead32 (QMP_BASE + 0x38);
  ZeroMem (Buffer, sizeof (Buffer));
  AsciiStrCpyS ((CHAR8 *)Buffer, sizeof (Buffer), Message);
  for (Word = 0; Word < ARRAY_SIZE (Buffer); Word++) {
    MmioWrite32 (Mailbox + sizeof (UINT32) * (Word + 1), Buffer[Word]);
  }
  MmioWrite32 (Mailbox, QMP_MSG_LEN);
  MmioRead32 (Mailbox);
  MmioWrite32 (IPCC_SEND_ID, 0);
  if (!QmpWait (Mailbox, 0)) {
    MmioWrite32 (Mailbox, 0);
    return FALSE;
  }
  return TRUE;
}

STATIC
VOID
QmpSendPdcInit (
  VOID
  )
{
  UINTN   Index;

  if (QmpOpen () != 0) {
    return;
  }
  for (Index = 0; Index < ARRAY_SIZE (mPcie0PdcInit); Index++) {
    if (!QmpSend (mPcie0PdcInit[Index])) {
      return;
    }
  }
}

STATIC
UINT32
Pcie0FindCapability (
  IN UINT8  CapabilityId
  )
{
  UINT32  Offset;
  UINT32  Header;
  UINTN   Count;

  Offset = MmioRead32 (DBI_BASE + 0x34) & 0xFC;
  for (Count = 0; (Offset >= 0x40) && (Count < 48); Count++) {
    Header = MmioRead32 (DBI_BASE + Offset);
    if ((Header & 0xFF) == CapabilityId) {
      return Offset;
    }
    Offset = (Header >> 8) & 0xFC;
  }
  return 0;
}

//
// Leaves the endpoint ready for the WLN0 driver: BAR0 at the fixed address its
// _CRS declares, an outbound MEM iATU window, the root port bridge windows and
// the DWC internal MSI controller (msi0, SPI 141) targeting a reserved page.
// Bus mastering on the endpoint is left to the driver. Returns 0 or the
// failing step.
//
STATIC
UINT32
Pcie0AssignResources (
  VOID
  )
{
  EFI_PHYSICAL_ADDRESS  MsiTarget;
  EFI_STATUS            Status;
  UINT32                Bar;
  UINT64                Mask;
  UINT64                Size;
  UINTN                 Retry;

  Bar = MmioRead32 (CONF_BASE + 0x10);
  MmioWrite32 (CONF_BASE + 0x10, MAX_UINT32);
  Mask = MmioRead32 (CONF_BASE + 0x10) & ~(UINT64)0xF;
  if ((Bar & 0x6) == 0x4) {
    MmioWrite32 (CONF_BASE + 0x14, MAX_UINT32);
    Mask |= LShiftU64 (MmioRead32 (CONF_BASE + 0x14), 32);
  } else {
    Mask |= 0xFFFFFFFF00000000ULL;
  }
  Size                      = ~Mask + 1;
  if (((Bar & BIT0) != 0) || (Size == 0) || (Size > BAR0_MAX_SIZE)) {
    return 1;
  }
  MmioWrite32 (CONF_BASE + 0x10, BAR0_BASE);
  if ((Bar & 0x6) == 0x4) {
    MmioWrite32 (CONF_BASE + 0x14, 0);
  }

  //
  // Outbound iATU region 1: MEM, identity mapped.
  //
  MmioWrite32 (ATU_BASE + 0x200 + 0x04, 0);
  MmioWrite32 (ATU_BASE + 0x200 + 0x08, MEM_WINDOW_BASE);
  MmioWrite32 (ATU_BASE + 0x200 + 0x0C, 0);
  MmioWrite32 (ATU_BASE + 0x200 + 0x10, MEM_WINDOW_LIMIT);
  MmioWrite32 (ATU_BASE + 0x200 + 0x14, MEM_WINDOW_BASE);
  MmioWrite32 (ATU_BASE + 0x200 + 0x18, 0);
  MmioWrite32 (ATU_BASE + 0x200 + 0x00, 0);
  MmioWrite32 (ATU_BASE + 0x200 + 0x04, BIT31);
  for (Retry = 0; (Retry < 5) && ((MmioRead32 (ATU_BASE + 0x200 + 0x04) & BIT31) == 0); Retry++) {
    gBS->Stall (1000);
  }
  if (Retry == 5) {
    return 2;
  }

  //
  // Root port: I/O and prefetchable windows closed, non-prefetchable window
  // covering the MEM range, memory decode and bus mastering on.
  //
  MmioWrite32 (DBI_BASE + 0x1C, 0x000000F0);
  MmioWrite32 (DBI_BASE + 0x20, ((MEM_WINDOW_LIMIT >> 16) & 0xFFF0) << 16 | ((MEM_WINDOW_BASE >> 16) & 0xFFF0));
  MmioWrite32 (DBI_BASE + 0x24, 0x0000FFF0);
  MmioWrite32 (DBI_BASE + 0x28, 0);
  MmioWrite32 (DBI_BASE + 0x2C, 0);
  MmioWrite32 (DBI_BASE + 0x04, (MmioRead32 (DBI_BASE + 0x04) & 0xFFFF) | BIT1 | BIT2);
  MmioWrite32 (CONF_BASE + 0x04, (MmioRead32 (CONF_BASE + 0x04) & 0xFFFF) | BIT1);

  MsiTarget = MAX_UINT32;
  Status    = gBS->AllocatePages (AllocateMaxAddress, EfiReservedMemoryType, 1, &MsiTarget);
  if (EFI_ERROR (Status)) {
    return 3;
  }
  MmioWrite32 (DBI_BASE + DBI_MSI_ADDR_LO, (UINT32)MsiTarget);
  MmioWrite32 (DBI_BASE + DBI_MSI_ADDR_HI, 0);
  MmioWrite32 (DBI_BASE + DBI_MSI_INTR0_MASK, 0);
  MmioWrite32 (DBI_BASE + DBI_MSI_INTR0_STATUS, MAX_UINT32);
  MmioWrite32 (DBI_BASE + DBI_MSI_INTR0_ENABLE, MAX_UINT32);

  return 0;
}

EFI_STATUS
Pcie0Initialize (
  VOID
  )
{
  STATIC CONST UINT32  VrmOffsets[] = { 0x0, 0x8, 0x4 };  // voltage, mode, enable
  STATIC CONST UINT32  VrmClockOffset = 0x4;
  STATIC CONST UINT32  BcmOffset      = 0x0;
  EFI_CLOCK_PROTOCOL   *Clock;
  EFI_STATUS           Status;
  UINT32               Values[3];
  UINT32               Address;
  UINT32               Capability;
  UINT32               Vendor;
  UINT32               Value;
  UINT32               Freq;
  UINTN                Index;
  UINTN                Id;
  UINTN                Retry;

  for (Index = 0; Index < ARRAY_SIZE (mPcie0Mmio); Index++) {
    Status = Pcie0MapMmio (mPcie0Mmio[Index][0], mPcie0Mmio[Index][1]);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Pcie0ConfigureGpio (GPIO_PERST, 0, GPIO_PULL_DOWN, TRUE, FALSE);
  Pcie0ConfigureGpio (GPIO_CLKREQ, 1, GPIO_PULL_UP, FALSE, FALSE);
  Pcie0ConfigureGpio (GPIO_WAKE, 0, GPIO_PULL_UP, FALSE, FALSE);
  Pcie0ConfigureGpio (GPIO_WLAN_EN, 0, GPIO_PULL_DOWN, TRUE, FALSE);

  QmpSendPdcInit ();
  for (Index = 0; Index < ARRAY_SIZE (mPcie0Rails); Index++) {
    Address = CmdDbAddress (mPcie0Rails[Index].Name);
    if (Address == 0) {
      Status = EFI_NOT_FOUND;
    } else if (mPcie0Rails[Index].Value == 0) {
      Values[0] = 1;
      Status    = RpmhWrite (Address, 1, &VrmClockOffset, Values);
    } else {
      Values[0] = mPcie0Rails[Index].Value;
      Values[1] = 7;  // PMIC5 LDO HPM / SMPS PWM
      Values[2] = 1;
      Status    = RpmhWrite (Address, ARRAY_SIZE (Values), VrmOffsets, Values);
    }
  }

  for (Index = 0; Index < ARRAY_SIZE (mPcie0Bcms); Index++) {
    Address = CmdDbAddress (mPcie0Bcms[Index].Name);
    if (Address == 0) {
      Status = EFI_NOT_FOUND;
    } else {
      Values[0] = BCM_TCS_CMD (mPcie0Bcms[Index].Value, mPcie0Bcms[Index].Value);
      Status    = RpmhWrite (Address, 1, &BcmOffset, Values);
    }
  }

  Pcie0ConfigureGpio (GPIO_WLAN_EN, 0, GPIO_PULL_DOWN, TRUE, TRUE);
  gBS->Stall (50000);

  Status = gBS->LocateProtocol (&gEfiClockProtocolGuid, NULL, (VOID **)&Clock);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  for (Index = 0; Index < ARRAY_SIZE (mPcie0PowerDomains); Index++) {
    Status = Clock->GetClockPowerDomainID (Clock, mPcie0PowerDomains[Index], &Id);
    if (!EFI_ERROR (Status)) {
      Status = Clock->EnableClockPowerDomain (Clock, Id);
    }
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  MmioOr32 (GCC_PCIE0_PHY_BCR, BIT0);
  gBS->Stall (300);
  MmioAnd32 (GCC_PCIE0_PHY_BCR, ~(UINT32)BIT0);
  for (Index = 0; Index < ARRAY_SIZE (mPcie0Clocks); Index++) {
    Freq   = 0;
    Status = Clock->GetClockID (Clock, mPcie0Clocks[Index].Name, &Id);
    if (!EFI_ERROR (Status) && (mPcie0Clocks[Index].Value != 0)) {
      Status = Clock->SetClockFreqHz (Clock, Id, mPcie0Clocks[Index].Value, EFI_CLOCK_FREQUENCY_HZ_CLOSEST, &Freq);
    }
    if (!EFI_ERROR (Status)) {
      Status = Clock->EnableClock (Clock, Id);
    }
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  MmioOr32 (GCC_PCIE0_BCR, BIT0);
  gBS->Stall (1000);
  MmioAnd32 (GCC_PCIE0_BCR, ~(UINT32)BIT0);
  gBS->Stall (1000);
  //
  // qcom_pcie_init_2_7_0
  //
  MmioWrite32 (PARF_BASE + PARF_DEVICE_TYPE, 0x4);
  MmioAnd32 (PARF_BASE + PARF_PHY_CTRL, ~(UINT32)BIT0);
  MmioWrite32 (PARF_BASE + PARF_DBI_BASE_V2, DBI_BASE);
  MmioWrite32 (PARF_BASE + PARF_DBI_BASE_V2 + 4, 0);
  MmioWrite32 (PARF_BASE + PARF_ATU_BASE, ATU_BASE);
  MmioWrite32 (PARF_BASE + PARF_ATU_BASE + 4, 0);
  MmioWrite32 (PARF_BASE + PARF_SLV_SIZE_V2, 0);
  MmioWrite32 (PARF_BASE + PARF_SLV_SIZE_V2 + 4, 0x80000000);
  MmioAnd32 (PARF_BASE + PARF_SYS_CTRL, ~(UINT32)BIT29);
  MmioOr32 (PARF_BASE + PARF_MHI_CLOCK_RESET, BIT4);
  MmioAnd32 (PARF_BASE + PARF_PM_CTRL, ~(UINT32)BIT5);
  MmioOr32 (PARF_BASE + PARF_AXI_MSTR_WR_HALT, BIT31);
  //
  // qmp_pcie_power_on, pipe clock source switched to the PHY
  //
  MmioOr32 (PHY_BASE + PCS_POWER_DOWN, 0x3);
  for (Index = 0; Index < ARRAY_SIZE (mPcie0PhyInit); Index++) {
    MmioWrite32 (PHY_BASE + mPcie0PhyInit[Index].Offset, mPcie0PhyInit[Index].Value);
  }
  MmioAnd32 (GCC_PCIE0_PIPE_MUX, ~(UINT32)0x3);
  MmioOr32 (GCC_BRANCH_VOTE, BIT4);
  MmioAnd32 (PHY_BASE + PCS_SW_RESET, ~(UINT32)BIT0);
  MmioOr32 (PHY_BASE + PCS_START, 0x3);
  gBS->Stall (1000);
  for (Retry = 0; Retry < 100; Retry++) {
    Value = MmioRead32 (PHY_BASE + PCS_STATUS1);
    if ((Value & BIT6) == 0) {
      break;
    }
    gBS->Stall (200);
  }
  if (Retry == 100) {
    return EFI_TIMEOUT;
  }

  Capability                    = Pcie0FindCapability (0x10);
  if (Capability == 0) {
    return EFI_DEVICE_ERROR;
  }

  //
  // Bring-up trains at 2.5 GT/s; bus 1 sits behind the root port.
  //
  MmioAndThenOr32 (DBI_BASE + Capability + 0x30, ~(UINT32)0xF, 1);
  MmioWrite32 (DBI_BASE + 0x18, 0x00010100);
  gBS->Stall (100000);
  Pcie0ConfigureGpio (GPIO_PERST, 0, GPIO_PULL_DOWN, TRUE, TRUE);
  gBS->Stall (1000);
  MmioOr32 (PARF_BASE + PARF_LTSSM, BIT8);
  for (Retry = 0; Retry < 100; Retry++) {
    gBS->Stall (10000);
    Value = MmioRead32 (DBI_BASE + Capability + 0x10);
    if ((Value & BIT29) != 0) {
      break;
    }
  }
  if (Retry == 100) {
    return EFI_TIMEOUT;
  }

  //
  // Outbound iATU region 0: CFG0 to bus 1, device 0, function 0.
  //
  gBS->Stall (100000);
  MmioWrite32 (ATU_BASE + 0x04, 0);
  MmioWrite32 (ATU_BASE + 0x08, CONF_BASE);
  MmioWrite32 (ATU_BASE + 0x0C, 0);
  MmioWrite32 (ATU_BASE + 0x10, CONF_BASE + SIZE_4KB - 1);
  MmioWrite32 (ATU_BASE + 0x14, 0x01000000);
  MmioWrite32 (ATU_BASE + 0x18, 0);
  MmioWrite32 (ATU_BASE + 0x00, 0x4);
  MmioWrite32 (ATU_BASE + 0x04, BIT31);
  for (Retry = 0; (Retry < 5) && ((MmioRead32 (ATU_BASE + 0x04) & BIT31) == 0); Retry++) {
    gBS->Stall (1000);
  }
  for (Retry = 0; Retry < 100; Retry++) {
    Value = MmioRead32 (CONF_BASE);
    Vendor                            = Value & 0xFFFF;
    if ((Vendor != 0xFFFF) && (Vendor != 0) && (Vendor != 0x0001)) {
      break;
    }
    gBS->Stall (10000);
  }
  if (Retry == 100) {
    return EFI_NO_RESPONSE;
  }
  if (Value != PCIE0_ENDPOINT_ID) {
    return EFI_INCOMPATIBLE_VERSION;
  }

  return Pcie0AssignResources () == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
