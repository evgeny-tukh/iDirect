#include <stdlib.h>
#include <Windows.h>
#include <Shlwapi.h>
#include "defs.h"

static const char *MODEM = "Modem";
static const char *AUTHORIZATION = "Authorization";

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

int getData (SOCKET connection, char *buffer, size_t size) {
    int bytesReceived;

    memset (buffer, 0, size);

    do {
        bytesReceived = recv (connection, buffer, size, 0);

        Sleep (0);
    } while (bytesReceived <= 0);

    return bytesReceived;
}

int checkData (SOCKET connection, char *buffer, size_t size) {
    memset (buffer, 0, size);

    return recv (connection, buffer, size, 0);
}

int getAwaitFollowingString (SOCKET connection, char *buffer, size_t size, const char *waitFor) {
    int bytesReceived;

    do {
        bytesReceived = getData (connection, buffer, size);

        Sleep (100);
    } while (strstr (buffer, waitFor) == 0);

    return bytesReceived;
}

void sendCommand (SOCKET connection, const char *command) {
    char buffer [200];

    strcpy (buffer, command);
    strcat (buffer, "\r\n");

    send (connection, buffer, strlen (buffer), 0);
}

void waitForCommandPrompt (SOCKET connection, char *output = 0) {
    char buffer [2000];

    getAwaitFollowingString (connection, buffer, sizeof (buffer), ">");

    if (output)
        strcpy (output, buffer);
}

void selectBeam (SOCKET connection, workerData *data, uint16_t beamID) {
    char command [100];

    addToLog (data->wnd, "Selecting the beam...\n");
    sprintf (command, "beamselector switch %d -f\n", beamID);

    sendCommand (connection, command);
    waitForCommandPrompt (connection);
    addToLog (data->wnd, "Beam selected.\n");
}

void processConnection (workerData *data, SOCKET connection, char *userName, char *password) {
    char buffer [2000];
    int bytesReceived;

    addToLog (data->wnd, "Waiting for authentication request...\n");
    getAwaitFollowingString (connection, buffer, sizeof (buffer), "Username");
    addToLog (data->wnd, "Sending user name...\n");
    sendCommand (connection, userName);
    getAwaitFollowingString (connection, buffer, sizeof (buffer), "Password");
    addToLog (data->wnd, "Sending password...\n");
    sendCommand (connection, password);
    Sleep (500);
    addToLog (data->wnd, "Pulling data...\n");
    waitForCommandPrompt (connection);
    addToLog (data->wnd, "Requesting for beam list...\n");
    sendCommand (connection, "beamselector list");
    Sleep (500);
    waitForCommandPrompt (connection, buffer);
    extractBeams (buffer, data->beams);
    addToLog (data->wnd, "Completed.\n");

    for (auto & beam: data->beams.list) {
        char *beamName = _strdup (beam.name.c_str ());

        PostMessage (data->wnd, UM_ADD_BEAM, beam.id, (LPARAM) beamName);
    }

    PostMessage (data->wnd, UM_SELECT_BEAM, 0, data->beams.selected);

    while (true) {
        while (data->messages.size () > 0) {
            auto & msg = data->messages.front ();

            switch (msg.type) {
                case msgType::SELECT_BEAM: {
                    selectBeam (connection, data, msg.data); break;
                }
            }

            data->messages.pop ();
        }

        Sleep (500);
    }
}

unsigned long CALLBACK workerProc (void *param) {
    char path [MAX_PATH];
    WSADATA sockData;
    workerData *data = (workerData *) param;
    SOCKET connection;
    uint16_t port = 23;
    char addr [50] = {"192.168.1.1"};
    char userName [50] = {"admin"};
    char password [50] = {"P@55w0rd!"};

    GetModuleFileName (0, path, sizeof (path));
    PathRemoveFileSpec (path);
    PathAppend (path, "iDirect.ini");
    
    port = GetPrivateProfileInt (MODEM, "Port", 23, path);

    GetPrivateProfileString (MODEM, "Address", "192.168.1.1", addr, sizeof (addr), path);
    GetPrivateProfileString (AUTHORIZATION, "Username", "admin", userName, sizeof (userName), path);
    GetPrivateProfileString (AUTHORIZATION, "Password", "P@55w0rd!", password, sizeof (password), path);

    WSAStartup (MAKEWORD (2, 2), & sockData);

    connection = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);

    SOCKADDR_IN router;

    router.sin_addr.S_un.S_addr = inet_addr (addr);
    router.sin_family = AF_INET;
    router.sin_port = htons (port);

    addToLog (data->wnd, "Connecting to the modem...\n");

    if (connect (connection, (sockaddr *) & router, sizeof (router)) == S_OK) {
        addToLog (data->wnd, "Connected.\n");
        processConnection (data, connection, userName, password);
    } else {
        addToLog (data->wnd, "Failed.\n");
    }

    closesocket (connection);
    
    return 0;
}

HANDLE startWorker (workerData *data) {
    return CreateThread (0, 0, workerProc, data, 0, 0);
}