#include "stdlib.h"
#include <stddef.h>

void memcpy(void* dest, void* src, int n)
{
    // Typecast src and dest addresses to (char *)
    char* csrc = (char*)src;
    char* cdest = (char*)dest;

    // Copy contents of src[] to dest[]
    for (int i = 0; i < n; i++)
        cdest[i] = csrc[i];
}

void* memset(void* b, int c, int len)
{
    int           i;
    unsigned char* p = b;
    i = 0;
    while (len > 0)
    {
        *p = c;
        p++;
        len--;
    }
    return(b);
}

int strlen(const char* str)
{
    const char* s;

    for (s = str; *s; ++s)
        ;
    return (s - str);
}

int memcmp(const void* s1, const void* s2, int len)
{
    // Aggiunto const per non scartare il qualificatore dei puntatori in ingresso
    const unsigned char* p = s1;
    const unsigned char* q = s2;
    int charCompareStatus = 0;
    
    // Se entrambi i puntatori puntano allo stesso blocco di memoria
    if (s1 == s2)
    {
        return charCompareStatus;
    }
    while (len > 0)
    {
        if (*p != *q)
        {
            // Confronta i caratteri che non corrispondono
            charCompareStatus = (*p > * q) ? 1 : -1;
            break;
        }
        len--;
        p++;
        q++;
    }
    return charCompareStatus;
}


char* strncpy(char* dst, const char* src, int n)
{
    int i;
    char* temp;
    temp = dst;
    for (i = 0; i < n; i++)
        *dst++ = *src++;
    return temp;
}

int strcmp(const char* X, const char* Y)
{
    while (*X)
    {
        // if characters differ or end of second string is reached
        if (*X != *Y)
            break;

        // move to next pair of characters
        X++;
        Y++;
    }

    // return the ASCII difference after converting char* to unsigned char*
    return *(const unsigned char*)X - *(const unsigned char*)Y;
}

struct div_t div(int numer, int denom)
{
    struct div_t out;
    out.quot = numer / denom;
    out.rem = numer % denom;
}

struct ldiv_t ldiv(long int numer, long int denom)
{
    struct div_t out;
    out.quot = numer / denom;
    out.rem = numer % denom;
}

char* strcpy(char* strDest, const char* strSrc)
{
    char* temp = strDest;
    while (*strDest++ = *strSrc++); // or while((*strDest++=*strSrc++) != '\0');
    return temp;
}
