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



#ifndef METATASKBAR_H

#define METATASKBAR_H



#include "stdhdrs.h"

#include <map>

#include "omnithread.h"



class ClientConnection;

class VNCviewerApp;



class MetaTaskbar

{

    enum TaskbarAction {

        TASKBAR_ADD,

        TASKBAR_DEL,

        TASKBAR_ACTIVATE

    };

    struct TaskSwState {

        HWND hwnd;

        ULONG state;

    };

    typedef std::map<ULONG, TaskSwState> IdStateMap;

 private:

    static const char *TaskSwClassNameTemplate;

    static const int TaskSwClassNameLen;

	ClientConnection *m_clientconn;

	VNCviewerApp *m_pApp; 

    IdStateMap m_taskSwitches;

    omni_mutex m_taskSwitchesLock;

 public:

	MetaTaskbar(ClientConnection *pCC, VNCviewerApp *pApp);

	virtual ~MetaTaskbar();

    void update(ULONG id, ULONG state);

    void updateText(ULONG id, const char *name, size_t nameLength);

    void setIcon(ULONG id, rfbServerDataIcon *iconData, ULONG iconDataLength);

    HWND createTaskSw(ULONG id, ULONG state);

    void clear();

 private:

    void destroyTaskSw(HWND hwnd);

    ULONG getTaskSwState(ULONG id);

    HWND  getTaskSwWnd(ULONG id);

    ULONG getId(HWND hwnd);

    BOOL taskbarControl(HWND hwnd, TaskbarAction action);

	static LRESULT CALLBACK WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);

};



#endif // METATASKBAR_H

