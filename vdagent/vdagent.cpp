/*
   Copyright (C) 2009 Red Hat, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "vdcommon.h"
#include "desktop_layout.h"
#include "display_setting.h"
#include "file_xfer.h"
#include "image.h"
#undef max
#undef min
#include <spice/macros.h>
#include <wtsapi32.h>
#include <lmcons.h>
#include <queue>
#include <set>
#include <vector>
#include <array>
#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>

#define VD_AGENT_LOG_PATH       TEXT("%svdagent.log")
#define VD_AGENT_WINCLASS_NAME  TEXT("VDAGENT")
#define VD_INPUT_INTERVAL_MS    20
#define VD_TIMER_ID             1
#define VD_CLIPBOARD_TIMEOUT_MS 3000
#define VD_CLIPBOARD_FORMAT_MAX_TYPES 16

// only in vista+, not yet in mingw
#ifndef WM_CLIPBOARDUPDATE
#define WM_CLIPBOARDUPDATE      0x031D
#endif

//FIXME: extract format/type stuff to win_vdagent_common for use by windows\platform.cpp as well
typedef struct VDClipboardFormat {
    uint32_t format;
    uint32_t types[VD_CLIPBOARD_FORMAT_MAX_TYPES];
} VDClipboardFormat;

static const VDClipboardFormat clipboard_formats[] = {
    {CF_UNICODETEXT, {VD_AGENT_CLIPBOARD_UTF8_TEXT, 0}},
    //FIXME: support more image types
    {CF_DIB, {VD_AGENT_CLIPBOARD_IMAGE_PNG, VD_AGENT_CLIPBOARD_IMAGE_BMP, 0}},
    {CF_HDROP, {VD_AGENT_CLIPBOARD_FILE_LIST, 0}},
};

#define clipboard_formats_count SPICE_N_ELEMENTS(clipboard_formats)

#include <spice/start-packed.h>
typedef struct SPICE_ATTR_PACKED VDIChunk {
    VDIChunkHeader hdr;
    uint8_t data[0];
} VDIChunk;
#include <spice/end-packed.h>

#define VD_MESSAGE_HEADER_SIZE (sizeof(VDIChunk) + sizeof(VDAgentMessage))
#define VD_READ_BUF_SIZE       (sizeof(VDIChunk) + VD_AGENT_MAX_DATA_SIZE)

typedef BOOL (WINAPI *PCLIPBOARD_OP)(HWND);

class VDAgent {
public:
    static VDAgent* get();
    ~VDAgent();
    bool run();

private:
    VDAgent();
    void input_desktop_message_loop();
    void event_dispatcher(DWORD timeout, DWORD wake_mask);
    bool handle_mouse_event(const VDAgentMouseState* state);
    bool handle_announce_capabilities(const VDAgentAnnounceCapabilities* announce_capabilities,
                                      uint32_t msg_size);
    bool handle_mon_config(const VDAgentMonitorsConfig* mon_config, uint32_t port);
    bool handle_clipboard(const VDAgentClipboard* clipboard, uint32_t size);
    bool handle_clipboard_grab(const VDAgentClipboardGrab* clipboard_grab, uint32_t size);
    bool handle_clipboard_request(const VDAgentClipboardRequest* clipboard_request);
    void handle_clipboard_release();
    bool handle_display_config(const VDAgentDisplayConfig* display_config, uint32_t port);
    bool handle_max_clipboard(const VDAgentMaxClipboard *msg, uint32_t size);
    void handle_chunk(const VDIChunk* chunk);
    void on_clipboard_grab();
    void on_clipboard_request(UINT format);
    void on_clipboard_release();
    DWORD get_buttons_change(DWORD mask, DWORD down_flag, DWORD up_flag);
    static HGLOBAL utf8_alloc(LPCSTR data, int size);
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    static DWORD WINAPI event_thread_proc(LPVOID param);
    static VOID CALLBACK read_completion(DWORD err, DWORD bytes, LPOVERLAPPED overlapped);
    static VOID CALLBACK write_completion(DWORD err, DWORD bytes, LPOVERLAPPED overlapped);
    void dispatch_message(VDAgentMessage* msg, uint32_t port);
    uint32_t get_clipboard_format(uint32_t type) const;
    uint32_t get_clipboard_type(uint32_t format) const;
    enum clipboard_owner_t { owner_none, owner_guest, owner_client };
    void set_clipboard_owner(clipboard_owner_t new_owner);
    enum control_command_t { CONTROL_STOP, CONTROL_RESET, CONTROL_DESKTOP_SWITCH, CONTROL_LOGON, CONTROL_CLIPBOARD };
    void set_control_event(control_command_t control_command);
    void handle_control_event();
    VDIChunk* new_chunk(DWORD bytes = 0);
    void enqueue_chunk(VDIChunk* msg);
    bool write_message(uint32_t type, uint32_t size, void* data);
    bool write_clipboard(VDAgentMessage* msg, uint32_t size);
    bool init_vio_serial();
    void close_vio_serial();
    bool send_input();
    void load_display_setting();
    bool send_announce_capabilities(bool request);
    void cleanup_in_msg();
    bool has_capability(unsigned int capability) const {
        return VD_AGENT_HAS_CAPABILITY(_client_caps.begin(), _client_caps.size(),
                                       capability);
    }

private:
    static VDAgent* _singleton;
    HWND _hwnd;
    HWND _hwnd_next_viewer;
    PCLIPBOARD_OP _add_clipboard_listener;
    PCLIPBOARD_OP _remove_clipboard_listener;
    clipboard_owner_t _clipboard_owner;
    DWORD _clipboard_tick;
    VDAgentMouseState _new_mouse = {};
    VDAgentMouseState _last_mouse = {};
    DWORD _input_time;
    HANDLE _control_event;
    HANDLE _stop_event;
    VDAgentMessage* _in_msg;
    uint32_t _in_msg_pos;
    bool _pending_input;
    bool _running;
    bool _session_is_locked;
    bool _desktop_switch;
    std::unique_ptr<DesktopLayout> _desktop_layout;
    bool _updating_display_config;
    DisplaySetting _display_setting;
    FileXfer _file_xfer;
    HANDLE _vio_serial;
    OVERLAPPED _read_overlapped;
    OVERLAPPED _write_overlapped;
    CHAR _read_buf[VD_READ_BUF_SIZE];
    DWORD _read_pos;
    DWORD _write_pos;
    mutex_t _control_mutex;
    mutex_t _message_mutex;
    std::queue<int> _control_queue;
    std::queue<VDIChunk*> _message_queue;

    bool _logon_desktop;
    bool _display_setting_initialized;
    bool _logon_occured;

    int32_t _max_clipboard;
    std::vector<uint32_t> _client_caps;
    std::array<uint32_t, VD_AGENT_CAPS_SIZE> _final_caps;

    std::set<uint32_t> _grab_types;

    VDLog* _log;

    UINT _cb_format_drop_effect;
};

VDAgent* VDAgent::_singleton = NULL;

#define VIOSERIAL_PORT_PATH L"\\\\.\\Global\\com.redhat.spice.0"

VDAgent* VDAgent::get()
{
    if (!_singleton) {
        _singleton = new VDAgent();
    }
    return _singleton;
}

VDAgent::VDAgent()
    : _hwnd (NULL)
    , _hwnd_next_viewer (NULL)
    , _add_clipboard_listener (NULL)
    , _remove_clipboard_listener (NULL)
    , _clipboard_owner (owner_none)
    , _clipboard_tick (0)
    , _input_time (0)
    , _control_event (NULL)
    , _stop_event (NULL)
    , _in_msg (NULL)
    , _in_msg_pos (0)
    , _pending_input (false)
    , _running (false)
    , _session_is_locked (false)
    , _desktop_switch (false)
    , _display_setting (VD_AGENT_REGISTRY_KEY)
    , _vio_serial (INVALID_HANDLE_VALUE)
    , _read_pos (0)
    , _write_pos (0)
    , _logon_desktop (false)
    , _display_setting_initialized (false)
    , _max_clipboard (-1)
    , _log (NULL)
{
    TCHAR log_path[MAX_PATH];
    TCHAR temp_path[MAX_PATH];

    if (GetTempPath(MAX_PATH, temp_path)) {
        swprintf_s(log_path, MAX_PATH, VD_AGENT_LOG_PATH, temp_path);
        _log = VDLog::get(log_path);
    }
    ZeroMemory(&_read_overlapped, sizeof(_read_overlapped));
    ZeroMemory(&_write_overlapped, sizeof(_write_overlapped));
    ZeroMemory(_read_buf, sizeof(_read_buf));

    _singleton = this;
}

VDAgent::~VDAgent()
{
    close_vio_serial();
    CloseHandle(_stop_event);
    CloseHandle(_control_event);
    delete _log;
}

DWORD WINAPI VDAgent::event_thread_proc(LPVOID param)
{
    VDAgent *agent = static_cast<VDAgent *>(param);
    // This event is monitored in a separate thread to avoid losing
    // events as the event is signaled with PulseEvent to wake up all
    // thread waiting for it.
    // This event allows to detect desktop switches which do not
    // change sessions like pressing Ctrl-Alt-Delete.
    HANDLE desktop_event = OpenEvent(SYNCHRONIZE, FALSE, L"WinSta0_DesktopSwitch");
    if (!desktop_event) {
        vd_printf("OpenEvent() failed: %lu", GetLastError());
        return 1;
    }
    while (agent->_running) {
        DWORD wait_ret = WaitForSingleObjectEx(desktop_event, INFINITE, TRUE);
        switch (wait_ret) {
        case WAIT_OBJECT_0:
            agent->set_control_event(CONTROL_DESKTOP_SWITCH);
            break;
        case WAIT_IO_COMPLETION:
            // handle APC events
            break;
        case WAIT_TIMEOUT:
        default:
            vd_printf("WaitForSingleObjectEx(): %lu", wait_ret);
        }
    }
    CloseHandle(desktop_event);
    return 0;
}

static VOID CALLBACK event_thread_stop_proc(ULONG_PTR)
{
}

bool VDAgent::run()
{
    DWORD session_id;
    HANDLE event_thread;
    WNDCLASS wcls;

    close_vio_serial();

    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_id)) {
        vd_printf("ProcessIdToSessionId failed %lu", GetLastError());
        return false;
    }
    vd_printf("***Agent started in session %lu***", session_id);
    log_version();
    if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)) {
        vd_printf("SetPriorityClass failed %lu", GetLastError());
    }
    if (!SetProcessShutdownParameters(0x100, 0)) {
        vd_printf("SetProcessShutdownParameters failed %lu", GetLastError());
    }

    HMODULE _user_lib = GetModuleHandle(L"User32");
    if (!_user_lib) {
        vd_printf("GetModuleHandle failed %lu", GetLastError());
        return false;
    }
    _add_clipboard_listener =
        (PCLIPBOARD_OP)GetProcAddress(_user_lib, "AddClipboardFormatListener");
    _remove_clipboard_listener =
        (PCLIPBOARD_OP)GetProcAddress(_user_lib, "RemoveClipboardFormatListener");
    // do not use FormatListener APIs if not available
    if (!_add_clipboard_listener || !_remove_clipboard_listener) {
        _add_clipboard_listener = nullptr;
        _remove_clipboard_listener = nullptr;
    }

    if (!_control_event)
        _control_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!_control_event) {
        vd_printf("CreateEvent() failed: %lu", GetLastError());
        return false;
    }
    ResetEvent(_control_event);

    if (!_stop_event)
        _stop_event = OpenEvent(SYNCHRONIZE, FALSE, VD_AGENT_STOP_EVENT);

    memset(&wcls, 0, sizeof(wcls));
    wcls.lpfnWndProc = &VDAgent::wnd_proc;
    wcls.lpszClassName = VD_AGENT_WINCLASS_NAME;
    if (!RegisterClass(&wcls)) {
        vd_printf("RegisterClass() failed: %lu", GetLastError());
        return false;
    }
    _desktop_layout.reset(new DesktopLayout());
    if (_desktop_layout->get_display_count() == 0) {
        vd_printf("No QXL devices!");
    }
    if (!init_vio_serial()) {
        return false;
    }
    if (!ReadFileEx(_vio_serial, _read_buf, sizeof(VDIChunk), &_read_overlapped, read_completion) &&
            GetLastError() != ERROR_IO_PENDING) {
        vd_printf("vio_serial read error %lu", GetLastError());
        return false;
    }
    _running = true;
    event_thread = CreateThread(NULL, 0, event_thread_proc, this, 0, NULL);
    if (!event_thread) {
        vd_printf("CreateThread() failed: %lu", GetLastError());
        return false;
    }
    send_announce_capabilities(true);
    vd_printf("Connected to server");

    _cb_format_drop_effect = RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);
    while (_running) {
        input_desktop_message_loop();
        if (_clipboard_owner == owner_guest) {
            set_clipboard_owner(owner_none);
        }
    }
    if (!QueueUserAPC(event_thread_stop_proc, event_thread, 0)) {
        TerminateThread(event_thread, 0);
    }
    WaitForSingleObject(event_thread, INFINITE);
    CloseHandle(event_thread);
    vd_printf("Agent stopped");
    return true;
}

void VDAgent::set_control_event(control_command_t control_command)
{
    MutexLocker lock(_control_mutex);
    _control_queue.push(control_command);
    if (_control_event && !SetEvent(_control_event)) {
        vd_printf("SetEvent() failed: %lu", GetLastError());
    }
}

void VDAgent::handle_control_event()
{
    MutexLocker lock(_control_mutex);
    while (_control_queue.size()) {
        int control_command = _control_queue.front();
        _control_queue.pop();
        vd_printf("Control command %d", control_command);
        switch (control_command) {
        case CONTROL_RESET:
            _file_xfer.reset();
            set_control_event(CONTROL_CLIPBOARD);
            set_clipboard_owner(owner_none);
            break;
        case CONTROL_STOP:
            _running = false;
            break;
        case CONTROL_DESKTOP_SWITCH:
            _desktop_switch = true;
            break;
        case CONTROL_LOGON:
            vd_printf("session logon");
            // loading the display settings for the current session's logged on user only
            // after 1) we receive logon event, and 2) the desktop switched from Winlogon
            if (!_logon_desktop) {
                vd_printf("LOGON display setting");
                _display_setting.load();
            } else {
                _logon_occured = true;
            }
            break;
        case CONTROL_CLIPBOARD:
            _clipboard_tick = 0;
            break;
        default:
            vd_printf("Unsupported control command %u", control_command);
        }
    }
}

void VDAgent::input_desktop_message_loop()
{
    TCHAR desktop_name[MAX_PATH];
    HDESK hdesk;

    hdesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!hdesk) {
        vd_printf("OpenInputDesktop() failed: %lu", GetLastError());
        _running = false;
        return;
    }
    if (!SetThreadDesktop(hdesk)) {
        vd_printf("SetThreadDesktop failed %lu", GetLastError());
        CloseDesktop(hdesk);
        _running = false;
        return;
    }
    if (GetUserObjectInformation(hdesk, UOI_NAME, desktop_name, sizeof(desktop_name), NULL)) {
        vd_printf("Desktop: %S", desktop_name);
    } else {
        vd_printf("GetUserObjectInformation failed %lu", GetLastError());
    }
    CloseDesktop(hdesk);

    if (FAILED(CoInitialize(NULL))) {
        vd_printf("CoInitialize failed");
        _running = false;
        return;
    }
    struct ComDeinit {
        ~ComDeinit()
        {
            CoUninitialize();
        }
    } com_deinit;

    // loading the display settings for the current session's logged on user only
    // after 1) we receive logon event, and 2) the desktop switched from Winlogon
    if (_tcscmp(desktop_name, TEXT("Winlogon")) == 0) {
        _logon_desktop = true;
    } else {
        // first load after connection
        if (!_display_setting_initialized) {
            vd_printf("First display setting");
            _display_setting.load();
            _display_setting_initialized = true;
        } else if (_logon_occured && _logon_desktop) {
            vd_printf("LOGON display setting");
            _display_setting.load();
        }
        _logon_occured = false;
        _logon_desktop = false;
    }

    _hwnd = CreateWindow(VD_AGENT_WINCLASS_NAME, NULL, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    if (!_hwnd) {
        vd_printf("CreateWindow() failed: %lu", GetLastError());
        _running = false;
        return;
    }
    if (!WTSRegisterSessionNotification(_hwnd, NOTIFY_FOR_ALL_SESSIONS)) {
        vd_printf("WTSRegisterSessionNotification() failed: %lu", GetLastError());
    }
    if (_add_clipboard_listener) {
        _add_clipboard_listener(_hwnd);
    } else {
        _hwnd_next_viewer = SetClipboardViewer(_hwnd);
    }
    while (_running && !_desktop_switch) {
        event_dispatcher(INFINITE, QS_ALLINPUT);
    }
    _desktop_switch = false;
    if (_pending_input) {
        KillTimer(_hwnd, VD_TIMER_ID);
        _pending_input = false;
    }
    if (_remove_clipboard_listener) {
        _remove_clipboard_listener(_hwnd);
    } else {
        ChangeClipboardChain(_hwnd, _hwnd_next_viewer);
    }
    WTSUnRegisterSessionNotification(_hwnd);
    DestroyWindow(_hwnd);
}

void VDAgent::event_dispatcher(DWORD timeout, DWORD wake_mask)
{
    HANDLE events[2];
    DWORD event_count = 1;
    DWORD wait_ret;
    MSG msg;
    enum {
        CONTROL_ACTION,
        STOP_ACTION,
    } actions[SPICE_N_ELEMENTS(events)], action;

    events[0] = _control_event;
    actions[0] = CONTROL_ACTION;
    if (_stop_event) {
        events[event_count] = _stop_event;
        actions[event_count] = STOP_ACTION;
        event_count++;
    }

    wait_ret = MsgWaitForMultipleObjectsEx(event_count, events, timeout, wake_mask, MWMO_ALERTABLE);
    if (wait_ret == WAIT_OBJECT_0 + event_count) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return;
    } else if (wait_ret == WAIT_IO_COMPLETION || wait_ret == WAIT_TIMEOUT) {
        return;
    } else if (wait_ret < WAIT_OBJECT_0 || wait_ret > WAIT_OBJECT_0 + event_count) {
        vd_printf("MsgWaitForMultipleObjectsEx failed: %lu %lu", wait_ret, GetLastError());
        _running = false;
        return;
    }

    action = actions[wait_ret - WAIT_OBJECT_0];
    switch (action) {
    case CONTROL_ACTION:
        handle_control_event();
        break;
    case STOP_ACTION:
        vd_printf("received stop event");
        _running = false;
        break;
    default:
        vd_printf("action not handled (%d)", action);
        _running = false;
    }
}

DWORD VDAgent::get_buttons_change(DWORD mask, DWORD down_flag, DWORD up_flag)
{
    DWORD ret = 0;
    if (!(_last_mouse.buttons & mask) && (_new_mouse.buttons & mask)) {
        ret = down_flag;
    } else if ((_last_mouse.buttons & mask) && !(_new_mouse.buttons & mask)) {
        ret = up_flag;
    }
    return ret;
}

bool VDAgent::send_input()
{
    DisplayMode* mode = NULL;
    DWORD mouse_move = 0;
    bool ret = true;
    INPUT inputs[3];
    INPUT *input = &inputs[0];

    if (_pending_input) {
        if (KillTimer(_hwnd, VD_TIMER_ID)) {
            _pending_input = false;
        } else {
            vd_printf("KillTimer failed: %lu", GetLastError());
            _running = false;
            return false;
        }
    }

    ASSERT(_desktop_layout);
    _desktop_layout->lock();
    if (_new_mouse.display_id < _desktop_layout->get_display_count()) {
        mode = _desktop_layout->get_display(_new_mouse.display_id);
    }
    if (!mode || !mode->get_attached()) {
        _desktop_layout->unlock();
        return true;
    }
    ZeroMemory(&inputs, sizeof(inputs));
    input->type = INPUT_MOUSE;
    if (_new_mouse.x != _last_mouse.x || _new_mouse.y != _last_mouse.y) {
        DWORD w = _desktop_layout->get_total_width();
        DWORD h = _desktop_layout->get_total_height();
        w = (w > 1) ? w-1 : 1; /* coordinates are 0..w-1, protect w==0 */
        h = (h > 1) ? h-1 : 1; /* coordinates are 0..h-1, protect h==0 */
        mouse_move = MOUSEEVENTF_MOVE;
        input->mi.dx = (mode->get_pos_x() + _new_mouse.x) * 0xffff / w;
        input->mi.dy = (mode->get_pos_y() + _new_mouse.y) * 0xffff / h;
    }
    input->mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | mouse_move;

    if (_new_mouse.buttons != _last_mouse.buttons) {
        DWORD buttons_change;
        DWORD mouse_wheel;

        buttons_change = get_buttons_change(VD_AGENT_LBUTTON_MASK,
                                            MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP) |
                         get_buttons_change(VD_AGENT_MBUTTON_MASK,
                                            MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP) |
                         get_buttons_change(VD_AGENT_RBUTTON_MASK,
                                            MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
        mouse_wheel = get_buttons_change(VD_AGENT_UBUTTON_MASK | VD_AGENT_DBUTTON_MASK,
                                         MOUSEEVENTF_WHEEL, 0);
        if (mouse_wheel) {
            if (_new_mouse.buttons & VD_AGENT_UBUTTON_MASK) {
                input->mi.mouseData = WHEEL_DELTA;
            } else if (_new_mouse.buttons & VD_AGENT_DBUTTON_MASK) {
                input->mi.mouseData = (DWORD)(-WHEEL_DELTA);
            }
        }
        input->mi.dwFlags |= mouse_wheel | buttons_change;

        auto set_xbutton = [&](DWORD vdagent_mask, DWORD xbutton) {
            buttons_change = get_buttons_change(vdagent_mask, MOUSEEVENTF_XDOWN, MOUSEEVENTF_XUP);
            if (buttons_change && (input->mi.dwFlags & (MOUSEEVENTF_WHEEL|MOUSEEVENTF_XDOWN|MOUSEEVENTF_XUP)) ){
                ++input;
                input->type = INPUT_MOUSE;
            }
            if (buttons_change) {
                input->mi.dwFlags |= buttons_change;
                input->mi.mouseData = xbutton;
            }
        };
        set_xbutton(VD_AGENT_SBUTTON_MASK, XBUTTON1);
        set_xbutton(VD_AGENT_EBUTTON_MASK, XBUTTON2);
    }

    const UINT num_inputs = input - inputs + 1;
    if (SendInput(num_inputs, inputs, sizeof(INPUT)) < num_inputs) {
        DWORD err = GetLastError();
        // Don't stop agent due to UIPI blocking, which is usually only for specific windows
        // of system security applications (anti-viruses etc.)
        if (err != ERROR_SUCCESS && err != ERROR_ACCESS_DENIED) {
            vd_printf("SendInput failed: %lu", err);
            ret = _running = false;
        }
    } else {
        _last_mouse = _new_mouse;
    }
    _input_time = GetTickCount();
    _desktop_layout->unlock();
    return ret;
}

bool VDAgent::handle_mouse_event(const VDAgentMouseState* state)
{
    _new_mouse = *state;
    if (_new_mouse.buttons != _last_mouse.buttons) {
        return send_input();
    }

    if (_new_mouse.x != _last_mouse.x || _new_mouse.y != _last_mouse.y) {
        if (GetTickCount() - _input_time > VD_INPUT_INTERVAL_MS) {
            return send_input();
        }

        if (!_pending_input) {
            if (!SetTimer(_hwnd, VD_TIMER_ID, VD_INPUT_INTERVAL_MS, NULL)) {
                vd_printf("SetTimer failed: %lu", GetLastError());
                _running = false;
                return false;
            }
            _pending_input = true;
        }
    }
    return true;
}

bool VDAgent::handle_mon_config(const VDAgentMonitorsConfig* mon_config, uint32_t port)
{
    VDIChunk* reply_chunk;
    VDAgentMessage* reply_msg;
    VDAgentReply* reply;
    size_t display_count;
    bool update_displays(false);

    _updating_display_config = true;

    display_count = _desktop_layout->get_display_count();
    for (uint32_t i = 0; i < display_count; i++) {
        DisplayMode* mode = _desktop_layout->get_display(i);
        if (!mode) {
            continue;
        }
        if (i >= mon_config->num_of_monitors) {
            vd_printf("%d. detached", i);
            mode->set_attached(false);
            update_displays = true;
            continue;
        }
        const VDAgentMonConfig* mon = &mon_config->monitors[i];
        vd_printf("%d. %u*%u*%u (%d,%d) %u", i, mon->width, mon->height, mon->depth, mon->x,
                  mon->y, !!(mon_config->flags & VD_AGENT_CONFIG_MONITORS_FLAG_USE_POS));
        if (mon->height == 0 && mon->depth == 0) {
            vd_printf("%d. detaching", i);
            update_displays = mode->get_attached() ? true : update_displays;
            mode->set_attached(false);
            continue;
        }
        if (mode->get_height() != mon->height || mode->get_width() != mon->width || mode->get_depth() != mon->depth) {
            mode->set_res(mon->width, mon->height, mon->depth);
            update_displays = true;
        }
        if (mon_config->flags & VD_AGENT_CONFIG_MONITORS_FLAG_USE_POS && (mode->get_pos_x() != mon->x || mode->get_pos_y() != mon->y)) {
            mode->set_pos(mon->x, mon->y);
            update_displays = true;
        }
        if (!mode->get_attached()) {
            mode->set_attached(true);
            update_displays = true;
        }
    }
    if (update_displays) {
        _desktop_layout->set_displays();
    }

    _updating_display_config = false;
    /* refresh again, in case something else changed */
    _desktop_layout->get_displays();

    DWORD msg_size = VD_MESSAGE_HEADER_SIZE + sizeof(VDAgentReply);
    reply_chunk = new_chunk(msg_size);
    if (!reply_chunk) {
        return false;
    }
    reply_chunk->hdr.port = port;
    reply_chunk->hdr.size = sizeof(VDAgentMessage) + sizeof(VDAgentReply);
    reply_msg = (VDAgentMessage*)reply_chunk->data;
    reply_msg->protocol = VD_AGENT_PROTOCOL;
    reply_msg->type = VD_AGENT_REPLY;
    reply_msg->opaque = 0;
    reply_msg->size = sizeof(VDAgentReply);
    reply = (VDAgentReply*)reply_msg->data;
    reply->type = VD_AGENT_MONITORS_CONFIG;
    reply->error = display_count ? VD_AGENT_SUCCESS : VD_AGENT_ERROR;
    enqueue_chunk(reply_chunk);
    return true;
}

static std::wstring spice_webdav_get_drive_letter()
{
    std::wstring drive(L"Z:");
    static const wchar_t spice_folder[] = L"\\\\localhost@9843\\DavWWWRoot";
    wchar_t remote[MAX_PATH];

    DWORD drives = GetLogicalDrives();

    /* spice-webdavd assigns drive letter from the end of the alphabet */
    for (int i = 25; i >= 0; i--) {
        int mask = 1 << i;
        if (drives & mask) {
            drive[0] = 'A' + i;
            DWORD size = SPICE_N_ELEMENTS(remote);

            if (WNetGetConnection(drive.c_str(), remote, &size) == NO_ERROR &&
                _wcsicmp(remote, spice_folder) == 0) {
                return drive;
            }
        }
    }

    return L"";
}

static std::wstring utf8_to_wchar(LPCSTR utf8)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len == 0) {
        return {};
    }

    std::wstring wchr;
    wchr.resize(len);

    len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &wchr[0], len);
    if (len == 0) {
        DWORD err = GetLastError();
        vd_printf("err converting to wchar: %lu", err);
        return {};
    }
    wchr.resize(len-1);

    return wchr;
}

static std::vector<wchar_t> paths_to_wchar_array(const std::vector<std::wstring> &paths)
{
    std::vector<wchar_t> arr;

    for (auto path: paths) {
        arr.insert(arr.end(), path.begin(), path.end());
        arr.push_back(L'\0');
    }
    /* Windows requires the array to be double-NULL-terminated */
    arr.push_back(L'\0');

    return arr;
}

static std::vector<wchar_t> clipboard_data_to_path_array(
    std::wstring drive, LPCSTR data, uint32_t size, DWORD &drop_effect_out)
{
    drop_effect_out = DROPEFFECT_NONE;

    if (!data || size < 1) {
        return {};
    }

    if (data[size-1]) {
        vd_printf("received list of paths that is not null-terminated");
        return {};
    }

    if (!strcmp(data, "copy")) {
        drop_effect_out = DROPEFFECT_COPY;
    } else if (!strcmp(data, "cut")) {
        drop_effect_out = DROPEFFECT_MOVE;
    } else {
        vd_printf("first line of path list must specify clipboard action");
        return {};
    }
    /* skip the action line, since we're only interested in
     * the actual paths from this point forward  */
    int len = strlen(data) + 1;
    data += len;
    size -= len;

    wchar_t buf[MAX_PATH];
    std::vector<std::wstring> paths;

    for (LPCSTR item = data; item < data + size; item += strlen(item) + 1) {
        auto wpath = utf8_to_wchar(item);

        std::replace(wpath.begin(), wpath.end(), L'/', L'\\');
        if (!wpath.length() || !PathCombine(buf, drive.c_str(), wpath.c_str())) {
            continue;
        }

        paths.push_back(std::wstring(buf));
    }
    vd_printf("received %zu paths", paths.size());

    return paths_to_wchar_array(paths);
}

static HANDLE get_dropfiles_handle(const std::vector<wchar_t>& path_arr)
{
    HANDLE clip_data = GlobalAlloc(GHND, sizeof(DROPFILES) +
        path_arr.size() * sizeof(wchar_t));

    if (!clip_data) {
        return NULL;
    }

    uint8_t *d = (uint8_t *)GlobalLock(clip_data);
    LPDROPFILES drop = (LPDROPFILES)d;
    if (!drop) {
        GlobalFree(clip_data);
        return NULL;
    }

    drop->pFiles = sizeof(DROPFILES); /* offset of the file array */
    drop->pt.x = 0; /* drop point, not used */
    drop->pt.y = 0;
    drop->fNC = FALSE; /* related to pt, not used */
    drop->fWide = TRUE; /* contains wide chars */
    std::copy(path_arr.begin(), path_arr.end(), (wchar_t *)(d + sizeof(DROPFILES)));

    GlobalUnlock(clip_data);

    return clip_data;
}

static HANDLE get_drop_effect_handle(DWORD effect)
{
    HANDLE clip_data = GlobalAlloc(GHND, sizeof(DWORD));

    if (!clip_data) {
        return NULL;
    }

    DWORD *e = (DWORD *)GlobalLock(clip_data);
    if (!e) {
        GlobalFree(clip_data);
        return NULL;
    }
    *e = effect;
    GlobalUnlock(clip_data);

    return clip_data;
}

bool VDAgent::handle_clipboard(const VDAgentClipboard* clipboard, uint32_t size)
{
    HANDLE clip_data = NULL;
    UINT format;
    bool ret = false;

    if (_clipboard_owner != owner_client) {
        vd_printf("Received clipboard data from client while clipboard is not owned by client");
        goto fin;
    }
    if (clipboard->type == VD_AGENT_CLIPBOARD_NONE) {
        goto fin;
    }
    format = get_clipboard_format(clipboard->type);
    switch (clipboard->type) {
    case VD_AGENT_CLIPBOARD_UTF8_TEXT:
        clip_data = utf8_alloc((LPCSTR)clipboard->data, size);
        break;
    case VD_AGENT_CLIPBOARD_IMAGE_PNG:
    case VD_AGENT_CLIPBOARD_IMAGE_BMP:
        clip_data = get_image_handle(*clipboard, size, format);
        break;
    case VD_AGENT_CLIPBOARD_FILE_LIST: {
        /* FIXME: does this block? cache it? */
        std::wstring drive = spice_webdav_get_drive_letter();
        if (drive.empty()) {
            vd_printf("Spice webdav not mapped");
            goto fin;
        }
        DWORD drop_effect;
        std::vector<wchar_t> path_arr = clipboard_data_to_path_array(drive,
            (LPCSTR)clipboard->data, size, drop_effect);
        HANDLE drop_effect_data = get_drop_effect_handle(drop_effect);
        if (!SetClipboardData(_cb_format_drop_effect, drop_effect_data)) {
            vd_printf("SetClipboardData drop effect failed: %lu", GetLastError());
            if (drop_effect_data) {
                GlobalFree(drop_effect_data);
            }
        }
        clip_data = get_dropfiles_handle(path_arr);
        format = CF_HDROP;
        break;
    }
    default:
        vd_printf("Unsupported clipboard type %u", clipboard->type);
        goto fin;
    }
    if (format == 0) {
        vd_printf("Unknown clipboard format, type %u", clipboard->type);
        goto fin;
    }
    ret = !!SetClipboardData(format, clip_data);
    if (ret) {
        clip_data = NULL;
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_NOT_ENOUGH_MEMORY) {
            vd_printf("Not enough memory to set clipboard data, size %u bytes", size);
        } else {
            vd_printf("SetClipboardData failed: %lu", err);
        }
    }
fin:
    if (clip_data) {
        GlobalFree(clip_data);
    }
    set_control_event(CONTROL_CLIPBOARD);
    return ret;
}

HGLOBAL VDAgent::utf8_alloc(LPCSTR data, int size)
{
    HGLOBAL handle;
    LPVOID buf;
    int len;

    // Received utf8 string is not null-terminated
    if (!(len = MultiByteToWideChar(CP_UTF8, 0, data, size, NULL, 0))) {
        return NULL;
    }
    len++;
    // Allocate and lock clipboard memory
    if (!(handle = GlobalAlloc(GMEM_DDESHARE, len * sizeof(WCHAR)))) {
        return NULL;
    }
    if (!(buf = GlobalLock(handle))) {
        GlobalFree(handle);
        return NULL;
    }
    // Translate data and set clipboard content
    if (!(MultiByteToWideChar(CP_UTF8, 0, data, size, (LPWSTR)buf, len))) {
        GlobalUnlock(handle);
        GlobalFree(handle);
        return NULL;
    }
    ((LPWSTR)buf)[len - 1] = L'\0';
    GlobalUnlock(handle);
    return handle;
}

void VDAgent::load_display_setting()
{
    _display_setting.load();
}

static const uint16_t supported_caps[] = {
    VD_AGENT_CAP_MOUSE_STATE,
    VD_AGENT_CAP_MONITORS_CONFIG,
    VD_AGENT_CAP_REPLY,
    VD_AGENT_CAP_DISPLAY_CONFIG,
    VD_AGENT_CAP_CLIPBOARD_BY_DEMAND,
    VD_AGENT_CAP_SPARSE_MONITORS_CONFIG,
    VD_AGENT_CAP_GUEST_LINEEND_CRLF,
    VD_AGENT_CAP_MAX_CLIPBOARD,
    VD_AGENT_CAP_FILE_XFER_DETAILED_ERRORS,
};

bool VDAgent::send_announce_capabilities(bool request)
{
    DWORD msg_size;
    VDIChunk* caps_chunk;
    VDAgentMessage* caps_msg;
    VDAgentAnnounceCapabilities* caps;
    uint32_t caps_size;
    uint32_t internal_msg_size = sizeof(VDAgentAnnounceCapabilities) + VD_AGENT_CAPS_BYTES;

    msg_size = VD_MESSAGE_HEADER_SIZE + internal_msg_size;
    caps_chunk = new_chunk(msg_size);
    if (!caps_chunk) {
        return false;
    }
    caps_size = VD_AGENT_CAPS_SIZE;
    caps_chunk->hdr.port = VDP_CLIENT_PORT;
    caps_chunk->hdr.size = sizeof(VDAgentMessage) + internal_msg_size;
    caps_msg = (VDAgentMessage*)caps_chunk->data;
    caps_msg->protocol = VD_AGENT_PROTOCOL;
    caps_msg->type = VD_AGENT_ANNOUNCE_CAPABILITIES;
    caps_msg->opaque = 0;
    caps_msg->size = internal_msg_size;
    caps = (VDAgentAnnounceCapabilities*)caps_msg->data;
    caps->request = request;
    memset(caps->caps, 0, VD_AGENT_CAPS_BYTES);
    for (auto cap : supported_caps) {
        VD_AGENT_SET_CAPABILITY(caps->caps, cap);
    }
    vd_printf("Sending capabilities:");
    for (uint32_t i = 0 ; i < caps_size; ++i) {
        vd_printf("%X", caps->caps[i]);
    }
    enqueue_chunk(caps_chunk);
    return true;
}

bool VDAgent::handle_announce_capabilities(const VDAgentAnnounceCapabilities* announce_capabilities,
                                           uint32_t msg_size)
{
    uint32_t caps_size = VD_AGENT_CAPS_SIZE_FROM_MSG_SIZE(msg_size);

    vd_printf("Got capabilities (%d)", caps_size);
    for (uint32_t i = 0 ; i < caps_size; ++i) {
        vd_printf("%X", announce_capabilities->caps[i]);
    }
    _client_caps.resize(caps_size);
    memcpy(_client_caps.data(), announce_capabilities->caps, sizeof(uint32_t) * caps_size);

    for (auto cap : supported_caps) {
        if (VD_AGENT_HAS_CAPABILITY(announce_capabilities->caps, caps_size, cap)) {
            VD_AGENT_SET_CAPABILITY(_final_caps.data(), cap);
        }
    }

    if (has_capability(VD_AGENT_CAP_MONITORS_CONFIG_POSITION))
        _desktop_layout->set_position_configurable(true);
    if (announce_capabilities->request) {
        return send_announce_capabilities(false);
    }
    return true;
}

bool VDAgent::handle_display_config(const VDAgentDisplayConfig* display_config, uint32_t port)
{
    DisplaySettingOptions disp_setting_opts;
    VDIChunk* reply_chunk;
    VDAgentMessage* reply_msg;
    VDAgentReply* reply;
    DWORD msg_size;

    if (display_config->flags & VD_AGENT_DISPLAY_CONFIG_FLAG_DISABLE_WALLPAPER) {
        disp_setting_opts._disable_wallpaper = TRUE;
    }

    if (display_config->flags & VD_AGENT_DISPLAY_CONFIG_FLAG_DISABLE_FONT_SMOOTH) {
       disp_setting_opts._disable_font_smoothing = TRUE;
    }

    if (display_config->flags & VD_AGENT_DISPLAY_CONFIG_FLAG_DISABLE_ANIMATION) {
        disp_setting_opts._disable_animation = TRUE;
    }

    _display_setting.set(disp_setting_opts);

    if (display_config->flags & VD_AGENT_DISPLAY_CONFIG_FLAG_SET_COLOR_DEPTH) {
        _desktop_layout->set_display_depth(display_config->depth);
    }

    msg_size = VD_MESSAGE_HEADER_SIZE + sizeof(VDAgentReply);
    reply_chunk = new_chunk(msg_size);
    if (!reply_chunk) {
        return false;
    }
    reply_chunk->hdr.port = port;
    reply_chunk->hdr.size = sizeof(VDAgentMessage) + sizeof(VDAgentReply);
    reply_msg = (VDAgentMessage*)reply_chunk->data;
    reply_msg->protocol = VD_AGENT_PROTOCOL;
    reply_msg->type = VD_AGENT_REPLY;
    reply_msg->opaque = 0;
    reply_msg->size = sizeof(VDAgentReply);
    reply = (VDAgentReply*)reply_msg->data;
    reply->type = VD_AGENT_DISPLAY_CONFIG;
    reply->error = VD_AGENT_SUCCESS;
    enqueue_chunk(reply_chunk);
    return true;
}

bool VDAgent::handle_max_clipboard(const VDAgentMaxClipboard *msg, uint32_t size)
{
    vd_printf("Set max clipboard size: %d", msg->max);
    _max_clipboard = msg->max;
    return true;
}

bool VDAgent::write_clipboard(VDAgentMessage* msg, uint32_t size)
{
    uint32_t pos = 0;
    bool ret = true;

    ASSERT(msg && size);
    //FIXME: do it smarter - no loop, no memcopy
    MutexLocker lock(_message_mutex);
    while (pos < size) {
        DWORD n = MIN(sizeof(VDIChunk) + size - pos, VD_AGENT_MAX_DATA_SIZE);
        VDIChunk* chunk = new_chunk(n);
        if (!chunk) {
            ret = false;
            break;
        }
        chunk->hdr.port = VDP_CLIENT_PORT;
        chunk->hdr.size = n - sizeof(VDIChunk);
        memcpy(chunk->data, (char*)msg + pos, n - sizeof(VDIChunk));
        enqueue_chunk(chunk);
        pos += (n - sizeof(VDIChunk));
    }
    return ret;
}

bool VDAgent::write_message(uint32_t type, uint32_t size = 0, void* data = NULL)
{
    VDIChunk* chunk;
    VDAgentMessage* msg;

    chunk = new_chunk(VD_MESSAGE_HEADER_SIZE + size);
    if (!chunk) {
        return false;
    }
    chunk->hdr.port = VDP_CLIENT_PORT;
    chunk->hdr.size = sizeof(VDAgentMessage) + size;
    msg = (VDAgentMessage*)chunk->data;
    msg->protocol = VD_AGENT_PROTOCOL;
    msg->type = type;
    msg->opaque = 0;
    msg->size = size;
    if (size && data) {
        memcpy(msg->data, data, size);
    }
    enqueue_chunk(chunk);
    return true;
}

void VDAgent::on_clipboard_grab()
{
    uint32_t types[clipboard_formats_count * VD_CLIPBOARD_FORMAT_MAX_TYPES];
    int count = 0;

    if (!has_capability(VD_AGENT_CAP_CLIPBOARD_BY_DEMAND)) {
        return;
    }
    if (CountClipboardFormats() == 0) {
        return;
    }
    for (unsigned int i = 0; i < clipboard_formats_count; i++) {
        if (clipboard_formats[i].format == CF_HDROP) {
            /* only unidirectional support atm */
            continue;
        }
        if (IsClipboardFormatAvailable(clipboard_formats[i].format)) {
            for (const uint32_t* ptype = clipboard_formats[i].types; *ptype; ptype++) {
                types[count++] = *ptype;
            }
        }
    }
    if (count) {
        write_message(VD_AGENT_CLIPBOARD_GRAB, count * sizeof(types[0]), types);
        set_clipboard_owner(owner_guest);
    } else {
        UINT format = 0;
        while ((format = EnumClipboardFormats(format))) {
            vd_printf("Unsupported clipboard format %u", format);
        }
    }
}

// In delayed rendering, Windows requires us to SetClipboardData before we return from
// handling WM_RENDERFORMAT. Therefore, we try our best by sending CLIPBOARD_REQUEST to the
// agent, while waiting alertably for a while (hoping for good) for receiving CLIPBOARD data
// or CLIPBOARD_RELEASE from the agent, which both will signal clipboard_event.
// In case of unsupported format, wrong clipboard owner or no clipboard capability, we do nothing in
// WM_RENDERFORMAT and return immediately.
// FIXME: need to be handled using request queue
void VDAgent::on_clipboard_request(UINT format)
{
    uint32_t type;

    if (_clipboard_owner != owner_client) {
        vd_printf("Received render request event for format %u"
                  "while clipboard is not owned by client", format);
        return;
    }
    if (!(type = get_clipboard_type(format))) {
        vd_printf("Unsupported clipboard format %u", format);
        return;
    }
    if (!has_capability(VD_AGENT_CAP_CLIPBOARD_BY_DEMAND)) {
        return;
    }

    VDAgentClipboardRequest request = {type};
    if (!write_message(VD_AGENT_CLIPBOARD_REQUEST, sizeof(request), &request)) {
        return;
    }

    _clipboard_tick = GetTickCount();
    while (_running && _clipboard_tick &&
           GetTickCount() < _clipboard_tick + VD_CLIPBOARD_TIMEOUT_MS) {
        event_dispatcher(VD_CLIPBOARD_TIMEOUT_MS, 0);
    }

    if (_clipboard_tick) {
        vd_printf("Clipboard wait timeout");
        _clipboard_tick = 0;
    } else {
        // reset incoming message state only upon completion (even after timeout)
        cleanup_in_msg();
    }
}

void VDAgent::on_clipboard_release()
{
    if (!has_capability(VD_AGENT_CAP_CLIPBOARD_BY_DEMAND)) {
        return;
    }
    if (_clipboard_owner == owner_guest) {
        write_message(VD_AGENT_CLIPBOARD_RELEASE, 0, NULL);
    }
}

bool VDAgent::handle_clipboard_grab(const VDAgentClipboardGrab* clipboard_grab, uint32_t size)
{
    std::set<uint32_t> grab_formats;

    _grab_types.clear();
    for (uint32_t i = 0; i < size / sizeof(clipboard_grab->types[0]); i++) {
        uint32_t format = get_clipboard_format(clipboard_grab->types[i]);
        vd_printf("grab type %u format=%u", clipboard_grab->types[i], format);
        //On first supported type, open and empty the clipboard
        if (format && grab_formats.empty()) {
            if (!OpenClipboard(_hwnd)) {
                return false;
            }
            EmptyClipboard();
        }
        //For all supported type set delayed rendering
        if (format) {
            _grab_types.insert(clipboard_grab->types[i]);
            if (grab_formats.insert(format).second) {
                SetClipboardData(format, NULL);
            }
        }
    }
    if (grab_formats.empty()) {
        vd_printf("No supported clipboard types in client grab");
        return true;
    }
    if (grab_formats.find(CF_HDROP) != grab_formats.end()) {
        SetClipboardData(_cb_format_drop_effect, NULL);
    }
    CloseClipboard();
    set_clipboard_owner(owner_client);
    return true;
}

// If handle_clipboard_request() fails, its caller sends VD_AGENT_CLIPBOARD message with type
// VD_AGENT_CLIPBOARD_NONE and no data, so the client will know the request failed.
bool VDAgent::handle_clipboard_request(const VDAgentClipboardRequest* clipboard_request)
{
    VDAgentMessage* msg;
    uint32_t msg_size;
    UINT format;
    HANDLE clip_data;
    void* new_data = NULL;
    long new_size = 0;
    size_t len = 0;
    VDAgentClipboard* clipboard = NULL;

    if (_clipboard_owner != owner_guest) {
        vd_printf("Received clipboard request from client while clipboard is not owned by guest");
        return false;
    }
    if (!(format = get_clipboard_format(clipboard_request->type))) {
        vd_printf("Unsupported clipboard type %u", clipboard_request->type);
        return false;
    }
    if (format == CF_HDROP) {
        /* only unidirectional support atm */
        return false;
    }
    // on encoding only, we use HBITMAP to keep the correct palette
    if (format == CF_DIB) {
        format = CF_BITMAP;
    }
    if (!IsClipboardFormatAvailable(format) || !OpenClipboard(_hwnd)) {
        return false;
    }
    if (!(clip_data = GetClipboardData(format))) {
        CloseClipboard();
        return false;
    }
    switch (clipboard_request->type) {
    case VD_AGENT_CLIPBOARD_UTF8_TEXT:
        if (!(new_data = GlobalLock(clip_data))) {
            break;
        }
        len = wcslen((LPCWSTR)new_data);
        new_size = WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)new_data, (int)len, NULL, 0, NULL, NULL);
        break;
    case VD_AGENT_CLIPBOARD_IMAGE_PNG:
    case VD_AGENT_CLIPBOARD_IMAGE_BMP:
        new_data = get_raw_clipboard_image(*clipboard_request, clip_data, new_size);
        break;
    }

    if (!new_size || !new_data) {
        vd_printf("clipboard is empty");
        goto handle_clipboard_request_fail;
    }
    if ((_max_clipboard != -1) && (new_size > _max_clipboard)) {
        vd_printf("clipboard is too large (%ld > %d), discarding",
                  new_size, _max_clipboard);
        goto handle_clipboard_request_fail;
    }

    msg_size = sizeof(VDAgentMessage) + sizeof(VDAgentClipboard) + new_size;
    msg = (VDAgentMessage*)new uint8_t[msg_size];
    msg->protocol = VD_AGENT_PROTOCOL;
    msg->type = VD_AGENT_CLIPBOARD;
    msg->opaque = 0;
    msg->size = (uint32_t)(sizeof(VDAgentClipboard) + new_size);
    clipboard = (VDAgentClipboard*)msg->data;
    clipboard->type = clipboard_request->type;

    switch (clipboard_request->type) {
    case VD_AGENT_CLIPBOARD_UTF8_TEXT:
        WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)new_data, (int)len, (LPSTR)clipboard->data,
                            new_size, NULL, NULL);
        GlobalUnlock(clip_data);
        break;
    case VD_AGENT_CLIPBOARD_IMAGE_PNG:
    case VD_AGENT_CLIPBOARD_IMAGE_BMP:
        memcpy(clipboard->data, new_data, new_size);
        free_raw_clipboard_image(new_data);
        break;
    }
    CloseClipboard();
    write_clipboard(msg, msg_size);
    delete[] (uint8_t *)msg;
    return true;

handle_clipboard_request_fail:
    if (clipboard_request->type == VD_AGENT_CLIPBOARD_UTF8_TEXT) {
       GlobalUnlock(clip_data);
    } else if (new_data) {
       free_raw_clipboard_image(new_data);
    }
    CloseClipboard();
    return false;
}

void VDAgent::handle_clipboard_release()
{
    if (_clipboard_owner != owner_client) {
        vd_printf("Received clipboard release from client while clipboard is not owned by client");
        return;
    }
    set_control_event(CONTROL_CLIPBOARD);
    set_clipboard_owner(owner_none);
}

uint32_t VDAgent::get_clipboard_format(uint32_t type) const
{
    for (unsigned int i = 0; i < clipboard_formats_count; i++) {
        for (const uint32_t* ptype = clipboard_formats[i].types; *ptype; ptype++) {
            if (*ptype == type) {
                return clipboard_formats[i].format;
            }
        }
    }
    return 0;
}

uint32_t VDAgent::get_clipboard_type(uint32_t format) const
{
    const uint32_t* types = NULL;

    if (format == _cb_format_drop_effect) {
        /* type is the same for both of these formats */
        format = CF_HDROP;
    }

    for (unsigned int i = 0; i < clipboard_formats_count && !types; i++) {
        if (clipboard_formats[i].format == format) {
            types = clipboard_formats[i].types;
        }
    }
    if (!types) {
        return 0;
    }
    for (const uint32_t* ptype = types; *ptype; ptype++) {
        if (_grab_types.find(*ptype) != _grab_types.end()) {
            return *ptype;
        }
    }
    return 0;
}

void VDAgent::set_clipboard_owner(clipboard_owner_t new_owner)
{
    // FIXME: Clear requests, clipboard data and state
    if (new_owner == owner_none) {
        on_clipboard_release();
    }
    _clipboard_owner = new_owner;
}

bool VDAgent::init_vio_serial()
{
    _vio_serial = CreateFile(VIOSERIAL_PORT_PATH, GENERIC_READ | GENERIC_WRITE , 0, NULL,
                             OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (_vio_serial == INVALID_HANDLE_VALUE) {
        vd_printf("Failed opening %ls, error %lu", VIOSERIAL_PORT_PATH, GetLastError());
        return false;
    }
    return true;
}

void VDAgent::close_vio_serial()
{
    if (_vio_serial != INVALID_HANDLE_VALUE) {
        CloseHandle(_vio_serial);
        _vio_serial = INVALID_HANDLE_VALUE;
    }
}

void VDAgent::dispatch_message(VDAgentMessage* msg, uint32_t port)
{
    bool res = true;

    // check message
    switch (agent_check_message(msg, msg->data, _final_caps.data(), _final_caps.size())) {
    case AGENT_CHECK_WRONG_PROTOCOL_VERSION:
        vd_printf("Invalid protocol %u", msg->protocol);
        _running = false;
        return;
    case AGENT_CHECK_UNKNOWN_MESSAGE:
        vd_printf("Unsupported message type %u size %u, ignoring", msg->type, msg->size);
        return;
    case AGENT_CHECK_INVALID_SIZE:
        vd_printf("Unexpected msg size %u for message type %u", msg->size, msg->type);
        _running = false;
        return;
    case AGENT_CHECK_TRUNCATED:
    case AGENT_CHECK_INVALID_DATA:
        vd_printf("Received malformed message, size %u type %u", msg->size, msg->type);
        _running = false;
        return;
    case AGENT_CHECK_NO_ERROR:
        break;
    }

    switch (msg->type) {
    case VD_AGENT_MOUSE_STATE:
        res = handle_mouse_event((VDAgentMouseState*)msg->data);
        break;
    case VD_AGENT_MONITORS_CONFIG:
        res = handle_mon_config((VDAgentMonitorsConfig*)msg->data, port);
        break;
    case VD_AGENT_CLIPBOARD:
        handle_clipboard((VDAgentClipboard*)msg->data, msg->size - sizeof(VDAgentClipboard));
        break;
    case VD_AGENT_CLIPBOARD_GRAB:
        handle_clipboard_grab((VDAgentClipboardGrab*)msg->data, msg->size);
        break;
    case VD_AGENT_CLIPBOARD_REQUEST:
        res = handle_clipboard_request((VDAgentClipboardRequest*)msg->data);
        if (!res) {
            VDAgentClipboard clipboard = {VD_AGENT_CLIPBOARD_NONE};
            res = write_message(VD_AGENT_CLIPBOARD, sizeof(clipboard), &clipboard);
        }
        break;
    case VD_AGENT_CLIPBOARD_RELEASE:
        handle_clipboard_release();
        break;
    case VD_AGENT_DISPLAY_CONFIG:
        res = handle_display_config((VDAgentDisplayConfig*)msg->data, port);
        break;
    case VD_AGENT_ANNOUNCE_CAPABILITIES:
        res = handle_announce_capabilities((VDAgentAnnounceCapabilities*)msg->data, msg->size);
        break;
    case VD_AGENT_FILE_XFER_START: {
        AgentFileXferStatusMessageFull status;
        size_t status_size = sizeof(status.common);
        if (_session_is_locked) {
            VDAgentFileXferStartMessage *s = (VDAgentFileXferStartMessage *)msg->data;
            status.common.id = s->id;
            status.common.result = VD_AGENT_FILE_XFER_STATUS_SESSION_LOCKED;
            vd_printf("Fail to start file-xfer %u due: Locked session", status.common.id);
            agent_prepare_filexfer_status(&status, &status_size,
                                          _client_caps.data(), _client_caps.size());
            write_message(VD_AGENT_FILE_XFER_STATUS, status_size, &status);
        } else if (_file_xfer.dispatch(msg, status, status_size)) {
            agent_prepare_filexfer_status(&status, &status_size,
                                          _client_caps.data(), _client_caps.size());
            write_message(VD_AGENT_FILE_XFER_STATUS, status_size, &status);
        }
        break;
    }
    case VD_AGENT_FILE_XFER_STATUS:
    case VD_AGENT_FILE_XFER_DATA: {
        AgentFileXferStatusMessageFull status;
        size_t status_size = sizeof(status.common);
        if (_file_xfer.dispatch(msg, status, status_size)) {
            agent_prepare_filexfer_status(&status, &status_size,
                                          _client_caps.data(), _client_caps.size());
            write_message(VD_AGENT_FILE_XFER_STATUS, status_size, &status);
        }
        break;
    }
    case VD_AGENT_CLIENT_DISCONNECTED:
        vd_printf("Client disconnected, resetting agent state");
        cleanup_in_msg();
        set_control_event(CONTROL_RESET);
        break;
    case VD_AGENT_MAX_CLIPBOARD:
        res = handle_max_clipboard((VDAgentMaxClipboard*)msg->data, msg->size);
        break;
    default:
        vd_printf("Unsupported message type %u size %u", msg->type, msg->size);
    }
    if (!res) {
        vd_printf("handling message type %u failed: %lu", msg->type, GetLastError());
        _running = false;
    }
}

VOID VDAgent::read_completion(DWORD err, DWORD bytes, LPOVERLAPPED overlapped)
{
    VDAgent* a = _singleton;
    VDIChunk* chunk = (VDIChunk*)a->_read_buf;
    DWORD count;

    if (err != 0 && err != ERROR_OPERATION_ABORTED && err != ERROR_NO_SYSTEM_RESOURCES) {
        vd_printf("vio_serial read completion error %lu", err);
        a->_running = false;
        return;
    }

    a->_read_pos += bytes;
    if (a->_read_pos < sizeof(VDIChunk)) {
        count = sizeof(VDIChunk) - a->_read_pos;
    } else if (a->_read_pos == sizeof(VDIChunk)) {
        count = chunk->hdr.size;
        if (count > sizeof(a->_read_buf) - a->_read_pos) {
            vd_printf("chunk is too large, size %u port %u", chunk->hdr.size, chunk->hdr.port);
            a->_running = false;
            return;
        }
    } else if (a->_read_pos == sizeof(VDIChunk) + chunk->hdr.size){
        a->handle_chunk(chunk);
        count = sizeof(VDIChunk);
        a->_read_pos = 0;
    } else {
        ASSERT(a->_read_pos < sizeof(VDIChunk) + chunk->hdr.size);
        count = sizeof(VDIChunk) + chunk->hdr.size - a->_read_pos;
    }

    if (!ReadFileEx(a->_vio_serial, a->_read_buf + a->_read_pos, count, overlapped,
                    read_completion) && GetLastError() != ERROR_IO_PENDING) {
        vd_printf("vio_serial read error %lu", GetLastError());
        a->_running = false;
    }
}

void VDAgent::handle_chunk(const VDIChunk* chunk)
{
    //FIXME: currently assumes that multi-part msg arrives only from client port
    if (_in_msg_pos == 0 || chunk->hdr.port == VDP_SERVER_PORT) {
        // ignore the chunk if too short
        if (chunk->hdr.size < sizeof(VDAgentMessage)) {
            return;
        }
        VDAgentMessage* msg = (VDAgentMessage*)chunk->data;
        if (msg->protocol != VD_AGENT_PROTOCOL) {
            vd_printf("Invalid protocol %u", msg->protocol);
            _running = false;
            return;
        }
        uint32_t msg_size = sizeof(VDAgentMessage) + msg->size;
        if (chunk->hdr.size == msg_size) {
            // we got an entire message, handle it
            dispatch_message(msg, chunk->hdr.port);
            return;
        }

        // got just the start, start to collapse all chunks into a
        // single buffer
        if (chunk->hdr.size >= msg_size) {
            vd_printf("Invalid VDAgentMessage message");
            _running = false;
            return;
        }
        _in_msg = (VDAgentMessage*)new uint8_t[msg_size];
        memcpy(_in_msg, chunk->data, chunk->hdr.size);
        _in_msg_pos = chunk->hdr.size;
        return;
    }

    // the previous chunk was a partial message, so append this chunk to the previous chunk
    if (chunk->hdr.size > sizeof(VDAgentMessage) + _in_msg->size - _in_msg_pos) {
        vd_printf("Invalid VDAgentMessage message");
        _running = false;
        return;
    }

    memcpy((uint8_t*)_in_msg + _in_msg_pos, chunk->data, chunk->hdr.size);
    _in_msg_pos += chunk->hdr.size;
    // update clipboard tick on each clipboard chunk for timeout setting
    if (_in_msg->type == VD_AGENT_CLIPBOARD && _clipboard_tick) {
        _clipboard_tick = GetTickCount();
    }
    if (_in_msg_pos == sizeof(VDAgentMessage) + _in_msg->size) {
        if (_in_msg->type == VD_AGENT_CLIPBOARD && !_clipboard_tick) {
            vd_printf("Clipboard received but dropped due to timeout");
        } else {
            dispatch_message(_in_msg, 0);
        }
        cleanup_in_msg();
    }
}

void VDAgent::cleanup_in_msg()
{
    _in_msg_pos = 0;
    delete[] (uint8_t *)_in_msg;
    _in_msg = NULL;
}

void VDAgent::write_completion(DWORD err, DWORD bytes, LPOVERLAPPED overlapped)
{
    VDAgent* a = _singleton;
    VDIChunk* chunk;
    DWORD count;

    ASSERT(!a->_message_queue.empty());
    if (err != 0) {
        vd_printf("vio_serial write completion error %lu", err);
        a->_running = false;
        return;
    }
    MutexLocker lock(a->_message_mutex);
    a->_write_pos += bytes;
    chunk = a->_message_queue.front();
    count = sizeof(VDIChunk) + chunk->hdr.size - a->_write_pos;
    if (count == 0) {
        a->_message_queue.pop();
        a->_write_pos = 0;
        delete chunk;
        if (!a->_message_queue.empty()) {
            chunk = a->_message_queue.front();
            count = sizeof(VDIChunk) + chunk->hdr.size;
        }
    }
    if (count) {
        if (!WriteFileEx(a->_vio_serial, (char*)chunk + a->_write_pos, count, overlapped,
                         write_completion) && GetLastError() != ERROR_IO_PENDING) {
            vd_printf("vio_serial write error %lu", GetLastError());
            a->_running = false;
        }
    }
}

VDIChunk* VDAgent::new_chunk(DWORD bytes)
{
    return (VDIChunk*)(new char[bytes]);
}

void VDAgent::enqueue_chunk(VDIChunk* chunk)
{
    MutexLocker lock(_message_mutex);
    _message_queue.push(chunk);
    if (_message_queue.size() == 1) {
        write_completion(0, 0, &_write_overlapped);
    }
}

LRESULT CALLBACK VDAgent::wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    VDAgent* a = _singleton;

    switch (message) {
    case WM_DISPLAYCHANGE:
        vd_printf("Display change");
        // the desktop layout needs to be updated for the mouse
        // position to be scaled correctly
        if (!a->_updating_display_config)
            a->_desktop_layout->get_displays();
        break;
    case WM_TIMER:
        a->send_input();
        break;
    case WM_CHANGECBCHAIN:
        if (a->_hwnd_next_viewer == (HWND)wparam) {
            a->_hwnd_next_viewer = (HWND)lparam;
        } else if (a->_hwnd_next_viewer) {
            SendMessage(a->_hwnd_next_viewer, message, wparam, lparam);
        }
        break;
    case WM_CLIPBOARDUPDATE:
    case WM_DRAWCLIPBOARD:
        if (a->_hwnd != GetClipboardOwner()) {
            a->set_clipboard_owner(owner_none);
            a->on_clipboard_grab();
        }
        if (a->_hwnd_next_viewer) {
            SendMessage(a->_hwnd_next_viewer, message, wparam, lparam);
        }
        break;
    case WM_RENDERFORMAT:
        a->on_clipboard_request((UINT)wparam);
        break;
    case WM_ENDSESSION:
        if (wparam) {
            vd_printf("Session ended");
            if (a->_clipboard_owner == owner_guest) {
                a->set_clipboard_owner(owner_none);
            }
            a->set_control_event(CONTROL_STOP);
        }
        break;
    case WM_WTSSESSION_CHANGE:
        if (wparam == WTS_SESSION_LOGON) {
            a->set_control_event(CONTROL_LOGON);
        } else if (wparam == WTS_SESSION_LOCK) {
            a->_session_is_locked = true;
        } else if (wparam == WTS_SESSION_UNLOCK) {
            a->_session_is_locked = false;
        }
        break;
    default:
        return DefWindowProc(hwnd, message, wparam, lparam);
    }
    return 0;
}

#ifdef __GNUC__
int main(int argc,char **argv)
#else
int APIENTRY _tWinMain(HINSTANCE instance, HINSTANCE prev_instance, LPTSTR cmd_line, int cmd_show)
#endif
{
    VDAgent* vdagent = VDAgent::get();
    vdagent->run();
    delete vdagent;
    return 0;
}

