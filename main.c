#include <stddef.h>
#include <stdint.h>
#include "lib/printf.h"
#include "lib/uart.h"
#include "lib/sd.h"
#include "lib/fat.h"

#define CMD_BUFFER_LENGTH 256
#define TRUE 1
#define FALSE 0



int DEBUG = 1;

#if defined(__cplusplus)
extern "C" /* Use C linkage for kernel_main. */
#endif
 
const char* gbanner = "\r\n-------------------------\r\nVladBootin v0.1 beta     \r\nBuilt for Raspberry Pi 2 \r\n-------------------------\r\n";

//Typedefs
typedef void (*entry_fn)(uint32_t r0, uint32_t r1, uint32_t atags);

//Functions header
void handleMenu();
void bootFromSerial(char*args,unsigned int args_len);
void parseCommand(char* buffer,unsigned int *length);
short unsigned int bufCompare(char* buf1,char* buf2,unsigned int len);


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

uint8_t sector[SECTOR_SIZE], sector2[SECTOR_SIZE];
uint32_t pstart, psize, i;
uint8_t pactive, ptype;
VOLINFO vi;
DIRINFO di;
DIRENT de;
uint32_t cache;
FILEINFO fi;
uint8_t *p;


void vladBootin_main(uint32_t r0, uint32_t r1, uint32_t atags)
{
    uart_init();
    sd_ret = sd_init();
    //INIT FAT32



    // Obtain pointer to first partition on first (only) unit
    pstart = DFS_GetPtnStart(0, sector, 0, &pactive, &ptype, &psize);
    if (pstart == 0xffffffff) {
        printf("Cannot find first partition\r\n");
        return -1;
    }

    printf("Partition 0 start sector 0x%-08.8lX active %-02.2hX type %-02.2hX size %-08.8lX\r\n", pstart, pactive, ptype, psize);

    if (DFS_GetVolInfo(0, sector, pstart, &vi)) {
        printf("Error getting volume information\n");
        return -1;
    }
    printf("Volume label '%-11.11s'\r\n", vi.label);
    printf("%d sector/s per cluster, %d reserved sector/s, volume total %d sectors.\r\n", vi.secperclus, vi.reservedsecs, vi.numsecs);
    printf("%d sectors per FAT, first FAT at sector #%d, root dir at #%d.\r\n",vi.secperfat,vi.fat1,vi.rootdir);
    printf("(For FAT32, the root dir is a CLUSTER number, FAT12/16 it is a SECTOR number)\r\n");
    printf("%d root dir entries, data area commences at sector #%d.\r\n",vi.rootentries,vi.dataarea);
    printf("%d clusters (%d bytes) in data area, filesystem IDd as ", vi.numclusters, vi.numclusters * vi.secperclus * SECTOR_SIZE);
    if (vi.filesystem == FAT12)
        printf("FAT12.\r\n");
    else if (vi.filesystem == FAT16)
        printf("FAT16.\r\n");
    else if (vi.filesystem == FAT32)
        printf("FAT32.\r\n");
    else
        printf("[unknown]\r\n");
    
    
    printf(gbanner);
    
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
                  else if(c==0x8 && position>=0)
                  {
                      command[position] = '\0';
                      if(position>0)
                        position--;
                      uart_putc(0x18);
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

const char* usage = "\r\n----------------------------------------------\r\nhelp - prints this\r\nbanner - prints VladBootin banner\r\nserialboot - starts boot from serial routine\r\nprintf - print something (printf <string>)\r\ndebug - enable debug log\r\nsdinit - init sd card\r\nfileboot - boot from file kernel7.img\r\ntestfile - dump test file\r\nls - list file\r\nmem - print memory map\r\n----------------------------------------------\r\n";

void parseCommand(char* buf,unsigned int *length)
{
    char command[CMD_BUFFER_LENGTH];
    unsigned int cmd_len = 0;
    char args[CMD_BUFFER_LENGTH];
    unsigned int args_len = 0;
    
    unsigned int r = 0;
    for(unsigned int i=0;i<*length;i++)
    {
        if(buf[i] == ' ' || buf[i] == 0)
        {
            if(DEBUG)
            {
                printf("\r\n[DEBUG]: Found space at index %d",i);
            }
            r = i;
            break;
        }
        command[i] = buf[i];
        cmd_len++;
    }

    if(DEBUG)
    {
        printf("\r\n[DEBUG]: CMD: %s",command);
    }
    
    for(unsigned int j=r;j<*length;j++)
    {
        if(buf[j] = '\0')
        {
            break;
        }
        args[args_len]=buf[j];
        args_len++;
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
        printf(gbanner);
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
        printf("\r\n%s\n",args);
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
        //fat_listdirectory();
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
    
    printf("\r\nCommand not found");
    emptyBuffer(buf,CMD_BUFFER_LENGTH);
    emptyBuffer(command,CMD_BUFFER_LENGTH);
    emptyBuffer(args,CMD_BUFFER_LENGTH);
    *length = 0;
}

void bootFromFile()
{

    printf("\r\nReading kernel7.img");

    if(sd_ret==SD_OK)
    {
        char *kernel = NULL;//readfile("KERNEL7 IMG");
      
        if (DFS_OpenFile(&vi, "KERNEL7.IMG", DFS_WRITE, sector, &fi)) {
            printf("error opening file\n");
            return -1;
        }
        
        if(kernel!=NULL)
        {
            printf("\r\nBooting kernel....");
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
        char *f = NULL;//readfile("CONFIG  TXT");
        if(f!=NULL)
        {
            uart_dump(f);
        }
    }
}

void bootFromSerial(char *args,unsigned int args_len)
{
    const unsigned int LOADER_ADDR = 0x8000;
    const unsigned int LOAD_ADDR =  &__end;
    #define ACK  0x6
    #define SYN  0x16
    
    printf("\r\nBooting from serial.....\r\n");
    
    printf("Waiting for console to attach......\r\n");
    
    char c;
    do
    {
        c = uart_getc();
        
    }while(c!=SYN);
    
    printf("Waiting for kernel image size......\r\n");
    
    unsigned int size = uart_getc();
    size |= uart_getc() << 8;
    size |= uart_getc() << 16;
    size |= uart_getc() << 24;
    
    
    printf("Recived Image size: 0x%x\r\n",size);
    
    //Q  W  E   R
    //51 57 45  52
    //0x52455751
    
    if(size == 0x52455751 && DEBUG)
    {
        printf("Recived exit sequence\r\n");
        return;
    }
    
    if (LOAD_ADDR + size < LOAD_ADDR || LOAD_ADDR + size > 0x3F000000)
    {
        printf("Wrong Image size\r\n");
        return;
    }
    else
    {
        printf("Image Size correct\r\n");
    }
    printf("Waiting for the Image......\r\n");
    
    char *kernel = (char*)LOAD_ADDR;
    while(size-- > 0)
    {
        *kernel++ = uart_getc();
    }

    printf("Booting the kernel\r\n");
    entry_fn fn = (entry_fn)LOAD_ADDR;
    fn(gr0, gr1, gatags);
    printf("Something went wrong. Dropping shell\r\n");

}
