#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/fs.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <zlib.h>

// Disable some warnings that are not helpful and will not be fixed.

#ifdef __clang__
// C89/C90/C94 are not supported.
#pragma clang diagnostic ignored "-Wdeclaration-after-statement"
// This warning is good for security, but it requires massive
// changes to how C code is written, and this code is still written
// in the unsafe "traditional" style.
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Only little-endian machines are supported
#endif

// Exit codes:
// 0: everything OK
// 1: unspecified error
// 2: I/O error
// 3: GPT might be valid but uses unsupported feature
// 4: No partition table found
// 5: GPT invalid, missing, or corrupt
// 6: Cannot create new GPT because of some constraint
// 7: Device needs fixing but "check" is passed
// 8: Disk appears truncated
#define EXIT_IO 2
#define EXIT_UNSUPPORTED 3
#define EXIT_NOT_PARTITIONED 4
#define EXIT_INVALID_GPT 5
#define EXIT_CONSTRAINT 6
#define EXIT_NEEDS_FIX 7
#define EXIT_TRUNCATED 8

struct ChsSpec {
    uint8_t spec[3];
};

struct Guid {
    uint32_t first;
    uint16_t second, third;
    uint8_t rest[8];
} __attribute__((packed, aligned(1)));

struct GPTHeader {
    uint8_t Signature[8];              /*  0 */
    uint32_t Revision;                 /*  8 */
    uint32_t HeaderSize;               /* 12 */
    uint32_t HeaderCRC32;              /* 16 */
    uint32_t Reserved;                 /* 20 */
    uint64_t MyLBA;                    /* 24 */
    uint64_t AlternateLBA;             /* 32 */
    uint64_t FirstUsableLBA;           /* 40 */
    uint64_t LastUsableLBA;            /* 48 */
    struct Guid DiskGUID;              /* 56 */
    uint64_t PartitionEntryLBA;        /* 72 */
    uint32_t NumberOfPartitionEntries; /* 80 */
    uint32_t SizeOfPartitionEntry;     /* 84 */
    uint32_t PartitionEntryArrayCRC32; /* 88 */
} __attribute__((packed, aligned(1)));
static_assert(sizeof(struct GPTHeader) == 92, "bug");

struct GPTPartitionEntry {
    struct Guid PartitionTypeGUID;   /*   0 */
    struct Guid UniquePartitionGUID; /*  16 */
    uint64_t StartingLBA;            /*  32 */
    uint64_t EndingLBA;              /*  40 */
    uint64_t Attributes;             /*  48 */
    uint8_t PartitionName[72];       /*  56 */
} __attribute__((packed, aligned(1)));
static_assert(sizeof(struct GPTPartitionEntry) == 128, "bug");

struct __attribute__((aligned(1))) MbrPartitionRecord {
    uint8_t boot_indicator;
    struct ChsSpec starting_chs;
    uint8_t os_type;
    struct ChsSpec ending_chs;
    uint8_t starting_lba[4];
    uint8_t size_in_lba[4];
};
#define ASSERT_OFFSET(a, b, offset, size)                                                                              \
    static_assert(offsetof(a, b) == offset, "wrong offset");                                                           \
    static_assert(sizeof(((a){0}).b) == size, "wrong size");
ASSERT_OFFSET(struct MbrPartitionRecord, boot_indicator, 0, 1)
ASSERT_OFFSET(struct MbrPartitionRecord, starting_chs, 1, 3)
ASSERT_OFFSET(struct MbrPartitionRecord, os_type, 4, 1)
ASSERT_OFFSET(struct MbrPartitionRecord, ending_chs, 5, 3)
ASSERT_OFFSET(struct MbrPartitionRecord, starting_lba, 8, 4)
ASSERT_OFFSET(struct MbrPartitionRecord, size_in_lba, 12, 4)

struct Mbr {
    uint8_t jmp[3];
    uint8_t name[8];
    uint8_t bytes_per_sector[2];
    uint8_t sectors_per_cluster;
    uint8_t boot_code[426];
    uint8_t unique_mbr_disk_signature[4];
    uint8_t unknown[2];
    struct MbrPartitionRecord partition_record[4];
    uint8_t signature[2];
};
ASSERT_OFFSET(struct Mbr, boot_code, 14, 426)
ASSERT_OFFSET(struct Mbr, unique_mbr_disk_signature, 440, 4)
ASSERT_OFFSET(struct Mbr, unknown, 444, 2)
ASSERT_OFFSET(struct Mbr, partition_record, 446, 64)
ASSERT_OFFSET(struct Mbr, signature, 510, 2)

struct UUID {
    uint8_t data[16];
};

#define max(a, b)                                                                                                      \
    (__extension__({                                                                                                   \
        typeof(a) _a = (a);                                                                                            \
        typeof(b) _b = (b);                                                                                            \
        _a > _b ? _a : _b;                                                                                             \
    }))

#define min(a, b)                                                                                                      \
    (__extension__({                                                                                                   \
        typeof(a) _a = (a);                                                                                            \
        typeof(b) _b = (b);                                                                                            \
        _a > _b ? _b : _a;                                                                                             \
    }))
#pragma GCC poison _a _b

// 512B -> 4096B:
//
// - Bytes 512 through 4096 will NOT be zero.
//
// 4096B -> 512B:
//
// - Bytes 512 through 4096 WILL be zero.

// Step 1: Validate GPT
// Step 2:

static bool bool_ioctl(int fd, long unsigned int op, void *arg) {
    switch (ioctl(fd, op, arg)) {
    case 0:
        return true;
    case -1:
        return false;
    default:
        warnx("invalid return value from ioctl()");
        abort();
    }
}

static FILE *log_file;
__attribute__((format(printf, 1, 2)))
static void vlog(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (fprintf(log_file, "%s: ", program_invocation_name) != (int)strlen(program_invocation_name) + 2 ||
        vfprintf(log_file, fmt, args) < 0 ||
        fputc((int)(unsigned char)'\n', log_file) != (int)(unsigned char)'\n')
        err(EXIT_IO, "gptfix: log write error");
    va_end(args);
}

static uint64_t gpt_partition_table_bytes(const struct GPTHeader *gpt) {
    /* Arguments are 32-bit so wraparound is not possible. */
    return (uint64_t)gpt->NumberOfPartitionEntries * (uint64_t)gpt->SizeOfPartitionEntry;
}

static bool valid_sector_size(uint32_t sector_size) {
    return (sector_size >= 512) && ((sector_size & (sector_size - 1)) == 0);
}

struct GPT {
    struct GPTHeader header;
    uint32_t used_entries;
    struct GPTPartitionEntry *entries;
    uint32_t sector_size;
    uint32_t _padding;
};

static uint64_t round_to_next_sector(uint64_t value, uint32_t sector_size) {
    uint64_t sum;
    assert(valid_sector_size(sector_size));
    if (__builtin_add_overflow(value, sector_size - 1, &sum))
        assert(NULL == "Arithmetic overflow calculating rounded value");
    return sum & ~(uint64_t)(sector_size - 1);
}

static uint64_t gpt_partition_table_required_bytes(const struct GPT *gpt, uint32_t sector_size, uint32_t entries) {
    assert(valid_sector_size(sector_size));
    /* Compute bytes required for partition entries, rounded to the next
     * multiple of the sector size.  Wraparound is impossible because
     * (uint64_t)UINT32_MAX * (uint64_t)UINT32_MAX + (uint64_t)UINT32_MAX <
     * UINT64_MAX.
     */
    return round_to_next_sector((uint64_t)gpt->header.SizeOfPartitionEntry * (uint64_t)entries,
                                sector_size);
}

static void check_mbr(struct Mbr *mbr, uint64_t size_in_sectors, bool skip_bpb_check, bool verbose) {
    if (mbr->signature[0] != 0x55 || mbr->signature[1] != 0xAA) {
        errx(EXIT_NOT_PARTITIONED, "Invalid MBR signature: expected 0x55 0xAA, got 0x%02" PRIX8 " 0x%02" PRIX8,
             mbr->signature[0], mbr->signature[1]);
    }
    if (mbr->partition_record[0].os_type != 0xEE) {
        errx(EXIT_UNSUPPORTED, "Bad protective MBR: OS type is not GPT Protective");
    }
    struct MbrPartitionRecord first_partition = {0};
    for (int i = 1; i <= 3; ++i) {
        if (memcmp(&first_partition, mbr->partition_record + i, sizeof(first_partition)) != 0) {
            errx(EXIT_UNSUPPORTED,
                 "Partition record %d is not zeroed - this device does not "
                 "have a protective MBR",
                 i + 1);
        }
    }

    first_partition = mbr->partition_record[0];
    uint32_t starting_lba, size_in_lba;
    memcpy(&starting_lba, first_partition.starting_lba, 4);
    memcpy(&size_in_lba, first_partition.size_in_lba, 4);
    if (starting_lba != 0x1) {
        errx(EXIT_INVALID_GPT, "Bad protective MBR: does not start at first block");
    }

    if (!skip_bpb_check) {
       for (size_t i = 3; i < 14; ++i) {
          if (((char *)mbr)[i] != 0) {
             errx(EXIT_UNSUPPORTED, "Refusing to change partition table that might overlap a FAT or NTFS volume");
          }
       }
    }

    uint32_t const expected_size_in_lba =
        (uint32_t)(size_in_sectors - 1 > UINT32_MAX ? UINT32_MAX : size_in_sectors - 1);
    if (size_in_lba != expected_size_in_lba) {
        if (verbose) {
            warnx("Protective MBR does not cover whole disk: size in LBA is "
                  "%" PRIu32 ", expected %" PRIu32,
                  size_in_lba, expected_size_in_lba);
        }
        memcpy(mbr->partition_record[0].size_in_lba, &expected_size_in_lba, 4);
    }
}

static bool unused_entry(const struct GPTPartitionEntry *const entry) {
    const char zero[sizeof(entry->PartitionTypeGUID)] = {0};
    return memcmp(zero, &entry->PartitionTypeGUID, sizeof(zero)) == 0;
}

static const struct GPT *foreach_loop_helper_check(const struct GPT *gpt)
{
    assert(gpt != NULL);
    assert(gpt->header.SizeOfPartitionEntry >= sizeof(struct GPTPartitionEntry));
    assert(gpt->header.SizeOfPartitionEntry % _Alignof(struct GPTPartitionEntry) == 0);
    assert(gpt->entries != NULL);
    return gpt;
}

#define for_each_used_gpt_entry(i, gpt, entry)                                                                                    \
    for (i = UINT32_C(0), entry = foreach_loop_helper_check(gpt)->entries; (i < gpt->used_entries);                               \
         ((void)++i, (void)(entry = (const struct GPTPartitionEntry *)((const char *)entry + gpt->header.SizeOfPartitionEntry)))) \
            if (unused_entry(entry))                                                                                              \
                ;                                                                                                                 \
            else /* user code */

// Gets the GPT entry at the corresponding 0-based index.  Asserts if the index is out of bounds.
static struct GPTPartitionEntry *gpt_entry_mut(struct GPT *gpt, uint32_t i)
{
    assert(i < gpt->header.NumberOfPartitionEntries);
    return (struct GPTPartitionEntry *)(((char *)gpt->entries) + (uint64_t)i * (uint64_t)gpt->header.SizeOfPartitionEntry);
}

// Gets the GPT entry at the corresponding 0-based index.  Asserts if the index is out of bounds.
static const struct GPTPartitionEntry *gpt_entry(const struct GPT *gpt, uint32_t i)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
    // The returned pointer is (implicitly) casted back to const, and gpt_entry_mut() doesn't
    // make any writes of its own.
    return gpt_entry_mut((struct GPT *)gpt, i);
#pragma GCC diagnostic pop
}

static void fixup_gpt(struct GPT *gpt, uint64_t bytes_required) {
    uint64_t partition_entry_bytes = gpt_partition_table_bytes(&gpt->header);
    assert(partition_entry_bytes <= UINT_MAX);
    gpt->header.PartitionEntryArrayCRC32 =
        (uint32_t)crc32(0UL, (const unsigned char *)gpt->entries, (unsigned int)partition_entry_bytes);
    gpt->header.HeaderCRC32 = (uint32_t)crc32(0UL, (const unsigned char *)&gpt->header, sizeof(gpt->header));
    memset((char *)gpt->entries + partition_entry_bytes, 0, bytes_required - partition_entry_bytes);
}

static struct GPT *gpt_adjust_sectors(uint64_t sector_offset, uint32_t sector_size,
                                      uint64_t sectors, const struct GPT *gpt,
                                      uint64_t *allocated, bool verbose) {
    assert(sectors >= 6);
    assert(valid_sector_size(gpt->sector_size));
    assert(valid_sector_size(sector_size));
    assert(sector_offset == 1 || sector_offset == sectors - 1);

    uint64_t const size = sector_size * sectors;
    // This is the largest value FirstUsableLBA can be set to.
    uint64_t first_start_byte_offset = UINT64_MAX;
    uint64_t last_end_byte_offset = 0;
    uint32_t partition_that_starts_first = UINT32_MAX;
    uint32_t partition_that_ends_last = UINT32_MAX;
    uint32_t i;
    const struct GPTPartitionEntry *entry;
    for_each_used_gpt_entry (i, gpt, entry) {
        uint64_t start_byte_offset = entry->StartingLBA * gpt->sector_size;
        uint64_t end_byte_offset = (entry->EndingLBA + 1) * gpt->sector_size;
        if (verbose) {
            vlog("Partition consumes bytes [0x%" PRIX64 ", 0x%" PRIX64 ")", start_byte_offset, end_byte_offset);
        }
        if (start_byte_offset % sector_size != 0) {
            errx(EXIT_CONSTRAINT,
                 "Cannot preserve starting LBA for partition %" PRIu32 ": starting byte offset 0x%" PRIX64
                 " is not a multiple of %" PRIu32,
                 i + 1, start_byte_offset, sector_size);
        }
        if (end_byte_offset % sector_size != 0) {
            // TODO better error message
            errx(EXIT_CONSTRAINT,
                 "Cannot preserve ending LBA for partition %" PRIu32 ": ending byte offset 0x%" PRIX64
                 " is not a multiple of %" PRIu32,
                 i + 1, end_byte_offset, sector_size);
        }
        // If there is a tie, use the first partition for start and last partition for end.
        if (start_byte_offset < first_start_byte_offset) {
            first_start_byte_offset = start_byte_offset;
            partition_that_starts_first = i;
        }
        if (last_end_byte_offset <= end_byte_offset) {
            last_end_byte_offset = end_byte_offset;
            partition_that_ends_last = i;
        }
    }
    assert(first_start_byte_offset < last_end_byte_offset);
    uint64_t partition_table_bytes = gpt_partition_table_required_bytes(gpt, sector_size, gpt->used_entries);
    // Add sector_size once for backup GPT header.
    uint64_t backup_gpt_required_bytes = partition_table_bytes + sector_size;
    // Add sector_size twice: once for protective MBR, and once for
    // primary GPT header.  Use 64-bit multiplication to prevent wraparound
    // in the absurd case of 2**31-byte sectors.
    uint64_t primary_gpt_required_bytes = partition_table_bytes + UINT64_C(2) * sector_size;

    // If the disk is 1000 bytes and the sector size is 10 bytes, the
    // backup GPT would be at offset 990 bytes.  If the first unused
    // byte is offset 980, there are 10 bytes for the partition table.
    assert(size >= last_end_byte_offset);
    if (size - last_end_byte_offset < backup_gpt_required_bytes) {
        vlog("The first byte after partition %" PRIu32 " is at offset 0x%" PRIX64 ", leaving 0x%" PRIX64 " bytes remaining.",
             partition_that_ends_last + 1,
             last_end_byte_offset,
             size - last_end_byte_offset);
        vlog("However, at least %" PRIu64 " bytes are needed at the end to hold the backup GPT header and backup partition table entries.",
             backup_gpt_required_bytes);
        exit(EXIT_CONSTRAINT);
    }

    // If the disk is 1000 bytes, the sector size is 10 bytes, and the
    // partition table used 10 bytes, the first sector must not start
    // before byte 30.
    if (primary_gpt_required_bytes > first_start_byte_offset) {
        errx(EXIT_CONSTRAINT,
             "The protective MBR, primary GPT header, and primary partition table entries need the first %" PRIu64
             " bytes, but partition %" PRIu32 " starts at offset %" PRIu64 ".",
             primary_gpt_required_bytes,
             partition_that_starts_first + 1,
             first_start_byte_offset);
    }

    // The number of bytes available for the partition table.
    // Equal to the number of bytes between the primary GPT header
    // and the first partition or the last partition and the
    // backup GPT header, whichever is smaller.
    uint64_t const partition_table_available_bytes =
        min((size - sector_size) - last_end_byte_offset,
            first_start_byte_offset - UINT64_C(2) * sector_size);

    // The number of partition table entries that can be held.
    // Equal to the number of available bytes divided by the size
    // of an entry, rounded down.
    uint64_t const max_partition_entries =
        partition_table_available_bytes / gpt->header.SizeOfPartitionEntry;
    assert(max_partition_entries >= gpt->used_entries);

    // The number of entries to be written is equal to the number there is space for
    // or the number in the source GPT, whichever is smaller.
    uint32_t const new_entries = (uint32_t)min(max_partition_entries,
                                               (uint64_t)gpt->header.NumberOfPartitionEntries);
    // Update sizes for the new number of entries.
    partition_table_bytes = gpt_partition_table_required_bytes(gpt, sector_size, new_entries);
    primary_gpt_required_bytes = partition_table_bytes + UINT64_C(2) * sector_size;
    backup_gpt_required_bytes = partition_table_bytes + sector_size;

    struct GPT *new_gpt = malloc(sector_size);
    if (new_gpt == NULL) {
        err(EXIT_FAILURE, "malloc()");
    }
    uint64_t sectors_required = partition_table_bytes >> __builtin_ctz(sector_size);

    // Preserve the first usable byte if possible, but increase it
    // if fitting the pMBR and primary partition table requires it.
    uint64_t const first_usable_byte =
        max(gpt->header.FirstUsableLBA * gpt->sector_size,
            primary_gpt_required_bytes);

    // Preserve the last usable byte if possible, but decrease it
    // if fitting the backup partition table requires it.  Add 1
    // because LastUsableLBA is an inclusive bound.
    uint64_t const first_unusable_byte =
        min((gpt->header.LastUsableLBA + 1) * (uint64_t)gpt->sector_size,
            size - backup_gpt_required_bytes);

    memset(new_gpt, 0, sector_size);
    bool primary = sector_offset == 1;
    new_gpt->header = (struct GPTHeader){
        .Signature = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'},
        .Revision = gpt->header.Revision,
        .HeaderSize = gpt->header.HeaderSize,
        .HeaderCRC32 = 0,
        .Reserved = 0,
        .MyLBA = sector_offset,
        .AlternateLBA = primary ? sectors - 1 : 1,
        // Round in the direction of less usable space (up).
        .FirstUsableLBA = round_to_next_sector(first_usable_byte, sector_size) / sector_size,
        // Round in the direction of less usable space (down).
        // Subtract 1 because LastUsableLBA is an inclusive bound.
        .LastUsableLBA = first_unusable_byte / sector_size - 1,
        .DiskGUID = gpt->header.DiskGUID,
        .PartitionEntryLBA = primary ? 2 : sectors - 1 - sectors_required,
        .NumberOfPartitionEntries = new_entries,
        .SizeOfPartitionEntry = gpt->header.SizeOfPartitionEntry,
        .PartitionEntryArrayCRC32 = 0,
    };
    new_gpt->used_entries = new_entries;

    if (verbose) {
        vlog("LBA is 0x%" PRIX64 ", Alternate LBA is 0x%" PRIX64 ", allocating 0x%" PRIX64 " bytes",
             sector_offset, new_gpt->header.AlternateLBA, partition_table_bytes);
    }

    new_gpt->entries = malloc(partition_table_bytes);
    if (new_gpt->entries == NULL) {
        err(EXIT_FAILURE, "malloc(%" PRIu64 ")", partition_table_bytes);
    }
    for (i = 0; i < new_entries; ++i) {
        const struct GPTPartitionEntry *old_entry = gpt_entry(gpt, i);
        struct GPTPartitionEntry *new_entry = gpt_entry_mut(new_gpt, i);
        memcpy(new_entry, old_entry, gpt->header.SizeOfPartitionEntry);
        if (!unused_entry(old_entry)) {
            uint64_t start_byte_offset = old_entry->StartingLBA * gpt->sector_size;
            uint64_t end_byte_offset = (old_entry->EndingLBA + 1) * gpt->sector_size;
            new_entry->StartingLBA = start_byte_offset / sector_size;
            new_entry->EndingLBA = end_byte_offset / sector_size - 1;
        }
    }
    *allocated = partition_table_bytes;
    fixup_gpt(new_gpt, partition_table_bytes);
    assert(new_gpt->entries);
    return new_gpt;
}

/* Next up: Process GUID partition table (GPT) */
static uint64_t check_gpt_header(const struct GPTHeader *header, uint64_t sector_offset, uint32_t sector_size,
                                 uint64_t sectors, bool verbose) {
    assert(sectors >= 6);
    assert(sector_offset < sectors);
    if (memcmp(header->Signature, "EFI PART", 8) != 0) {
        if (verbose) {
            warnx("Wrong GPT signature");
        }
        return 0;
    }
    struct GPTHeader h = *header;
    h.HeaderCRC32 = 0;
    if (header->Revision != 0x00010000) {
        warnx("Partition table revision 0x%08" PRIX32 " != 0x00010000",
              header->Revision);
        return 0;
    }
    if (header->HeaderCRC32 != crc32(0UL, (const unsigned char *)&h, sizeof(h))) {
        warnx("Partition table at sector offset 0x%" PRIX64 " has bad CRC", header->MyLBA);
        return 0;
    }
    if (header->MyLBA != sector_offset) {
        warnx("Header at wrong offset: %" PRIu64 " != %" PRIu64, header->MyLBA, sector_offset);
    }

    if (header->HeaderSize < sizeof(*header)) {
        warnx("Header size %" PRIu32 " is less than %zu", header->HeaderSize, sizeof(*header));
        return 0;
    }

    if (header->HeaderSize > sector_size) {
        warnx("Header size %" PRIu32 " exceeds block size %" PRIu32, header->HeaderSize, sector_size);
        return 0;
    }

    if (header->SizeOfPartitionEntry < sizeof(struct GPTPartitionEntry)) {
        warnx("Partition entry size %" PRIu32 " is less than %zu", header->SizeOfPartitionEntry,
              sizeof(struct GPTPartitionEntry));
        return 0;
    }

    if (header->SizeOfPartitionEntry > sector_size) {
        warnx("Partition entry size %" PRIu32 " exceeds block size %" PRIu32, header->SizeOfPartitionEntry,
              sector_size);
        return 0;
    }

    if (header->SizeOfPartitionEntry > sizeof(struct GPTPartitionEntry)) {
        // This could be handled in the future if there is ever a need for
        // it, but it would make anything involving modification and
        // writing significantly more complex.
        errx(EXIT_UNSUPPORTED, "Partition entry size %" PRIu32 " is greater than %zu, which is not supported",
             header->SizeOfPartitionEntry, sizeof(struct GPTPartitionEntry));
    }

    if (header->FirstUsableLBA < 3) {
        warnx("First usable LBA %" PRIu64 " is less than 3", header->FirstUsableLBA);
        return 0;
    }

    if (header->LastUsableLBA < header->FirstUsableLBA) {
        warnx("No usable space: %" PRIu64 " < %" PRIu64, header->LastUsableLBA, header->FirstUsableLBA);
        return 0;
    }

    uint64_t partition_table_usable_space;

    if (sector_offset != 1) {
        if (header->AlternateLBA != 1) {
            warnx("Alternate LBA for backup GPT is at 0x%" PRIx64 ", not 1", header->AlternateLBA);
            return 0;
        }

        if (header->LastUsableLBA >= header->PartitionEntryLBA) {
            warnx("Last usable LBA 0x%" PRIX64 " not before backup partition table at 0x%" PRIX64,
                  header->LastUsableLBA, header->PartitionEntryLBA);
            return 0;
        }

        if (header->PartitionEntryLBA >= header->MyLBA) {
            warnx("Backup partition table LBA 0x%" PRIX64 " not before backup partition header at 0x%" PRIX64,
                  header->PartitionEntryLBA, header->MyLBA);
            return 0;
        }

        partition_table_usable_space = header->MyLBA - header->PartitionEntryLBA;
    } else {
        if (header->PartitionEntryLBA != 2) {
            warnx("Primary partition table at offset %" PRIu64 ", not 2", header->PartitionEntryLBA);
            return 0;
        }

        if (header->LastUsableLBA >= header->AlternateLBA) {
            warnx("Alternate partition header %" PRIu64 " overlaps usable space ending at %" PRIu64,
                  header->AlternateLBA, header->LastUsableLBA);
            return 0;
        }

        if (header->AlternateLBA >= sectors) {
            errx(EXIT_TRUNCATED,
                 "Alternate partition header %" PRIu64 " after end of device at %" PRIu64, header->AlternateLBA,
                 sectors);
        }

        partition_table_usable_space = header->FirstUsableLBA - header->PartitionEntryLBA;
    }

    if (header->NumberOfPartitionEntries < 1) {
        warnx("No partitions");
        return 0;
    }

    const uint64_t partition_table_bytes = gpt_partition_table_bytes(header);

    /* Sector counts bounds checked so wraparound is not possible */
    const uint64_t partition_table_usable_bytes = partition_table_usable_space * sector_size;
    if (partition_table_usable_bytes < partition_table_bytes) {
        warnx("Partition table does not fit in available space");
        return 0;
    }

    if (header->MyLBA != 1 && partition_table_usable_bytes - partition_table_bytes >= sector_size) {
        warnx("Backup partition table has too much padding: 0x%" PRIx64 " bytes padding, sector size 0x%" PRIx32,
              partition_table_usable_bytes - partition_table_bytes, sector_size);
        // Allow this case as it isn't technically going to prevent reading
        // the table and doesn't violate any invariants other code depends on.
        // At the very least it is better to try to read the backup GPT than
        // to give up and cause loss of user data.
    }

    if (partition_table_bytes > (1UL << 20)) {
        errx(EXIT_UNSUPPORTED, "Partition table size %" PRIu64 " exceeds 1MiB limit", partition_table_bytes);
    }

    if (verbose) {
        vlog("Partition table is %" PRIu64 " bytes long", partition_table_bytes);
    }
    return partition_table_bytes;
}

static struct GPT *read_and_check_gpt(int fd, uint64_t sector_offset, uint32_t sector_size, uint64_t sectors,
                                      bool verbose) {
    char *header_buffer = NULL, *entries_buffer = NULL;

    if (sector_offset >= sectors) {
        warnx("Request for GPT header at offset (in sectors) past end of "
              "disk: "
              "%" PRIu64 " >= %" PRIu64,
              sector_offset, sectors);
        return NULL;
    }

    // 1 for pMBR, 1 for primary header, 1 for primary table,
    // 1 for usable space, 1 for backup table, 1 for backup header
    if (sector_offset != 1 && sector_offset < 5) {
        warnx("Request for GPT at invalid offset %" PRIu64 ": offset must be 1 or >= 5", sector_offset);
        return NULL;
    }

    header_buffer = malloc(sector_size);
    if (header_buffer == NULL) {
        err(EXIT_FAILURE, "malloc(%" PRIu32 ")", sector_size * 2);
    }

    if (pread(fd, header_buffer, sector_size, (off_t)(sector_offset * sector_size)) != sector_size) {
        err(EXIT_IO, "Failed reading GPT header from block device or short read");
    }
    /* If we made it here, the MBR is okay.  We think! */
    struct GPTHeader *header = (struct GPTHeader *)header_buffer;

    const uint64_t partition_table_bytes = check_gpt_header(header, sector_offset, sector_size, sectors, verbose);
    if (partition_table_bytes == 0) {
        goto fail;
    }
    if (verbose) {
        vlog("Partition table entries consume %" PRIu64 " bytes", partition_table_bytes);
    }

    static_assert(offsetof(struct GPT, used_entries) == sizeof(struct GPTHeader), "struct def bug");
    memset(header_buffer + offsetof(struct GPT, used_entries), 0, sector_size - offsetof(struct GPT, used_entries));

    /* Cannot overflow because sectors == size / sector_size
     * and header->PartitionEntryLBA < sectors.  Will always fit in
     * "size" for the same reason. */
    const uint64_t partition_table_offset = sector_size * header->PartitionEntryLBA;
    const uint64_t needed = round_to_next_sector(partition_table_bytes, sector_size);
    entries_buffer = malloc(needed);
    if (entries_buffer == NULL) {
        err(EXIT_FAILURE, "allocating partition table");
    }
    ssize_t read = pread64(fd, entries_buffer, needed, (off_t)partition_table_offset);
    if (read == -1) {
        err(EXIT_SUCCESS, "Error reading partition table");
    }

    if (read != (ssize_t)needed) {
        errx(EXIT_IO, "Expected to read %" PRIu64 " bytes, but only got %zd bytes", partition_table_bytes, read);
    }

    if (header->PartitionEntryArrayCRC32 !=
        crc32(0UL, (const unsigned char *)entries_buffer, (uint32_t)partition_table_bytes)) {
        warnx("Partition table array at sector offset 0x%" PRIX64 " has bad CRC", header->PartitionEntryLBA);
        goto fail;
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
    struct GPT *gpt = (struct GPT *)header_buffer;
#pragma GCC diagnostic pop

    gpt->entries = (struct GPTPartitionEntry *)entries_buffer;
    gpt->sector_size = sector_size;
    uint32_t used_entries = 0, i;
    gpt->used_entries = header->NumberOfPartitionEntries;
    const struct GPTPartitionEntry *entry;
    for_each_used_gpt_entry (i, gpt, entry) {
        if (entry->StartingLBA < header->FirstUsableLBA) {
            warnx("Starting LBA %" PRIu64 " less than first usable LBA %" PRIu64, entry->StartingLBA,
                  header->FirstUsableLBA);
            goto fail;
        }
        if (entry->EndingLBA > header->LastUsableLBA) {
            warnx("Ending LBA %" PRIu64 " greater than last usable LBA %" PRIu64, entry->EndingLBA,
                  header->LastUsableLBA);
            goto fail;
        }
        used_entries = i + 1;
    }
    if (used_entries == 0) {
        // This corresponds to a disk with no partitions at all, which
        // makes no real sense.
        errx(EXIT_UNSUPPORTED, "No used partitions");
    }
    gpt->used_entries = used_entries;
    return gpt;
fail:
    free(entries_buffer);
    free(header_buffer);
    return NULL;
}

static void print_gpts(struct GPT *gpt) {
    struct GPTHeader *header = &gpt->header;
    vlog("GPT Partition Table Header:");
    vlog("  Revision:                 0x%08" PRIx32, header->Revision);
    vlog("  HeaderSize:               %" PRIu32, header->HeaderSize);
    vlog("  HeaderCRC32:              %" PRIu32, header->HeaderCRC32);
    vlog("  Reserved:                 %" PRIu32, header->Reserved);
    vlog("  MyLBA:                    %" PRIu64, header->MyLBA);
    vlog("  AlternateLBA:             %" PRIu64, header->AlternateLBA);
    vlog("  FirstUsableLBA:           %" PRIu64, header->FirstUsableLBA);
    vlog("  LastUsableLBA:            %" PRIu64, header->LastUsableLBA);
    vlog("  DiskGUID[16]:             "
         "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
         header->DiskGUID.first, header->DiskGUID.second, header->DiskGUID.third,
         header->DiskGUID.rest[0], header->DiskGUID.rest[1], header->DiskGUID.rest[2], header->DiskGUID.rest[3],
         header->DiskGUID.rest[4], header->DiskGUID.rest[5], header->DiskGUID.rest[6], header->DiskGUID.rest[7]);
    vlog("  PartitionEntryLBA:        %" PRIu64, header->PartitionEntryLBA);
    vlog("  NumberOfPartitionEntries: %" PRIu32, header->NumberOfPartitionEntries);
    vlog("  SizeOfPartitionEntry:     %" PRIu32, header->SizeOfPartitionEntry);
    vlog("  PartitionEntryArrayCRC32: %" PRIu32, header->PartitionEntryArrayCRC32);
    uint32_t i;
    const struct GPTPartitionEntry *entry;
    for_each_used_gpt_entry (i, gpt, entry) {
        vlog("GPT Partition Table Entry:");
        vlog("  PartitionTypeGUID:        %08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             entry->PartitionTypeGUID.first, entry->PartitionTypeGUID.second, entry->PartitionTypeGUID.third,
             entry->PartitionTypeGUID.rest[0], entry->PartitionTypeGUID.rest[1], entry->PartitionTypeGUID.rest[2], entry->PartitionTypeGUID.rest[3],
             entry->PartitionTypeGUID.rest[4], entry->PartitionTypeGUID.rest[5], entry->PartitionTypeGUID.rest[6], entry->PartitionTypeGUID.rest[7]);
        vlog("  UniquePartitionGUID:      %08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             entry->UniquePartitionGUID.first, entry->UniquePartitionGUID.second, entry->UniquePartitionGUID.third,
             entry->UniquePartitionGUID.rest[0], entry->UniquePartitionGUID.rest[1], entry->UniquePartitionGUID.rest[2], entry->UniquePartitionGUID.rest[3],
             entry->UniquePartitionGUID.rest[4], entry->UniquePartitionGUID.rest[5], entry->UniquePartitionGUID.rest[6], entry->UniquePartitionGUID.rest[7]);
        vlog("  StartingLBA:              0x%" PRIx64, entry->StartingLBA);
        vlog("  EndingLBA:                0x%" PRIx64, entry->EndingLBA);
        vlog("  Attributes:               0x%016" PRIx64, entry->Attributes);
    }
}

static void sync_pwrite(int fd, const void *data, uint64_t length, uint64_t offset) {
    assert(fd >= 0);
    assert(data != NULL);
    assert(length <= SSIZE_MAX);
    assert((off_t)offset >= 0);
    assert((uint64_t)(off_t)offset == offset);

    ssize_t res = pwrite64(fd, data, (size_t)length, (off_t)offset);
    if (res == -1)
        err(EXIT_IO, "write error");
    if ((size_t)res != length)
        errx(EXIT_IO, "short write: expected to write %" PRIu64 " bytes, but only wrote %zd", length, res);
}

static void create_new_gpt(int fd, uint64_t sector_to_write, uint64_t sectors, uint32_t sector_size,
                           struct GPT *gpt, struct Mbr *mbr, bool verbose) {
    struct GPT *new_gpt;
    uint64_t to_write = 0;
    new_gpt = gpt_adjust_sectors(sector_to_write, sector_size, sectors, gpt, &to_write, verbose);
    assert(new_gpt->entries);
    assert(check_gpt_header(&new_gpt->header, sector_to_write, sector_size, sectors, false) != 0);
    assert(new_gpt->entries);
    if (verbose) {
        print_gpts(new_gpt);
    }
    assert(new_gpt->entries);
    assert(to_write);
    void *entries = new_gpt->entries;
    assert(entries != NULL);
    memset((char *)new_gpt + offsetof(struct GPT, used_entries), 0, sector_size - offsetof(struct GPT, used_entries));
    const uint64_t big_size = 4096;
    char *buf = malloc(big_size);
    if (buf == NULL)
        err(EXIT_FAILURE, "malloc(%" PRIu64 ")", big_size);
    memset(buf, 0, big_size);
    if (sector_to_write == sectors - 1) {
        // Writing backup GPT.  gpt_adjust_sectors() checked that there is no user
        // data in the area going to be overwritten.
        sync_pwrite(fd, buf, big_size, sector_size * sectors - big_size);
    } else if (sector_to_write == 1) {
        // Writing primary GPT.
        if (sector_size == big_size) {
            assert(gpt->sector_size == 512);
            // Write the pMBR followed by (4096 - 512) bytes of zeroes in a single (atomic) operation.
            // This clobbers the 512-byte-sector GPT header, and there is no GPT header for 4096-byte sectors
            // or otherwise this code would not be running.  Note that mbr points to sector_size valid bytes here.
            sync_pwrite(fd, mbr, sector_size, 0);
        } else {
            assert(sector_size == 512);
            assert(gpt->sector_size == big_size);
            // Write the pMBR.
            sync_pwrite(fd, mbr, sector_size, 0);
            // Overwrite the 512-byte-sector GPT header, which definitely did not exist.
            sync_pwrite(fd, buf, 512, 512);
            // Overwrite the 4096-byte-sector GPT header.  If this existed, a backup GPT (for 512-byte-sectors) has been
            // written.  This is not atomic, but overwriting the first sector of the header (its own header) _is_
            // atomic.
            sync_pwrite(fd, buf, big_size, big_size);
        }
    } else {
        assert(0);
    }
    sync_pwrite(fd, entries, to_write, new_gpt->header.PartitionEntryLBA * sector_size);
    sync_pwrite(fd, new_gpt, sector_size, new_gpt->header.MyLBA * sector_size);
    free(buf);
    free(new_gpt);
    free(entries);
}

// Refuse to operate on a partition.  There is no convienient ioctl() interface
// to check if a device is a partition, so sysfs must be used instead.  The
// Linux block device API really is terrible :(.
static void check_whole_block_device(struct stat *buf)
{
    char *buffer;
    struct stat new_statbuf;
    int buflen = asprintf(&buffer, "/sys/dev/block/%ju:%ju/partition",
                         (uintmax_t)major(buf->st_rdev),
                         (uintmax_t)minor(buf->st_rdev));
    if (buflen < (int)sizeof("/sys/dev/block/1:1/partition") - 1) {
        errx(1, "asprintf() problem");
    }
    switch (lstat(buffer, &new_statbuf)) {
    case 0:
        if ((new_statbuf.st_mode & S_IFMT) != S_IFREG) {
            errx(1, "bad sysfs entry (\"partition\" attribute is not regular file)");
        } else {
            errx(1, "Refusing to operate on partition: pass the whole block device instead");
        }
    case -1:
        if (errno == ENOENT) {
            free(buffer);
            return;
        }
        err(1, "lstat(\"%s\") failed: cannot check if device is a partition", buffer);
    default:
        abort();
    }
}

static const struct option options[] = {
    { "unsafe-skip-bpb-check", no_argument, NULL, 'b' },
    { "verbose", no_argument, NULL, 'v' },
    { NULL, 0, NULL, 0 },
};

int main(int argc, char **argv) {
    const char *dev = NULL;
    bool verbose = false;
    bool skip_bpb_check = false;
    enum {
        MODE_UNKNOWN,
        MODE_CHECK,
        MODE_FIX,
    } mode;

    log_file = stderr;
    for (;;) {
        int longindex;
        int lastind = optind;
        switch (getopt_long(argc, argv, "+", options, &longindex)) {
        case '?':
        case ':':
            goto usage;
        case 'b':
            skip_bpb_check = true;
            break;
        case 'v':
            verbose = true;
            break;
        case 'h':
            log_file = stdout;
            goto usage;
        case -1:
            /* Check if "--" was provided */
            if (argc - optind != 2) {
                goto usage;
            }
            if (optind - lastind != 1 || lastind < 1 || strcmp(argv[lastind], "--") != 0) {
                for (int j = optind; j < argc; ++j) {
                    if (argv[j][0] == '-') {
                        errx(1, "Use '--' to end options if a non-option argument starts with '-'");
                    }
                }
            }
            if (strcmp(argv[optind], "check") == 0) {
                mode = MODE_CHECK;
            } else if (strcmp(argv[optind], "fix") == 0) {
                mode = MODE_FIX;
            } else {
                goto usage;
            }
            dev = argv[optind + 1];
            // blame C for not having nested break
            goto okay;
        default:
            abort();
        }
    }
okay:;

    int const fd = open(dev, O_CLOEXEC | O_NOCTTY |O_RDONLY);
    if (fd < 0) {
        err(EXIT_FAILURE, "Cannot open %s", dev);
    }

    if (fd < 3) {
        // Caller messed up!  We do _not_ want to accidentally overwrite the block device
        // with a message, so exit immediately before printing anything.
        return EXIT_IO;
    }

    struct stat statbuf;
    if (fstat(fd, &statbuf)) {
        err(EXIT_IO, "fstat()");
    }

    if ((statbuf.st_mode & S_IFMT) != S_IFBLK) {
        errx(EXIT_FAILURE, "%s is not a block device", dev);
    }

    check_whole_block_device(&statbuf);
    // Avoid racing with systemd-udevd
    if (flock(fd, LOCK_EX)) {
        err(EXIT_FAILURE, "%s cannot be locked", dev);
    }

    int signed_sector_size;
    if (!bool_ioctl(fd, BLKSSZGET, &signed_sector_size)) {
        err(EXIT_FAILURE, "ioctl(%d, BLKSSZGET, %p)", fd, (void *)&signed_sector_size);
    }

    const uint32_t sector_size = (uint32_t)signed_sector_size; // size in bytes of a sector

    if (sector_size != 512 && sector_size != 4096) {
        errx(EXIT_UNSUPPORTED, "block size is 0x%" PRIX32 " is neither 512 or 4096 and we don't support that",
             sector_size);
    }

    uint64_t size; // size in bytes
    if (!bool_ioctl(fd, BLKGETSIZE64, &size)) {
        err(EXIT_IO, "ioctl(%d, BLKGETSIZE64, %p)", fd, (void *)&size);
    }

    if ((off_t)size < 0 || (uint64_t)(off_t)size != size) {
        errx(EXIT_UNSUPPORTED, "size 0x%" PRIx64 " does not fit in off_t", size);
    }

    if (size % sector_size != 0) {
        errx(EXIT_UNSUPPORTED, "size 0x%" PRIx64 " is not multiple of sector size 0x%" PRIx32, size, sector_size);
    }
    const uint64_t sectors = size / sector_size; // number of sectors
    if (sectors < 6) {
        errx(EXIT_UNSUPPORTED, "Device has too few sectors for GPT: need 6, found %" PRIu64, sectors);
    }

    // Ensure the kernel buffer cache is in sync with the underlying device.
    if (fsync(fd)) {
        err(EXIT_IO, "fsync(\"%s\")", dev);
    }

    char *buffer = malloc(sector_size);
    if (pread(fd, buffer, sector_size, 0) != sector_size) {
        err(EXIT_IO, "Failed reading MBR from block device or short read");
    }
    memset(buffer + sizeof(struct Mbr), 0, sector_size - sizeof(struct Mbr));
    static_assert(sizeof(struct Mbr) <= 512, "Bad MBR size");
    check_mbr((struct Mbr *)buffer, sectors, skip_bpb_check, verbose);
    if (verbose) {
        vlog("Sector size is %" PRIu32 ", device size is 0x%" PRIx64 ", 0x%" PRIx64 " sectors", sector_size, size, sectors);
    }

    struct GPT *gpt = read_and_check_gpt(fd, 1, sector_size, sectors, verbose);
    if (gpt == NULL) {
        gpt = read_and_check_gpt(fd, sectors - 1, sector_size, sectors, verbose);
    }
    bool try_changing = gpt == NULL;
    if (try_changing) {
        if (size % 4096) {
            errx(EXIT_UNSUPPORTED, "Disk size is not a multiple of 4096");
        }
        uint32_t old_sector_size = (sector_size == 4096) ? 512 : 4096;
        uint64_t old_sectors = (sector_size == 4096) ? (sectors << 3) : (sectors >> 3);
        bool used_primary = true;
        gpt = read_and_check_gpt(fd, 1, old_sector_size, old_sectors, verbose);
        if (gpt == NULL) {
            used_primary = false;
            gpt = read_and_check_gpt(fd, old_sectors - 1, old_sector_size, old_sectors, verbose);
        }
        if (gpt == NULL) {
            errx(EXIT_INVALID_GPT, "Cannot find valid GPT");
        }
        if (mode == MODE_FIX) {
            // Reopen device for writing
            {
                char *buf;
                int r = asprintf(&buf, "/proc/self/fd/%d", fd);
                if (r < (int)sizeof("/proc/self/fd/")) {
                    err(1, "asprintf()");
                }
                int const new_fd = open(buf, O_CLOEXEC | O_NOCTTY | O_RDWR | O_SYNC | O_EXCL);
                if (new_fd == -1) {
                    err(1, "cannot reopen block device %s for writing", dev);
                }
                if (dup3(new_fd, fd, O_CLOEXEC) == -1) {
                    err(1, "Cannot duplicate %d over %d", new_fd, fd);
                }
                if (close(new_fd) == -1) {
                    err(1, "Cannot close %d", new_fd);
                }
                free(buf);
            }
            warnx("Found GPT with different sector size, altering");
            // "First, do no harm": always start by overwriting the GPT we
            // did *not* use, so there is always at least one good GPT!
            create_new_gpt(fd, used_primary ? sectors - 1 : 1, sectors, sector_size, gpt,
                           (struct Mbr *)buffer, verbose);
            create_new_gpt(fd, used_primary ? 1 : sectors - 1, sectors, sector_size, gpt,
                           (struct Mbr *)buffer, verbose);
        } else {
            assert(mode == MODE_CHECK);
            errx(EXIT_NEEDS_FIX, "GPT needs to be updated for new sector size");
        }
    } else {
        if (mode == MODE_CHECK && verbose) {
            print_gpts(gpt);
        }
    }

    free(buffer);
    free(gpt->entries);
    free(gpt);

    if (mode == MODE_FIX && try_changing) {
        if (!bool_ioctl(fd, BLKRRPART, NULL) && errno != EINVAL) {
            err(EXIT_IO, "Cannot reload kernel partition table");
        }
    }

    close(fd);
    fflush(NULL);
    if (ferror(stdout)) {
        err(EXIT_IO, "Write error on stdout");
    }
    return 0;

usage:
    vlog("Usage:");
    vlog(" gptfix [OPTIONS] [--] [check|fix] block-device-path\n%s:", program_invocation_name);
    vlog("Options:");
    vlog("--unsafe-skip-bpb-check              Do not check for a BIOS Parameter Block (BPB) before making changes.");
    vlog("                                     WARNING: in some (EXTREMELY rare) cases, this can corrupt a");
    vlog("                                     FAT or NTFS file system that happens to have a valid GPT.");
    vlog("--verbose                            Log a bunch of messages to stderr.");
    vlog("--help                               Print this message.");
    return log_file == stderr ? EXIT_FAILURE : 0;
}
