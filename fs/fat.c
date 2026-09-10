/*
 * Copyright (C) 2018 bzt (bztsrc@github.com)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "../driver/sd/sd.h"
#include "../driver/uart/uart.h"
#include "../core/mm.h"
#include "../lib/stdlib.h"
#include "../lib/printf.h"
#include "fat.h"

/* Partition start LBA */
unsigned int partitionlba = 0;

/*
 * BIOS Parameter Block / FAT32 boot sector fields.
 */
typedef struct {
    char            jmp[3];
    char            oem[8];

    unsigned char   bps0;
    unsigned char   bps1;
    unsigned char   spc;
    unsigned short  rsc;
    unsigned char   nf;

    unsigned char   nr0;
    unsigned char   nr1;

    unsigned short  ts16;
    unsigned char   media;
    unsigned short  spf16;

    unsigned short  spt;
    unsigned short  nh;

    unsigned int    hs;
    unsigned int    ts32;
    unsigned int    spf32;
    unsigned int    flg;
    unsigned int    rc;

    char            vol[6];
    char            fst[8];
    char            dmy[20];
    char            fst2[8];
} __attribute__((packed)) bpb_t;

/*
 * FAT directory entry (32 bytes).
 */
typedef struct {
    unsigned char   name[8];
    unsigned char   ext[3];
    unsigned char   attr;
    unsigned char   ntres;
    unsigned char   crt_time_tenth;
    unsigned short  crt_time;
    unsigned short  crt_date;
    unsigned short  lst_acc_date;
    unsigned short  cl_hi;
    unsigned short  wrt_time;
    unsigned short  wrt_date;
    unsigned short  cl_lo;
    unsigned int    size;
} __attribute__((packed)) fatdir_t;

/*
 * FAT Long File Name entry (32 bytes).
 */
typedef struct {
    unsigned char   order;
    unsigned short  name1[5];
    unsigned char   attr;
    unsigned char   type;
    unsigned char   checksum;
    unsigned short  name2[6];
    unsigned short  zero;
    unsigned short  name3[2];
} __attribute__((packed)) fat_lfn_t;

/* Static sector buffers to avoid heap fragmentation */
static bpb_t bpb_data;
bpb_t *bpb = &bpb_data;

static unsigned int cached_fat_lba = 0xFFFFFFFF;
static unsigned int fat_cache_buf[128]; // 512 bytes

unsigned int lastFileDimension = 0;

unsigned int getLastFileSize(void)
{
    return lastFileDimension;
}

/**
 * Reads the FAT entry for a given cluster using a 512-byte sector cache.
 */
static unsigned int fat_get_next_cluster(unsigned int cluster)
{
    if(!partitionlba || bpb->rsc == 0)
        return 0x0FFFFFF7;

    unsigned int fat_offset_bytes = cluster * 4;
    unsigned int fat_sector = fat_offset_bytes / 512;
    unsigned int fat_lba = partitionlba + bpb->rsc + fat_sector;
    unsigned int entry_index = (fat_offset_bytes % 512) / 4;

    if(fat_lba != cached_fat_lba)
    {
        if(!sd_readblock(fat_lba, fat_cache_buf, 1))
        {
            cached_fat_lba = 0xFFFFFFFF;
            return 0x0FFFFFF7;
        }
        cached_fat_lba = fat_lba;
    }

    return fat_cache_buf[entry_index] & 0x0FFFFFFF;
}

/**
 * Get starting LBA of FAT partition from MBR.
 */
int fat_getpartition(void)
{
    if(partitionlba != 0)
        return 1;

    unsigned int mbr_buf[128]; // 512 bytes on stack
    unsigned char *mbr = (unsigned char *)mbr_buf;

    if(!sd_readblock(0, mbr_buf, 1))
    {
        uart_puts("\r\n[FAT] ERROR: Unable to read MBR");
        return 0;
    }

    if(mbr[510] != 0x55 || mbr[511] != 0xAA)
    {
        uart_puts("\r\n[FAT] ERROR: Bad MBR signature");
        return 0;
    }

    /* Check 4 partition entries */
    unsigned int part_lba = 0;
    for(int i = 0; i < 4; i++)
    {
        int off = 0x1BE + (i * 16);
        unsigned char type = mbr[off + 4];

        /* FAT32 LBA (0x0C), FAT32 CHS (0x0B), FAT16 LBA (0x0E) */
        if(type == 0x0C || type == 0x0B || type == 0x0E || type == 0x06)
        {
            part_lba = ((unsigned int)mbr[off + 8]) |
                       ((unsigned int)mbr[off + 9] << 8) |
                       ((unsigned int)mbr[off + 10] << 16) |
                       ((unsigned int)mbr[off + 11] << 24);
            if(part_lba != 0)
                break;
        }
    }

    if(part_lba == 0)
    {
        uart_puts("\r\n[FAT] ERROR: No valid FAT partition found in MBR");
        return 0;
    }

    partitionlba = part_lba;
    printf("\r\n[FAT] Partition LBA: 0x%x", partitionlba);

    /* Read Volume Boot Record */
    if(!sd_readblock(partitionlba, (unsigned int *)bpb, 1))
    {
        uart_puts("\r\n[FAT] ERROR: Unable to read boot record");
        partitionlba = 0;
        return 0;
    }

    /* Basic BPB sanity checks */
    unsigned int bps = bpb->bps0 | ((unsigned int)bpb->bps1 << 8);
    if(bps != 512 || bpb->spc == 0 || bpb->rsc == 0 || bpb->nf == 0 || bpb->spf32 == 0)
    {
        printf("\r\n[FAT] ERROR: Unsupported BPB parameters (bps=%u, spc=%u, rsc=%u, spf32=%u)",
               bps, bpb->spc, bpb->rsc, bpb->spf32);
        partitionlba = 0;
        return 0;
    }

    cached_fat_lba = 0xFFFFFFFF;
    return 1;
}

static char to_upper_char(char c)
{
    return (c >= 'a' && c <= 'z') ? (c - 32) : c;
}

static int strcasecmp_custom(const char *s1, const char *s2)
{
    while(*s1 && *s2)
    {
        char c1 = to_upper_char(*s1);
        char c2 = to_upper_char(*s2);
        if(c1 != c2)
            return c1 - c2;
        s1++;
        s2++;
    }
    return to_upper_char(*s1) - to_upper_char(*s2);
}

/**
 * Converts user input (e.g. "kernel7.img" or "CMDLINE.TXT") into 11-byte space-padded FAT 8.3 format.
 */
static void filename_to_83(const char *fn, char *out_83)
{
    for(int i = 0; i < 11; i++)
        out_83[i] = ' ';

    int has_dot = 0;
    for(int i = 0; fn[i] != '\0'; i++)
    {
        if(fn[i] == '.')
        {
            has_dot = 1;
            break;
        }
    }

    /* Already in 8.3 format (e.g. "KERNEL7 IMG") */
    if(!has_dot && strlen(fn) == 11)
    {
        for(int i = 0; i < 11; i++)
            out_83[i] = to_upper_char(fn[i]);
        return;
    }

    int i = 0, name_idx = 0;
    while(fn[i] != '\0' && fn[i] != '.' && name_idx < 8)
    {
        out_83[name_idx++] = to_upper_char(fn[i++]);
    }

    while(fn[i] != '\0' && fn[i] != '.')
        i++;

    if(fn[i] == '.')
    {
        i++;
        int ext_idx = 8;
        while(fn[i] != '\0' && ext_idx < 11)
        {
            out_83[ext_idx++] = to_upper_char(fn[i++]);
        }
    }
}

/**
 * Extracts 13 characters from an LFN entry into a buffer.
 */
static void lfn_extract(const fat_lfn_t *lfn, char *lfn_buf)
{
    unsigned int seq = (lfn->order & 0x1F);
    if(seq == 0 || seq > 20)
        return;

    unsigned int base_idx = (seq - 1) * 13;

    for(int i = 0; i < 5; i++)
    {
        if(base_idx + i < 255)
        {
            unsigned short ch = lfn->name1[i];
            lfn_buf[base_idx + i] = (ch == 0 || ch == 0xFFFF) ? '\0' : (char)(ch & 0xFF);
        }
    }
    for(int i = 0; i < 6; i++)
    {
        if(base_idx + 5 + i < 255)
        {
            unsigned short ch = lfn->name2[i];
            lfn_buf[base_idx + 5 + i] = (ch == 0 || ch == 0xFFFF) ? '\0' : (char)(ch & 0xFF);
        }
    }
    for(int i = 0; i < 2; i++)
    {
        if(base_idx + 11 + i < 255)
        {
            unsigned short ch = lfn->name3[i];
            lfn_buf[base_idx + 11 + i] = (ch == 0 || ch == 0xFFFF) ? '\0' : (char)(ch & 0xFF);
        }
    }
}

/**
 * Builds standard dot-separated 8.3 filename string (e.g. "KERNEL7.IMG") from directory entry.
 */
static void dir_to_shortname(const fatdir_t *dir, char *out)
{
    int pos = 0;
    for(int i = 0; i < 8 && dir->name[i] != ' '; i++)
        out[pos++] = dir->name[i];

    if(dir->ext[0] != ' ')
    {
        out[pos++] = '.';
        for(int i = 0; i < 3 && dir->ext[i] != ' '; i++)
            out[pos++] = dir->ext[i];
    }
    out[pos] = '\0';
}

/**
 * Find a file in the root directory across all clusters.
 * Supports standard filenames ("kernel7.img"), 8.3 ("KERNEL7.IMG", "KERNEL7 IMG"),
 * and long filenames ("bcm2709-rpi-2-b.dtb").
 */
unsigned int fat_getcluster_ex(const char *fn, unsigned int *out_size)
{
    if(!fat_getpartition() || !fn || fn[0] == '\0')
        return 0;

    char target_83[11];
    filename_to_83(fn, target_83);

    unsigned int data_sec = partitionlba + bpb->rsc + ((unsigned int)bpb->nf * bpb->spf32);
    unsigned int dir_cluster = bpb->rc;

    /* Buffer for reading one cluster of directory entries */
    unsigned int dir_sectors = bpb->spc;
    unsigned int dir_bytes = dir_sectors * 512;
    fatdir_t *dir_buf = (fatdir_t *)alloc(dir_bytes);
    if(!dir_buf)
    {
        uart_puts("\r\n[FAT] ERROR: Unable to allocate directory buffer");
        return 0;
    }

    char lfn_buf[256];
    memset(lfn_buf, 0, sizeof(lfn_buf));
    int has_lfn = 0;

    while(dir_cluster >= 2 && dir_cluster < 0x0FFFFFF8)
    {
        unsigned int lba = data_sec + ((dir_cluster - 2) * bpb->spc);

        if(!sd_readblock(lba, (unsigned int *)dir_buf, dir_sectors))
        {
            uart_puts("\r\n[FAT] ERROR: Unable to read directory cluster");
            return 0;
        }

        fatdir_t *entry = dir_buf;
        unsigned int entries_per_cluster = dir_bytes / sizeof(fatdir_t);

        for(unsigned int i = 0; i < entries_per_cluster; i++, entry++)
        {
            if(entry->name[0] == 0x00)
                return 0; /* End of directory */

            if(entry->name[0] == 0xE5)
            {
                has_lfn = 0;
                memset(lfn_buf, 0, sizeof(lfn_buf));
                continue;
            }

            /* LFN entry */
            if(entry->attr == FAT_ATTR_LFN)
            {
                lfn_extract((const fat_lfn_t *)entry, lfn_buf);
                has_lfn = 1;
                continue;
            }

            /* Skip volume label */
            if(entry->attr & FAT_ATTR_VOLUME_ID)
            {
                has_lfn = 0;
                memset(lfn_buf, 0, sizeof(lfn_buf));
                continue;
            }

            char short_name[16];
            dir_to_shortname(entry, short_name);

            int matched = 0;

            /* Check 1: 11-byte 8.3 match */
            if(memcmp(entry->name, target_83, 11) == 0)
                matched = 1;

            /* Check 2: Short name string match */
            if(!matched && strcasecmp_custom(fn, short_name) == 0)
                matched = 1;

            /* Check 3: LFN match */
            if(!matched && has_lfn && strcasecmp_custom(fn, lfn_buf) == 0)
                matched = 1;

            if(matched)
            {
                unsigned int cluster = ((unsigned int)entry->cl_hi << 16) | entry->cl_lo;
                if(out_size)
                    *out_size = entry->size;

                lastFileDimension = entry->size;
                return cluster;
            }

            has_lfn = 0;
            memset(lfn_buf, 0, sizeof(lfn_buf));
        }

        dir_cluster = fat_get_next_cluster(dir_cluster);
    }

    return 0;
}

unsigned int fat_getcluster(char *fn)
{
    return fat_getcluster_ex(fn, NULL);
}

/**
 * Reads a file directly to a destination physical address.
 * Coalesces contiguous clusters for fast multi-block SD transfers.
 */
unsigned int fat_readfile_to(unsigned int cluster, void *dest, unsigned int max_size)
{
    if(!fat_getpartition() || cluster < 2 || !dest || max_size == 0)
        return 0;

    unsigned int data_sec = partitionlba + bpb->rsc + ((unsigned int)bpb->nf * bpb->spf32);
    unsigned int cluster_bytes = bpb->spc * 512;
    unsigned char *out = (unsigned char *)dest;
    unsigned int total_read = 0;

    while(cluster >= 2 && cluster < 0x0FFFFFF8 && total_read < max_size)
    {
        unsigned int run_start = cluster;
        unsigned int run_clusters = 1;
        unsigned int next_cl = fat_get_next_cluster(cluster);

        /* Coalesce contiguous clusters up to 128 sectors (64 KB) */
        while(next_cl == cluster + 1 &&
              ((run_clusters + 1) * bpb->spc <= 128) &&
              (total_read + (run_clusters + 1) * cluster_bytes <= max_size))
        {
            run_clusters++;
            cluster = next_cl;
            next_cl = fat_get_next_cluster(cluster);
        }

        unsigned int lba = data_sec + ((run_start - 2) * bpb->spc);
        unsigned int sectors_to_read = run_clusters * bpb->spc;
        unsigned int bytes_to_read = sectors_to_read * 512;

        if(total_read + bytes_to_read > max_size)
        {
            sectors_to_read = (max_size - total_read + 511) / 512;
            bytes_to_read = sectors_to_read * 512;
        }

        if(!sd_readblock(lba, (unsigned int *)out, sectors_to_read))
        {
            printf("\r\n[FAT] ERROR: Read error at cluster 0x%x (LBA 0x%x)", run_start, lba);
            break;
        }

        out += bytes_to_read;
        total_read += bytes_to_read;
        cluster = next_cl;
    }

    lastFileDimension = total_read;
    return total_read;
}

/**
 * Read file into a heap-allocated buffer.
 */
unsigned int fat_readfile(unsigned int cluster)
{
    if(lastFileDimension == 0)
        lastFileDimension = 1024 * 1024; // fallback 1 MB

    unsigned int alloc_size = ((lastFileDimension + 511) / 512) * 512 + 512;
    unsigned char *buf = (unsigned char *)alloc(alloc_size);
    if(!buf)
    {
        uart_puts("\r\n[FAT] ERROR: Unable to allocate buffer for file");
        return 0;
    }

    unsigned int read_bytes = fat_readfile_to(cluster, buf, alloc_size);
    buf[read_bytes] = '\0';
    return (unsigned int)buf;
}

/**
 * List all entries in the root directory across all clusters.
 * Displays attributes, start cluster, file size, 8.3 name and long filename.
 */
void fat_listdirectory(void)
{
    if(!fat_getpartition())
        return;

    unsigned int data_sec = partitionlba + bpb->rsc + ((unsigned int)bpb->nf * bpb->spf32);
    unsigned int dir_cluster = bpb->rc;
    unsigned int dir_sectors = bpb->spc;
    unsigned int dir_bytes = dir_sectors * 512;

    fatdir_t *dir_buf = (fatdir_t *)alloc(dir_bytes);
    if(!dir_buf)
    {
        uart_puts("\r\n[FAT] ERROR: Unable to allocate directory buffer");
        return;
    }

    char lfn_buf[256];
    memset(lfn_buf, 0, sizeof(lfn_buf));
    int has_lfn = 0;

    printf("\r\n%-6s %-10s %-10s %-12s %s\r\n", "Attrib", "Cluster", "Size", "8.3 Name", "Long Name");
    printf("----------------------------------------------------------------------\r\n");

    while(dir_cluster >= 2 && dir_cluster < 0x0FFFFFF8)
    {
        unsigned int lba = data_sec + ((dir_cluster - 2) * bpb->spc);

        if(!sd_readblock(lba, (unsigned int *)dir_buf, dir_sectors))
        {
            uart_puts("\r\n[FAT] ERROR: Unable to read directory cluster");
            return;
        }

        fatdir_t *entry = dir_buf;
        unsigned int entries_per_cluster = dir_bytes / sizeof(fatdir_t);

        for(unsigned int i = 0; i < entries_per_cluster; i++, entry++)
        {
            if(entry->name[0] == 0x00)
                return; /* End of directory */

            if(entry->name[0] == 0xE5)
            {
                has_lfn = 0;
                memset(lfn_buf, 0, sizeof(lfn_buf));
                continue;
            }

            if(entry->attr == FAT_ATTR_LFN)
            {
                lfn_extract((const fat_lfn_t *)entry, lfn_buf);
                has_lfn = 1;
                continue;
            }

            /* Skip volume ID entries */
            if(entry->attr & FAT_ATTR_VOLUME_ID)
            {
                has_lfn = 0;
                memset(lfn_buf, 0, sizeof(lfn_buf));
                continue;
            }

            char attr_str[7];
            attr_str[0] = (entry->attr & FAT_ATTR_READ_ONLY) ? 'R' : '.';
            attr_str[1] = (entry->attr & FAT_ATTR_HIDDEN)    ? 'H' : '.';
            attr_str[2] = (entry->attr & FAT_ATTR_SYSTEM)    ? 'S' : '.';
            attr_str[3] = (entry->attr & FAT_ATTR_DIRECTORY) ? 'D' : '.';
            attr_str[4] = (entry->attr & FAT_ATTR_ARCHIVE)   ? 'A' : '.';
            attr_str[5] = '\0';

            unsigned int cl = ((unsigned int)entry->cl_hi << 16) | entry->cl_lo;
            char short_name[16];
            dir_to_shortname(entry, short_name);

            printf("%-6s 0x%-8x %-10u %-12s %s\r\n",
                   attr_str, cl, entry->size, short_name, has_lfn ? lfn_buf : "");

            has_lfn = 0;
            memset(lfn_buf, 0, sizeof(lfn_buf));
        }

        dir_cluster = fat_get_next_cluster(dir_cluster);
    }
}