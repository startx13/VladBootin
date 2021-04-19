/*
 * Copyright (C) 2018 bzt (bztsrc@github)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
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

#include "sd.h"
#include "uart.h"
#include "stdlib.h"
#include "printf.h"
// get the end of bss segment from linker

extern unsigned char __end;
unsigned int partitionlba = 0;

// the BIOS Parameter Block (in Volume Boot Record)
typedef struct {
    unsigned char       bootjmp[3];
    unsigned char       oem_name[8];
    unsigned short      bytes_per_sector;
    unsigned char       sectors_per_cluster;
    unsigned short      reserved_sector_count;
    unsigned char       table_count;
    unsigned short      root_entry_count;
    unsigned short      total_sectors_16;
    unsigned char       media_type;
    unsigned short      table_size_16;
    unsigned short      sectors_per_track;
    unsigned short      head_side_count;
    unsigned int        hidden_sector_count;
    unsigned int        total_sectors_32;
    unsigned int        table_size_32;
    unsigned short      extended_flags;
    unsigned short      fat_version;
    unsigned int        root_cluster;
    unsigned short      fat_info;
    unsigned short      backup_BS_sector;
    unsigned char       reserved_0[12];
    unsigned char       drive_number;
    unsigned char       reserved_1;
    unsigned char       boot_signature;
    unsigned int        volume_id;
    unsigned char       volume_label[11];
    unsigned char       fat_type_label[8];
} __attribute__((packed)) bpb_t;

// directory entry structure
typedef struct {
    char            name[8];
    char            ext[3];
    char            attr[9];
    unsigned short  ch;
    unsigned int    attr2;
    unsigned short  cl;
    unsigned int    size;
} __attribute__((packed)) fatdir_t;

/**
 * Get the starting LBA address of the first partition
 * so that we know where our FAT file system starts, and
 * read that volume's BIOS Parameter Block
 */

static unsigned char *mbr;
static bpb_t *bpb;

int fat_getpartition(void)
{
    mbr = alloc(512);
    bpb = alloc(sizeof(bpb_t));
    // read the partitioning table
    if(sd_readblock(0,mbr,1)) {
        // check magic
        if(mbr[510]!=0x55 || mbr[511]!=0xAA) {
            uart_puts("\r\nERROR: Bad magic in MBR");
            return 0;
        }
        // check partition type
        if(mbr[0x1C2]!=0x0c)
        {
            uart_puts("\r\nERROR: Wrong partition type");
            return 0;
        }
        // should be this, but compiler generates bad code...
        partitionlba= (mbr[0x1C6] + (mbr[0x1c7]<<8) + (mbr[0x1c8]<<16) + (mbr[0x1c9]<<32));
        printf("\r\nPartition LBA is 0x%x",partitionlba);
        // read the boot record
        if(!sd_readblock(partitionlba,bpb,1)) {
            uart_puts("\r\nERROR: Unable to read boot record");
            return 0;
        }
        
        printf("\r\nbootjmp: 0x%x 0x%x 0x%x",bpb->bootjmp[0],bpb->bootjmp[1],bpb->bootjmp[2]);
        
        return 1;
    }
    return 0;
}

/**
 * Find a file in root directory entries
 */
unsigned int fat_getcluster(char *fn)
{
    fat_getpartition();

}

/**
 * Read a file into memory
 */
char *fat_readfile(unsigned int cluster)
{
    fat_getpartition();
    
}

void fat_listdirectory(void)
{
    fat_getpartition();
   
}
