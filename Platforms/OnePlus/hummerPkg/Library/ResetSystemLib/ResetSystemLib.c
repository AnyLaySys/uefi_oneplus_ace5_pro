#include <Uefi.h>
#include <IndustryStandard/ArmStdSmc.h>
#include <Library/ArmMonitorLib.h>
#include <Library/ResetSystemLib.h>

static VOID Call(UINTN Function) {
  ARM_MONITOR_ARGS Args = {0};
  Args.Arg0 = Function;
  ArmMonitorCall(&Args);
}
VOID EFIAPI ResetCold(VOID) { Call(ARM_SMC_ID_PSCI_SYSTEM_RESET); }
VOID EFIAPI ResetShutdown(VOID) { Call(ARM_SMC_ID_PSCI_SYSTEM_OFF); }
VOID EFIAPI ResetWarm(VOID) {
  ARM_MONITOR_ARGS Args = {0};
  Args.Arg0 = ARM_SMC_ID_PSCI_FEATURES;
  Args.Arg1 = ARM_SMC_ID_PSCI_SYSTEM_RESET2_AARCH64;
  ArmMonitorCall(&Args);
  if (Args.Arg0 == ARM_SMC_PSCI_RET_SUCCESS) Call(ARM_SMC_ID_PSCI_SYSTEM_RESET2_AARCH64);
  ResetCold();
}
VOID EFIAPI ResetPlatformSpecific(UINTN DataSize, VOID *ResetData) {
  (VOID)DataSize;
  (VOID)ResetData;
  ResetCold();
}
VOID EFIAPI ResetSystem(EFI_RESET_TYPE ResetType, EFI_STATUS ResetStatus, UINTN DataSize, VOID *ResetData) {
  (VOID)ResetStatus;
  switch (ResetType) {
    case EfiResetCold: ResetCold(); break;
    case EfiResetWarm: ResetWarm(); break;
    case EfiResetShutdown: ResetShutdown(); break;
    case EfiResetPlatformSpecific: ResetPlatformSpecific(DataSize, ResetData); break;
    default: break;
  }
}
