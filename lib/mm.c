#include "printf.h"

struct block
{
    unsigned int size;
    unsigned int ptr;
    unsigned int next;
};

extern unsigned char __end;

unsigned int nextptr = &__end;
unsigned int memoryEnd = 0x3F000000;

unsigned int alloc(unsigned int size)
{
    unsigned int ptr = nextptr;
    if(ptr + size > memoryEnd)
    {
        printf("\r\nNot enough space after __end");
        return NULL;
    }
    nextptr = ptr+size;
    return ptr;
}
