from pathlib import Path
import argparse
import os
import re
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parent
header = r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#define STATIC static
#define IN
#define OUT
#define OPTIONAL
#define VOID void
#define CONST const
#define TRUE 1
#define FALSE 0
#define BIT31 0x80000000U
#define SIZE_1MB 1048576U
#define SIZE_128MB 134217728U
#define MAX_UINT32 UINT32_MAX
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define EFI_SUCCESS 0
#define EFI_UNSUPPORTED 1
#define EFI_OUT_OF_RESOURCES 2
#define EFI_COMPROMISED_DATA 3
#define EFI_DEVICE_ERROR 4
#define EFI_NOT_FOUND 5
#define EFI_BUFFER_TOO_SMALL 6
#define EFI_BAD_BUFFER_SIZE 7
#define EFI_ERROR(s) ((s)!=EFI_SUCCESS)
#define EFI_FILE_MODE_READ 1
#define EFI_FILE_DIRECTORY 16
#define SIGNATURE_16(a,b) ((uint16_t)(a)|((uint16_t)(b)<<8))
#define SIGNATURE_32(a,b,c,d) ((uint32_t)SIGNATURE_16(a,b)|((uint32_t)SIGNATURE_16(c,d)<<16))
typedef uint8_t BOOLEAN;
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int32_t INT32;
typedef uintptr_t UINTN;
typedef char CHAR8;
typedef uint16_t CHAR16;
typedef uint64_t EFI_STATUS;
typedef int EFI_GUID;
typedef struct { UINT64 FileSize,Attribute; } EFI_FILE_INFO;
typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
struct EFI_FILE_PROTOCOL {
  EFI_STATUS (*Open)(EFI_FILE_PROTOCOL *,EFI_FILE_PROTOCOL **,CHAR16 *,UINT64,UINT64);
  EFI_STATUS (*Close)(EFI_FILE_PROTOCOL *);
  EFI_STATUS (*Read)(EFI_FILE_PROTOCOL *,UINTN *,void *);
  EFI_STATUS (*SetPosition)(EFI_FILE_PROTOCOL *,UINT64);
  EFI_STATUS (*GetInfo)(EFI_FILE_PROTOCOL *,EFI_GUID *,UINTN *,void *);
};
extern EFI_GUID gEfiFileInfoGuid;
static inline UINT16 ReadUnaligned16(const UINT16 *p){UINT16 v;memcpy(&v,p,2);return v;}
static inline UINT32 ReadUnaligned32(const UINT32 *p){UINT32 v;memcpy(&v,p,4);return v;}
static inline UINT64 ReadUnaligned64(const UINT64 *p){UINT64 v;memcpy(&v,p,8);return v;}
static inline CHAR8 AsciiCharToUpper(CHAR8 c){return c>='a'&&c<='z'?c-32:c;}
static inline CHAR16 CharToUpper(CHAR16 c){return c>='a'&&c<='z'?c-32:c;}
#define AsciiStrLen strlen
#define ZeroMem(p,n) memset(p,0,n)
#define CopyMem memcpy
#define AllocatePool HostAllocate
#define FreePool HostFree
void *HostAllocate(UINTN size);
void HostFree(void *data);
void *AllocateCopyPool(UINTN size,const void *data);
void *ReallocatePool(UINTN oldSize,UINTN newSize,void *old);
UINTN AsciiSPrint(char *,UINTN,const char *,...);
UINTN StrLen(const CHAR16 *);
int StrCpyS(CHAR16 *,UINTN,const CHAR16 *);
int StrCatS(CHAR16 *,UINTN,const CHAR16 *);
'''


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def put32(data, offset, value):
    struct.pack_into('<I', data, offset, value & 0xffffffff)


def checksum(data):
    value = 0
    for offset in range(0, 508, 4):
        value ^= u32(data, offset)
    put32(data, 508, 1 if value == 0 else 0xfffffffe if value == 0xffffffff else value)


def marvin(data):
    # Independent test encoder for aligned HvLE buffers; fixed vectors are checked in C.
    low, high = 0x7a4e55c5, 0x82ef4d88
    mask = 0xffffffff
    rot = lambda value, bits: ((value << bits) | (value >> (32 - bits))) & mask
    for word in [*struct.unpack('<' + 'I' * (len(data) // 4), data), 0x80, 0]:
        low = (low + word) & mask
        high ^= low
        low = (rot(low, 20) + high) & mask
        high = rot(high, 9) ^ low
        low = (rot(low, 27) + high) & mask
        high = rot(high, 19)
    return high << 32 | low


class Hive:
    def __init__(self, kind='lh', unicode=False):
        self.data = bytearray(32)
        self.kind, self.unicode = kind, unicode
        self.nodes = {}

    def cell(self, data):
        offset = len(self.data)
        size = (len(data) + 11) & ~7
        self.data += struct.pack('<i', -size) + data + bytes(size - 4 - len(data))
        return offset

    def key(self, name, children=(), values=()):
        children = sorted(children, key=lambda child: child[0].upper())
        name_bytes = name.encode('utf-16le') if self.unicode else name.encode()
        node = bytearray(76)
        struct.pack_into('<2sH', node, 0, b'nk', 0 if self.unicode else 0x20)
        put32(node, 16, 0xffffffff)
        if children:
            leaf_kind = 'lh' if self.kind == 'ri' else self.kind
            leaf = bytearray(leaf_kind.encode() + struct.pack('<H', len(children)))
            for child_name, child_offset in children:
                leaf += struct.pack('<I', child_offset)
                if leaf_kind == 'lh':
                    hash_value = 0
                    for character in child_name.upper():
                        hash_value = (hash_value * 37 + ord(character)) & 0xffffffff
                    leaf += struct.pack('<I', hash_value)
                elif leaf_kind == 'lf':
                    leaf += child_name.encode()[:4].ljust(4, b'\0')
            offset = self.cell(leaf)
            if self.kind == 'ri':
                offset = self.cell(b'ri\1\0' + struct.pack('<I', offset))
            put32(node, 20, len(children))
            put32(node, 28, offset)
        else:
            put32(node, 28, 0xffffffff)
        offsets = []
        for value_name, value_type, data, inline in values:
            name_data = value_name.encode('utf-16le') if self.unicode else value_name.encode()
            value = bytearray(20)
            struct.pack_into('<2sH', value, 0, b'vk', len(name_data))
            put32(value, 4, len(data) | (0x80000000 if inline else 0))
            put32(value, 8, int.from_bytes(data.ljust(4, b'\0'), 'little') if inline else self.cell(data))
            put32(value, 12, value_type)
            struct.pack_into('<H', value, 16, 0 if self.unicode else 1)
            offsets.append(self.cell(value + name_data))
        put32(node, 36, len(offsets))
        put32(node, 40, self.cell(struct.pack('<' + 'I' * len(offsets), *offsets)) if offsets else 0xffffffff)
        struct.pack_into('<H', node, 72, len(name_bytes))
        offset = self.cell(node + name_bytes)
        self.nodes[name] = offset
        return name, offset

    def finish(self, root_key, sequence=10, padding=0):
        length = (len(self.data) + padding + 4095) & ~4095
        bins = self.data + bytes(length - len(self.data))
        bins[:4] = b'hbin'
        put32(bins, 8, length)
        header = bytearray(4096)
        header[:4] = b'regf'
        for offset, value in [(4, sequence), (8, sequence), (20, 1), (24, 5), (28, 0),
                              (32, 1), (36, root_key[1]), (40, length), (44, 1)]:
            put32(header, offset, value)
        checksum(header)
        return header + bins


def fixture(kind='lh', unicode=False, audio=None, gpu=None, padding=0, many=0, default=True):
    hive = Hive(kind, unicode)
    audio = audio if audio is not None else r'\SystemRoot\System32\DriverStore\FileRepository\sia9177.inf_arm64_new\sia9177.sys'
    gpu = gpu if gpu is not None else r'\SystemRoot\System32\DriverStore\FileRepository\qcom_gpu.inf_arm64_new\qcom_gpu.sys'
    services = [hive.key(name, values=[('ImagePath', 2, (path + '\0').encode('utf-16le'), False)])
                for name, path in [('sia9177', audio), ('qcom_gpu', gpu)]]
    services += [hive.key(f'unrelated{i}') for i in range(many)]
    control2 = hive.key('ControlSet002', [hive.key('Services', services)])
    old = hive.key('ControlSet001', [hive.key('Services', [hive.key('sia9177', values=[
        ('ImagePath', 1, (r'System32\DriverStore\FileRepository\old\sia9177.sys' + '\0').encode('utf-16le'), False)])])])
    values = [('Current', 4, struct.pack('<I', 1 if default else 2), True)]
    if default:
        values += [('Default', 4, struct.pack('<I', 2), True)]
    top = hive.key('SYSTEM', [old, control2, hive.key('Select', values=values)])
    return hive.finish(top, padding=padding), hive


def log(header, sequence, pages, length=None):
    entry = bytearray(40)
    entry[:4] = b'HvLE'
    put32(entry, 12, sequence)
    put32(entry, 16, length if length is not None else u32(header, 40))
    put32(entry, 20, len(pages))
    entry += b''.join(struct.pack('<II', offset, len(data)) for offset, data in pages)
    entry += b''.join(data for offset, data in pages)
    entry += bytes((-len(entry)) % 512)
    put32(entry, 4, len(entry))
    struct.pack_into('<Q', entry, 24, marvin(entry[40:]))
    struct.pack_into('<Q', entry, 32, marvin(entry[:32]))
    base = bytearray(header[:512])
    put32(base, 4, sequence)
    put32(base, 8, sequence)
    put32(base, 28, 6)
    checksum(base)
    return base + entry


def write_case(work, data, logs=()):
    config = work / 'Windows/System32/config'
    config.mkdir(parents=True, exist_ok=True)
    (config / 'SYSTEM').write_bytes(data)
    for name in ['SYSTEM.LOG1', 'SYSTEM.LOG2']:
        (config / name).unlink(missing_ok=True)
    for name, content in logs:
        (config / name).write_bytes(content)
    for name, files in [('sia9177.inf_arm64_new', ['ADSP/adsp.mdt', 'ADSP/adsp_dtb.mdt']),
                        ('qcom_gpu.inf_arm64_new', ['gen80000_zap.mbn']), ('old', ['ADSP/adsp.mdt', 'ADSP/adsp_dtb.mdt'])]:
        directory = work / 'Windows/System32/DriverStore/FileRepository' / name
        for filename in files:
            target = directory / filename
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b'firmware')


def run(binary, work, *args, env=None):
    result = subprocess.run([str(binary), str(work), *map(str, args)], check=True, capture_output=True, text=True, env=env)
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--samples', type=Path, help='directory containing independent Windows SYSTEM/log samples')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='hummer-registry-') as directory:
        work = Path(directory)
        includes = work / 'include'
        includes.mkdir()
        (includes / 'Uefi.h').write_text(header)
        for name in ['Library/BaseLib.h', 'Library/BaseMemoryLib.h', 'Library/MemoryAllocationLib.h',
                     'Library/PrintLib.h', 'Guid/FileInfo.h', 'Protocol/SimpleFileSystem.h']:
            path = includes / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include <Uefi.h>\n')
        binary = work / 'test'
        subprocess.run(['cc', '-std=c17', '-D_POSIX_C_SOURCE=200809L', '-fshort-wchar', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-g', '-I', str(includes), str(root / 'registry-host.c'),
                        '-o', str(binary)], check=True)
        for kind in ['li', 'lf', 'lh', 'ri']:
            for unicode in [False, True]:
                data, hive = fixture(kind, unicode)
                write_case(work, data)
                output = run(binary, work)
                assert 'status=0 ' in output and 'arm64_new' in output and '/old' not in output, output
        for prefix in ['%SystemRoot%\\', '%windir%\\', 'System32\\', '\\Windows\\', 'C:\\Windows\\',
                       '\\??\\D:\\Windows\\', '\\\\?\\D:\\Windows\\', '"\\SystemRoot\\']:
            tail = r'System32\DriverStore\FileRepository\sia9177.inf_arm64_new\sia9177.sys'
            if prefix == 'System32\\':
                tail = tail[len(prefix):]
            path = prefix + tail + ('"' if prefix.startswith('"') else '')
            data, _ = fixture(audio=path)
            write_case(work, data)
            output = run(binary, work)
            assert 'audio=/Windows/' in output, (path, output)
        data, _ = fixture(default=False)
        write_case(work, data)
        assert 'audio=' in run(binary, work)
        data, _ = fixture(padding=8 * 1024 * 1024, many=2000)
        write_case(work, data)
        output = run(binary, work)
        assert 'audio=' in output and int(re.search(r'bytes=(\d+)', output)[1]) < 65536, output
        # Allocation failures must release every file handle and allocation.
        allocation_count = int(re.search(r'allocations=(\d+)', output)[1])
        for failure in range(1, allocation_count + 1):
            run(binary, work, env={**os.environ, 'FAIL_ALLOCATION': str(failure)})
        data, _ = fixture()
        old = bytearray(data)
        old[4096:] = bytes(len(old) - 4096)
        put32(old, 4, 11)
        checksum(old)
        transaction = log(data, 10, [(0, bytes(data[4096:]))])
        for name in ['SYSTEM.LOG1', 'SYSTEM.LOG2']:
            write_case(work, old, [(name, transaction)])
            output = run(binary, work)
            assert 'audio=' in output and 'gpu=' in output, output
        # A later transaction in the other log replaces the earlier service path.
        earlier, _ = fixture(audio=r'System32\DriverStore\FileRepository\old\sia9177.sys')
        first = log(earlier, 10, [(0, bytes(earlier[4096:]))])
        second = log(data, 11, [(0, bytes(data[4096:]))])
        for logs in [[('SYSTEM.LOG1', first), ('SYSTEM.LOG2', second)], [('SYSTEM.LOG2', first), ('SYSTEM.LOG1', second)]]:
            write_case(work, old, logs)
            output = run(binary, work)
            assert 'audio=' in output and '/old' not in output, output
        damaged = bytearray(old)
        damaged[508] ^= 1
        write_case(work, damaged, [('SYSTEM.LOG1', first), ('SYSTEM.LOG2', second)])
        output = run(binary, work)
        assert 'audio=' in output and '/old' not in output, output
        # Replay can extend the primary file, and sequence numbers can wrap.
        enlarged, _ = fixture(padding=4096)
        put32(enlarged, 4, 0)
        put32(enlarged, 8, 0xffffffff)
        checksum(enlarged)
        wrapped_old = bytearray(old)
        put32(wrapped_old, 4, 0)
        put32(wrapped_old, 8, 0xffffffff)
        checksum(wrapped_old)
        first = log(data, 0xffffffff, [(0, bytes(data[4096:]))])
        second = log(enlarged, 0, [(0, bytes(enlarged[4096:]))])
        write_case(work, wrapped_old, [('SYSTEM.LOG1', second), ('SYSTEM.LOG2', first)])
        output = run(binary, work)
        assert 'audio=' in output and 'gpu=' in output, output
        allocation_count = int(re.search(r'allocations=(\d+)', output)[1])
        for failure in range(1, allocation_count + 1):
            run(binary, work, env={**os.environ, 'FAIL_ALLOCATION': str(failure)})
        # Old logs cannot roll a hive back; invalid hashes cannot be replayed.
        bad = bytearray(transaction)
        bad[-1] ^= 1
        for logs in [[], [('SYSTEM.LOG1', bad)], [('SYSTEM.LOG1', log(data, 9, [(0, bytes(data[4096:]))]))]]:
            write_case(work, old, logs)
            output = run(binary, work)
            assert 'status=0 ' not in output and 'audio=' not in output, output
        write_case(work, old, [('SYSTEM.LOG1', transaction + bytes(512))])
        assert 'audio=' in run(binary, work)
        # Registry offsets, list loops and truncated files must fail safely.
        loop, tree = fixture()
        index = u32(loop, 4096 + tree.nodes['SYSTEM'] + 4 + 28)
        loop[4096 + index + 4:4096 + index + 12] = b'ri\1\0' + struct.pack('<I', index)
        corruptions = [data[:500], data[:4096], bytearray(data), bytearray(data), loop]
        put32(corruptions[2], 36, 0xfffffff8)
        checksum(corruptions[2])
        corruptions[3][0] = 0
        for corruption in corruptions:
            write_case(work, corruption)
            assert 'audio=' not in run(binary, work)
        for path in [r'\SystemRoot\System32\..\evil\driver.sys', r'\Device\HarddiskVolume1\driver.sys', '"unterminated']:
            data, _ = fixture(audio=path)
            write_case(work, data)
            assert 'audio=' not in run(binary, work)
        if args.samples:
            for sample in sorted(args.samples.iterdir()):
                if not sample.is_dir():
                    continue
                original = (sample / 'SYSTEM').read_bytes()
                logs = [(name, (sample / name).read_bytes()) for name in ['SYSTEM.LOG1', 'SYSTEM.LOG2']]
                write_case(work, original, logs)
                recovered = work / 'recovered'
                output = run(binary, work, recovered)
                assert 'status=0 ' in output and 'default=1 ' in output, output
                expected = sample / 'recovered-bins'
                if expected.exists():
                    length = int(re.search(r'length=(\d+)', output)[1])
                    assert recovered.read_bytes()[4096:4096 + length] == expected.read_bytes(), sample
                print(f'{sample.name}: independent Windows SYSTEM/log recovery passed')
        print('Registry package lookup, ControlSets, path forms, sparse reads, allocation failures, log recovery and corrupt input checks passed')


if __name__ == '__main__':
    main()
