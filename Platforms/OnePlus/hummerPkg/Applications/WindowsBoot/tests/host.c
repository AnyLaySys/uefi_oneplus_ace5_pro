#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static UINT32 regs[SIZE_1MB / 4],before[SIZE_1MB / 4],writes;
static BOOLEAN iort,denyProtocol,bypassQuirk,denySmr,race;
static UINT32 readsFree;
EFI_GUID gEfiAcpiSdtProtocolGuid;

static EFI_STATUS GetTable(UINTN index,EFI_ACPI_SDT_HEADER **table,EFI_ACPI_TABLE_VERSION *version,UINTN *key) {
    static EFI_ACPI_SDT_HEADER data;
    (void)version;(void)key;
    if(!iort||index)return EFI_NOT_FOUND;
    data.Signature=SIGNATURE_32('I','O','R','T');*table=&data;return EFI_SUCCESS;
}
static EFI_ACPI_SDT_PROTOCOL sdt={GetTable};
static EFI_STATUS Locate(EFI_GUID *guid,VOID *registration,VOID **protocol) {
    (void)guid;(void)registration;
    if(denyProtocol)return EFI_NOT_FOUND;
    *protocol=&sdt;return EFI_SUCCESS;
}
static EFI_BOOT_SERVICES services={Locate};
EFI_BOOT_SERVICES *gBS=&services;
void HostDebug(int level,const char *format,...) {(void)level;(void)format;}

UINT32 MmioRead32(UINTN address) {
    assert(address>=0x15000000&&address<0x15100000);
    UINTN offset=address-0x15000000;
    assert(!(offset>=0xa00&&offset<0xa10));
    assert(!(offset>=0xe00&&offset<0xe10));
    if(offset==0x818&&race&&++readsFree==2)regs[offset/4]=0x80001800;
    return regs[offset/4];
}
UINT32 MmioWrite32(UINTN address,UINT32 value) {
    UINTN offset=address-0x15000000;
    assert(offset==0xc18||offset==0x818);
    writes++;
    if(offset==0xc18&&bypassQuirk&&value==0x10000)value=0x20000;
    if(offset==0x818&&denySmr&&value)value=0;
    regs[offset/4]=value;return value;
}

static void Fresh(void) {
    memset(regs,0,sizeof(regs));writes=readsFree=0;
    iort=denyProtocol=bypassQuirk=denySmr=race=FALSE;
    regs[0x20/4]=0x4c017e84;regs[0x24/4]=0x60000053;regs[0]=0x002e0406;
    const UINT32 smrs[]={0x80000060,0x80000040,0x80000480,0x80020800,0x800000b6,0x800000a3};
    for(UINT32 i=0;i<6;i++){regs[(0x800+4*i)/4]=smrs[i];regs[(0xc00+4*i)/4]=i<4?i:0;}
    for(UINT32 i=6;i<128;i++)regs[(0xc00+4*i)/4]=0x0300006e;
    for(UINT32 i=0;i<4;i++){regs[(0x1000+4*i)/4]=0x1f000;regs[(0x80000+4096*i)/4]=0xe0;}
    regs[0x80000/4]=0x0a9f00e0;
    regs[(0x800+126*4)/4]=0x80201041;regs[(0x800+127*4)/4]=0x80801001;
    regs[(0xc00+126*4)/4]=regs[(0xc00+127*4)/4]=0;
    memcpy(before,regs,sizeof(regs));
}
static void Preserved(void) {
    for(UINT32 i=0;i<SIZE_1MB/4;i++)if(i!=0x818/4&&i!=0xc18/4)assert(regs[i]==before[i]);
}

int main(void) {
    Fresh();assert(HummerCameraDmaInitialize()==EFI_SUCCESS);assert(writes==2);
    assert(regs[0x818/4]==0x80001c00&&regs[0xc18/4]==0);Preserved();
    writes=0;assert(HummerCameraDmaInitialize()==EFI_SUCCESS&&writes==0);Preserved();
    Fresh();iort=TRUE;assert(HummerCameraDmaInitialize()==EFI_UNSUPPORTED&&writes==0);Preserved();
    Fresh();denyProtocol=TRUE;assert(HummerCameraDmaInitialize()==EFI_NOT_FOUND&&writes==0);Preserved();
    Fresh();regs[0]|=1;assert(HummerCameraDmaInitialize()==EFI_UNSUPPORTED&&writes==0);
    Fresh();regs[0x20/4]^=1;assert(HummerCameraDmaInitialize()==EFI_UNSUPPORTED&&writes==0);
    Fresh();regs[0x818/4]=0x80011c00;assert(HummerCameraDmaInitialize()==EFI_ACCESS_DENIED&&writes==0);
    Fresh();regs[0x818/4]=regs[0x81c/4]=0x80001c00;regs[0xc18/4]=regs[0xc1c/4]=0x10000;
    assert(HummerCameraDmaInitialize()==EFI_ACCESS_DENIED&&writes==0);
    Fresh();regs[0x818/4]=0x80001c00;regs[0xc18/4]=0;regs[0x80000/4]|=1;
    assert(HummerCameraDmaInitialize()==EFI_ACCESS_DENIED&&writes==0);
    Fresh();regs[0x818/4]=0x80001c00;regs[0xc18/4]=0x20000;
    assert(HummerCameraDmaInitialize()==EFI_ACCESS_DENIED&&writes==0);
    Fresh();for(UINT32 i=6;i<128;i++)regs[(0x800+4*i)/4]=0x80001800;
    assert(HummerCameraDmaInitialize()==EFI_OUT_OF_RESOURCES&&writes==0);
    Fresh();race=TRUE;assert(HummerCameraDmaInitialize()==EFI_NOT_READY&&writes==0);Preserved();
    Fresh();for(UINT32 i=0;i<4;i++)regs[(0x80000+4096*i)/4]|=1;
    assert(HummerCameraDmaInitialize()==EFI_SUCCESS&&writes==2);assert(regs[0xc18/4]==0x10000);
    Fresh();for(UINT32 i=0;i<4;i++)regs[(0x1000+4*i)/4]=0x3f000;
    bypassQuirk=TRUE;assert(HummerCameraDmaInitialize()==EFI_DEVICE_ERROR&&writes==1);assert(regs[0x818/4]==0);
    Fresh();denySmr=TRUE;assert(HummerCameraDmaInitialize()==EFI_DEVICE_ERROR&&writes==3);
    assert(regs[0x818/4]==0);Preserved();
    puts("Camera DMA identity, mapping preservation, overlap rejection, firmware quirks and 128-slot limit passed");
    return 0;
}
