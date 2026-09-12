#include <stddef.h>
#include <stdint.h>

#include "lib/stdlib.h"
#include "lib/printf.h"
#include "lib/fdt.h"
#include "lib/crypto/sha256.h"
#include "lib/crypto/rsa_pkcs1.h"

#include "core/mm.h"
#include "core/boot_mode/serial_boot.h"
#include "core/boot_mode/file_boot.h"
#include "core/signature_check.h"

#include "driver/framebuffer/lfb.h"
#include "driver/uart/uart.h"
#include "driver/sd/sd.h"
#include "fs/fat.h"

#include "lib/crypto/boot_pubkey.h"     /* BOOT_PUBKEY_N, BOOT_PUBKEY_N_EXPONENT */
#include "lib/crypto/test_signature.h"  /* signature_bytes, generata sopra */

#include "defs.h"

unsigned short int DEBUG = 0;
 
const char* gbanner = "\r\n-------------------------\r\nVladBootin v0.1 beta     \r\nBuilt for Raspberry Pi 2 \r\nBuild Timestamp: %s\r\nGCC version: %d.%d\r\n-------------------------\r\n";
const char* usage = "\r\n----------------------------------------------\r\nhelp - prints this\r\nbanner - prints VladBootin banner\r\nserialboot - starts boot from serial routine\r\nprintf - print something (printf <string>)\r\ndebug - enable debug log\r\nsdinit - init sd card\r\nfileboot - boot from file kernel7.img\r\ntestfile - dump test file\r\nls - list file\r\nmem - print memory map\r\nrelocate - relocate the program at __end\r\ndump - dump heap to stdio\r\ntestalloc - test alloc routine\r\nfatpart - find partition LBA\r\nhomer - show picture\r\nmemreset - clear memory\r\nclearfb - clear framebuffer\r\ncat - print a file (cat <file>)\r\nboot - boot from file (boot <kernel> [dtb])\r\nhexcat - read file in hex format\r\ntestrsa - test rsa signature verification\r\n----------------------------------------------\r\n";

//Typedefs
typedef void (*entry_fn)(uint32_t r0, uint32_t r1, uint32_t atags);

//Functions header
void printMemoryMap();
void handleMenu();
void parseCommand(char* buffer,unsigned int *length);
short unsigned int bufCompare(char* buf1,char* buf2,unsigned int len);
void testAlloc();
void testRead();
void relocate();
void memoryDump();


//Global Vars
uint32_t gr0;
uint32_t gr1;
uint32_t gatags;

int sd_ret = 0;

__attribute__((section(".sig")))
const uint8_t boot_signature[256] = {
    /* placeholder: sarà patchato post-link con la firma reale */
    [0 ... 255] = 0xAA
};

//Roba esterna
extern unsigned char __start;
extern unsigned char __text_start;
extern unsigned char __text_end;
extern unsigned char __rodata_start;
extern unsigned char __rodata_end;
extern unsigned char __sig_start;
extern unsigned char __sig_end;
extern unsigned char __data_start;
extern unsigned char __data_end;
extern unsigned char __bss_start;
extern unsigned char __bss_end;
extern unsigned char __bss_size;
extern unsigned char __end;

extern void halt(void);
extern void prepare_boot(void);
extern void clean_dcache_range(unsigned int start, unsigned int end);
extern void linux_boot(uint32_t kernel_entry,uint32_t machine_type,uint32_t dtb);

//Functions

void testRSA()
{
    rsa_pubkey_t pubkey;
    bn_from_be_bytes(&pubkey.n, BOOT_PUBKEY_N, sizeof(BOOT_PUBKEY_N));
    pubkey.e = BOOT_PUBKEY_N_EXPONENT;

    const char *prova = "Testo Di Prova";
    uint8_t digest[32];
    sha256(prova, strlen(prova), digest);
    print_sha256(digest);

    rsa_verify_result_t res = rsa_pkcs1_v15_verify_sha256(&pubkey, signature_bytes, digest);

    if (res == RSA_VERIFY_OK) {
        printf("\r\n[RSA] Signature OK");
    } else {
        printf("\r\n[RSA] Signature FAILED (code %d)", (int)res);
    }
}

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
    char homer_f[] = "homer";
    char memreset_f[] = "memreset";
    char clearfb_f[] = "clearfb";
    char cat_f[] = "cat";
    char boot_f[] = "boot";
    char hexcat_f[] = "hexcat";
    char testrsa_f[] = "testrsa";
    
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

    if(bufCompare(command,testrsa_f,cmd_len))
    {
        testRSA();
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


void vladBootin_main(uint32_t r0, uint32_t r1, uint32_t atags)
{
    uart_init();  
    
    if (!selfcheck_verify_signature()) {
        printf("\r\n[SELFCHECK] Integrity check failed, halting.");
        halt();
    }

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
    
    int stat = bootFromSerial(NULL, 0); 
    
    switch(stat)
    {
        case RET_ERR: 
            handleMenu(); // Errore seriale -> Va al menu
            break;

        case RET_TIMEOUT:
            sd_ret = sd_init();
            if(sd_ret != SD_OK)
            {
                printf("\r\n[BOOT] SD card initialization failed");
                return;
            }
              
            bootFromFile(NULL, 0);
            break;

        case RET_EXIT:
            handleMenu(); // Utente preme ESC -> Va al menu
            break;
    }
    
    handleMenu();

}

void stop_core()
{
    printf("\r\nStopping Core");
    halt();
}
