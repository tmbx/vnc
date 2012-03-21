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

#include "IconUtil.h"
#include "png.h"
#include <stdlib.h>

typedef struct {
    char *buf;
    unsigned long size;
    unsigned long offset;
} mem_io_t;

static void mem_io_read(png_structp png_ptr, png_bytep data, png_size_t length)
{
    mem_io_t *mem_io_p = (mem_io_t *)png_get_io_ptr(png_ptr);
    memcpy(data, mem_io_p->buf + mem_io_p->offset, length);
    mem_io_p->offset += length;
}

BOOL IconUtil::readPNG(UCHAR *image, UCHAR *mask, int iconsize, BOOL bTopDown, UCHAR *iconData, ULONG iconDataLength)
{
    mem_io_t mem_io;
	int i;

    mem_io.size = iconDataLength;
    mem_io.buf  = (char *)iconData;
    mem_io.offset = 0;

    /* read PNG image */
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL)
        return FALSE;

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
        return FALSE;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
        return FALSE;
    }

    png_set_read_fn(png_ptr, (png_voidp)&mem_io,  mem_io_read);
    png_byte **row_pointers = (png_byte **)malloc(iconsize * sizeof(png_byte *));
    if (row_pointers == NULL)
        png_error(png_ptr, "memory allocation error!");

    if (bTopDown) {
        for (i = 0; i < iconsize; i++) {
            row_pointers[iconsize - i - 1] = (png_byte *)&image[i * iconsize * 4];
        }
    } else {
        for (i = 0; i < iconsize; i++) {
            row_pointers[i] = (png_byte *)&image[i * iconsize * 4];
        }
    }
    png_set_rows(png_ptr, info_ptr, row_pointers);

    png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_BGR, NULL);

    png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);

    /* split mask from image */
    for (i = 0; i < iconsize * iconsize * 4; i += 4) {
        memset(&mask[i], 0xff - image[i+3], 4);
    }
    return TRUE;
}

HICON IconUtil::createIcon(rfbServerDataIcon *iconData, ULONG iconDataLength)
{
    UCHAR* image;
    UCHAR* mask;
    int iconsize = iconData->size;

    /* allocate bitmap image buffers */
    image = (UCHAR *)malloc(iconsize * iconsize * 4);
    if (image == NULL) {
        return FALSE;
    }
    mask  = (UCHAR *)malloc(iconsize * iconsize * 4);
    if (mask == NULL) {
        free(image);
        return FALSE;
    }

    /* read PNG image */
    if (!readPNG(image, mask, iconsize, FALSE,
                 iconData->iconData,
                 iconDataLength - sz_rfbServerDataIconHdr)) {
        free(image);
        free(mask);
        return FALSE;
    }

    /* create icon */
    ICONINFO iconInfo;
    iconInfo.fIcon    = TRUE;
    iconInfo.hbmColor = CreateBitmap(iconsize, iconsize, 1, 32, image);
    iconInfo.hbmMask  = CreateBitmap(iconsize, iconsize, 1, 32, mask );

    HICON hIcon = CreateIconIndirect(&iconInfo);

    DeleteObject(iconInfo.hbmColor);
    DeleteObject(iconInfo.hbmMask);
    free(image);
    free(mask);

    return hIcon;
}

BOOL IconUtil::saveIconFile(wchar_t *iconPath, rfbServerDataIcon *iconData, ULONG iconDataLength)
{
    UCHAR* image;
    UCHAR* mask;
    int iconsize = iconData->size;

    /* allocate bitmap image buffers */
    image = (UCHAR *)malloc(iconsize * iconsize * 4);
    if (image == NULL) {
        return FALSE;
    }
    mask  = (UCHAR *)malloc(iconsize * iconsize * 4);
    if (mask == NULL) {
        free(image);
        return FALSE;
    }

    /* read PNG image */
    if (!readPNG(image, mask, iconsize, TRUE,
                 iconData->iconData,
                 iconDataLength - sz_rfbServerDataIconHdr)) {
        free(image);
        free(mask);
        return FALSE;
    }

    /* save .ico file */
    FILE *fp = _wfopen(iconPath, L"wb");
    
    struct ICONDIR {
        WORD           idReserved;   // Reserved (must be 0)
        WORD           idType;       // Resource Type (1 for icons)
        WORD           idCount;      // How many images?
        //ICONDIRENTRY   idEntries[1]; // An entry for each image (idCount of 'em)
    } id;
    id.idReserved = 0;
    id.idType = 1;
    id.idCount = 1;
    fwrite(&id, sizeof(WORD), 3, fp);

    struct ICONDIRENTRY {
        BYTE        bWidth;          // Width, in pixels, of the image
        BYTE        bHeight;         // Height, in pixels, of the image
        BYTE        bColorCount;     // Number of colors in image (0 if >=8bpp)
        BYTE        bReserved;       // Reserved ( must be 0)
        WORD        wPlanes;         // Color Planes
        WORD        wBitCount;       // Bits per pixel
        DWORD       dwBytesInRes;    // How many bytes in this resource?
        DWORD       dwImageOffset;   // Where in the file is this image?
    } ide;
    ide.bWidth = iconsize;
    ide.bHeight = iconsize;
    ide.bColorCount = 0;
    ide.bReserved = 0;
    ide.wPlanes = 1;
    ide.wBitCount = 32;
    ide.dwBytesInRes  = sizeof(BITMAPINFOHEADER) + iconsize * iconsize * 4 * 2;
    ide.dwImageOffset = sizeof(WORD)*3 + sizeof(ICONDIRENTRY);
    fwrite(&ide, sizeof(ICONDIRENTRY), 1, fp);

    /* BITMAPINFOHEADER */
    BITMAPINFOHEADER bi;
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth  = iconsize;
    bi.biHeight = iconsize * 2; //XXX why?
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = iconsize * iconsize * 4;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;
    fwrite(&bi, sizeof(BITMAPINFOHEADER), 1, fp);
    fwrite(image, 4, iconsize * iconsize, fp);
    fwrite(mask,  4, iconsize * iconsize, fp);

    fclose(fp);
    free(image);
    free(mask);

    return TRUE;
}
