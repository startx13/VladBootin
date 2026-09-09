#include <stddef.h>
#include <stdint.h>
#include "lib/printf.h"
#include "lib/uart.h"
#include "lib/sd.h"
#include "lib/fat.h"
#include "lib/stdlib.h"
#include "lib/mm.h"
#include "lib/lfb.h"

#define CMD_BUFFER_LENGTH 256
#define TRUE 1
#define FALSE 0

#define KERNEL_LOAD_ADDR 0x10000000
#define DTB_LOAD_ADDR    0x03000000
#define MEMORY_END       0x3F000000
#define MAX_DTB_SIZE     0x00100000

unsigned short int DEBUG = 0;
 
const char* gbanner = "\r\n-------------------------\r\nVladBootin v0.1 beta     \r\nBuilt for Raspberry Pi 2 \r\nBuild Timestamp: %s\r\nGCC version: %d.%d\r\n-------------------------\r\n";
const char* usage = "\r\n----------------------------------------------\r\nhelp - prints this\r\nbanner - prints VladBootin banner\r\nserialboot - starts boot from serial routine\r\nprintf - print something (printf <string>)\r\ndebug - enable debug log\r\nsdinit - init sd card\r\nfileboot - boot from file kernel7.img\r\ntestfile - dump test file\r\nls - list file\r\nmem - print memory map\r\nrelocate - relocate the program at __end\r\ndump - dump heap to stdio\r\ntestalloc - test alloc routine\r\nfatpart - find partition LBA\r\nmmuinit - Start the MMU and interrupt\r\nhomer - show picture\r\nmemreset - clear memory\r\nclearfb - clear framebuffer\r\ncat - print a file (cat <file>)\r\nboot - boot from file (boot <kernel> [dtb])\r\nhexcat - read file in hex format\r\n----------------------------------------------\r\n";

//Typedefs
typedef void (*entry_fn)(uint32_t r0, uint32_t r1, uint32_t atags);

//Functions header
void printMemoryMap();
void handleMenu();
void bootFromSerial(char*args,unsigned int args_len);
void parseCommand(char* buffer,unsigned int *length);
short unsigned int bufCompare(char* buf1,char* buf2,unsigned int len);
void testAlloc();
void testRead();
void bootFromFile(const char *kname, const char *dtbname);
void relocate();
void memoryDump();
extern void halt(void);
extern void prepare_boot(void);
extern void clean_dcache_range(unsigned int start, unsigned int end);
extern void linux_boot(uint32_t kernel_entry,uint32_t machine_type,uint32_t dtb);

//Global Vars
uint32_t gr0;
uint32_t gr1;
uint32_t gatags;

int sd_ret = 0;

extern unsigned char __start;
extern unsigned char __text_start;
extern unsigned char __text_end;
extern unsigned char __rodata_start;
extern unsigned char __rodata_end;
extern unsigned char __data_start;
extern unsigned char __data_end;
extern unsigned char __bss_start;
extern unsigned char __bss_end;
extern unsigned char __bss_size;
extern unsigned char __end;

//Functions

void printMemoryMap()
{
    printf("\r\nMEMORY MAP");
    printf("\r\n__start 0x%x",&__start);
    
    printf("\r\n__text_start 0x%x",&__text_start);
    printf("\r\n__text_end 0x%x",&__text_end);
    
    printf("\r\n__rodata_start 0x%x",&__rodata_start);
    printf("\r\n__rodata_end 0x%x",&__rodata_end);
    
    printf("\r\n__data_start 0x%x",&__data_start);
    printf("\r\n__data_end 0x%x",&__data_end);
    
    printf("\r\n__bss_start 0x%x",&__bss_start);
    printf("\r\n__bss_end 0x%x",&__bss_end);
    printf("\r\n__bss_size 0x%x",&__bss_size);
    
    printf("\r\n__end 0x%x\r\n",&__end);

}

void emptyBuffer(char* buf,unsigned int l)
{
    for(unsigned int i=0;i<l;i++)
    {
        buf[i] = '\0';
    }
}

void handleMenu()
{
    char command[CMD_BUFFER_LENGTH];
    unsigned int position = 0;
    
    emptyBuffer(command,CMD_BUFFER_LENGTH);
    
    uart_putc('>');
    while(1)
    {
        char c = uart_getc();
        
        if(c!='\r' && c!='\n')
        {
            if(position<CMD_BUFFER_LENGTH)
            {
                  if(c>=32 && c<=126)
                  {
                    command[position] = c;
                    position++;
                    uart_putc(c);
                  }
                  else if((c==0x08 || c==0x7F) && position>=0) //0x08 || 0x7F
                  {
                      if(position>0)
                      {
                          position--;
                          command[position] = '\0';
                          uart_putc(0x08);
                          uart_putc(' ');
                          uart_putc(0x08);
                      }
                  }
            }
            else
            {
                printf("\r\nCommand too long\r\n>");
                position = 0;
            }
        }
        else
        {
            if (DEBUG)
            {
                printf("\r\n[DEBUG]: Command: %s",command);
            }
            command[position]='\0';
            parseCommand(command,&position);
            uart_putc('\r');
            uart_putc('\n');
            uart_putc('>');
            position=0;

        }
    }
}

short unsigned int bufCompare(char* buf1, char* buf2, unsigned int len)
{
    for(unsigned int i = 0; i < len; i++)
    {
        if(buf1[i] != buf2[i] || buf1[i] == '\0')
            return FALSE;
    }
    return (buf2[len] == '\0');
}

void parseCommand(char* buf,unsigned int *length)
{
    char command[CMD_BUFFER_LENGTH];
    unsigned int cmd_len = 0;
    
    char args[CMD_BUFFER_LENGTH];
    unsigned int args_len = 0;
    
    emptyBuffer(command,CMD_BUFFER_LENGTH);
    emptyBuffer(args,CMD_BUFFER_LENGTH);


    for(unsigned int i=0;i<*length;i++)
    {
        if(buf[i] == ' ' || buf[i] == 0)
        {
            if(DEBUG)
            {
                printf("\r\n[DEBUG]: Found space at index %d",i);
            }
            break;
        }
        command[i] = buf[i];
        cmd_len++;
    }

    if(DEBUG)
    {
        printf("\r\n[DEBUG]: CMD: %s",command);
    }
    
    for(unsigned int j=cmd_len+1;j<*length;j++)
    {
        args[args_len]=buf[j];
        args_len++;
        if(buf[j] == '\0')
        {
            break;
        }
    }
    
    if(DEBUG)
    {
        printf("\r\n[DEBUG]: ARGS: %s",args);
    }
    
    //Defined commands
    char serialboot[] = "serialboot";
    char banner[] = "banner";
    char help[] = "help";
    char printf_f[] = "printf";
    char debug_f[] = "debug";
    char sdinit_f[] = "sdinit";
    char fileboot_f[] = "fileboot";
    char testfile_f[] = "testfile";
    char ls_f[] = "ls";
    char mem_f[] = "mem";
    char relocate_f[] = "relocate";
    char dump_f[] = "dump";
    char testalloc_f[] = "testalloc";
    char fatpart_f[] = "fatpart";
    char mmuinit_f[] = "mmuinit";
    char homer_f[] = "homer";
    char memreset_f[] = "memreset";
    char clearfb_f[] = "clearfb";
    char cat_f[] = "cat";
    char boot_f[] = "boot";
    char hexcat_f[] = "hexcat";
    
    if(bufCompare(command,serialboot,cmd_len))
    {
        bootFromSerial(args,1);
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,banner,cmd_len))
    {
        printf(gbanner,__TIMESTAMP__,__GNUC__, __GNUC_MINOR__);
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,help,cmd_len))
    {
        printf(usage);
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,printf_f,cmd_len))
    {
        printf("\r\n%s",args);
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,debug_f,cmd_len))
    {
        if(DEBUG)
        {
            DEBUG = 0;
            printf("\r\nDebug disabled");
            emptyBuffer(buf,CMD_BUFFER_LENGTH);
            emptyBuffer(command,CMD_BUFFER_LENGTH);
            emptyBuffer(args,CMD_BUFFER_LENGTH);
            *length = 0;
            return;
            
        }
        DEBUG = 1;
        printf("\r\nDebug enabled");
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,sdinit_f,cmd_len))
    {
        sd_ret = sd_init();
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,fileboot_f,cmd_len))
    {
        bootFromFile(NULL, NULL);
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,testfile_f,cmd_len))
    {
        testRead();
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,ls_f,cmd_len))
    {
        sd_ret = sd_init();
        fat_listdirectory();
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,mem_f,cmd_len))
    {
        printMemoryMap();
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,relocate_f,cmd_len))
    {
        relocate();
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,dump_f,cmd_len))
    {
        memoryDump();
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,testalloc_f,cmd_len))
    {
        testAlloc();
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,fatpart_f,cmd_len))
    {
        fat_getpartition();
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,mmuinit_f,cmd_len))
    {
        init_mmu();
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,homer_f,cmd_len))
    {
        lfb_init();
        lfb_init();
        lfb_showhomer();
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,memreset_f,cmd_len))
    {
        reset_mem();
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,clearfb_f,cmd_len))
    {
        lfb_clear();
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,cat_f,cmd_len))
    {
        if(sd_ret != SD_OK)
            sd_ret = sd_init();

        if(sd_ret == SD_OK)
        {
            unsigned int fsize = 0;
            unsigned int cluster = fat_getcluster_ex(args, &fsize);
            if(cluster)
            {
                unsigned int to_read = (fsize > 65536) ? 65536 : fsize;
                unsigned char *file_buf = (unsigned char *)alloc(to_read + 1);
                if(file_buf)
                {
                    unsigned int r = fat_readfile_to(cluster, file_buf, to_read);
                    file_buf[r] = '\0';
                    printf("\r\n%s\r\n", file_buf);
                }
            }
            else
            {
                printf("\r\n[CAT] File not found: %s", args);
            }
        }
        else
        {
            printf("\r\n[CAT] SD card not initialized");
        }
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,boot_f,cmd_len))
    {
        char k_arg[128];
        char dtb_arg[128];
        emptyBuffer(k_arg, 128);
        emptyBuffer(dtb_arg, 128);

        int p = 0, kp = 0, dp = 0;
        while(args[p] == ' ') p++;
        while(args[p] != ' ' && args[p] != '\0' && kp < 127) k_arg[kp++] = args[p++];
        while(args[p] == ' ') p++;
        while(args[p] != ' ' && args[p] != '\0' && dp < 127) dtb_arg[dp++] = args[p++];

        bootFromFile(kp > 0 ? k_arg : NULL, dp > 0 ? dtb_arg : NULL);

        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,hexcat_f,cmd_len))
    {
        if(sd_ret != SD_OK)
            sd_ret = sd_init();

        if(sd_ret == SD_OK)
        {
            unsigned int fsize = 0;
            unsigned int cluster = fat_getcluster_ex(args, &fsize);
            if(cluster)
            {
                unsigned int to_read = (fsize > 2048) ? 2048 : fsize;
                unsigned char *file_buf = (unsigned char *)alloc(to_read);
                if(file_buf)
                {
                    unsigned int r = fat_readfile_to(cluster, file_buf, to_read);
                    printf("\r\n");
                    uart_dump((unsigned int)file_buf, r);
                }
            }
            else
            {
                printf("\r\n[HEXCAT] File not found: %s", args);
            }
        }
        else
        {
            printf("\r\n[HEXCAT] SD card not initialized");
        }
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    printf("\r\nCommand not found");
    emptyBuffer(buf,CMD_BUFFER_LENGTH);
    emptyBuffer(command,CMD_BUFFER_LENGTH);
    emptyBuffer(args,CMD_BUFFER_LENGTH);
    *length = 0;
}

void testAlloc()
{
    printf("\r\nAllocating block");
    unsigned int *blockA = (unsigned int *)alloc(512);

    if(blockA !=NULL)
    {
        printf("\r\nFilling blocks");
        memset(blockA,'A',512);
    }
    
}

void memoryDump()
{
    unsigned char *start = &__end;
    unsigned char *end = (unsigned char *)0x3F000000;
    unsigned int size = last_block();
    uart_dump((unsigned int)start,size);
}

void relocate()
{
    //relocate the program at __end
    unsigned char *start = &__start;
    unsigned char *end = &__end;
    unsigned int size = end - start;
    
    unsigned int *relocate_addr = (unsigned int *)alloc(size);
    printf("\r\nRelocation: __start: 0x%x __end: 0x%x size: 0x%x",start,end,size);
    
    for(unsigned int i=0;i<size;i++)
    {
        relocate_addr[i] = start[i];
    }
    printf("\r\nRelocation Done. Jumping");
    
    entry_fn fn = (entry_fn)(relocate_addr);
    fn(gr0, gr1, gatags);
    
}

void fdt_update_memory(void *dtb_base, unsigned int memory_size)
{
    unsigned char *dtb = (unsigned char *)dtb_base;

    /* Check FDT magic */
    if(dtb[0] != 0xd0 || dtb[1] != 0x0d ||
       dtb[2] != 0xfe || dtb[3] != 0xed)
    {
        printf("\r\n[BOOT] ERROR: Invalid FDT magic");
        return;
    }

    unsigned int off_struct =
        ((unsigned int)dtb[8]  << 24) |
        ((unsigned int)dtb[9]  << 16) |
        ((unsigned int)dtb[10] << 8) |
        (unsigned int)dtb[11];

    unsigned int off_strings =
        ((unsigned int)dtb[12] << 24) |
        ((unsigned int)dtb[13] << 16) |
        ((unsigned int)dtb[14] << 8) |
        (unsigned int)dtb[15];

    unsigned int size_struct =
        ((unsigned int)dtb[36] << 24) |
        ((unsigned int)dtb[37] << 16) |
        ((unsigned int)dtb[38] << 8) |
        (unsigned int)dtb[39];

    unsigned char *struct_base = dtb + off_struct;
    unsigned char *struct_end  = struct_base + size_struct;
    unsigned char *strings     = dtb + off_strings;

    unsigned int depth = 0;
    int in_memory_node = 0;

    unsigned char *p = struct_base;

    while(p + 4 <= struct_end)
    {
        unsigned int tag =
            ((unsigned int)p[0] << 24) |
            ((unsigned int)p[1] << 16) |
            ((unsigned int)p[2] << 8) |
            (unsigned int)p[3];

        p += 4;

        /* FDT_BEGIN_NODE */
        if(tag == 1)
        {
            char *node_name = (char *)p;

            depth++;

            if(strcmp(node_name, "memory@0") == 0)
            {
                in_memory_node = depth;

                printf("\r\n[BOOT] Found memory@0");
            }

            unsigned int len = strlen(node_name) + 1;
            len = (len + 3) & ~3;

            p += len;
        }

        /* FDT_END_NODE */
        else if(tag == 2)
        {
            if(in_memory_node == (int)depth)
                in_memory_node = 0;

            if(depth > 0)
                depth--;
        }

        /* FDT_PROP */
        else if(tag == 3)
        {
            if(p + 8 > struct_end)
                return;

            unsigned int len =
                ((unsigned int)p[0] << 24) |
                ((unsigned int)p[1] << 16) |
                ((unsigned int)p[2] << 8) |
                (unsigned int)p[3];

            unsigned int nameoff =
                ((unsigned int)p[4] << 24) |
                ((unsigned int)p[5] << 16) |
                ((unsigned int)p[6] << 8) |
                (unsigned int)p[7];

            unsigned char *data = p + 8;

            /*
             * reg in memory@0:
             *
             *   <address size>
             *
             * Raspberry Pi 2:
             *   address = 32 bit
             *   size    = 32 bit
             *
             * So the property is exactly 8 bytes.
             */
            if(in_memory_node == (int)depth &&
               nameoff < 4096 &&
               strcmp((char *)(strings + nameoff), "reg") == 0 &&
               len == 8)
            {
                /* Base address = 0 */
                data[0] = 0x00;
                data[1] = 0x00;
                data[2] = 0x00;
                data[3] = 0x00;

                /* RAM size, big endian */
                data[4] = (memory_size >> 24) & 0xff;
                data[5] = (memory_size >> 16) & 0xff;
                data[6] = (memory_size >> 8)  & 0xff;
                data[7] = memory_size & 0xff;

                printf("\r\n[BOOT] Updated memory: %u MB",
                       memory_size / (1024 * 1024));

                return;
            }

            unsigned int total = 8 + len;
            total = (total + 3) & ~3;

            if(p + total > struct_end)
                return;

            p += total;
        }

        /* FDT_NOP */
        else if(tag == 4)
        {
            /* Nothing */
        }

        /* FDT_END */
        else if(tag == 9)
        {
            break;
        }

        else
        {
            printf("\r\n[BOOT] ERROR: Unknown FDT tag 0x%08x", tag);
            return;
        }
    }

    printf("\r\n[BOOT] ERROR: memory@0/reg not found");
}

static void fdt_update_bootargs(void *dtb_base, const char *cmdline)
{
    if(!dtb_base || !cmdline || cmdline[0] == '\0')
        return;

    unsigned char *dtb = (unsigned char *)dtb_base;
    if(dtb[0] != 0xd0 || dtb[1] != 0x0d || dtb[2] != 0xfe || dtb[3] != 0xed)
        return;

    unsigned int off_struct = ((unsigned int)dtb[8] << 24) | ((unsigned int)dtb[9] << 16) |
                              ((unsigned int)dtb[10] << 8) | (unsigned int)dtb[11];
    unsigned int off_strings = ((unsigned int)dtb[12] << 24) | ((unsigned int)dtb[13] << 16) |
                               ((unsigned int)dtb[14] << 8) | (unsigned int)dtb[15];
    unsigned int size_struct = ((unsigned int)dtb[32] << 24) | ((unsigned int)dtb[33] << 16) |
                               ((unsigned int)dtb[34] << 8) | (unsigned int)dtb[35];

    /* Find "bootargs" in strings block */
    unsigned char *strings = dtb + off_strings;
    int bootargs_nameoff = -1;
    for(int i = 0; i < 4096; i++)
    {
        if(strings[i] == 'b' && strings[i+1] == 'o' && strings[i+2] == 'o' &&
           strings[i+3] == 't' && strings[i+4] == 'a' && strings[i+5] == 'r' &&
           strings[i+6] == 'g' && strings[i+7] == 's' && strings[i+8] == '\0')
        {
            bootargs_nameoff = i;
            break;
        }
    }

    if(bootargs_nameoff < 0)
        return;

    /* Scan struct block for FDT_PROP with bootargs nameoff */
    unsigned char *p = dtb + off_struct;
    unsigned char *end = p + size_struct;

    while(p + 12 <= end)
    {
        unsigned int tag = ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
                           ((unsigned int)p[2] << 8) | (unsigned int)p[3];

        if(tag == 1) /* FDT_BEGIN_NODE */
        {
            p += 4;
            while(p < end && *p != '\0') p++;
            p++;
            while(((unsigned long)p & 3) != 0) p++;
        }
        else if(tag == 2) /* FDT_END_NODE */
        {
            p += 4;
        }
        else if(tag == 3) /* FDT_PROP */
        {
            unsigned int len = ((unsigned int)p[4] << 24) | ((unsigned int)p[5] << 16) |
                               ((unsigned int)p[6] << 8) | (unsigned int)p[7];
            unsigned int nameoff = ((unsigned int)p[8] << 24) | ((unsigned int)p[9] << 16) |
                                   ((unsigned int)p[10] << 8) | (unsigned int)p[11];

            if(nameoff == (unsigned int)bootargs_nameoff)
            {
                unsigned int cmd_len = strlen(cmdline) + 1;
                if(cmd_len <= len)
                {
                    memcpy(p + 12, (void *)cmdline, cmd_len);
                }
                else
                {
                    memcpy(p + 12, (void *)cmdline, len - 1);
                    p[12 + len - 1] = '\0';
                }
                printf("\r\n[BOOT] Injected bootargs into DTB: %s", (char *)(p + 12));
                return;
            }

            p += 12;
            p += (len + 3) & ~3;
        }
        else if(tag == 4) /* FDT_NOP */
        {
            p += 4;
        }
        else if(tag == 9) /* FDT_END */
        {
            break;
        }
        else
        {
            break;
        }
    }
}

void bootFromFile(const char *kname, const char *dtbname)
{
    if(sd_ret != SD_OK)
        sd_ret = sd_init();

    if(sd_ret != SD_OK)
    {
        printf("\r\n[BOOT] SD card initialization failed");
        return;
    }

    if(!kname || kname[0] == '\0')
        kname = "kernel7.img";

    if(!dtbname || dtbname[0] == '\0')
        dtbname = "bcm2709-rpi-2-b.dtb";

    printf("\r\n[BOOT] Locating kernel: %s", kname);
    unsigned int k_size = 0;
    unsigned int k_cl = fat_getcluster_ex(kname, &k_size);
    if(!k_cl)
    {
        printf("\r\n[BOOT] ERROR: Kernel '%s' not found on SD card", kname);
        return;
    }

    printf("\r\n[BOOT] Locating DTB: %s", dtbname);
    unsigned int dtb_size = 0;
    unsigned int dtb_cl = fat_getcluster_ex(dtbname, &dtb_size);
    if(!dtb_cl)
    {
        /* Try short-name fallback BCM270~7.DTB */
        dtb_cl = fat_getcluster_ex("BCM270~7.DTB", &dtb_size);
    }

    if(!dtb_cl)
    {
        printf("\r\n[BOOT] ERROR: DTB '%s' not found on SD card", dtbname);
        return;
    }

    printf("\r\n[BOOT] Loading kernel (%u bytes) to 0x%08x...", k_size, KERNEL_LOAD_ADDR);
    unsigned int r_k = fat_readfile_to(k_cl, (void *)KERNEL_LOAD_ADDR, k_size);
    if(r_k < k_size)
    {
        printf("\r\n[BOOT] ERROR: Kernel read truncated (%u / %u bytes)", r_k, k_size);
        return;
    }
    printf(" OK");

    printf("\r\n[BOOT] Loading DTB (%u bytes) to 0x%08x...", dtb_size, DTB_LOAD_ADDR);
    unsigned int r_dtb = fat_readfile_to(dtb_cl, (void *)DTB_LOAD_ADDR, dtb_size);
    if(r_dtb < dtb_size)
    {
        printf("\r\n[BOOT] ERROR: DTB read truncated (%u / %u bytes)", r_dtb, dtb_size);
        return;
    }
    printf(" OK");

    /* Validate DTB magic: 0xd00dfeed in big-endian */
    unsigned char *dtb = (unsigned char *)DTB_LOAD_ADDR;
    if(dtb[0] != 0xd0 || dtb[1] != 0x0d || dtb[2] != 0xfe || dtb[3] != 0xed)
    {
        printf("\r\n[BOOT] ERROR: Bad DTB magic (0x%02x%02x%02x%02x)",
               dtb[0], dtb[1], dtb[2], dtb[3]);
        return;
    }

    /* Read cmdline.txt if present and update DTB bootargs */
    char cmdline_buf[256];
    emptyBuffer(cmdline_buf, sizeof(cmdline_buf));
    unsigned int cmd_size = 0;
    unsigned int cmd_cl = fat_getcluster_ex("cmdline.txt", &cmd_size);
    if(cmd_cl)
    {
        unsigned int to_read = (cmd_size < sizeof(cmdline_buf) - 1) ? cmd_size : (sizeof(cmdline_buf) - 1);
        fat_readfile_to(cmd_cl, cmdline_buf, to_read);
        for(int i = 0; i < sizeof(cmdline_buf); i++)
        {
            if(cmdline_buf[i] == '\r' || cmdline_buf[i] == '\n')
                cmdline_buf[i] = '\0';
        }
    }

    if(cmdline_buf[0] == '\0')
    {
        strcpy(cmdline_buf, "console=ttyAMA0,115200 root=/dev/mmcblk0p2 rootwait nosmp");
    }

    fdt_update_bootargs((void *)DTB_LOAD_ADDR, cmdline_buf);
    fdt_update_memory((void *)DTB_LOAD_ADDR, 0x3c000000); // Assume 512MB RAM for now

    printf("\r\n[BOOT] Preparing CPU for Linux handoff...");
    prepare_boot();

    linux_boot(
        KERNEL_LOAD_ADDR,
        0xFFFFFFFF,
        DTB_LOAD_ADDR
    );

    while(1) { halt(); }
}

void testRead()
{
    if(sd_ret != SD_OK)
        sd_ret = sd_init();

    if(sd_ret == SD_OK)
    {
        unsigned int size = 0;
        unsigned int cl = fat_getcluster_ex("cmdline.txt", &size);
        if(cl)
        {
            char buf[512];
            unsigned int r = fat_readfile_to(cl, buf, sizeof(buf) - 1);
            buf[r] = '\0';
            printf("\r\n[CMDLINE.TXT]: %s\r\n", buf);
        }
        else
        {
            printf("\r\n[TEST] cmdline.txt not found");
        }
    }
}

void bootFromSerial(char *args, unsigned int args_len)
{
    #define ACK              0x06
    #define NAK              0x15
    #define SYN              0x16
    #define CHUNK_SIZE       256

    printf("\r\nBooting from serial.....");
    printf("\r\nWaiting for console to attach......");

    unsigned char c;

    /*
     * ---------------------------------------------------------------------
     * Wait for SYN
     * ---------------------------------------------------------------------
     */

    do
    {
        c = uart_getc();

        if(c == 0x03 || c == 0x1B)   // Ctrl+C / ESC
            return;
    }
    while(c != SYN);

    uart_putc(ACK);

    unsigned int kernel_size = 0;

    kernel_size |= ((unsigned int)uart_getc());
    kernel_size |= ((unsigned int)uart_getc()) << 8;
    kernel_size |= ((unsigned int)uart_getc()) << 16;
    kernel_size |= ((unsigned int)uart_getc()) << 24;

    if(kernel_size == 0x52455751 && DEBUG)
    {
        uart_putc(ACK);
        return;
    }


    if(kernel_size == 0)
    {
        uart_putc(NAK);
        return;
    }


    unsigned char *kernel_start =
        (unsigned char *)KERNEL_LOAD_ADDR;

    unsigned char *kernel_end =
        kernel_start + kernel_size;


    if(kernel_end < kernel_start ||
       kernel_end > (unsigned char *)DTB_LOAD_ADDR)
    {
        uart_putc(NAK);
        return;
    }


    uart_putc(ACK);


    unsigned char *kernel = kernel_start;
    unsigned int remaining = kernel_size;

    while(remaining > 0)
    {
        unsigned int chunk = remaining;

        if(chunk > CHUNK_SIZE)
            chunk = CHUNK_SIZE;

        for(unsigned int i = 0; i < chunk; i++)
        {
            *kernel++ = uart_getc();
        }

        remaining -= chunk;

        uart_putc(ACK);
    }


    unsigned int dtb_size = 0;

    dtb_size |= ((unsigned int)uart_getc());
    dtb_size |= ((unsigned int)uart_getc()) << 8;
    dtb_size |= ((unsigned int)uart_getc()) << 16;
    dtb_size |= ((unsigned int)uart_getc()) << 24;

    if(dtb_size == 0 ||
       dtb_size > MAX_DTB_SIZE)
    {
        uart_putc(NAK);
        return;
    }

    unsigned char *dtb_start =
        (unsigned char *)DTB_LOAD_ADDR;

    unsigned char *dtb_end =
        dtb_start + dtb_size;


    /*
     * Validate DTB range.
     */
    if(dtb_end < dtb_start ||
       dtb_end > (unsigned char *)MEMORY_END)
    {
        uart_putc(NAK);
        return;
    }

    if(dtb_start < kernel_end)
    {
        uart_putc(NAK);
        return;
    }

    uart_putc(ACK);

    unsigned char *dtb = dtb_start;
    remaining = dtb_size;

    while(remaining > 0)
    {
        unsigned int chunk = remaining;

        if(chunk > CHUNK_SIZE)
            chunk = CHUNK_SIZE;

        for(unsigned int i = 0; i < chunk; i++)
        {
            *dtb++ = uart_getc();
        }

        remaining -= chunk;

        uart_putc(ACK);
    }




    if(dtb_start[0] != 0xd0 ||
       dtb_start[1] != 0x0d ||
       dtb_start[2] != 0xfe ||
       dtb_start[3] != 0xed)
    {
        return;
    }



    printf("\r\nKernel received: %u bytes.", kernel_size);
    printf("\r\nDTB received: %u bytes.", dtb_size);
    printf("\r\nPreparing CPU for Linux...");

    /*
    * Nothing that can touch peripherals after this point.
    */

    prepare_boot();

    //entry_fn fn = (entry_fn)kernel_start;

    //fn(0,0xFFFFFFFF,DTB_LOAD_ADDR);

    linux_boot(
    (uint32_t)kernel_start,
    0xFFFFFFFF,
    DTB_LOAD_ADDR
);

    while(1){}
}


void vladBootin_main(uint32_t r0, uint32_t r1, uint32_t atags)
{
    
    //init_mmu();
    sd_init();
    uart_init();    

    lfb_init();
    lfb_init();
    

    printf(gbanner,__TIMESTAMP__,__GNUC__, __GNUC_MINOR__);
    lfb_showpicture();
    
    if(DEBUG==1)
        printMemoryMap();
    
    gr0 = r0;
    gr1 = r1;
    gatags = atags;
    
    //Try to boot from serial. If it fails go to shell.
    
    bootFromSerial(NULL,0);
    
    handleMenu();
}

void stop_core()
{
    printf("\r\nStopping Core");
    halt();
}
