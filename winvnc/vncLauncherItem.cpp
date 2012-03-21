//  Copyright (C) 2004-2006 UCHINO Satoshi. All Rights Reserved.
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

#include "vncLauncherItem.h"

vncLauncherItem::vncLauncherItem(int id, wchar_t *commandpath, char *menupath, int menulen) {
    m_id = id;
    m_commandpath = (wchar_t *)malloc((wcslen(commandpath) + 1) * sizeof(wchar_t));
    wcscpy(m_commandpath, commandpath);
    if (isLink(commandpath))
        menulen -= 4;   // remove extension ".lnk"
    m_menupath = (char *)malloc(menulen * sizeof(char));
    strncpy(m_menupath, menupath, menulen);
    m_menupathlen = menulen;
}

vncLauncherItem::~vncLauncherItem() {
    if (m_commandpath)
        free(m_commandpath);
	if (m_menupath)
        free(m_menupath);
}

HICON vncLauncherItem::getSmallIcon() {
    SHFILEINFOW sfi;
    DWORD ret = SHGetFileInfoW(m_commandpath, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON);
    if (ret == NULL)
        return NULL;
    return sfi.hIcon;    
}

HICON vncLauncherItem::getLargeIcon() {
    SHFILEINFOW sfi;
    DWORD ret = SHGetFileInfoW(m_commandpath, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON);
    if (ret == NULL)
        return NULL;
    return sfi.hIcon;    
}
