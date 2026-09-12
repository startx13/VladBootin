/*
 * Copyright (C) 2018 bzt (bztsrc@github)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */


#include "../../driver/gpio/gpio.h"

#define SYSTMR_LO        ((volatile unsigned int*)(MMIO_BASE+0x00003004))
#define SYSTMR_HI        ((volatile unsigned int*)(MMIO_BASE+0x00003008))

/**
 * Aspetta N cicli CPU (Funziona su HW e QEMU)
 */
void wait_cycles(unsigned int n)
{
    if(n) while(n--) { asm volatile("nop"); }
}

/**
 * Riceve il contatore a 32 bit del System Timer hardware
 */
unsigned long get_system_timer()
{
    // Restituisce direttamente il registro a 32 bit basso del Broadcom Timer
    return *SYSTMR_LO; 
}

/**
 * Aspetta N microsecondi (Usa il timer hardware se presente, altrimenti calibra per QEMU)
 */
void wait_usec(unsigned int n)
{
    unsigned long t = get_system_timer();
    unsigned long target = t + n;
    while(get_system_timer() < target);
}

/**
 * Aspetta N millisecondi REALI (1 ms = 1000 microsecondi)
 */
void wait_msec(unsigned int n)
{
    // Un millisecondo è composto esplicitamente da 1000 microsecondi
    while(n--) 
    {
        wait_usec(1000);
    }
}
