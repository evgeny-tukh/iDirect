#pragma once

#include <Windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include <queue>

#define UM_ADD_TO_LOG   WM_USER + 1
#define UM_ADD_BEAM     WM_USER + 2
#define UM_SELECT_BEAM  WM_USER + 3

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

enum msgType {
    SELECT_BEAM,
};

struct msg {
    msgType type;
    uint32_t data;

    msg (msgType _type, uint32_t _data): type (_type), data (_data) {}
};

typedef std::queue<msg> msgQueue;

struct workerData {
    HWND wnd;
    beamList beams;
    msgQueue messages;
};

