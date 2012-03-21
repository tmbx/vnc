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

#include "stdhdrs.h"
#include "vncviewer.h"
#include "MetaLauncher.h"
#include "IconUtil.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <olectl.h>
#include <sys/types.h>
#include <sys/stat.h>

const wchar_t *MetaLauncher::topMenuFolder = L"Remote Programs";
const wchar_t *MetaLauncher::topDataFolder = L"MetaVNC";
const wchar_t *MetaLauncher::menuListName  = L"menulist.dat";

// static method
#define VWR_WND_CLASS_NAME _T("VNCviewer") //XXX
void MetaLauncher::launch(HWND hwnd, ULONG id)
{
    TCHAR className[20];
    if (GetClassName(hwnd, className, 20) > 0 &&
        _tcscmp(className, VWR_WND_CLASS_NAME) == 0) {
        // post request to the viewer
        PostMessage(hwnd, RFB_METALAUNCH, id, 0);
    }
}

// static method
// clean up menus and icons of unterminated sessions
void MetaLauncher::cleanAllMenus()
{
    TCHAR appDataDir[MAX_PATH];

    // check if any vncviewer is running.
    if (FindWindow(VWR_WND_CLASS_NAME, NULL) != NULL) {
        MessageBox(NULL, _T("Close all vncviewers and try again!"), NULL, MB_OK | MB_ICONERROR);
        return;
    }

    // move to MetaVNC application data folder
    if (SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appDataDir) != S_OK) {
        MessageBox(NULL, _T("Failed to find APPDATA folder."), NULL, MB_OK | MB_ICONERROR);
        return;
    }
    if (!(SetCurrentDirectory(appDataDir) &&
          SetCurrentDirectoryW(topDataFolder))) {
        MessageBox(NULL, _T("Could not find MetaVNC data folder."), NULL, MB_OK | MB_ICONERROR);
        return;
    }

    // clean up menus and icons
    WIN32_FIND_DATA FindFileData;
    HANDLE hFind;
    hFind = FindFirstFile(_T("*.*"), &FindFileData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                _tcscmp(FindFileData.cFileName, _T("."))  != 0 &&
                _tcscmp(FindFileData.cFileName, _T("..")) != 0 &&
                SetCurrentDirectory(FindFileData.cFileName)) {
                cleanMenus();
                cleanIcons();
                SetCurrentDirectory("..");
                RemoveDirectory(FindFileData.cFileName);
            }
        } while (FindNextFile(hFind, &FindFileData));
        FindClose(hFind);
    }

    MessageBox(NULL, _T("Clean up finished!"), _T("MetaVNC"), MB_OK | MB_ICONINFORMATION);
}

// static method
// clean up menus in the current directory
void MetaLauncher::cleanMenus()
{
    FILE *fp = _wfopen(menuListName, L"rb");
    if (fp) {
        wchar_t menuPath[MAX_PATH];
        while (fgetws(menuPath, MAX_PATH, fp) != NULL) {
            menuPath[wcslen(menuPath) - 1] = L'\0'; // remove '\n'
            removeMenu(menuPath);
        }
        fclose(fp);
    }
}
// static method
// clean up icons in the current directory
void MetaLauncher::cleanIcons()
{
    WIN32_FIND_DATA FindFileData;
    HANDLE hFind;
    hFind = FindFirstFile(_T("*.ico"), &FindFileData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE)
                _tremove(FindFileData.cFileName);
        } while (FindNextFile(hFind, &FindFileData));
        FindClose(hFind);
    }
}

MetaLauncher::MetaLauncher(ClientConnection *pCC)
{
    wchar_t path[MAX_PATH];
	m_clientconn = pCC;
    m_fpMenuList = NULL;
    m_baseMenuFolder = NULL;
    m_baseDataFolder = NULL;

    // setup desktopFolderName
	size_t len = wcslen(m_clientconn->m_desktopNameW) + 1;
    m_desktopFolderName = new wchar_t[len];
    wcscpy(m_desktopFolderName, m_clientconn->m_desktopNameW);

    wchar_t *p;
    while (p = wcsrchr(m_desktopFolderName, L':'))
        *p = L'-';

    // setup programName
    m_programName = new wchar_t[wcslen(GetCommandLineW()) + 1];
    wcscpy(m_programName, GetCommandLineW());
    wchar_t *s, *e;
    s = wcschr(m_programName, L'"');
    e = wcsrchr(m_programName, L'"');
    if (s && e) {
        // remove ""
        *e = L'\0';
        wcscpy(m_programName, s + 1);
    }

    // setup baseMenuFolder
    setBaseMenuFolder();

    // setup baseDataFolder
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, path) == S_OK) {
        m_baseDataFolder = new wchar_t[wcslen(path) + wcslen(topDataFolder) + wcslen(m_desktopFolderName) + 3];
        wcscpy(m_baseDataFolder, path);
        wcscat(m_baseDataFolder, L"\\");
        wcscat(m_baseDataFolder, topDataFolder);
        if (CreateDirectoryW(m_baseDataFolder, NULL) == 0) {
            vnclog.Print(0, _T("Failed to create data directory. (error %d)\n"), GetLastError());
        }
        wcscat(m_baseDataFolder, L"\\");
        wcscat(m_baseDataFolder, m_desktopFolderName);
        if (CreateDirectoryW(m_baseDataFolder, NULL) == 0) {
            vnclog.Print(0, _T("Failed to create data directory. (error %d)\n"), GetLastError());
        }
    }

    // save current directory
    if (GetCurrentDirectoryW(MAX_PATH, path) == 0)
        path[0] = L'\0';

    // open menulist file
    if (SetCurrentDirectoryW(m_baseDataFolder)) {
        // clean up menus of previous session if exists
        cleanMenus();
        m_fpMenuList = _wfopen(menuListName, L"wb");
    }

    // restore current directory
    if (wcslen(path) > 0)
        SetCurrentDirectoryW(path);
}

MetaLauncher::~MetaLauncher()
{
    clear();
    removeMenuDirs(m_baseMenuFolder);

    // close and delete menulist file
    if (m_fpMenuList)
        fclose(m_fpMenuList);
    if (SetCurrentDirectoryW(m_baseDataFolder)) {
        _wremove(menuListName);
        SetCurrentDirectoryW(L"..");
    }
    RemoveDirectoryW(m_baseDataFolder);

    delete[] m_desktopFolderName;
    if (m_baseMenuFolder)
        delete[] m_baseMenuFolder;
    if (m_baseDataFolder)
        delete[] m_baseDataFolder;
}

// setup baseMenuFolder
void MetaLauncher::setBaseMenuFolder()
{
    wchar_t path[MAX_PATH];

	if (m_clientconn->m_opts.m_menuLoc == MENULOC_MIXLOCAL) {
        if (SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, SHGFP_TYPE_CURRENT, path) != S_OK) {
            vnclog.Print(0, _T("Failed to get menu directory.\n"));
            return;
        }
        m_baseMenuFolder = new wchar_t[wcslen(path) + 1];
        wcscpy(m_baseMenuFolder, path);
    } else {
        if (SHGetFolderPathW(NULL, CSIDL_STARTMENU, NULL, SHGFP_TYPE_CURRENT, path) != S_OK) {
            vnclog.Print(0, _T("Failed to get menu directory.\n"));
            return;
        }
        m_baseMenuFolder = new wchar_t[wcslen(path) + wcslen(topMenuFolder) + wcslen(m_desktopFolderName) + 3];
        wcscpy(m_baseMenuFolder, path);
        wcscat(m_baseMenuFolder, L"\\");
        wcscat(m_baseMenuFolder, topMenuFolder);
        if (CreateDirectoryW(m_baseMenuFolder, NULL) == 0) {
            vnclog.Print(0, _T("Failed to create menu directory. (error %d)\n"), GetLastError());
            return;
        }
        if (m_clientconn->m_opts.m_menuLoc == MENULOC_SEPARATE) {
            wcscat(m_baseMenuFolder, L"\\");
            wcscat(m_baseMenuFolder, m_desktopFolderName);
            if (CreateDirectoryW(m_baseMenuFolder, NULL) == 0) {
                vnclog.Print(0, _T("Failed to create menu directory. (error %d)\n"), GetLastError());
                return;
            }
        }
    }
}

// remove menupath and its parent directories
// argument menupath will be destroyed
void MetaLauncher::removeMenu(wchar_t *menuPath)
{
    if (_wremove(menuPath) != 0) {
        vnclog.Print(0, _T("failed to remove link. (error %d)\n"), GetLastError());
        return;
    }

    // remove link filename from menupath
    wchar_t *p;
    p = wcsrchr(menuPath, L'\\');
    if (p == NULL)
        return;
    *p = L'\0';
    removeMenuDirs(menuPath);
}

// remove menu directories recursively
// argument menupath will be destroyed
void MetaLauncher::removeMenuDirs(wchar_t *menuPath)
{
    wchar_t *p;
    while (RemoveDirectoryW(menuPath)) {
        // remove the outermost directory
        p = wcsrchr(menuPath, L'\\');
        if (p == NULL)
            break;
        *p = L'\0';
    }
}

// create sub directories
void MetaLauncher::createMenuDirs(wchar_t *menuPath)
{
    wchar_t *p;
    p = menuPath;
    while ((p = wcschr(p + 1, L'\\')) != NULL) {
        *p = L'\0';
        CreateDirectoryW(menuPath, NULL);
        *p = L'\\';
    }
}

void MetaLauncher::clear()
{
vnclog.Print(1, _T("%s:\n"), __FUNCTION__);
    // clean up menulist
    if (m_fpMenuList)
        fclose(m_fpMenuList);
    if (SetCurrentDirectoryW(m_baseDataFolder)) {
        cleanMenus();
        cleanIcons();
        m_fpMenuList = _wfopen(menuListName, L"wb");
    }
    removeMenuDirs(m_baseMenuFolder);

    // reset baseMenuFolder
    setBaseMenuFolder();
}

// caller have to delete[] the returned memory
wchar_t *MetaLauncher::createIconPath(ULONG id)
{
    size_t iconPathLen = wcslen(m_baseDataFolder) + 14;
    wchar_t *iconPath = new wchar_t[iconPathLen];
    _snwprintf(iconPath, iconPathLen, L"%s\\%08X.ico", m_baseDataFolder, id);
    return iconPath;
}

BOOL MetaLauncher::createMenuItem(ULONG id, const char *path, size_t pathlen)
{
vnclog.Print(1, _T("%s: id=0x%X\n"), __FUNCTION__, id);
    wchar_t *p;
    // setup link path : m_baseMenuFolder + path (+ @desktopname) + ".lnk"
    size_t linkPathLen = wcslen(m_baseMenuFolder) + pathlen + wcslen(m_clientconn->m_desktopNameW) + 6;
    wchar_t *linkPath = new wchar_t[linkPathLen];
    wcscpy(linkPath, m_baseMenuFolder);

    // append path with converting UTF-8 to UCS-2LE
    int pos = wcslen(linkPath);
    int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                   path, (int)pathlen,
                                   &linkPath[pos], linkPathLen - pos);
    if (wlen <= 0) {
        vnclog.Print(0, _T("MultiByteToWideChar failed. (error %d)\n"), GetLastError());
        delete[] linkPath;
        return FALSE;
    }
    linkPath[pos + wlen] = L'\0';

    // convert '/' to '\\'
    while ((p = wcschr(linkPath, L'/')) != NULL)
        *p = L'\\';

    // setup description text
    wchar_t *appName = wcsrchr(linkPath, '\\');
    if (appName != NULL)
        appName++;      // remove first '\\'
    else
        appName = linkPath;
    wchar_t *description = new wchar_t[wcslen(appName) + 1 + wcslen(m_clientconn->m_desktopNameW) + 1];
    wcscpy(description, appName);
    wcscat(description, L"@");
    wcscat(description, m_clientconn->m_desktopNameW);

    // add remote desktop name to linkpath if menu directory is not separated
    // XXX: maybe another option is needed
    if (m_clientconn->m_opts.m_menuLoc != MENULOC_SEPARATE) {
        wcscat(linkPath, L"@");
        wcscat(linkPath, m_clientconn->m_desktopNameW);
    }
    wcscat(linkPath, L".lnk");  // add extension

    // check if linkPath already exists
    struct _stat st;
    if (_wstat(linkPath, &st) == 0) {
        vnclog.Print(0, _T("Failed to create identical link path (id=0x%X).\n"), id);
        delete[] description;
        delete[] linkPath;
        return FALSE;
    }

    // create sub directories
    createMenuDirs(linkPath);

    // remember link path
    if (m_fpMenuList) {
        fputws(linkPath, m_fpMenuList);
        fputws(L"\n", m_fpMenuList);
    }

    // setup target command arguments
    wchar_t targetArgs[30];
    _snwprintf(targetArgs, 30, L"-launch %08X:%08X", m_clientconn->m_hwnd1, id);
    // setup icon path
    wchar_t *iconPath = createIconPath(id);

    // create shortcut
    HRESULT      hr;
    IShellLinkW  *pShellLink;
    IPersistFile *pPersistFile;
    
    CoInitialize(NULL);
    hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                          IID_IShellLinkW, (void**)&pShellLink);
    if (hr == S_OK) hr = pShellLink->SetPath(m_programName);
    if (hr == S_OK) hr = pShellLink->SetArguments(targetArgs);
    if (hr == S_OK) hr = pShellLink->SetIconLocation(iconPath, 0);
    if (hr == S_OK) hr = pShellLink->SetDescription(description);
    if (hr == S_OK) hr = pShellLink->QueryInterface(IID_IPersistFile, (void **)&pPersistFile);
    if (hr == S_OK) hr = pPersistFile->Save(linkPath, TRUE);

    // clean up
    pPersistFile->Release();
    pShellLink->Release();
    CoUninitialize();

    delete[] iconPath;
    delete[] description;
    delete[] linkPath;
    return (hr == S_OK);
}

BOOL MetaLauncher::createIconFile(ULONG id, rfbServerDataIcon *iconData, ULONG iconDataLength)
{
    wchar_t *iconPath = createIconPath(id);
    BOOL result = IconUtil::saveIconFile(iconPath, iconData, iconDataLength);
    delete[] iconPath;
    return result;
}
