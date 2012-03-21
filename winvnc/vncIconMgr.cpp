//  Copyright (C) 2005-2006 UCHINO Satoshi. All Rights Reserved.
//
//  This file is part of the VNC system.
//
//  The VNC system is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
//  USA.

#include "vncIconMgr.h"
#include "png.h"
#include <stdlib.h>

typedef struct {
    char *buf;
    unsigned long size;
    unsigned long offset;
} mem_io_t;

void mem_io_write(png_structp png_ptr, png_bytep data, png_size_t length)
{
    mem_io_t *mem_io_p = (mem_io_t *)png_get_io_ptr(png_ptr);

    if (mem_io_p->offset + length > mem_io_p->size) {
        size_t new_size = mem_io_p->size * 2;
        while (mem_io_p->offset + length > new_size)
            new_size *= 2;

        mem_io_p->buf = (char *)realloc(mem_io_p->buf, new_size);
        if (mem_io_p->buf == NULL) {
            png_error(png_ptr, "memory allocation error!");
            /* NOTREACHED */
        }
        mem_io_p->size = new_size;
    }

    memcpy(mem_io_p->buf + mem_io_p->offset, data, length);
    mem_io_p->offset += length;
}

BOOL vncIconMgr::createIcon(HDC hMemDC, HICON hIcon, int iconsize,
                         rfbServerDataIcon **iconData, ULONG *iconDataLength)
{
    ICONINFO iconInfo;
    mem_io_t mem_io;
    UCHAR* image;
    UCHAR* mask;
    rfbServerDataIcon *icon;
	int i;

    if (!GetIconInfo(hIcon, &iconInfo)) {
        vnclog.Print(LL_INTERR, VNCLOG("failed to get icon info (err=%lu\n"), GetLastError());
        return FALSE;
    }
    mem_io.size = iconsize * iconsize * 4 + sz_rfbServerDataIconHdr;
    mem_io.buf = (char *)malloc(mem_io.size);
    if (mem_io.buf == NULL) {
        vnclog.Print(LL_INTERR, VNCLOG("malloc failed (size=%lu)\n"), mem_io.size);
        return FALSE;
    }
    /* set up header */

    icon = (rfbServerDataIcon *)mem_io.buf;
    icon->bpp  = 32;
    icon->size = iconsize;
    icon->formatType = rfbIconFormatTypePng;

    mem_io.offset = sz_rfbServerDataIconHdr;

    /* get bitmap images */

    image = (UCHAR *)malloc(iconsize * iconsize * 4);
    if (image == NULL) {
        free(mem_io.buf);
        vnclog.Print(LL_INTERR, VNCLOG("malloc failed (size=%lu)\n"), iconsize * iconsize * 4);
        return FALSE;
    }
    mask  = (UCHAR *)malloc(iconsize * iconsize * 4);
    if (mask == NULL) {
        free(mem_io.buf);
        free(image);
        return FALSE;
    }

    BITMAPINFO bmi;
    bmi.bmiHeader.biSize   = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth  = iconsize;
    bmi.bmiHeader.biHeight = -iconsize;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    if (GetDIBits(hMemDC, iconInfo.hbmColor, 0, iconsize, image, &bmi, DIB_RGB_COLORS) == 0) {
        vnclog.Print(LL_INTERR, VNCLOG("GetDIBits failed (err=%lu)\n"), GetLastError());
        free(mem_io.buf);
        free(image);
        free(mask);
        return FALSE;
    }
    if (GetDIBits(hMemDC, iconInfo.hbmMask, 0, iconsize, mask, &bmi, DIB_RGB_COLORS) == 0) {
        vnclog.Print(LL_INTERR, VNCLOG("GetDIBits failed (err=%lu)\n"), GetLastError());
        free(mem_io.buf);
        free(image);
        free(mask);
        return FALSE;
    }

    /* set alpha channel */
    for (i = 0; i < iconsize * iconsize * 4; i += 4) {
        if (image[i+3] == 0)
            image[i+3] = 0xff - mask[i];
    }
    free(mask);

    /* create PNG image */

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        vnclog.Print(LL_INTERR, VNCLOG("png_create_write_struct failed\n"));
        free(mem_io.buf);
        free(image);
        return FALSE;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        vnclog.Print(LL_INTERR, VNCLOG("png_create_info_struct failed\n"));
        png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
        free(mem_io.buf);
        free(image);
        return FALSE;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        vnclog.Print(LL_INTERR, VNCLOG("error in creating png\n"));
        png_destroy_write_struct(&png_ptr, &info_ptr);
        free(mem_io.buf);
        free(image);
        return FALSE;
    }

    png_set_write_fn(png_ptr, (png_voidp)&mem_io,  mem_io_write, NULL);
    png_set_IHDR(png_ptr, info_ptr,
                 iconsize, iconsize, 8,
                 PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_byte **row_pointers = (png_byte **)malloc(iconsize * sizeof(png_byte *));
    if (row_pointers == NULL)
        png_error(png_ptr, "memory allocation error!");

    for (i = 0; i < iconsize; i++) {
        row_pointers[i] = (png_byte *)&image[i * iconsize * 4];
    }

    png_set_rows(png_ptr, info_ptr, row_pointers);

    png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_BGR, NULL);

    free(row_pointers);
    free(image);
    png_destroy_write_struct(&png_ptr, &info_ptr);

    *iconData       = (rfbServerDataIcon *)mem_io.buf;
    *iconDataLength = mem_io.offset;

    return TRUE;
}

BOOL vncIconMgr::deleteIcon(rfbServerDataIcon *iconData)
{
    free(iconData);
    return TRUE;
}
