//  Copyright (C) 2006 UCHINO Satoshi. All Rights Reserved.
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

#ifndef METALAUNCHER_H
#define METALAUNCHER_H

#include "stdhdrs.h"

class ClientConnection;

class MetaLauncher
{
 private:
    static const wchar_t *topMenuFolder;
    static const wchar_t *topDataFolder;
    static const wchar_t *menuListName;
    wchar_t *m_desktopFolderName;
    wchar_t *m_programName;
    wchar_t *m_baseMenuFolder;
    wchar_t *m_baseDataFolder;
    FILE *m_fpMenuList;
	ClientConnection *m_clientconn;
 public:
    static void launch(HWND hwnd, ULONG id);
    static void cleanAllMenus();
    MetaLauncher(ClientConnection *pCC);
	virtual ~MetaLauncher();
    BOOL createMenuItem(ULONG id, const char *path, size_t pathlen);
    BOOL createIconFile(ULONG id, rfbServerDataIcon *iconData, ULONG iconDataLength);
    void clear();
 private:
    static void cleanMenus();
    static void cleanIcons();
    static void removeMenu(wchar_t *menuPath);
    static void removeMenuDirs(wchar_t *menuPath);
    static void createMenuDirs(wchar_t *menuPath);
    void setBaseMenuFolder();
    wchar_t *createIconPath(ULONG id);
};

#endif // METALAUNCHER_H
