#include <stdlib.h>
#include <Windows.h>
#include <Shlwapi.h>
#include "defs.h"

static const char *MODEM = "Modem";
static const char *AUTHORIZATION = "Authorization";

int getAwaitFollowingString (SOCKET& connection, workerData *data, char *buffer, size_t size, const char *waitFor, const char *reconnectAfter = 0);
void sendCommand (SOCKET& connection, workerData *data, const char *command);
void waitForCommandPrompt (SOCKET& connection, workerData *data, char *output = 0);
void loadBeams (workerData *data, SOCKET& connection);

void addToLog (HWND wnd, char *text) {
    PostMessage (wnd, UM_ADD_TO_LOG, 0, (LPARAM) text);
}

void splitLines (char *buffer, strings& lines) {
    std::string line;

    lines.clear ();

    for (auto i = 0; buffer [i]; ++ i) {
        switch (buffer [i]) {
            case '\r':
                if (!line.empty ())
                    lines.push_back (line);

                line.clear (); break;

            case '\n':
                break;

            default:
                line.push_back (buffer [i]);
        }
    }

    if (!line.empty ())
        lines.push_back (line);
}

void extractBeams (char *buffer, beamList& beams) {
    strings lines;

    splitLines (buffer, lines);

    beams.list.clear ();

    for (auto & line: lines) {
        if (line.find ("is currently selected") != std::string::npos) {
            beams.selected = atoi (line.c_str ());
        } else {
            auto pos = line.find (" = ");

            if (pos != std::string::npos) {
                beams.list.emplace_back ((uint16_t) atoi (line.c_str ()), line.substr (pos + 3));
            }
        }
    }
}

SOCKET connectToModem (workerData *data) {
    SOCKET connection = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);

    SOCKADDR_IN router;

    data->lastError = 0;

    router.sin_addr.S_un.S_addr = inet_addr (data->addr);
    router.sin_family = AF_INET;
    router.sin_port = htons (data->port);

    addToLog (data->wnd, "Connecting to the modem...\n");

    if (connect (connection, (sockaddr *) & router, sizeof (router)) == S_OK) {
        addToLog (data->wnd, "Connected.\n");            
    } else {
        closesocket (connection);

        connection = INVALID_SOCKET;

        addToLog (data->wnd, "Failed.\n");
    }

    return connection;
};

void authenticate (workerData *data, SOCKET& connection) {
    char buffer [2000];

    addToLog (data->wnd, "Waiting for authentication request...\n");
    getAwaitFollowingString (connection, data, buffer, sizeof (buffer), "Username");
    addToLog (data->wnd, "Sending user name...\n");
    sendCommand (connection, data, data->userName);
    getAwaitFollowingString (connection, data, buffer, sizeof (buffer), "Password");
    addToLog (data->wnd, "Sending password...\n");
    sendCommand (connection, data, data->password);
    Sleep (500);
    addToLog (data->wnd, "Pulling data...\n");
    waitForCommandPrompt (connection, data);
    loadBeams (data, connection);
}

void reconnect (SOCKET& connection, workerData *data, int wait) {
    closesocket (connection);

    addToLog (data->wnd, "Connection lost.\n");
    addToLog (data->wnd, "Cleaning up...\n");
    
    PostMessage (data->wnd, UM_REMOVE_ALL_BEAMS, 0, 0);

    data->beams.list.clear ();

    data->beams.selected = 0;

    if (wait > 1) {
        for (auto i = 30; i > 0; -- i) {
            char msg [50];

            sprintf (msg, "Waiting for %d sec...\n", i);
            addToLog (data->wnd, msg);

            Sleep (1000);
        }
    } else {
        Sleep (1000);
    }

    addToLog (data->wnd, "Reconnecting...\n");

    connection = connectToModem (data);

    if (connection != INVALID_SOCKET) {
        WSASetLastError (0);

        data->lastError = 0;
    }

    authenticate (data, connection);
}

bool checkConnection (SOCKET& connection, workerData *data) {
    bool result = true;

    if (data->lastError) {
        switch (data->lastError) {
            case WSAENETDOWN:
            case WSAENOTCONN:
            case WSAENETRESET:
            case WSAENOTSOCK:
            case WSAESHUTDOWN:
            case WSAECONNABORTED:
            case WSAETIMEDOUT:
            case WSAECONNRESET: {
                reconnect (connection, data, 1);

                result = false; break;
            }
        }
    }

    return result;
}

int getData (SOCKET& connection, workerData *data, char *buffer, size_t size) {
    int bytesReceived;

    memset (buffer, 0, size);

    do {
        bytesReceived = recv (connection, buffer, size, 0);

        data->lastError = bytesReceived == SOCKET_ERROR ? (int) WSAGetLastError () : 0;

        WSASetLastError (0);

        Sleep (0);
    } while (!checkConnection (connection, data));

    return bytesReceived;
}

int checkData (SOCKET& connection, workerData *data, char *buffer, size_t size) {
    int result;

    memset (buffer, 0, size);

    do {
        result = recv (connection, buffer, size, 0);

        data->lastError = result == SOCKET_ERROR ? WSAGetLastError () : 0;

        WSASetLastError (0);
    } while (!checkConnection (connection, data));

    return result;
}

int getAwaitFollowingString (SOCKET& connection, workerData *data, char *buffer, size_t size, const char *waitFor, const char *reconnectAfter) {
    int bytesReceived;

    do {
        bytesReceived = getData (connection, data, buffer, size);

        if (reconnectAfter && strstr (buffer, reconnectAfter)) {
            addToLog (data->wnd, "The unit is going to restart.\n");
            reconnect (connection, data, 30);
            break;
        }
        Sleep (100);
    } while (strstr (buffer, waitFor) == 0);

    return bytesReceived;
}

void sendCommand (SOCKET& connection, workerData *data, const char *command) {
    char buffer [200];

    strcpy (buffer, command);
    strcat (buffer, "\r\n");

    do {
        int bytesSent = send (connection, buffer, strlen (buffer), 0);

        data->lastError = bytesSent == SOCKET_ERROR ? WSAGetLastError () : 0;

        WSASetLastError (0);
    } while (!checkConnection (connection, data));
}

void waitForCommandPrompt (SOCKET& connection, workerData *data, char *output) {
    char buffer [2000];

    getAwaitFollowingString (connection, data, buffer, sizeof (buffer), ">", "Scheduling Service Restart");

    if (output)
        strcpy (output, buffer);
}

void loadBeams (workerData *data, SOCKET& connection) {
    char buffer [2000];

    PostMessage (data->wnd, UM_REMOVE_ALL_BEAMS, 0, 0);

    addToLog (data->wnd, "Requesting for beam list...\n");
    sendCommand (connection, data, "beamselector list");
    Sleep (500);
    waitForCommandPrompt (connection, data, buffer);
    extractBeams (buffer, data->beams);
    addToLog (data->wnd, "Completed.\n");

    for (auto & beam: data->beams.list) {
        char *beamName = _strdup (beam.name.c_str ());
        WPARAM wParam = beam.id;

        if (beam.id == data->beams.selected)
            wParam |= 0x80000000;

        PostMessage (data->wnd, UM_ADD_BEAM, wParam, (LPARAM) beamName);
    }

    //PostMessage (data->wnd, UM_SELECT_BEAM, 0, data->beams.selected);
}

void selectBeam (SOCKET& connection, workerData *data, uint16_t beamID) {
    char command [100];

    addToLog (data->wnd, "Selecting the beam...\n");
    Sleep (500);
    sprintf (command, "beamselector switch %d -f\n", beamID);

    sendCommand (connection, data, command);
    waitForCommandPrompt (connection, data);
    addToLog (data->wnd, "Beam selected.\n");
    loadBeams (data, connection);
}

void processConnection (workerData *data, SOCKET& connection) {
    /*char buffer [2000];

    addToLog (data->wnd, "Waiting for authentication request...\n");
    getAwaitFollowingString (connection, data, buffer, sizeof (buffer), "Username");
    addToLog (data->wnd, "Sending user name...\n");
    sendCommand (connection, userName);
    getAwaitFollowingString (connection, data, buffer, sizeof (buffer), "Password");
    addToLog (data->wnd, "Sending password...\n");
    sendCommand (connection, password);
    Sleep (500);
    addToLog (data->wnd, "Pulling data...\n");
    waitForCommandPrompt (connection, data);
    loadBeams (data, connection);*/
    /*addToLog (data->wnd, "Requesting for beam list...\n");
    sendCommand (connection, "beamselector list");
    Sleep (500);
    waitForCommandPrompt (connection, buffer);
    extractBeams (buffer, data->beams);
    addToLog (data->wnd, "Completed.\n");

    for (auto & beam: data->beams.list) {
        char *beamName = _strdup (beam.name.c_str ());

        PostMessage (data->wnd, UM_ADD_BEAM, beam.id, (LPARAM) beamName);
    }

    PostMessage (data->wnd, UM_SELECT_BEAM, 0, data->beams.selected);*/

    while (IsWindow (data->wnd)) {
        while (data->messages.size () > 0) {
            auto & msg = data->messages.front ();

            switch (msg.type) {
                case msgType::SELECT_BEAM: {
                    selectBeam (connection, data, msg.data); break;
                }
            }

            data->messages.pop ();

            checkConnection (connection, data);
        }

        Sleep (500);
    }
}

unsigned long CALLBACK workerProc (void *param) {
    char path [MAX_PATH];
    WSADATA sockData;
    workerData *data = (workerData *) param;
    SOCKET connection;

    data->port = 23;

    strcpy (data->addr,"192.168.1.1");
    strcpy (data->userName, "admin");
    strcpy (data->password, "P@55w0rd!");

    GetModuleFileName (0, path, sizeof (path));
    PathRemoveFileSpec (path);
    PathAppend (path, "iDirect.ini");
    
    data->port = GetPrivateProfileInt (MODEM, "Port", 23, path);

    GetPrivateProfileString (MODEM, "Address", "192.168.1.1", data->addr, sizeof (data->addr), path);
    GetPrivateProfileString (AUTHORIZATION, "Username", "admin", data->userName, sizeof (data->userName), path);
    GetPrivateProfileString (AUTHORIZATION, "Password", "P@55w0rd!", data->password, sizeof (data->password), path);

    WSAStartup (MAKEWORD (2, 2), & sockData);

    connection = connectToModem (data);

    if (connection != INVALID_SOCKET) {
        authenticate (data, connection);
        processConnection (data, connection);
    }

    closesocket (connection);
    
    return 0;
}

HANDLE startWorker (workerData *data) {
    return CreateThread (0, 0, workerProc, data, 0, 0);
}