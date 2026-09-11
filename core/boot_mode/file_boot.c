#include <stdint.h>
#include "file_boot.h"
#include "../../driver/sd/sd.h"
#include "../../fs/fat.h"
#include "../../lib/printf.h"
#include "../../lib/fdt.h"
#include "../../lib/stdlib.h"
#include "../../defs.h"
#include "../../lib/crypto/sha256.h"

extern void halt(void);
extern void prepare_boot(void);
extern void clean_dcache_range(unsigned int start, unsigned int end);
extern void linux_boot(uint32_t kernel_entry,uint32_t machine_type,uint32_t dtb);

void bootFromFile(const char *kname, const char *dtbname)
{

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

    printf("\r\n[DEBUG] BEFORE CMD: k_size=%u", k_size);
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
        strcpy(cmdline_buf, "console=ttyAMA0,115200 root=/dev/mmcblk0p2 rootwait");
    }

    fdt_update_bootargs((void *)DTB_LOAD_ADDR, cmdline_buf);
    fdt_update_memory((void *)DTB_LOAD_ADDR, 0x3c000000); // Assume 512MB RAM for now
 
    uint8_t digest[32];
    sha256((void *)KERNEL_LOAD_ADDR, (size_t)k_size, digest);
    print_sha256(digest);
    
    //printf("\r\n[BOOT] Preparing CPU for Linux handoff...");
    //prepare_boot();
    
    //linux_boot(KERNEL_LOAD_ADDR, 0xFFFFFFFF, DTB_LOAD_ADDR);

}