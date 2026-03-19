/*******************************************************************************
 * Size: 12 px
 * Bpp: 1
 * Opts: --bpp 1 --size 12 --no-compress --stride 1 --align 1 --font Montserrat-Bold.ttf --range 32-127,8226 --format lvgl -o montserrat_12_1bpp.c
 ******************************************************************************/

#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif



#ifndef MONTSERRAT_12_1BPP
#define MONTSERRAT_12_1BPP 1
#endif

#if MONTSERRAT_12_1BPP

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xff, 0xf0, 0xc0,

    /* U+0022 "\"" */
    0xff, 0xf0,

    /* U+0023 "#" */
    0x36, 0x26, 0xff, 0x26, 0x24, 0x64, 0xff, 0x64,
    0x6c,

    /* U+0024 "$" */
    0x10, 0x21, 0xfe, 0xad, 0x1f, 0x1f, 0x1f, 0x17,
    0x2f, 0xf0, 0x81, 0x0,

    /* U+0025 "%" */
    0x61, 0xa4, 0xc9, 0x22, 0x58, 0x6d, 0x86, 0x91,
    0x24, 0xc9, 0x61, 0x80,

    /* U+0026 "&" */
    0x38, 0x4c, 0x4c, 0x7c, 0x72, 0xfb, 0xce, 0xc6,
    0x7b,

    /* U+0027 "'" */
    0xfc,

    /* U+0028 "(" */
    0x6f, 0x6d, 0xb6, 0xd9, 0xb0,

    /* U+0029 ")" */
    0xd9, 0xb6, 0xdb, 0x6f, 0x60,

    /* U+002A "*" */
    0x27, 0xdd, 0xf2, 0x0,

    /* U+002B "+" */
    0x30, 0xc3, 0x3f, 0x30, 0xc0,

    /* U+002C "," */
    0xd8,

    /* U+002D "-" */
    0xe0,

    /* U+002E "." */
    0xc0,

    /* U+002F "/" */
    0x18, 0xc4, 0x63, 0x11, 0x8c, 0x46, 0x31, 0x0,

    /* U+0030 "0" */
    0x38, 0xdb, 0x1e, 0x3c, 0x78, 0xf1, 0xb6, 0x38,

    /* U+0031 "1" */
    0xf3, 0x33, 0x33, 0x33, 0x30,

    /* U+0032 "2" */
    0xfa, 0x30, 0xc3, 0x1c, 0xe7, 0x18, 0xfc,

    /* U+0033 "3" */
    0xfc, 0x61, 0x8c, 0x38, 0x30, 0xe3, 0xf8,

    /* U+0034 "4" */
    0xc, 0x1c, 0x18, 0x30, 0x36, 0x66, 0xff, 0x6,
    0x6,

    /* U+0035 "5" */
    0x7c, 0xc1, 0x83, 0x7, 0xc0, 0xc1, 0xa3, 0xfc,

    /* U+0036 "6" */
    0x3e, 0xc3, 0x7, 0xee, 0x78, 0xf1, 0xb3, 0x3c,

    /* U+0037 "7" */
    0xff, 0x9b, 0x30, 0x61, 0x83, 0x6, 0x18, 0x30,

    /* U+0038 "8" */
    0x7d, 0x8f, 0x1e, 0x37, 0xd8, 0xf1, 0xe3, 0x7c,

    /* U+0039 "9" */
    0x79, 0x8b, 0x1e, 0x77, 0xe0, 0xc1, 0x86, 0xf8,

    /* U+003A ":" */
    0xc0, 0xc,

    /* U+003B ";" */
    0xc0, 0xd, 0x80,

    /* U+003C "<" */
    0x4, 0x7f, 0x30, 0xf0, 0x70, 0x40,

    /* U+003D "=" */
    0xfc, 0x0, 0x3f,

    /* U+003E ">" */
    0x3, 0x87, 0x83, 0x7b, 0x80, 0x0,

    /* U+003F "?" */
    0xfb, 0x30, 0xc3, 0x18, 0xe3, 0x0, 0x30,

    /* U+0040 "@" */
    0x1f, 0x6, 0x11, 0xff, 0x7d, 0xdf, 0x1b, 0xe3,
    0x7c, 0x6e, 0xdd, 0x7e, 0xc6, 0x0, 0x7e, 0x0,

    /* U+0041 "A" */
    0x1c, 0xe, 0x5, 0x86, 0xc3, 0x23, 0x19, 0xfd,
    0x83, 0xc1, 0x80,

    /* U+0042 "B" */
    0xfc, 0xc6, 0xc6, 0xc6, 0xfe, 0xc3, 0xc3, 0xc3,
    0xfe,

    /* U+0043 "C" */
    0x3e, 0x63, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0x63,
    0x3e,

    /* U+0044 "D" */
    0xfc, 0xc6, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc6,
    0xfc,

    /* U+0045 "E" */
    0xff, 0x83, 0x6, 0xf, 0xd8, 0x30, 0x60, 0xfe,

    /* U+0046 "F" */
    0xff, 0x83, 0x6, 0xf, 0xd8, 0x30, 0x60, 0xc0,

    /* U+0047 "G" */
    0x3e, 0x63, 0xc0, 0xc0, 0xc3, 0xc3, 0xc3, 0x63,
    0x3f,

    /* U+0048 "H" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xff, 0xc3, 0xc3, 0xc3,
    0xc3,

    /* U+0049 "I" */
    0xff, 0xff, 0xc0,

    /* U+004A "J" */
    0x7c, 0x30, 0xc3, 0xc, 0x30, 0xd3, 0x78,

    /* U+004B "K" */
    0xc6, 0xce, 0xcc, 0xd8, 0xf8, 0xfc, 0xcc, 0xc6,
    0xc7,

    /* U+004C "L" */
    0xc3, 0xc, 0x30, 0xc3, 0xc, 0x30, 0xfc,

    /* U+004D "M" */
    0xc0, 0xf8, 0x7e, 0x1f, 0xcf, 0xf3, 0xf7, 0xbc,
    0xcf, 0x33, 0xc0, 0xc0,

    /* U+004E "N" */
    0xc3, 0xe3, 0xf3, 0xfb, 0xff, 0xdf, 0xcf, 0xc7,
    0xc3,

    /* U+004F "O" */
    0x3e, 0x31, 0xb0, 0x78, 0x3c, 0x1e, 0xf, 0x6,
    0xc6, 0x3e, 0x0,

    /* U+0050 "P" */
    0xfd, 0x8f, 0x1e, 0x3c, 0x7f, 0xb0, 0x60, 0xc0,

    /* U+0051 "Q" */
    0x3e, 0x31, 0xb0, 0x78, 0x3c, 0x1e, 0xf, 0x6,
    0xc6, 0x3e, 0x6, 0x41, 0xe0,

    /* U+0052 "R" */
    0xfd, 0x8f, 0x1e, 0x3c, 0x7f, 0xb2, 0x66, 0xc6,

    /* U+0053 "S" */
    0x7f, 0x8b, 0x7, 0xc7, 0xc7, 0xc1, 0xc3, 0xfc,

    /* U+0054 "T" */
    0xfe, 0x30, 0x60, 0xc1, 0x83, 0x6, 0xc, 0x18,

    /* U+0055 "U" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x66,
    0x3c,

    /* U+0056 "V" */
    0xc1, 0xf0, 0x98, 0xcc, 0x63, 0x61, 0xb0, 0xf0,
    0x38, 0x1c, 0x0,

    /* U+0057 "W" */
    0xc3, 0xd, 0x8c, 0x66, 0x79, 0x99, 0xe6, 0x37,
    0xb0, 0xf3, 0xc3, 0xcf, 0x7, 0x38, 0x18, 0x60,

    /* U+0058 "X" */
    0x63, 0x99, 0x8f, 0x83, 0xc1, 0xc0, 0xf0, 0xd8,
    0xe6, 0x61, 0x80,

    /* U+0059 "Y" */
    0xc3, 0x66, 0x66, 0x3c, 0x3c, 0x18, 0x18, 0x18,
    0x18,

    /* U+005A "Z" */
    0xfe, 0x1c, 0x30, 0xc3, 0x86, 0x18, 0x70, 0xfe,

    /* U+005B "[" */
    0xfc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcf,

    /* U+005C "\\" */
    0x41, 0x86, 0x8, 0x30, 0xc1, 0x6, 0x18, 0x20,
    0xc3,

    /* U+005D "]" */
    0xf3, 0x33, 0x33, 0x33, 0x33, 0x3f,

    /* U+005E "^" */
    0x21, 0xc5, 0x16, 0xca, 0x20,

    /* U+005F "_" */
    0xfc,

    /* U+0060 "`" */
    0x42,

    /* U+0061 "a" */
    0xf8, 0x30, 0xdf, 0xcf, 0x37, 0xc0,

    /* U+0062 "b" */
    0xc1, 0x83, 0x7, 0xee, 0xd8, 0xf1, 0xe3, 0xed,
    0xf8,

    /* U+0063 "c" */
    0x3c, 0xcb, 0x6, 0xc, 0xc, 0x8f, 0x0,

    /* U+0064 "d" */
    0x6, 0xc, 0x1b, 0xf6, 0xf8, 0xf1, 0xe3, 0x6e,
    0xfc,

    /* U+0065 "e" */
    0x38, 0xcb, 0x1f, 0xfc, 0xc, 0x8f, 0x0,

    /* U+0066 "f" */
    0x3b, 0x19, 0xf6, 0x31, 0x8c, 0x63, 0x0,

    /* U+0067 "g" */
    0x7f, 0x8f, 0x1e, 0x3c, 0x6f, 0xc1, 0xa3, 0x7c,

    /* U+0068 "h" */
    0xc1, 0x83, 0x7, 0xec, 0x78, 0xf1, 0xe3, 0xc7,
    0x8c,

    /* U+0069 "i" */
    0x30, 0xff, 0xfc,

    /* U+006A "j" */
    0x3, 0x0, 0x33, 0x33, 0x33, 0x33, 0xe0,

    /* U+006B "k" */
    0xc1, 0x83, 0x6, 0x7d, 0xdf, 0x3e, 0x7c, 0xcd,
    0x8c,

    /* U+006C "l" */
    0xff, 0xff, 0xf0,

    /* U+006D "m" */
    0xfb, 0xb3, 0x3c, 0xcf, 0x33, 0xcc, 0xf3, 0x3c,
    0xcc,

    /* U+006E "n" */
    0xfd, 0x8f, 0x1e, 0x3c, 0x78, 0xf1, 0x80,

    /* U+006F "o" */
    0x38, 0xdb, 0x1e, 0x3c, 0x6d, 0x8e, 0x0,

    /* U+0070 "p" */
    0xfd, 0xdb, 0x1e, 0x3c, 0x7d, 0xff, 0x60, 0xc0,

    /* U+0071 "q" */
    0x7e, 0xdf, 0x1e, 0x3c, 0x6d, 0xdf, 0x83, 0x6,

    /* U+0072 "r" */
    0xfe, 0xcc, 0xcc, 0xc0,

    /* U+0073 "s" */
    0x7b, 0x2e, 0x3e, 0x3a, 0x2f, 0x0,

    /* U+0074 "t" */
    0x63, 0x3e, 0xc6, 0x31, 0x8c, 0x38,

    /* U+0075 "u" */
    0xc7, 0x8f, 0x1e, 0x3c, 0x79, 0xdf, 0x80,

    /* U+0076 "v" */
    0xc7, 0x8d, 0xb3, 0x63, 0xc7, 0xe, 0x0,

    /* U+0077 "w" */
    0xc6, 0x69, 0xcd, 0xb9, 0x35, 0xe3, 0xbc, 0x73,
    0xc, 0x60,

    /* U+0078 "x" */
    0x66, 0xd8, 0xf0, 0xc3, 0xcd, 0xb9, 0x80,

    /* U+0079 "y" */
    0xc6, 0x8d, 0xb3, 0x63, 0x87, 0xe, 0x18, 0xe0,

    /* U+007A "z" */
    0xfc, 0x61, 0x8c, 0x61, 0x8f, 0xc0,

    /* U+007B "{" */
    0x36, 0x66, 0x6c, 0x66, 0x66, 0x63,

    /* U+007C "|" */
    0xff, 0xff, 0xff,

    /* U+007D "}" */
    0xc6, 0x66, 0x63, 0x66, 0x66, 0x6c,

    /* U+007E "~" */
    0xe6, 0x70,

    /* U+2022 "•" */
    0xff, 0x80
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 54, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 55, .box_w = 2, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 4, .adv_w = 84, .box_w = 4, .box_h = 3, .ofs_x = 1, .ofs_y = 6},
    {.bitmap_index = 6, .adv_w = 138, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 15, .adv_w = 122, .box_w = 7, .box_h = 13, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 27, .adv_w = 168, .box_w = 10, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 39, .adv_w = 140, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 48, .adv_w = 44, .box_w = 2, .box_h = 3, .ofs_x = 1, .ofs_y = 6},
    {.bitmap_index = 49, .adv_w = 69, .box_w = 3, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 54, .adv_w = 69, .box_w = 3, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 59, .adv_w = 83, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 63, .adv_w = 115, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 68, .adv_w = 50, .box_w = 2, .box_h = 3, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 69, .adv_w = 74, .box_w = 3, .box_h = 1, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 70, .adv_w = 50, .box_w = 2, .box_h = 1, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 71, .adv_w = 75, .box_w = 5, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 79, .adv_w = 130, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 87, .adv_w = 75, .box_w = 4, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 92, .adv_w = 113, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 99, .adv_w = 114, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 106, .adv_w = 132, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 115, .adv_w = 114, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 123, .adv_w = 122, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 131, .adv_w = 119, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 139, .adv_w = 127, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 147, .adv_w = 122, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 155, .adv_w = 50, .box_w = 2, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 157, .adv_w = 50, .box_w = 2, .box_h = 9, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 160, .adv_w = 115, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 166, .adv_w = 115, .box_w = 6, .box_h = 4, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 169, .adv_w = 115, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 175, .adv_w = 113, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 182, .adv_w = 199, .box_w = 11, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 198, .adv_w = 147, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 209, .adv_w = 147, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 218, .adv_w = 139, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 227, .adv_w = 159, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 236, .adv_w = 129, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 244, .adv_w = 123, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 252, .adv_w = 148, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 261, .adv_w = 155, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 270, .adv_w = 63, .box_w = 2, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 273, .adv_w = 104, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 280, .adv_w = 142, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 289, .adv_w = 116, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 296, .adv_w = 183, .box_w = 10, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 308, .adv_w = 155, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 317, .adv_w = 162, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 328, .adv_w = 141, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 336, .adv_w = 162, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 349, .adv_w = 141, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 357, .adv_w = 122, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 365, .adv_w = 119, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 373, .adv_w = 151, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 382, .adv_w = 143, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 393, .adv_w = 223, .box_w = 14, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 409, .adv_w = 137, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 420, .adv_w = 130, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 429, .adv_w = 129, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 437, .adv_w = 71, .box_w = 4, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 443, .adv_w = 75, .box_w = 6, .box_h = 12, .ofs_x = -1, .ofs_y = -1},
    {.bitmap_index = 452, .adv_w = 71, .box_w = 4, .box_h = 12, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 458, .adv_w = 115, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 463, .adv_w = 96, .box_w = 6, .box_h = 1, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 464, .adv_w = 115, .box_w = 4, .box_h = 2, .ofs_x = 1, .ofs_y = 8},
    {.bitmap_index = 465, .adv_w = 118, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 471, .adv_w = 132, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 480, .adv_w = 113, .box_w = 7, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 487, .adv_w = 133, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 496, .adv_w = 121, .box_w = 7, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 503, .adv_w = 74, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 510, .adv_w = 134, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 518, .adv_w = 133, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 527, .adv_w = 58, .box_w = 2, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 530, .adv_w = 59, .box_w = 4, .box_h = 13, .ofs_x = -1, .ofs_y = -2},
    {.bitmap_index = 537, .adv_w = 126, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 546, .adv_w = 58, .box_w = 2, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 549, .adv_w = 201, .box_w = 10, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 558, .adv_w = 133, .box_w = 7, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 565, .adv_w = 126, .box_w = 7, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 572, .adv_w = 132, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 580, .adv_w = 132, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 588, .adv_w = 83, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 592, .adv_w = 102, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 598, .adv_w = 84, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 604, .adv_w = 132, .box_w = 7, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 611, .adv_w = 115, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 618, .adv_w = 180, .box_w = 11, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 628, .adv_w = 114, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 635, .adv_w = 115, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 643, .adv_w = 104, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 649, .adv_w = 75, .box_w = 4, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 655, .adv_w = 59, .box_w = 2, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 658, .adv_w = 75, .box_w = 4, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 664, .adv_w = 115, .box_w = 6, .box_h = 2, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 666, .adv_w = 69, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = 2}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 8226, .range_length = 1, .glyph_id_start = 96,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 2,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif

};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t montserrat_12_1bpp = {
#else
lv_font_t montserrat_12_1bpp = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 13,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    // .static_bitmap = 0,
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if MONTSERRAT_12_1BPP*/
