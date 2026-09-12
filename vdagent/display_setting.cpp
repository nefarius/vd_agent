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
#include <windows.h>
#include <stdio.h>
#include <sddl.h>
#include <string.h>
#include <tlhelp32.h>
#include <spice/macros.h>
#include "display_setting.h"
#include "vdlog.h"

enum DisplaySettingFlags {
    DISPLAY_SETTING_FLAGS_DISABLE_WALLPAPER = (1 << 0),
    DISPLAY_SETTING_FLAGS_DISABLE_FONT_SMOOTH = (1 << 1),
    DISPLAY_SETTING_FLAGS_DISABLE_ANIMATION = (1 << 2),
};

#define DISPLAY_SETTING_MASK_REG_VALUE "DisplaySettingMask"
#define USER_DESKTOP_REGISTRY_KEY "Control Panel\\Desktop"

void DisplaySetting::set(DisplaySettingOptions& opts)
{
    HKEY hkey;
    DWORD dispos;
    LSTATUS status;
    BYTE reg_mask = 0;

    vd_printf("setting display options");

    if (opts._disable_wallpaper) {
        reg_mask |= DISPLAY_SETTING_FLAGS_DISABLE_WALLPAPER;
    }

    if (opts._disable_font_smoothing) {
        reg_mask |= DISPLAY_SETTING_FLAGS_DISABLE_FONT_SMOOTH;
    }

    if (opts._disable_animation) {
        reg_mask |= DISPLAY_SETTING_FLAGS_DISABLE_ANIMATION;
    }

    status = RegCreateKeyExA(HKEY_LOCAL_MACHINE, _reg_key.c_str(), 0, NULL,
                             REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hkey, &dispos);
    if (status != ERROR_SUCCESS) {
        vd_printf("create/open registry key: fail %ld", status);
    } else {
        status = RegSetValueExA(hkey, DISPLAY_SETTING_MASK_REG_VALUE, 0,
                                REG_BINARY, &reg_mask, sizeof(reg_mask));
        if (status != ERROR_SUCCESS) {
            vd_printf("setting registry key DisplaySettingMask: fail %ld", status);
        }
        RegCloseKey(hkey);
    }

    load(opts);
}

void DisplaySetting::load()
{
    LSTATUS status;
    HKEY hkey;
    DWORD value_type;
    DWORD value_size;
    BYTE setting_mask;
    DisplaySettingOptions display_opts;

    vd_printf("loading display setting");

    status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, _reg_key.c_str(), 0, KEY_READ, &hkey);
    if (status != ERROR_SUCCESS) {
        vd_printf("open registry key: fail %lu", status);
        return;
    }

    value_size = sizeof(setting_mask);
    status = RegQueryValueExA(hkey, DISPLAY_SETTING_MASK_REG_VALUE, NULL,
                              &value_type, &setting_mask, &value_size);

    if (status != ERROR_SUCCESS) {
        vd_printf("get registry mask value: fail %ld", status);
        RegCloseKey(hkey);
        return;
    }

    RegCloseKey(hkey);

    if (value_type != REG_BINARY) {
        vd_printf("get registry mask value: bad value type %lu", value_type);
        return;
    }

    if (setting_mask & DISPLAY_SETTING_FLAGS_DISABLE_WALLPAPER) {
        display_opts._disable_wallpaper = true;
    }

    if (setting_mask & DISPLAY_SETTING_FLAGS_DISABLE_FONT_SMOOTH) {
        display_opts._disable_font_smoothing = true;
    }

    if (setting_mask & DISPLAY_SETTING_FLAGS_DISABLE_ANIMATION) {
        display_opts._disable_animation = true;
    }

    load(display_opts);
}

// returns 0 if failes
DWORD DisplaySetting::get_user_process_id()
{
    PROCESSENTRY32 proc_entry;
    DWORD explorer_pid = 0;
    DWORD agent_session_id;

    if (!ProcessIdToSessionId(GetCurrentProcessId(), &agent_session_id)) {
        vd_printf("ProcessIdToSessionId for current process failed %lu", GetLastError());
        return 0;
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        vd_printf("CreateToolhelp32Snapshot() failed %lu", GetLastError());
        return 0;
    }
    ZeroMemory(&proc_entry, sizeof(proc_entry));
    proc_entry.dwSize = sizeof(PROCESSENTRY32);
    if (!Process32First(snap, &proc_entry)) {
        vd_printf("Process32First() failed %lu", GetLastError());
        CloseHandle(snap);
        return 0;
    }
    do {
        if (_tcsicmp(proc_entry.szExeFile, TEXT("explorer.exe")) == 0) {
            DWORD explorer_session_id;
            if (!ProcessIdToSessionId(proc_entry.th32ProcessID, &explorer_session_id)) {
                vd_printf("ProcessIdToSessionId for explorer failed %lu", GetLastError());
                break;
            }

            if (explorer_session_id == agent_session_id) {
                explorer_pid = proc_entry.th32ProcessID;
                break;
            }
        }
    } while (Process32Next(snap, &proc_entry));

    CloseHandle(snap);
    if (explorer_pid == 0) {
        vd_printf("explorer.exe not found");
        return 0;
    }
    return explorer_pid;
}

bool DisplaySetting::load(DisplaySettingOptions& opts)
{
    bool need_reload = false;
    bool ret = true;

    if (opts._disable_wallpaper) {
        ret &= disable_wallpaper();
    } else {
        need_reload = true;
    }

    if (opts._disable_font_smoothing) {
        ret &= disable_font_smoothing();
    } else {
        need_reload = true;
    }

    if (opts._disable_animation) {
        ret &= disable_animation();
    } else {
        need_reload = true;
    }

    if (need_reload) {
        ret &= reload_from_registry(opts);
    }

    return ret;
}

bool DisplaySetting::reload_from_registry(DisplaySettingOptions& opts)
{
    DWORD user_pid;
    HANDLE hprocess = NULL;
    HANDLE htoken = NULL;
    HKEY hkey_cur_user = NULL;
    HKEY hkey_desktop = NULL;
    LONG status;
    bool impersonated = false;
    bool ret = false;

    user_pid = get_user_process_id();

    if (!user_pid) {
        vd_printf("get_user_process_id failed");
        return false;
    }

    vd_printf("explorer pid %ld", user_pid);

    hprocess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, user_pid);
    if (!hprocess) {
        vd_printf("OpenProcess: failed %lu", GetLastError());
        return false;
    }

    if (!OpenProcessToken(hprocess, TOKEN_ALL_ACCESS, &htoken)) {
        vd_printf("OpenProcessToken: failed %lu", GetLastError());
        CloseHandle(hprocess);
        return false;
    }

    if (!ImpersonateLoggedOnUser(htoken)) {
        vd_printf("ImpersonateLoggedOnUser: failed %lu", GetLastError());
        goto cleanup;
    }
    impersonated = true;

    status = RegOpenCurrentUser(KEY_READ, &hkey_cur_user);
    if (status != ERROR_SUCCESS) {
        vd_printf("RegOpenCurrentUser: failed %ld", status);
        goto cleanup;
    }

    status = RegOpenKeyExA(hkey_cur_user, USER_DESKTOP_REGISTRY_KEY, 0,
                           KEY_READ, &hkey_desktop);
    if (status != ERROR_SUCCESS) {
        vd_printf("RegOpenKeyExA: failed %ld", status);
        goto cleanup;
    }

    ret = true;
    if (!opts._disable_wallpaper) {
        ret &= reload_wallpaper(hkey_desktop);
    }

    if (!opts._disable_font_smoothing) {
        ret &= reload_font_smoothing(hkey_desktop);
    }

    if (!opts._disable_animation) {
        ret &= reload_animation(hkey_desktop);
    }

cleanup:
    if (hkey_desktop) {
        RegCloseKey(hkey_desktop);
    }
    if (hkey_cur_user) {
        RegCloseKey(hkey_cur_user);
    }
    if (impersonated) {
        if (!RevertToSelf()) {
            DWORD err = GetLastError();
            vd_printf("RevertToSelf: failed %lu", err);
            _fatal = true;
        }
    }
    CloseHandle(htoken);
    CloseHandle(hprocess);
    return ret;
}

bool DisplaySetting::disable_wallpaper()
{
    if (SystemParametersInfoA(SPI_SETDESKWALLPAPER, 0, (void*)"", 0)) {
        vd_printf("disable wallpaper: success");
        return true;
    } else {
        vd_printf("disable wallpaper: fail %lu", GetLastError());
        return false;
    }
}

#if defined(UNICODE) || defined(_UNICODE)
#define PRIsTSTR "ls"
#else
#define PRIsTSTR "s"
#endif

static bool RegReadString(HKEY key, const TCHAR *name, TCHAR *buffer, size_t buffer_len)
{
    DWORD value_size = (DWORD) (buffer_len * sizeof(buffer[0]));
    DWORD value_type;
    LONG status;

    status = RegQueryValueEx(key, name, NULL, &value_type, (LPBYTE)buffer, &value_size);
    if (status == ERROR_SUCCESS && value_type == REG_SZ) {
        // ensure NUL-terminated
        value_size /= sizeof(buffer[0]);
        if (value_size == buffer_len) {
            // full buffer but not terminated?
            if (buffer[value_size-1] != '\0') {
                status = ERROR_MORE_DATA;
            }
        } else {
            // append a NUL. If there's already a NUL character this
            // new one will be ignored
            buffer[value_size] = '\0';
        }
    }
    if (status != ERROR_SUCCESS) {
        vd_printf("RegQueryValueEx(%" PRIsTSTR ") : fail %ld", name, status);
        return false;
    }

    if (value_type != REG_SZ) {
        vd_printf("bad %" PRIsTSTR " value type %lu (expected REG_SZ)", name, value_type);
        return false;
    }

    return true;
}

template <size_t N>
static inline bool RegReadString(HKEY key, const TCHAR *name, TCHAR (&buffer)[N])
{
    return RegReadString(key, name, buffer, N);
}

bool DisplaySetting::reload_wallpaper(HKEY desktop_reg_key)
{
    TCHAR wallpaper_path[MAX_PATH + 1];
    TCHAR cur_wallpaper[MAX_PATH + 1];

    vd_printf("");
    if (!RegReadString(desktop_reg_key, TEXT("Wallpaper"), wallpaper_path)) {
        return false;
    }

    if (SystemParametersInfo(SPI_GETDESKWALLPAPER, SPICE_N_ELEMENTS(cur_wallpaper), cur_wallpaper, 0)) {
        if (_tcscmp(cur_wallpaper, TEXT("")) != 0) {
            vd_printf("wallpaper wasn't disabled");
            return true;
        }
    } else {
        vd_printf("SPI_GETDESKWALLPAPER failed");
    }

    if (SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, wallpaper_path, 0)) {
        vd_printf("reload wallpaper: success");
        return true;
    } else {
        vd_printf("reload wallpaper: failed %lu", GetLastError());
        return false;
    }
}

bool DisplaySetting::disable_font_smoothing()
{
    if (SystemParametersInfoA(SPI_SETFONTSMOOTHING, FALSE, NULL, 0)) {
        vd_printf("disable font smoothing: success");
        return true;
    } else {
        vd_printf("disable font smoothing: fail %lu", GetLastError());
        return false;
    }
}

bool DisplaySetting::reload_font_smoothing(HKEY desktop_reg_key)
{
    TCHAR smooth_value[4];
    BOOL cur_font_smooth;

    vd_printf("");
    if (!RegReadString(desktop_reg_key, TEXT("FontSmoothing"), smooth_value)) {
        return false;
    }

    if (_tcscmp(smooth_value, TEXT("0")) == 0) {
        vd_printf("font smoothing is disabled in registry. do nothing");
        return true;
    } else if (_tcscmp(smooth_value, TEXT("2")) != 0) {
        vd_printf("unexpectd font smoothing value %ls", smooth_value);
        return false;
    }

    if (SystemParametersInfo(SPI_GETFONTSMOOTHING, 0, &cur_font_smooth, 0)) {
        if (cur_font_smooth)  {
            vd_printf("font smoothing value didn't change");
            return true;
        }
    } else {
        vd_printf("SPI_GETFONTSMOOTHING failed");
    }

    if (SystemParametersInfo(SPI_SETFONTSMOOTHING, TRUE, NULL, 0)) {
        vd_printf("reload font smoothing: success");
        return true;
    } else {
        vd_printf("reload font smoothing: failed %lu", GetLastError());
        return false;
    }
}

bool DisplaySetting::disable_animation()
{
    ANIMATIONINFO win_animation;
    bool ret = true;

    ret &= set_bool_system_parameter_info(SPI_SETUIEFFECTS, FALSE);

    win_animation.cbSize = sizeof(ANIMATIONINFO);
    win_animation.iMinAnimate = 0;

    if (SystemParametersInfoA(SPI_SETANIMATION, sizeof(ANIMATIONINFO),
                              &win_animation, 0)) {
        vd_printf("disable window animation: success");
    } else {
        vd_printf("disable window animation: fail %lu", GetLastError());
        ret = false;
    }

    return ret;
}

bool DisplaySetting::reload_win_animation(HKEY desktop_reg_key)
{
    HKEY win_metrics_hkey;
    TCHAR win_anim_value[4];
    LONG status;
    ANIMATIONINFO active_win_animation;

    vd_printf("");

    status = RegOpenKeyExA(desktop_reg_key, "WindowMetrics", 0,
                           KEY_READ, &win_metrics_hkey);
    if (status != ERROR_SUCCESS) {
        vd_printf("RegOpenKeyExA(WindowMetrics) : fail %ld", status);
        return false;
    }

    if (!RegReadString(win_metrics_hkey, TEXT("MinAnimate"), win_anim_value)) {
        RegCloseKey(win_metrics_hkey);
        return false;
    }

    RegCloseKey(win_metrics_hkey);

    if (!_tcscmp(win_anim_value, TEXT("0"))) {
        vd_printf("window animation is disabled in registry. do nothing");
        return true;
    }  else if (_tcscmp(win_anim_value, TEXT("1")) != 0) {
        vd_printf("unexpectd window animation value %ls", win_anim_value);
        return false;
    }
    active_win_animation.cbSize = sizeof(ANIMATIONINFO);
    active_win_animation.iMinAnimate = 1;

    if (SystemParametersInfoA(SPI_SETANIMATION, sizeof(ANIMATIONINFO),
                              &active_win_animation, 0)) {
        vd_printf("reload window animation: success");
        return true;
    } else {
        vd_printf("reload window animation: fail %lu", GetLastError());
        return false;
    }
}

bool DisplaySetting::set_bool_system_parameter_info(int action, BOOL param)
{
    if (!SystemParametersInfo(action, 0, (PVOID)(uintptr_t)param, 0)) {
        vd_printf("SystemParametersInfo %d: failed %lu", action, GetLastError());
        return false;
    }
    return true;
}

bool DisplaySetting::reload_ui_effects(HKEY desktop_reg_key)
{
    DWORD ui_mask[2]; // one DWORD in xp, two DWORD in Windows 7
    DWORD value_size = sizeof(ui_mask);
    DWORD value_type;
    LONG status;
    bool ret = true;
    vd_printf("");
    status = RegQueryValueExA(desktop_reg_key, "UserPreferencesMask", NULL,
                              &value_type, (LPBYTE)&ui_mask, &value_size);
    if (status != ERROR_SUCCESS) {
        vd_printf("RegQueryValueEx(UserPreferencesMask) : fail %ld", status);
        return false;
    }

    if (value_type != REG_BINARY) {
        vd_printf("bad UserPreferencesMask value type %lu (expected REG_BINARY)", value_type);
        return false;
    }

    vd_printf("UserPreferencesMask = %lx %lx", ui_mask[0], ui_mask[1]);

    ret &= set_bool_system_parameter_info(SPI_SETUIEFFECTS, ui_mask[0] & 0x80000000);
    ret &= set_bool_system_parameter_info(SPI_SETACTIVEWINDOWTRACKING, ui_mask[0] & 0x01);
    ret &= set_bool_system_parameter_info(SPI_SETMENUANIMATION, ui_mask[0] & 0x02);
    ret &= set_bool_system_parameter_info(SPI_SETCOMBOBOXANIMATION, ui_mask[0] & 0x04);
    ret &= set_bool_system_parameter_info(SPI_SETLISTBOXSMOOTHSCROLLING, ui_mask[0] & 0x08);
    ret &= set_bool_system_parameter_info(SPI_SETGRADIENTCAPTIONS, ui_mask[0] & 0x10);
    ret &= set_bool_system_parameter_info(SPI_SETKEYBOARDCUES, ui_mask[0] & 0x20);
    ret &= set_bool_system_parameter_info(SPI_SETACTIVEWNDTRKZORDER, ui_mask[0] & 0x40);
    ret &= set_bool_system_parameter_info(SPI_SETHOTTRACKING, ui_mask[0] & 0x80);

    ret &= set_bool_system_parameter_info(SPI_SETMENUFADE, ui_mask[0] & 0x200);
    ret &= set_bool_system_parameter_info(SPI_SETSELECTIONFADE, ui_mask[0] & 0x400);
    ret &= set_bool_system_parameter_info(SPI_SETTOOLTIPANIMATION, ui_mask[0] & 0x800);
    ret &= set_bool_system_parameter_info(SPI_SETTOOLTIPFADE, ui_mask[0] & 0x1000);
    ret &= set_bool_system_parameter_info(SPI_SETCURSORSHADOW, ui_mask[0] & 0x2000);

    return true;
}

bool DisplaySetting::reload_animation(HKEY desktop_reg_key)
{
    bool ret = true;
    vd_printf("");
    ret &= reload_win_animation(desktop_reg_key);
    ret &= reload_ui_effects(desktop_reg_key);
    return ret;
}
