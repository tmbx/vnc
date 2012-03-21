//  Copyright (C) 1999 AT&T Laboratories Cambridge. All Rights Reserved.
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
//
// TightVNC distribution homepage on the Web: http://www.tightvnc.com/
//
// If the source code for the VNC system is not available from the place 
// whence you received this file, check http://www.uk.research.att.com/vnc or contact
// the authors on vnc@uk.research.att.com for information on obtaining it.


// WinVNC.cpp

// 24/11/97		WEZ

// WinMain and main WndProc for the new version of WinVNC

////////////////////////////
// System headers
#include "stdhdrs.h"

////////////////////////////
// Custom headers
#include "VSocket.h"
#include "WinVNC.h"

#include "vncServer.h"
#include "vncMenu.h"
#include "vncInstHandler.h"
#include "vncService.h"
#include "vncLauncher.h"

extern "C" {
#include "ParseHost.h"
}

// Application instance and name
HINSTANCE	hAppInstance;
const char	*szAppName = "WinVNC";

DWORD		mainthreadId;

/* parse szCmdLine and get the next argument.
 * Start parsing at *pos the function is not
 * reentrant, but it's only used in the WinMain
 * which is not multithreaded.
 */
static char *NextArg(PSTR szCmdLine, size_t *pos)
{
	static char *arg = NULL;
	int start = *pos, end;
	while (szCmdLine[start] && szCmdLine[start] <= ' ') start++;
	if (szCmdLine[start] == '"') {
		start++;
		char *ptr = strchr(&szCmdLine[start], '"');
		if (ptr == NULL) {
			end = strlen(szCmdLine);
			*pos = end;
		} else {
			end = ptr - szCmdLine;
			*pos = end + 1;
		}
	} else {
		end = start;
		while (szCmdLine[end] > ' ') end++;
		*pos = end;
	}
	if (end - start > 0) {
		delete[] arg;
		arg = new char[end - start + 1];
		if (arg != NULL) {
			strncpy(arg, &szCmdLine[start], end - start);
			arg[end - start] = 0;
		}
	}
	return arg;
}

// WinMain parses the command line and either calls the main App
// routine or, under NT, the main service routine.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine, int iCmdShow)
{
#ifdef _DEBUG
	{
		// Get current flag
		int tmpFlag = _CrtSetDbgFlag( _CRTDBG_REPORT_FLAG );

		// Turn on leak-checking bit
		tmpFlag |= _CRTDBG_LEAK_CHECK_DF;

		// Set flag to the new value
		_CrtSetDbgFlag( tmpFlag );
	}
#endif

	// Save the application instance and main thread id
	hAppInstance = hInstance;
	mainthreadId = GetCurrentThreadId();

	// Initialise the VSocket system
	VSocketSystem socksys;
	if (!socksys.Initialised())
	{
		MessageBox(NULL, "Failed to initialise the socket system", szAppName, MB_OK);
		return 0;
	}
	vnclog.Print(LL_STATE, VNCLOG("sockets initialised\n"));

	// Make the command-line lowercase and parse it
	size_t i;
	for (i = 0; i < strlen(szCmdLine); i++)
	{
		szCmdLine[i] = tolower(szCmdLine[i]);
	}

	BOOL argfound = FALSE;
	char *connectName = NULL;
	int connectPort;
	bool cancelConnect = false;

	for (i = 0; i < strlen(szCmdLine); i++)
	{
		if (szCmdLine[i] <= ' ')
			continue;
		argfound = TRUE;

		// Determine the length of current argument in the command line
		size_t arglen = strcspn(&szCmdLine[i], " \t\r\n\v\f");

		// Now check for command-line arguments
		if (strncmp(&szCmdLine[i], winvncRunServiceHelper, arglen) == 0 &&
			arglen == strlen(winvncRunServiceHelper))
		{
            BOOL bLauncher;

			// NB : This flag MUST be parsed BEFORE "-service", otherwise it will match
			// the wrong option!  (This code should really be replaced with a simple
			// parser machine and parse-table...)

            // MetaVNC: kill the existing launcher window and create a new one
            vncLauncherClose();
            bLauncher = vncLauncherCreate();

			// Run the WinVNC Service Helper app
			vncService::PostUserHelperMessage();

            if (bLauncher) {
                // MetaVNC uses this context as a launcher
                return vncLauncherMain();
            }
			return 0;
		}
		if (strncmp(&szCmdLine[i], winvncRunService, arglen) == 0 &&
			arglen == strlen(winvncRunService))
		{
			// Run WinVNC as a service
			return vncService::WinVNCServiceMain();
		}
		if (strncmp(&szCmdLine[i], winvncRunAsUserApp, arglen) == 0 &&
			arglen == strlen(winvncRunAsUserApp))
		{
			// WinVNC is being run as a user-level program
			return WinVNCAppMain();
		}
		if (strncmp(&szCmdLine[i], winvncInstallService, arglen) == 0 &&
			arglen == strlen(winvncInstallService))
		{
			// Install WinVNC as a service
			vncService::InstallService();
			i += arglen;
			continue;
		}
		if (strncmp(&szCmdLine[i], winvncReinstallService, arglen) == 0 &&
			arglen == strlen(winvncReinstallService))
		{
			// Silently remove WinVNC, then re-install it
			vncService::ReinstallService();
			i += arglen;
			continue;
		}
		if (strncmp(&szCmdLine[i], winvncRemoveService, arglen) == 0 &&
			arglen == strlen(winvncRemoveService))
		{
			// Remove the WinVNC service
			vncService::RemoveService();
			i += arglen;
			continue;
		}
		if (strncmp(&szCmdLine[i], winvncReload, arglen) == 0 &&
			arglen == strlen(winvncReload))
		{
			// Reload Properties from the registry
			vncService::PostReloadMessage();
			i += arglen;
			continue;
		}
		if (strncmp(&szCmdLine[i], winvncShowProperties, arglen) == 0 &&
			arglen == strlen(winvncShowProperties))
		{
			// Show the Properties dialog of an existing instance of WinVNC
			vncService::ShowProperties();
			i += arglen;
			continue;
		}
		if (strncmp(&szCmdLine[i], winvncShowDefaultProperties, arglen) == 0 &&
			arglen == strlen(winvncShowDefaultProperties))
		{
			// Show the Properties dialog of an existing instance of WinVNC
			vncService::ShowDefaultProperties();
			i += arglen;
			continue;
		}
		if (strncmp(&szCmdLine[i], winvncShowAbout, arglen) == 0 &&
			arglen == strlen(winvncShowAbout))
		{
			// Show the About dialog of an existing instance of WinVNC
			vncService::ShowAboutBox();
			i += arglen;
			continue;
		}
		if (strncmp(&szCmdLine[i], winvncKillAllClients, arglen) == 0 &&
			arglen == strlen(winvncKillAllClients))
		{
			// NB : This flag MUST be parsed BEFORE "-kill", otherwise it will match
			// the wrong option!

			// Kill all connected clients
			vncService::KillAllClients();
			i += arglen;
			continue;
		}
		if (strncmp(&szCmdLine[i], winvncKillRunningCopy, arglen) == 0 &&
			arglen == strlen(winvncKillRunningCopy))
		{
			// Kill any already running copy of WinVNC
			vncService::KillRunningCopy();
			i += arglen;
			continue;
		}
		if (strncmp(&szCmdLine[i], winvncShareAll, arglen) == 0 &&
			arglen == strlen(winvncShareAll))
		{
			// Show full desktop to VNC clients
			vncService::PostShareAll();
			i += arglen;
			continue;
		}
		if (strncmp(&szCmdLine[i], winvncSharePrimary, arglen) == 0 &&
			arglen == strlen(winvncSharePrimary))
		{
			// Show only the primary display to VNC clients
			vncService::PostSharePrimary();
			i += arglen;
			continue;
		}
		if (strncmp(&szCmdLine[i], winvncShareArea, arglen) == 0 &&
			arglen == strlen(winvncShareArea))
		{
			// Show a specified rectangular area to VNC clients
			i += arglen;

			// First, we have to parse the command line to get an argument
			int start, end;
			start = i;
			while (szCmdLine[start] && szCmdLine[start] <= ' ') start++;
			end = start;
			while (szCmdLine[end] > ' ') end++;
			i = end;
			if (end == start)
				continue;

			// Parse the argument -- it should look like 640x480+320+240
			unsigned short x, y, w, h;
			int n = sscanf(&szCmdLine[start], "%hux%hu+%hu+%hu", &w, &h, &x, &y);
			if (n == 4 && w > 0 && h > 0)
				vncService::PostShareArea(x, y, w, h);

			continue;
		}
		if (strncmp(&szCmdLine[i], winvncShareWindow, arglen) == 0 &&
			arglen == strlen(winvncShareWindow))
		{
			// Find a window to share, by its title
			i += arglen;

			cancelConnect = true;	// Ignore the -connect option unless
									// there will be valid window to share
			char *title = NextArg(szCmdLine, &i);
			HWND hwndFound = vncService::FindWindowByTitle(title);
			if (hwndFound != NULL) {
				cancelConnect = false;
				vncService::PostShareWindow(hwndFound);
			}
			continue;
		}

		if (strncmp(&szCmdLine[i], winvncShareHWND, arglen) == 0 &&
			arglen == strlen(winvncShareHWND))
		{
			
			// Find a window to share, by its hwnd.
			i += arglen;

			cancelConnect = true;	// Ignore the -connect option unless
									// there will be valid window to share

			int start = i, end;
			while (szCmdLine[start] && szCmdLine[start] <= ' ') start++;
			end = start;
			while (szCmdLine[end] > ' ') end++;
			i = end;
			if (end - start > 0) {
				char *hwnd_text = new char[end - start + 1];
				if (hwnd_text != NULL) {
					strncpy(hwnd_text, &szCmdLine[start], end - start);
					hwnd_text[end - start] = 0;
					HWND hwndFound = (HWND)atol(hwnd_text);
					if (IsWindow(hwndFound))
						cancelConnect = false;
					vncService::PostShareWindow(hwndFound);
					delete [] hwnd_text;
				}
			}
			continue;
		}

		if (strncmp(&szCmdLine[i], winvncShareApp, arglen) == 0 &&
			arglen == strlen(winvncShareApp))
		{
			
			// Find a application to share, by its process id.
			i += arglen;

			int start = i, end;
			while (szCmdLine[start] && szCmdLine[start] <= ' ') start++;
			end = start;
			while (szCmdLine[end] > ' ') end++;
			i = end;
			if (end - start > 0) {
				char *hwnd_text = new char[end - start + 1];
				if (hwnd_text != NULL) {
					strncpy(hwnd_text, &szCmdLine[start], end - start);
					hwnd_text[end - start] = 0;
					DWORD pid = (DWORD)atol(hwnd_text);
					vncService::PostShareApp(pid);
					delete [] hwnd_text;
				}
			}
			continue;
		}

		if (strncmp(&szCmdLine[i], winvncChangePassword, arglen) == 0 &&
			arglen == strlen(winvncChangePassword))
		{			
			// Change the password.
			i += arglen;
			char *pwd1 = NextArg(szCmdLine, &i);
			vncService::PostPassword(PWD_INTERACTIVE, pwd1);

			continue;
		}

		if (strncmp(&szCmdLine[i], winvncAddNewClient, arglen) == 0 &&
			arglen == strlen(winvncAddNewClient) && connectName == NULL)
		{
			// Add a new client to an existing copy of winvnc
			i += arglen;

			// First, we have to parse the command line to get the hostname to use
			int start, end;
			start=i;
			while (szCmdLine[start] && szCmdLine[start] <= ' ') start++;
			end = start;
			while (szCmdLine[end] > ' ') end++;

			connectName = new char[end-start+1];

			// Was there a hostname (and optionally a port number) given?
			if (end-start > 0) {
				if (connectName != NULL) {
					strncpy(connectName, &(szCmdLine[start]), end-start);
					connectName[end-start] = 0;
					connectPort = ParseHostPort(connectName, INCOMING_PORT_OFFSET);
				}
			} else {
				if (connectName != NULL)
					connectName[0] = '\0';
			}
			i = end;
			continue;
		}

		// Either the user gave the -help option or there is something odd on the cmd-line!

		// Show the usage dialog
		MessageBox(NULL, winvncUsageText, "WinVNC Usage", MB_OK | MB_ICONINFORMATION);
		break;
	}

	// If no arguments were given then just run
	if (!argfound)
		return WinVNCAppMain();

	// Process the -connect option at the end
	if (connectName != NULL && !cancelConnect) {
		if (connectName[0] != '\0') {
			VCard32 address = VSocket::Resolve(connectName);
			if (address != 0) {
				// Post the IP address to the server
				vncService::PostAddNewClient(address, connectPort);
			}
		} else {
			// Tell the server to show the Add New Client dialog
			vncService::PostAddNewClient(0, 0);
		}
	}
	if (connectName != NULL)
		delete[] connectName;

	return 0;
}

// This is the main routine for WinVNC when running as an application
// (under Windows 95 or Windows NT)
// Under NT, WinVNC can also run as a service.  The WinVNCServerMain routine,
// defined in the vncService header, is used instead when running as a service.

int WinVNCAppMain()
{
	// Set this process to be the last application to be shut down.
	SetProcessShutdownParameters(0x100, 0);
	
	// Check for previous instances of WinVNC!
	vncInstHandler instancehan;
	if (!instancehan.Init())
	{
		// We don't allow multiple instances!
		MessageBox(NULL, "Another instance of WinVNC is already running", szAppName, MB_OK);
		return 0;
	}

	// CREATE SERVER
	vncServer server;

	// Set the name and port number
	server.SetName(szAppName);
	vnclog.Print(LL_STATE, VNCLOG("server created ok\n"));

	// Create tray icon & menu if we're running as an app
	vncMenu *menu = new vncMenu(&server);
	if (menu == NULL)
	{
		vnclog.Print(LL_INTERR, VNCLOG("failed to create tray menu\n"));
		PostQuitMessage(0);
	}

	// Now enter the message handling loop until told to quit!
	MSG msg;
	while (GetMessage(&msg, NULL, 0,0) ) {
		vnclog.Print(LL_INTINFO, VNCLOG("message %d received\n"), msg.message);
		TranslateMessage(&msg);	// convert key ups and downs to chars
		DispatchMessage(&msg);
	}

	vnclog.Print(LL_STATE, VNCLOG("shutting down server\n"));

	if (menu != NULL)
		delete menu;

	return msg.wParam;
}
