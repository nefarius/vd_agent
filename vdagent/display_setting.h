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

#ifndef _H_DISPLAY_SETTING
#define _H_DISPLAY_SETTING

#include <string>

class DisplaySettingOptions {
public:
    DisplaySettingOptions()
        : _disable_wallpaper (false)
        , _disable_font_smoothing (false)
        , _disable_animation (false) {}

public:
    bool _disable_wallpaper;
    bool _disable_font_smoothing;
    bool _disable_animation;
};

class DisplaySetting {
public:
    DisplaySetting(const char* registry_key)
        : _reg_key (registry_key)
        , _fatal (false) {}
    void set(DisplaySettingOptions& opts);
    void load();
    bool fatal() const { return _fatal; }

private:
    bool disable_wallpaper();
    bool disable_font_smoothing();
    bool disable_animation();

    bool load(DisplaySettingOptions& opts);
    bool reload_from_registry(DisplaySettingOptions& opts);
    bool reload_wallpaper(HKEY desktop_reg_key);
    bool reload_font_smoothing(HKEY desktop_reg_key);
    bool reload_animation(HKEY desktop_reg_key);

    bool reload_win_animation(HKEY desktop_reg_key);
    bool reload_ui_effects(HKEY desktop_reg_key);

    bool set_bool_system_parameter_info(int action, BOOL param);
    DWORD get_user_process_id();

private:
    std::string _reg_key;
    bool _fatal;

};

#endif
