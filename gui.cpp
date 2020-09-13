#include <Windows.h>
#include <stdlib.h>
#include <cstdint>
#include <CommCtrl.h>
#include "defs.h"

static const uint16_t IDC_LOG = 100;
static const uint16_t IDC_BEAMS = 101;
static const uint16_t IDC_STATUS = 102;

static const int WINDOW_WIDTH = 600;
static const int WINDOW_HEIGHT = 300;
static const int LOG_HEIGHT = 100;
static const int BEAMLIST_WIDTH = 250;

static const char *className = "iDirectTerm";

HINSTANCE appInstance;

extern HANDLE startWorker (workerData *data);

void onCreate (HWND wnd) {
    RECT client;
    HWND logBox, beams, status;

    GetClientRect (wnd, & client);
    
    logBox = CreateWindow (
        "EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, 0, client.bottom - LOG_HEIGHT, client.right + 1, LOG_HEIGHT, wnd, 
        (HMENU) IDC_LOG, appInstance, 0
    );

    beams = CreateWindow (
        WC_LISTVIEW, "", WS_VISIBLE | WS_TABSTOP | WS_CHILD | WS_BORDER | LVS_REPORT | LVS_NOSORTHEADER | LVS_SHOWSELALWAYS | LVS_SINGLESEL, 0, 0, BEAMLIST_WIDTH,
        client.bottom - LOG_HEIGHT, wnd, (HMENU) IDC_BEAMS, appInstance, 0
    );

    status = CreateWindow (
        WC_STATIC, "N/A", WS_VISIBLE | WS_CHILD | WS_BORDER | SS_LEFT, BEAMLIST_WIDTH, 0, client.right - BEAMLIST_WIDTH - 1, client.bottom - LOG_HEIGHT, wnd,
        (HMENU) IDC_STATUS, appInstance, 0
    );

    ListView_SetExtendedListViewStyle (beams, LVS_EX_FULLROWSELECT);

    LVCOLUMN column;

    column.cx = 30;
    column.fmt = LVCFMT_LEFT;
    column.iSubItem = 0;
    column.mask = LVCF_TEXT | LVCF_SUBITEM | LVCF_WIDTH;
    column.pszText = "ID";

    ListView_InsertColumn (beams, 0, & column);

    column.cx = 200;
    column.pszText = "Name";

    ListView_InsertColumn (beams, 1, & column);
}

void processNotification (HWND wnd, workerData *data, NMHDR *genericHeader) {
    if (genericHeader->idFrom == IDC_BEAMS) {
        NMLISTVIEW *header = (NMLISTVIEW *) genericHeader;

        switch (header->hdr.code) {
            case NM_CLICK: {
                NMITEMACTIVATE *header = (NMITEMACTIVATE *) genericHeader;

                if (header->iItem >= 0) {
                    LVITEM item;

                    item.mask = LVIF_PARAM;
                    item.iItem = header->iItem;

                    SendDlgItemMessage (wnd, IDC_BEAMS, LVM_GETITEM, 0, (LPARAM) & item);
                    SendDlgItemMessage (wnd, IDC_BEAMS, LVM_DELETEALLITEMS, 0, 0);
                    SetDlgItemText (wnd, IDC_STATUS, "N/A");

                    data->messages.emplace (msgType::SELECT_BEAM, (uint32_t) item.lParam);
                }

                break;
            }

            /*case LVN_ITEMCHANGED: {
                if (header->iItem >= 0 && header->uChanged & LVIF_STATE) {
                    if ((header->uNewState & LVIS_SELECTED) && (header->uOldState & LVIS_SELECTED) == 0 && (!data->beams.ignoreBeamClick && data->beams.selected != header->lParam)) {
                        SendDlgItemMessage (wnd, IDC_BEAMS, LVM_DELETEALLITEMS, 0, 0);

                        data->messages.emplace (msgType::SELECT_BEAM, (uint32_t) header->lParam);
                    }
                }

                break;
            }*/
        }
    }
}

LRESULT CALLBACK wndProc (HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static workerData *data = 0;

    switch (msg) {
        case UM_SET_STATUS: {
            SetDlgItemText (wnd, IDC_STATUS, (const char *) lParam);
            
            free ((void *) lParam); break;
        }

        case UM_REMOVE_ALL_BEAMS: {
            data->beams.selected = 0;

            SetDlgItemText (wnd, IDC_STATUS, "N/A");
            SendDlgItemMessage (wnd, IDC_BEAMS, LVM_DELETEALLITEMS, 0, 0); break;
        }

        case UM_ADD_BEAM: {
            HWND beamList = GetDlgItem (wnd, IDC_BEAMS);
            LVITEM item;
            char buffer [10];
            bool selected = false;

            if (wParam & 0x80000000) {
                selected = true;
                wParam &= 0x7FFFFFFF;
            }

            item.pszText = itoa (wParam, buffer, 10);
            item.iItem = 0xFFFF;
            item.iSubItem = 0;
            item.lParam = wParam;
            item.mask = LVIF_PARAM | LVIF_TEXT;

            if (selected) {
                item.mask |= LVIF_STATE;
                item.stateMask = 15;
                item.state = LVIS_FOCUSED | LVIS_SELECTED;
            }

            auto itemNo = ListView_InsertItem (beamList, & item);

            ListView_SetItemText (beamList, itemNo, 1, (char *) lParam);

            free ((char *) lParam);

            SetFocus (beamList);

            break;
        }

        case UM_SELECT_BEAM: {
            HWND beamList = GetDlgItem (wnd, IDC_BEAMS);
            LVITEM item;

            for (int i = 0, count = ListView_GetItemCount (beamList); i < count; ++ i) {
                item.iItem = i;
                item.mask = LVIF_PARAM;

                ListView_GetItem (beamList, & item);

                if (item.lParam == lParam)
                    ListView_SetItemState (beamList, i, LVIS_SELECTED | LVIS_FOCUSED, 15);
            }

            break;
        }

        case UM_ADD_TO_LOG: {
            auto length = GetWindowTextLength (GetDlgItem (wnd, IDC_LOG));

            SendDlgItemMessage (wnd, IDC_LOG, EM_SETSEL, length, length);
            SendDlgItemMessage (wnd, IDC_LOG, EM_REPLACESEL, 0, lParam);

            break;
        }

        case WM_SYSCOMMAND: {
            if (wParam == SC_CLOSE) {
                if (MessageBox (wnd, "Exit the application?", "Request", MB_YESNO | MB_ICONQUESTION) == IDYES)
                    DestroyWindow (wnd);

                return 0;
            }

            break;
        }

        case WM_DESTROY: {
            PostQuitMessage (0); break;
        }

        case WM_CREATE: {
            CREATESTRUCT *createData = (CREATESTRUCT *) lParam;

            data = (workerData *) createData->lpCreateParams;
            
            onCreate (wnd); break;
        }

        case WM_NOTIFY:
            processNotification (wnd, data, (NMHDR *) lParam);

        default: {
            return DefWindowProc (wnd, msg, wParam, lParam);
        }
    }

    return 1;
}

bool registerClass (HINSTANCE instance) {
    WNDCLASS classInfo;

    memset (& classInfo, 0, sizeof (classInfo));

    classInfo.hbrBackground = (HBRUSH) GetStockObject (WHITE_BRUSH);
    classInfo.hCursor = LoadCursor (0, IDC_ARROW);
    classInfo.hIcon = LoadIcon (0, IDI_APPLICATION);
    classInfo.hInstance = instance;
    classInfo.lpfnWndProc = wndProc;
    classInfo.lpszClassName = className;
    classInfo.lpszMenuName = 0;
    classInfo.style = CS_HREDRAW | CS_VREDRAW;

    return RegisterClassA (& classInfo) != 0;
}

int WinMain (HINSTANCE instance, HINSTANCE prevInstance, char *cmdLine, int showCmd) {
    MSG msg;
    HANDLE worker;
    workerData data;

    appInstance = instance;

    registerClass (instance);

    data.wnd = CreateWindow (
        className, "iDirect Terminal", WS_VISIBLE | WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT, HWND_DESKTOP, 0, instance, & data
    );

    worker = startWorker (& data);

    while (GetMessage (& msg, 0, 0, 0)) {
        if (!IsDialogMessage (data.wnd, & msg)) {
            TranslateMessage (& msg);
            DispatchMessage (& msg);
        }
    }

    if (WaitForSingleObject (worker, 5000) == WAIT_TIMEOUT)
        TerminateThread (worker, 0);
    else
        CloseHandle (worker);
        
    return 0;
}