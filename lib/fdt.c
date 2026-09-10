#include "fdt.h"
#include "stdlib.h"
#include "printf.h"

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

void fdt_update_bootargs(void *dtb_base, const char *cmdline)
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