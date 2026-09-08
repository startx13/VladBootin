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

unsigned short int DEBUG = 1;
 
const char* gbanner = "\r\n-------------------------\r\nVladBootin v0.1 beta     \r\nBuilt for Raspberry Pi 2 \r\nBuild Timestamp: %s\r\nGCC version: %d.%d\r\n-------------------------\r\n";
const char* usage = "\r\n----------------------------------------------\r\nhelp - prints this\r\nbanner - prints VladBootin banner\r\nserialboot - starts boot from serial routine\r\nprintf - print something (printf <string>)\r\ndebug - enable debug log\r\nsdinit - init sd card\r\nfileboot - boot from file kernel7.img\r\ntestfile - dump test file\r\nls - list file\r\nmem - print memory map\r\nrelocate - relocate the program at __end\r\ndump - dump heap to stdio\r\ntestalloc - test alloc routine\r\nfatpart - find partition LBA\r\nmmuinit - Start the MMU and interrupt\r\nhomer - show picture\r\nmemreset - clear memory\r\nclearfb - clear framebuffer\r\ncat - print a file (cat <file>)\r\nboot - boot from file (boot <file>)\r\nhexcat - read file in hex format\r\n----------------------------------------------\r\n";

//Typedefs
typedef void (*entry_fn)(uint32_t r0, uint32_t r1, uint32_t atags);

//Functions header
void printMemoryMap();
void handleMenu();
void bootFromSerial(char*args,unsigned int args_len);
void parseCommand(char* buffer,unsigned int *length);
short unsigned int bufCompare(char* buf1,char* buf2,unsigned int len);
void parseCommand(char* buf,unsigned int *length);
void testAlloc();
void testRead();
void bootFromSerial(char *args,unsigned int args_len);
void bootFromFile();
void relocate();
void memoryDump();

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
            command[position+1]='\0';
            parseCommand(command,&position);
            uart_putc('\r');
            uart_putc('\n');
            uart_putc('>');
            position=0;

        }
    }
}

short unsigned int bufCompare(char* buf1,char* buf2,unsigned int len)
{
    if(buf1[0] == '\0' || buf2[0] == '\0')
    {
        return FALSE;
    }
    for(unsigned int i = 0;i<=len;i++)
    {
        if(buf1[i] != buf2[i] && buf1[i] != '\0' && buf2[i] !='\0')
            return FALSE;
    }
    return TRUE;
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
        if(buf[j] = '\0')
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
        bootFromFile();
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
        if(sd_ret==SD_OK)
        {
            unsigned int cluster = fat_getcluster(args);
             if(cluster)
             {
                unsigned char *file = fat_readfile(cluster);
                printf("\r\n");
                printf(file);
             }
             else
             {
                 printf("\r\n[MAIN] Error Reading file");
             }
        }
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,boot_f,cmd_len))
    {
        if(sd_ret==SD_OK)
        {
            unsigned int cluster = fat_getcluster(args);
             if(cluster)
             {
                 unsigned char *file = fat_readfile(cluster);
                 printf("\r\nBooting image at 0x%d.....",file);
                 entry_fn fn = (entry_fn)file;
                 fn(gr0, gr1, gatags);
                
             }
             else
             {
                 printf("\r\n[MAIN] Error Reading file");
             }
        }
        emptyBuffer(buf,CMD_BUFFER_LENGTH);
        emptyBuffer(command,CMD_BUFFER_LENGTH);
        emptyBuffer(args,CMD_BUFFER_LENGTH);
        *length = 0;
        return;
    }
    
    if(bufCompare(command,hexcat_f,cmd_len))
    {
        if(sd_ret==SD_OK)
        {
            unsigned int cluster = fat_getcluster(args);
             if(cluster)
             {
                unsigned char *file = fat_readfile(cluster);
                printf("\r\n");
                uart_dump(file,getLastFileSize());
             }
             else
             {
                 printf("\r\n[MAIN] Error Reading file");
             }
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
    unsigned int *blockA = alloc(512);

    if(blockA !=NULL)
    {
        printf("\r\nFilling blocks");
        memset(blockA,'A',512);
    }
    
}

void memoryDump()
{
    unsigned char *start = &__end;
    unsigned char *end = 0x3F000000;
    unsigned int size = last_block();
    uart_dump(start,size);
}

void relocate()
{
    //relocate the program at __end
    unsigned char *start = &__start;
    unsigned char *end = &__end;
    unsigned int size = end - start;
    
    unsigned int *relocate_addr = alloc(size);
    printf("\r\nRelocation: __start: 0x%x __end: 0x%x size: 0x%x",start,end,size);
    
    for(unsigned int i=0;i<size;i++)
    {
        relocate_addr[i] = start[i];
    }
    printf("\r\nRelocation Done. Jumping");
    
    entry_fn fn = (entry_fn)(relocate_addr);
    fn(gr0, gr1, gatags);
    
}



void bootFromFile()
{

    if(sd_ret==SD_OK)
    {
        unsigned int cluster = fat_getcluster("KERNEL7 IMG");
        
        if(cluster)
        {
            unsigned char *kernel = fat_readfile(cluster);

            printf("\r\nBooting kernel ad 0x%x....",kernel);
            entry_fn fn = (entry_fn)kernel;
            fn(gr0, gr1, gatags);
        }
    }
}

void testRead()
{
        // initialize EMMC and detect SD card type
    if(sd_ret==SD_OK)
    {
        unsigned char *file = fat_readfile(fat_getcluster("CMDLINE TXT"));
        printf("\r\n");
        printf(file);
    }
}

void bootFromSerial(char *args, unsigned int args_len)
{
    #define ACK 0x6
    #define SYN 0x16

    printf("\r\nBooting from serial.....");

    printf("\r\nWaiting for console to attach......");

    char c;
    do
    {
        c = uart_getc();

    } while(c != SYN);

    printf("\r\nWaiting for kernel image size......");

    unsigned int size = uart_getc();
    size |= ((unsigned int)uart_getc()) << 8;
    size |= ((unsigned int)uart_getc()) << 16;
    size |= ((unsigned int)uart_getc()) << 24;

    printf("\r\nRecived Image size: 0x%x", size);

    // Q  W  E  R
    // 51 57 45 52
    // 0x52455751

    if(size == 0x52455751 && DEBUG)
    {
        printf("\r\nRecived exit sequence");
        return;
    }

    unsigned char *kernel = alloc(size);

    if(kernel == NULL)
    {
        printf("\r\nWrong Image size");
        return;
    }

    if(kernel + size < kernel || kernel + size > (unsigned char *)0x3F000000)
    {
        printf("\r\nWrong Image size");
        return;
    }
    else
    {
        printf("\r\nImage Size correct");
    }

    printf("\r\nWaiting for the Image......");

    /* Preserve the beginning of the kernel image */
    unsigned char *kernel_start = kernel;

    while(size-- > 0)
    {
        *kernel++ = uart_getc();
    }

    printf("\r\nBooting the kernel");

    entry_fn fn = (entry_fn)kernel_start;

    fn(gr0, gr1, gatags);

    printf("\r\nSomething went wrong. Dropping shell");
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
    if(!DEBUG)
        bootFromSerial(NULL,0);
    
    handleMenu();
}

void stop_core()
{
    printf("\r\nStopping Core");
    halt();
}
