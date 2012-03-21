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

#if !defined(_ICON_UTIL)
#define _ICON_UTIL

#include <windows.h>
#include "rfb.h"

class IconUtil {
 private:
    static BOOL readPNG(UCHAR *image, UCHAR *mask, int iconsize, BOOL bTopDown, UCHAR *iconData, ULONG iconDataLength);
 public:
    static HICON createIcon(rfbServerDataIcon *iconData, ULONG iconDataLength);
    static BOOL saveIconFile(wchar_t *iconPath, rfbServerDataIcon *iconData, ULONG iconDataLength);
};

#endif // !defined(_ICON_UTIL)
