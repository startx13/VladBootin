#ifndef _FAT_H_
#define _FAT_H_

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       0x0F

int fat_getpartition(void);
unsigned int fat_getcluster(char *fn);
unsigned int fat_getcluster_ex(const char *fn, unsigned int *out_size);
unsigned int fat_readfile(unsigned int cluster);
unsigned int fat_readfile_to(unsigned int cluster, void *dest, unsigned int max_size);
void fat_listdirectory(void);
unsigned int getLastFileSize(void);

#endif
