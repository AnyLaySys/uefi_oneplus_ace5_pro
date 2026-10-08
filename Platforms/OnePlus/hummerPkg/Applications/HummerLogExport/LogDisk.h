#define LOG_PARTITION_START 2048
#define LOG_VOLUME_SECTORS 16384
#define LOG_DISK_SECTORS (LOG_PARTITION_START + LOG_VOLUME_SECTORS)
#define LOG_SECTOR_SIZE 512
#define LOG_FAT_SECTORS 64
#define LOG_ROOT_SECTOR 129
#define LOG_DATA_SECTOR 161

STATIC UINT8 *mLogDisk;
STATIC UINT8 *mLogVolume;
STATIC EFI_BLOCK_IO_MEDIA mLogMedia;
STATIC EFI_BLOCK_IO_PROTOCOL mLogBlockIo;
STATIC UINT16 mNextCluster;
STATIC UINTN mRootEntry;

STATIC EFI_STATUS EFIAPI
LogReset (EFI_BLOCK_IO_PROTOCOL *This, BOOLEAN ExtendedVerification)
{
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
LogRead (EFI_BLOCK_IO_PROTOCOL *This, UINT32 MediaId, EFI_LBA Lba,
         UINTN BufferSize, VOID *Buffer)
{
  UINTN Blocks;

  if ((mLogDisk == NULL) || (MediaId != mLogMedia.MediaId)) {
    return EFI_MEDIA_CHANGED;
  }
  if (BufferSize == 0) {
    return EFI_SUCCESS;
  }
  if ((Buffer == NULL) || ((BufferSize % LOG_SECTOR_SIZE) != 0)) {
    return EFI_BAD_BUFFER_SIZE;
  }
  Blocks = BufferSize / LOG_SECTOR_SIZE;
  if ((Lba >= LOG_DISK_SECTORS) || (Blocks > LOG_DISK_SECTORS - Lba)) {
    return EFI_INVALID_PARAMETER;
  }
  CopyMem (Buffer, mLogDisk + (UINTN)Lba * LOG_SECTOR_SIZE, BufferSize);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
LogWrite (EFI_BLOCK_IO_PROTOCOL *This, UINT32 MediaId, EFI_LBA Lba,
          UINTN BufferSize, VOID *Buffer)
{
  UINTN Blocks;

  if ((mLogDisk == NULL) || (MediaId != mLogMedia.MediaId)) {
    return EFI_MEDIA_CHANGED;
  }
  if (BufferSize == 0) {
    return EFI_SUCCESS;
  }
  if ((Buffer == NULL) || ((BufferSize % LOG_SECTOR_SIZE) != 0)) {
    return EFI_BAD_BUFFER_SIZE;
  }
  Blocks = BufferSize / LOG_SECTOR_SIZE;
  if ((Lba >= LOG_DISK_SECTORS) || (Blocks > LOG_DISK_SECTORS - Lba)) {
    return EFI_INVALID_PARAMETER;
  }
  CopyMem (mLogDisk + (UINTN)Lba * LOG_SECTOR_SIZE, Buffer, BufferSize);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
LogFlush (EFI_BLOCK_IO_PROTOCOL *This)
{
  return EFI_SUCCESS;
}

STATIC VOID
ReleaseLogDisk (VOID)
{
  if (mLogDisk != NULL) {
    FreePages (mLogDisk, EFI_SIZE_TO_PAGES (LOG_DISK_SECTORS * LOG_SECTOR_SIZE));
    mLogDisk = NULL;
    mLogVolume = NULL;
  }
}

STATIC EFI_STATUS
AddLogFile (CONST CHAR8 *Name, CONST VOID *Data, UINT32 Size)
{
  UINTN Clusters;
  UINTN Index;
  UINT8 *Entry;
  UINT8 *Fat;
  UINTN Sector;
  UINT16 Next;

  Clusters = (Size + LOG_SECTOR_SIZE - 1) / LOG_SECTOR_SIZE;
  Sector = LOG_DATA_SECTOR + mNextCluster - 2;
  if ((Clusters == 0) || (Sector >= LOG_VOLUME_SECTORS) ||
      (Clusters > LOG_VOLUME_SECTORS - Sector) ||
      (mRootEntry >= 512)) {
    return EFI_BAD_BUFFER_SIZE;
  }
  Fat = mLogVolume + LOG_SECTOR_SIZE;
  for (Index = 0; Index < Clusters; Index++) {
    Next = Index + 1 == Clusters ? 0xFFFF : mNextCluster + Index + 1;
    WriteUnaligned16 ((UINT16 *)(Fat + (mNextCluster + Index) * 2), Next);
  }
  Entry = mLogVolume + LOG_ROOT_SECTOR * LOG_SECTOR_SIZE + mRootEntry * 32;
  CopyMem (Entry, Name, 11);
  Entry[11] = 0x21;
  WriteUnaligned16 ((UINT16 *)(Entry + 24), (46 << 9) | (10 << 5) | 8);
  WriteUnaligned16 ((UINT16 *)(Entry + 26), mNextCluster);
  WriteUnaligned32 ((UINT32 *)(Entry + 28), Size);
  CopyMem (mLogVolume + Sector * LOG_SECTOR_SIZE, Data, Size);
  mNextCluster += Clusters;
  mRootEntry++;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
BuildLogDisk (VOID)
{
  UINT8 *Console;
  UINT8 *Pmsg;
  CHAR8 Info[512];
  UINT32 ConsoleCrc;
  UINT32 PmsgCrc;
  UINTN InfoSize;
  EFI_STATUS Status;

  mLogDisk = AllocatePages (EFI_SIZE_TO_PAGES (LOG_DISK_SECTORS * LOG_SECTOR_SIZE));
  if (mLogDisk == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  ZeroMem (mLogDisk, LOG_DISK_SECTORS * LOG_SECTOR_SIZE);
  WriteUnaligned32 ((UINT32 *)(mLogDisk + 440), 0x4D340019);
  mLogDisk[447] = 0xFE;
  mLogDisk[448] = 0xFF;
  mLogDisk[449] = 0xFF;
  mLogDisk[450] = 0x0E;
  mLogDisk[451] = 0xFE;
  mLogDisk[452] = 0xFF;
  mLogDisk[453] = 0xFF;
  WriteUnaligned32 ((UINT32 *)(mLogDisk + 454), LOG_PARTITION_START);
  WriteUnaligned32 ((UINT32 *)(mLogDisk + 458), LOG_VOLUME_SECTORS);
  mLogDisk[510] = 0x55;
  mLogDisk[511] = 0xAA;
  mLogVolume = mLogDisk + LOG_PARTITION_START * LOG_SECTOR_SIZE;
  mLogVolume[0] = 0xEB;
  mLogVolume[1] = 0x3C;
  mLogVolume[2] = 0x90;
  CopyMem (mLogVolume + 3, "HUMMER  ", 8);
  WriteUnaligned16 ((UINT16 *)(mLogVolume + 11), LOG_SECTOR_SIZE);
  mLogVolume[13] = 1;
  WriteUnaligned16 ((UINT16 *)(mLogVolume + 14), 1);
  mLogVolume[16] = 2;
  WriteUnaligned16 ((UINT16 *)(mLogVolume + 17), 512);
  WriteUnaligned16 ((UINT16 *)(mLogVolume + 19), LOG_VOLUME_SECTORS);
  mLogVolume[21] = 0xF8;
  WriteUnaligned16 ((UINT16 *)(mLogVolume + 22), LOG_FAT_SECTORS);
  WriteUnaligned16 ((UINT16 *)(mLogVolume + 24), 32);
  WriteUnaligned16 ((UINT16 *)(mLogVolume + 26), 64);
  WriteUnaligned32 ((UINT32 *)(mLogVolume + 28), LOG_PARTITION_START);
  mLogVolume[36] = 0x80;
  mLogVolume[38] = 0x29;
  WriteUnaligned32 ((UINT32 *)(mLogVolume + 39), 0x4D344C47);
  CopyMem (mLogVolume + 43, "HUMMERLOGS ", 11);
  CopyMem (mLogVolume + 54, "FAT16   ", 8);
  mLogVolume[510] = 0x55;
  mLogVolume[511] = 0xAA;
  WriteUnaligned16 ((UINT16 *)(mLogVolume + LOG_SECTOR_SIZE), 0xFFF8);
  WriteUnaligned16 ((UINT16 *)(mLogVolume + LOG_SECTOR_SIZE + 2), 0xFFFF);
  CopyMem (mLogVolume + LOG_ROOT_SECTOR * LOG_SECTOR_SIZE, "HUMMERLOGS ", 11);
  mLogVolume[LOG_ROOT_SECTOR * LOG_SECTOR_SIZE + 11] = 0x08;
  mNextCluster = 2;
  mRootEntry = 1;
  Console = (UINT8 *)(UINTN)0x880000000ULL;
  Pmsg = Console + 0x40000;
  Status = AddLogFile ("CONSOLE BIN", Console, 0x40000);
  if (!EFI_ERROR (Status)) {
    Status = AddLogFile ("PMSG    BIN", Pmsg, 0x200000);
  }
  if (EFI_ERROR (Status)) {
    ReleaseLogDisk ();
    return Status;
  }
  Console = mLogVolume + LOG_DATA_SECTOR * LOG_SECTOR_SIZE;
  Pmsg = Console + 0x40000;
  gBS->CalculateCrc32 (Console, 0x40000, &ConsoleCrc);
  gBS->CalculateCrc32 (Pmsg, 0x200000, &PmsgCrc);
  InfoSize = AsciiSPrint (Info, sizeof (Info),
    "HUMMER M4 RAM LOG EXPORT\r\n"
    "region_base=0x880000000\r\nconsole_bytes=262144\r\npmsg_bytes=2097152\r\n"
    "console_crc32=%08x\r\npmsg_crc32=%08x\r\n"
    "Raw ring headers are included. Validate the original Linux frame CRCs.\r\n"
    "Writes affect this temporary RAM copy only. Original logs and UFS are untouched.\r\n",
    ConsoleCrc, PmsgCrc);
  Status = AddLogFile ("INFO    TXT", Info, (UINT32)InfoSize);
  if (EFI_ERROR (Status)) {
    ReleaseLogDisk ();
    return Status;
  }
  CopyMem (mLogVolume + (1 + LOG_FAT_SECTORS) * LOG_SECTOR_SIZE,
    mLogVolume + LOG_SECTOR_SIZE, LOG_FAT_SECTORS * LOG_SECTOR_SIZE);
  ZeroMem (&mLogMedia, sizeof (mLogMedia));
  mLogMedia.MediaId = 0x4D344C47;
  mLogMedia.RemovableMedia = TRUE;
  mLogMedia.MediaPresent = TRUE;
  mLogMedia.ReadOnly = FALSE;
  mLogMedia.BlockSize = LOG_SECTOR_SIZE;
  mLogMedia.IoAlign = 1;
  mLogMedia.LastBlock = LOG_DISK_SECTORS - 1;
  ZeroMem (&mLogBlockIo, sizeof (mLogBlockIo));
  mLogBlockIo.Revision = EFI_BLOCK_IO_PROTOCOL_REVISION;
  mLogBlockIo.Media = &mLogMedia;
  mLogBlockIo.Reset = LogReset;
  mLogBlockIo.ReadBlocks = LogRead;
  mLogBlockIo.WriteBlocks = LogWrite;
  mLogBlockIo.FlushBlocks = LogFlush;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
AssignStorageHandles (VOID)
{
  EFI_STATUS Status;
  UINT8 Capacity;
  UINTN Lun;

  Capacity = 0;
  Status = mUsbMsd->QueryMaxLun (mUsbMsd, &Capacity);
  if (EFI_ERROR (Status) || (Capacity == 0)) {
    return EFI_UNSUPPORTED;
  }
  for (Lun = 0; Lun < Capacity; Lun++) {
    Status = mUsbMsd->AssignBlkIoHandle (mUsbMsd, NULL, (UINT32)Lun);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  Status = BuildLogDisk ();
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = mUsbMsd->AssignBlkIoHandle (mUsbMsd, &mLogBlockIo, 0);
  if (EFI_ERROR (Status)) {
    ReleaseLogDisk ();
    return Status;
  }
  mAssignedLuns = 1;
  AddLine ("USB0 = HUMMERLOGS / 9 MiB RAM disk");
  return EFI_SUCCESS;
}
