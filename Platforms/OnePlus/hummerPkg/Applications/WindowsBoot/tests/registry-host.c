#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "../Registry.c"

typedef struct {
  EFI_FILE_PROTOCOL Protocol;
  FILE *Stream;
  char Path[4096];
  UINT64 Size;
  BOOLEAN Directory;
} HOST_FILE;

static char rootPath[4096];
static UINTN allocations,liveAllocations,handles,readBytes,reads,opens;
static UINTN failAllocation;
EFI_GUID gEfiFileInfoGuid;

void *HostAllocate(UINTN size) {
  if(++allocations==failAllocation)return NULL;
  void *data=malloc(size?size:1);
  if(data)liveAllocations++;
  return data;
}
void HostFree(void *data) {
  assert(data&&liveAllocations);
  liveAllocations--;free(data);
}
void *AllocateCopyPool(UINTN size,const void *data) {
  void *copy=HostAllocate(size);if(copy)memcpy(copy,data,size);return copy;
}
void *ReallocatePool(UINTN oldSize,UINTN newSize,void *old) {
  void *data=HostAllocate(newSize);
  if(data){memset(data,0,newSize);memcpy(data,old,MIN(oldSize,newSize));HostFree(old);}
  return data;
}
UINTN AsciiSPrint(char *data,UINTN size,const char *format,...) {
  va_list args;va_start(args,format);int result=vsnprintf(data,size,format,args);va_end(args);return (UINTN)result;
}
UINTN StrLen(const CHAR16 *text) {UINTN size=0;while(text[size])size++;return size;}
int StrCpyS(CHAR16 *out,UINTN size,const CHAR16 *text) {
  assert(StrLen(text)<size);memcpy(out,text,(StrLen(text)+1)*2);return 0;
}
int StrCatS(CHAR16 *out,UINTN size,const CHAR16 *text) {return StrCpyS(out+StrLen(out),size-StrLen(out),text);}

static EFI_STATUS Close(EFI_FILE_PROTOCOL *protocol) {
  HOST_FILE *file=(HOST_FILE *)protocol;
  if(file->Stream)fclose(file->Stream);
  assert(handles);handles--;free(file);return EFI_SUCCESS;
}
static EFI_STATUS Read(EFI_FILE_PROTOCOL *protocol,UINTN *size,void *data) {
  HOST_FILE *file=(HOST_FILE *)protocol;
  assert(!file->Directory); // No directory enumeration is allowed in this test.
  *size=fread(data,1,*size,file->Stream);readBytes+=*size;reads++;
  return ferror(file->Stream)?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS Seek(EFI_FILE_PROTOCOL *protocol,UINT64 offset) {
  HOST_FILE *file=(HOST_FILE *)protocol;
  return !file->Directory&&!fseeko(file->Stream,(off_t)offset,SEEK_SET)?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
static EFI_STATUS Info(EFI_FILE_PROTOCOL *protocol,EFI_GUID *guid,UINTN *size,void *data) {
  (void)guid;HOST_FILE *file=(HOST_FILE *)protocol;
  if(*size<sizeof(EFI_FILE_INFO)||!data){*size=sizeof(EFI_FILE_INFO);return EFI_BUFFER_TOO_SMALL;}
  EFI_FILE_INFO *info=data;memset(info,0,sizeof(*info));
  info->FileSize=file->Size;info->Attribute=file->Directory?EFI_FILE_DIRECTORY:0;return EFI_SUCCESS;
}
static EFI_STATUS Open(EFI_FILE_PROTOCOL *protocol,EFI_FILE_PROTOCOL **out,CHAR16 *name,UINT64 mode,UINT64 attributes) {
  (void)attributes;assert(mode==EFI_FILE_MODE_READ);opens++;
  HOST_FILE *parent=(HOST_FILE *)protocol;
  char path[4096];UINTN length;
  snprintf(path,sizeof(path),"%s",name[0]=='\\'?rootPath:parent->Path);
  length=strlen(path);
  if(name[0]!='\\')path[length++]='/';
  for(UINTN i=0;name[i];i++){assert(length+1<sizeof(path)&&name[i]<128);path[length++]=name[i]=='\\'?'/':(char)name[i];}
  path[length]=0;
  struct stat statbuf;if(stat(path,&statbuf))return EFI_NOT_FOUND;
  HOST_FILE *file=calloc(1,sizeof(*file));assert(file);
  file->Protocol=(EFI_FILE_PROTOCOL){Open,Close,Read,Seek,Info};
  file->Directory=S_ISDIR(statbuf.st_mode);file->Size=(UINT64)statbuf.st_size;
  snprintf(file->Path,sizeof(file->Path),"%s",path);
  if(!file->Directory){file->Stream=fopen(path,"rb");if(!file->Stream){free(file);return EFI_DEVICE_ERROR;}}
  *out=&file->Protocol;handles++;return EFI_SUCCESS;
}

int main(int argc,char **argv) {
  assert(argc>=2);snprintf(rootPath,sizeof(rootPath),"%s",argv[1]);
  if(getenv("FAIL_ALLOCATION"))failAllocation=strtoull(getenv("FAIL_ALLOCATION"),NULL,10);
  HOST_FILE root={.Protocol={Open,Close,Read,Seek,Info},.Directory=TRUE};
  snprintf(root.Path,sizeof(root.Path),"%s",rootPath);
  EFI_STATUS status;
  if(argc>2) {
    REG_HIVE hive;status=RegOpen(&root.Protocol,L"\\Windows\\System32\\config\\SYSTEM",&hive);
    if(!EFI_ERROR(status)) {
      hive.Root=R32(hive.Header+36);hive.Length=R32(hive.Header+40);
      if(!RegHeaderValid(hive.Header,0)||R32(hive.Header+4)!=R32(hive.Header+8))status=RegRecover(&root.Protocol,&hive);
      if(!EFI_ERROR(status)) {
        if(!hive.Data)status=RegLoad(&hive);
        if(!EFI_ERROR(status)) {
          FILE *output=fopen(argv[2],"wb");assert(output);
          assert(fwrite(hive.Data,1,hive.Size,output)==hive.Size);fclose(output);
          UINT32 select=RegKey(&hive,hive.Root,"Select");
          printf("root=%u length=%u default=%u current=%u\n",hive.Root,hive.Length,
                 RegDword(&hive,select,"Default"),RegDword(&hive,select,"Current"));
        }
      }
      RegClose(&hive);
    }
  } else {
    EFI_FILE_PROTOCOL *packages[2];status=RegistryOpenPackages(&root.Protocol,packages);
    for(UINTN i=0;i<2;i++)if(packages[i]){printf("%s=%s\n",i?"gpu":"audio",((HOST_FILE *)packages[i])->Path+strlen(rootPath));packages[i]->Close(packages[i]);}
  }
  assert(liveAllocations==0&&handles==0);
  printf("status=%llu allocations=%zu reads=%zu bytes=%zu opens=%zu\n",(unsigned long long)status,allocations,reads,readBytes,opens);
  assert(RegHash((const UINT8[32]){0},32)==0xda91a06ac5359223ULL);
  return 0;
}
