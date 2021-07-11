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

unsigned int partitionlba = 0;

// the BIOS Parameter Block (in Volume Boot Record)
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

unsigned char *mbr;
bpb_t *bpb;

int fat_getpartition(void)
{
   if(partitionlba == 0)
   {
        mbr = alloc(512);
        bpb = alloc(sizeof(bpb_t));
        
        if(sd_readblock(0,mbr,1)) {
            // check magic
            if(mbr[510]!=0x55 || mbr[511]!=0xAA) {
                uart_puts("\r\n[FAT] ERROR: Bad magic in MBR");
                return 0;
            }
                // check partition type
            if(mbr[0x1C2]!=0x0c)
            {
                uart_puts("\r\n[FAT] ERROR: Wrong partition type");
                return 0;
            }
                // should be this, but compiler generates bad code...
            partitionlba= (mbr[0x1C6] + (mbr[0x1c7]<<8) + (mbr[0x1c8]<<16) + (mbr[0x1c9]<<32));
            printf("\r\n[FAT] Partition LBA is 0x%x",partitionlba);
                // read the boot record
            if(!sd_readblock(partitionlba,bpb,1)) {
                uart_puts("\r\n[FAT] ERROR: Unable to read boot record");
                return 0;
            }
                            
            return 1;
        }
        return 0;
    }
    else
    {
        return 1;
    }
}

/**
 * Find a file in root directory entries
 */
unsigned int fat_getcluster(char *fn)
{
    fat_getpartition();
    
    unsigned int root_sec, s;
    // find the root directory's LBA
    root_sec=((bpb->spf32)*bpb->nf)+bpb->rsc;
    s = (bpb->nr0 + (bpb->nr1 << 8)) * sizeof(fatdir_t);

    // adjust for FAT32
    root_sec+=(bpb->rc-2)*bpb->spc;
    // add partition LBA
    root_sec+=partitionlba;
    // load the root directory
    fatdir_t *dir=alloc(512 * (s/512+2));
    if(sd_readblock(root_sec,dir,s/512+2)) {
        // iterate on each entry and check if it's the one we're looking for
        for(;dir->name[0]!=0;dir++) {
            // is it a valid entry?
            if(dir->name[0]==0xE5 || dir->attr[0]==0xF) continue;
            // filename match?
            if(!memcmp(dir->name,fn,11)) {
                uart_puts("\r\n[FAT]FAT File ");
                uart_puts(fn);
                uart_puts(" starts at cluster: ");
                uart_hex(((unsigned int)dir->ch)<<16|dir->cl);
                // if so, return starting cluster
                return ((unsigned int)dir->ch)<<16|dir->cl;
            }
        }
            uart_puts("\r\n[FAT] ERROR: file not found\n");
        } else {
            uart_puts("\r\n[FAT] ERROR: Unable to load root directory\n");
        }
        return 0;
}

unsigned int lastFileDimension = 0;

unsigned int getLastFileSize()
{
    return lastFileDimension;
}

/**
 * Read a file into memory
 */
unsigned int fat_readfile(unsigned int cluster)
{
    // BIOS Parameter Block
    // File allocation tables. We choose between FAT16 and FAT32 dynamically
    lastFileDimension = 0;
    // Data pointers
    unsigned int data_sec, s;
    unsigned int *data, *ptr;
    // find the LBA of the first data sector
    data_sec=((bpb->spf32)*bpb->nf)+bpb->rsc;
    s = (bpb->nr0 + (bpb->nr1 << 8)) * sizeof(fatdir_t);

    // add partition LBA
    data_sec+=partitionlba;
    // dump important properties
    uart_puts("\r\nFAT Bytes per Sector: ");
    uart_hex(bpb->bps0 + (bpb->bps1 << 8));
    uart_puts("\r\nFAT Sectors per Cluster: ");
    uart_hex(bpb->spc);
    uart_puts("\r\nFAT Number of FAT: ");
    uart_hex(bpb->nf);
    uart_puts("\r\nFAT Sectors per FAT: ");
    uart_hex((bpb->spf16?bpb->spf16:bpb->spf32));
    uart_puts("\r\nFAT Reserved Sectors Count: ");
    uart_hex(bpb->rsc);
    uart_puts("\r\nFAT First data sector: ");
    uart_hex(data_sec);
    // load FAT table
    unsigned int *fat32;
    fat32=alloc(((bpb->spf32)+bpb->rsc)*512);
    s=sd_readblock(partitionlba+2,fat32,(bpb->spf32)+bpb->rsc);
    // end of FAT in memory
    printf("\r\n[FAT] Cluster Size in Memory: 0x%x", bpb->spc*512);

    unsigned short int firstSector = 0;
    // iterate on cluster chain
    do {
        if(!firstSector)
        {
            data=ptr=alloc(bpb->spc*512);
            firstSector = 1;
        }
        else
        {
            ptr=alloc(bpb->spc*512);
        }
        // load all sectors in a cluster
        lastFileDimension += sd_readblock((cluster-2)*bpb->spc+data_sec,ptr,bpb->spc);
        // move pointer, sector per cluster * bytes per sector
        // get the next cluster in chain
        printf("\r\nfat32[cluster]: 0x%x",fat32[cluster]);
        cluster=fat32[cluster];
    }while(cluster>1 && cluster<0x0FFFFFF8);
    
    return data;
}

void fat_listdirectory(void)
{
    fat_getpartition();
   
    unsigned int root_sec, s;
    // find the root directory's LBA
    root_sec=((bpb->spf16?bpb->spf16:bpb->spf32)*bpb->nf)+bpb->rsc;
    s = (bpb->nr0 || (bpb->nr1 << 8));
    uart_puts("\r\n[FAT] FAT number of root diretory entries: ");
    uart_hex(s);
    s *= sizeof(fatdir_t);
    if(bpb->spf16==0) {
        // adjust for FAT32
        root_sec+=(bpb->rc-2)*bpb->spc;
    }
    // add partition LBA
    root_sec+=partitionlba;
    uart_puts("\r\n[FAT] FAT root directory LBA: ");
    uart_hex(root_sec);

    // load the root directory
    fatdir_t *dir = alloc(512 * (s/512+2));
    if(sd_readblock(root_sec,(unsigned char*)dir,s/512+2)) {
        uart_puts("\r\nAttrib Cluster  Size     Name\r\n");
        // iterate on each entry and print out
        for(;dir->name[0]!=0 && dir->name[0]!='U';dir++) {
            // is it a valid entry?
            if(dir->name[0]==0xE5 || dir->attr[0]==0xF) continue;
            // decode attributes
            uart_putc(dir->attr[0]& 1?'R':'.');  // read-only
            uart_putc(dir->attr[0]& 2?'H':'.');  // hidden
            uart_putc(dir->attr[0]& 4?'S':'.');  // system
            uart_putc(dir->attr[0]& 8?'L':'.');  // volume label
            uart_putc(dir->attr[0]&16?'D':'.');  // directory
            uart_putc(dir->attr[0]&32?'A':'.');  // archive
            uart_putc(' ');
            // staring cluster
            uart_hex(((unsigned int)dir->ch)<<16|dir->cl);
            uart_putc(' ');
            // size
            uart_hex(dir->size);
            uart_putc(' ');
            // filename
            dir->attr[0]=0;
            uart_puts(dir->name);
            uart_puts("\r\n");
        }
    }
}
