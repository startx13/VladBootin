/*
 * Copyright (C) 2018 bzt (bztsrc@github)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
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

#include "lfb.h"
#include "font.h"

#include "../../driver/uart/uart.h"
#include "../../driver/mbox/mbox.h"
#include "../../core/graphics/homer.h"
#include "../../core/graphics/banner.h"
#include "../../lib/printf.h"


unsigned int width, height, pitch, isrgb;
unsigned char *lfb;

unsigned int console_cols;
unsigned int console_rows;
unsigned int cursor_x;
unsigned int cursor_y;


/*
 * Set screen resolution.
 */
void lfb_init()
{
    mbox[0] = 35 * 4;
    mbox[1] = MBOX_REQUEST;

    mbox[2] = 0x48003;  // set physical width/height
    mbox[3] = 8;
    mbox[4] = 8;
    mbox[5] = WID;
    mbox[6] = HEI;

    mbox[7] = 0x48004;  // set virtual width/height
    mbox[8] = 8;
    mbox[9] = 8;
    mbox[10] = WID;
    mbox[11] = HEI;

    mbox[12] = 0x48009; // set virtual offset
    mbox[13] = 8;
    mbox[14] = 8;
    mbox[15] = 0;
    mbox[16] = 0;

    mbox[17] = 0x48005; // set depth
    mbox[18] = 4;
    mbox[19] = 4;
    mbox[20] = 32;

    mbox[21] = 0x48006; // set pixel order
    mbox[22] = 4;
    mbox[23] = 4;
    mbox[24] = 1;       // RGB

    mbox[25] = 0x40001; // get framebuffer
    mbox[26] = 8;
    mbox[27] = 8;
    mbox[28] = 4096;
    mbox[29] = 0;

    mbox[30] = 0x40008; // get pitch
    mbox[31] = 4;
    mbox[32] = 4;
    mbox[33] = 0;

    mbox[34] = MBOX_TAG_LAST;


    if (mbox_call(MBOX_CH_PROP) &&
        mbox[20] == 32 &&
        mbox[28] != 0) {

        /*
         * Convert GPU framebuffer address into ARM address.
         */
        mbox[28] &= 0x3FFFFFFF;

        width  = mbox[5];
        height = mbox[6];
        pitch  = mbox[33];

        lfb = (void *)((unsigned long)mbox[28]);

        /*
         * We explicitly requested RGB pixel order above.
         */
        isrgb = 1;

        /*
         * Console geometry.
         */
        console_cols = width / FONT_WIDTH;
        console_rows = height / FONT_HEIGHT;

        cursor_x = 0;
        cursor_y = 0;

    } else {

        /*
         * Keep framebuffer disabled if initialization failed.
         */
        lfb = 0;

        width = 0;
        height = 0;
        pitch = 0;

        console_cols = 0;
        console_rows = 0;

        cursor_x = 0;
        cursor_y = 0;

        printf(
            "\r\n[LFB] Unable to set screen resolution to %dx%dx32",
            WID,
            HEI
        );
    }
}


/*
 * Draw one character at the current console cursor.
 */
static void lfb_draw_char(char c)
{
    unsigned int x;
    unsigned int y;

    unsigned char *glyph;

    if (!lfb)
        return;

    /*
     * Font table contains 128 characters.
     */
    if ((unsigned char)c >= 128)
        c = '?';

    glyph = font8x16[(unsigned char)c];


    for (y = 0; y < FONT_HEIGHT; y++) {

        unsigned char bits = glyph[y];

        for (x = 0; x < FONT_WIDTH; x++) {

            unsigned int px =
                cursor_x * FONT_WIDTH + x;

            unsigned int py =
                cursor_y * FONT_HEIGHT + y;

            unsigned char *ptr;


            if (px >= width || py >= height)
                continue;


            ptr = lfb + py * pitch + px * 4;


            if (bits & (0x80 >> x)) {

                /*
                 * Foreground = white.
                 */
                ptr[0] = 0xFF;
                ptr[1] = 0xFF;
                ptr[2] = 0xFF;
                ptr[3] = 0x00;

            } else {

                /*
                 * Background = black.
                 */
                ptr[0] = 0x00;
                ptr[1] = 0x00;
                ptr[2] = 0x00;
                ptr[3] = 0x00;
            }
        }
    }
}


/*
 * Clear one console character cell.
 */
static void lfb_clear_cell(
    unsigned int column,
    unsigned int row)
{
    unsigned int x;
    unsigned int y;

    unsigned int px = column * FONT_WIDTH;
    unsigned int py = row * FONT_HEIGHT;


    if (!lfb)
        return;


    for (y = 0; y < FONT_HEIGHT; y++) {

        for (x = 0; x < FONT_WIDTH; x++) {

            unsigned char *ptr;

            if (px + x >= width ||
                py + y >= height)
                continue;

            ptr =
                lfb +
                (py + y) * pitch +
                (px + x) * 4;

            ptr[0] = 0x00;
            ptr[1] = 0x00;
            ptr[2] = 0x00;
            ptr[3] = 0x00;
        }
    }
}


/*
 * Clear an entire console row.
 */
static void lfb_clear_row(unsigned int row)
{
    unsigned int x;

    if (!lfb)
        return;

    for (x = 0; x < console_cols; x++)
        lfb_clear_cell(x, row);
}


/*
 * Scroll the framebuffer console by one text row.
 *
 * Since VladBootin is freestanding, we don't depend on memmove().
 */
static void lfb_scroll(void)
{
    unsigned int y;
    unsigned int x;

    unsigned char *src;
    unsigned char *dst;


    if (!lfb)
        return;


    /*
     * Move every framebuffer scanline upward by FONT_HEIGHT.
     */
    for (y = FONT_HEIGHT; y < height; y++) {

        src = lfb + y * pitch;
        dst = lfb + (y - FONT_HEIGHT) * pitch;

        for (x = 0; x < pitch; x++)
            dst[x] = src[x];
    }


    /*
     * Clear the bottom console row.
     */
    lfb_clear_row(console_rows - 1);
}


/*
 * Reset console cursor.
 */
static void lfb_console_reset(void)
{
    cursor_x = 0;
    cursor_y = 0;
}


/*
 * Output one character to framebuffer.
 */
void lfb_putchar(char c)
{
    if (!lfb)
        return;


    switch (c) {

        /*
         * Carriage return.
         */
        case '\r':

            cursor_x = 0;

            return;


        /*
         * Newline.
         */
        case '\n':

            cursor_x = 0;
            cursor_y++;

            break;


        /*
         * Tab.
         *
         * Move to next 8-column boundary.
         */
        case '\t':

            cursor_x =
                (cursor_x + 8) & ~7U;

            if (cursor_x >= console_cols) {

                cursor_x = 0;
                cursor_y++;
            }

            break;


        /*
         * Backspace.
         */
        case '\b':

            if (cursor_x > 0) {

                cursor_x--;

                lfb_clear_cell(
                    cursor_x,
                    cursor_y
                );
            }

            return;


        /*
         * DEL.
         *
         * Treat it like backspace too.
         */
        case 0x7F:

            if (cursor_x > 0) {

                cursor_x--;

                lfb_clear_cell(
                    cursor_x,
                    cursor_y
                );
            }

            return;


        /*
         * Normal character.
         */
        default:

            /*
             * Ignore other control characters.
             */
            if ((unsigned char)c < 0x20)
                return;


            lfb_draw_char(c);

            cursor_x++;


            /*
             * Automatic line wrap.
             */
            if (cursor_x >= console_cols) {

                cursor_x = 0;
                cursor_y++;
            }

            break;
    }


    /*
     * Bottom of the console reached.
     */
    if (cursor_y >= console_rows) {

        lfb_scroll();

        cursor_y =
            console_rows - 1;
    }
}


/*
 * Show banner picture.
 */
void lfb_showpicture()
{
    int x, y;

    unsigned char *ptr = lfb;
    char *data = banner_data;
    char pixel[4];


    ptr +=
        (height - banner_height) / 2 * pitch +
        (width - banner_width) * 2;


    for (y = 0; y < banner_height; y++) {

        for (x = 0; x < banner_width; x++) {

            HEADER_PIXEL_BANNER(data, pixel);

            /*
             * Image is RGB.
             */
            *((unsigned int *)ptr) =
                isrgb
                    ? *((unsigned int *)&pixel)
                    : (unsigned int)(
                        pixel[0] << 16 |
                        pixel[1] << 8 |
                        pixel[2]
                    );

            ptr += 4;
        }

        ptr += pitch - banner_width * 4;
    }
}


/*
 * Show Homer picture.
 */
void lfb_showhomer()
{
    int x, y;

    unsigned char *ptr = lfb;
    char *data = homer_data;
    char pixel[4];


    ptr +=
        (height - homer_height) / 2 * pitch +
        (width - homer_width) * 2;


    for (y = 0; y < homer_height; y++) {

        for (x = 0; x < homer_width; x++) {

            HEADER_PIXEL_HOMER(data, pixel);

            /*
             * Image is RGB.
             */
            *((unsigned int *)ptr) =
                isrgb
                    ? *((unsigned int *)&pixel)
                    : (unsigned int)(
                        pixel[0] << 16 |
                        pixel[1] << 8 |
                        pixel[2]
                    );

            ptr += 4;
        }

        ptr += pitch - homer_width * 4;
    }
}


/*
 * Clear the entire framebuffer.
 *
 * This is now a real framebuffer clear rather than only clearing
 * the old banner area.
 */
void lfb_clear()
{
    unsigned int x;
    unsigned int y;

    unsigned char *ptr;


    if (!lfb)
        return;


    for (y = 0; y < height; y++) {

        ptr = lfb + y * pitch;

        for (x = 0; x < width; x++) {

            ptr[0] = 0x00;
            ptr[1] = 0x00;
            ptr[2] = 0x00;
            ptr[3] = 0x00;

            ptr += 4;
        }
    }


    lfb_console_reset();
}
