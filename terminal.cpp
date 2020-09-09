#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>

typedef std::vector<std::string> strings;

struct beam {
    uint16_t id;
    std::string name;

    beam (uint16_t _id, std::string _name): id (_id), name (_name.c_str ()) {}
};

struct beamList {
    uint16_t selected;
    std::vector<beam> list;

    beamList () : selected (0) {}
};

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

void processConnection (SOCKET connection, char *userName, char *password) {
    char buffer [2000];
    int bytesReceived;
    beamList beams;

    printf ("\nWaiting for authentication request...");
    getAwaitFollowingString (connection, buffer, sizeof (buffer), "Username");
    printf ("\nSending user name...");
    sendCommand (connection, userName);
    getAwaitFollowingString (connection, buffer, sizeof (buffer), "Password");
    printf ("\nSending password...");
    sendCommand (connection, password);
    Sleep (500);
    printf ("\nPulling data...");
    waitForCommandPrompt (connection);
    printf ("\nRequesting for beam list...\n");
    sendCommand (connection, "beamselector list");
    Sleep (500);
    waitForCommandPrompt (connection, buffer);
    extractBeams (buffer, beams);

    printf ("Selected beam id is %d\n", beams.selected);

    for (auto beam: beams.list)
        printf ("Beam [%d]: %s\n", beam.id, beam.name.c_str ());


    while (true) {
        strings lines;

        printf ("Requesting modem status...\n");
        sendCommand (connection, "remotestate");
        Sleep (500);
        waitForCommandPrompt (connection, buffer);
        splitLines (buffer, lines);

        for (auto& line: lines) {
            if (line [0] == ' ' && line [1] == ' ')
                printf ("%s\n", line.c_str ());
        }

        Sleep (500);

        printf ("Requesting sonar RX...\n");
        sendCommand (connection, "rx snr");
        Sleep (500);
        waitForCommandPrompt (connection, buffer);
        splitLines (buffer, lines);

        for (auto& line: lines) {
            if (line [0] == 'R' && line [1] == 'x')
                printf ("%s\n", line.c_str ());
        }

        Sleep (5000);
    }
}

int main (int argCount, char *args []) {
    SOCKET connection;
    uint16_t port = 23;
    char addr [50] = {"192.168.1.1"};
    char userName [50] = {"admin"};
    char password [50] = {"P@55w0rd!"};
    
    printf ("iDirect terminal\n");

    for (auto i = 1; i < argCount; ++ i) {
        char *arg = args [i];

        if (*arg != '-' && *arg != '/') {
            printf ("Invalid command line\n");
            exit (0);
        }

        switch (tolower (arg [1])) {
            case '?':
            case 'h':
                printf (
                    "USAGE:\n"
                    "\tterminal [-p:port] [-a:addr] [-u:username] [-s:password]\n\n"
                );

                exit (0);

            case 'p':
                if (arg [2] == ':')
                    port = atoi (arg + 3);

                break;

            case 'a':
                if (arg [2] == ':')
                    strcpy (addr, arg + 3);

                break;

            case 'u':
                if (arg [2] == ':')
                    strcpy (userName, arg + 3);

                break;

            case 's':
                if (arg [2] == ':')
                    strcpy (password, arg + 3);

                break;
        }
    }

    WSADATA sockData;

    WSAStartup (MAKEWORD (2, 2), & sockData);

    connection = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);

    SOCKADDR_IN router;

    router.sin_addr.S_un.S_addr = inet_addr (addr);
    router.sin_family = AF_INET;
    router.sin_port = htons (port);

    if (connect (connection, (sockaddr *) & router, sizeof (router)) == S_OK)
        processConnection (connection, userName, password);

    closesocket (connection);

    WSACleanup ();
}