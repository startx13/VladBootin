#define SD_OK                0
#define SD_TIMEOUT          -1
#define SD_ERROR            -2

int sd_init();
int sd_readblock(unsigned int lba, unsigned int *buffer, unsigned int num);
