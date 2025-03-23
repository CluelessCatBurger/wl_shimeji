/*
    config.h - wl_shimeji's config file parser

    Copyright (C) 2025  CluelessCatBurger <github.com/CluelessCatBurger>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, see <https://www.gnu.org/licenses/>.
*/

#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define CONFIG_PARAM_BREEDING "BREEDING"
#define CONFIG_PARAM_DRAGGING "DRAGGING"
#define CONFIG_PARAM_IE_INTERACTIONS "WINDOW_INTERACTIONS"
#define CONFIG_PARAM_IE_THROWING "WINDOW_THROWING"
#define CONFIG_PARAM_CURSOR_DATA "CURSOR_POSITION"
#define CONFIG_PARAM_MASCOT_LIMIT "MASCOT_LIMIT"
#define CONFIG_PARAM_IE_THROW_POLICY "WINDOW_THROW_POLICY"
#define CONFIG_PARAM_ALLOW_DISMISS_ANIMATIONS "DISMISS_ANIMATIONS"
#define CONFIG_PARAM_PER_MASCOT_INTERACTIONS "AFFORDANCES"
#define CONFIG_PARAM_INTERPOLATION_FRAMERATE "INTERPOLATION_FRAMERATE"
#define CONFIG_PARAM_OVERLAY_LAYER "WLR_SHELL_LAYER"
#define CONFIG_PARAM_TABLETS_ENABLED "TABLETS_ENABLED"
#define CONFIG_PARAM_POINTER_LEFT_BUTTON "POINTER_LEFT_BUTTON"
#define CONFIG_PARAM_POINTER_RIGHT_BUTTON "POINTER_RIGHT_BUTTON"
#define CONFIG_PARAM_POINTER_MIDDLE_BUTTON "POINTER_MIDDLE_BUTTON"
#define CONFIG_PARAM_ON_TOOL_PEN "ON_TOOL_PEN"
#define CONFIG_PARAM_ON_TOOL_ERASER "ON_TOOL_ERASER"
#define CONFIG_PARAM_ON_TOOL_BRUSH "ON_TOOL_BRUSH"
#define CONFIG_PARAM_ON_TOOL_PENCIL "ON_TOOL_PENCIL"
#define CONFIG_PARAM_ON_TOOL_AIRBRUSH "ON_TOOL_AIRBRUSH"
#define CONFIG_PARAM_ON_TOOL_FINGER "ON_TOOL_FINGER"
#define CONFIG_PARAM_ON_TOOL_LENS "ON_TOOL_LENS"
#define CONFIG_PARAM_ON_TOOL_MOUSE "ON_TOOL_MOUSE"
#define CONFIG_PARAM_ON_TOOL_BUTTON1 "ON_TOOL_BUTTON1"
#define CONFIG_PARAM_ON_TOOL_BUTTON2 "ON_TOOL_BUTTON2"
#define CONFIG_PARAM_ON_TOOL_BUTTON3 "ON_TOOL_BUTTON3"
#define CONFIG_PARAM_ALLOW_THROWING_MULTIHEAD "ALLOW_THROWING_MULTIHEAD"
#define CONFIG_PARAM_ALLOW_DRAGGING_MULTIHEAD "ALLOW_DRAGGING_MULTIHEAD"
#define CONFIG_PARAM_UNIFIED_OUTPUTS "UNIFIED_OUTPUTS"
#define CONFIG_PARAM_COUNT 29

#define POINTER_PRIMARY_BUTTON 0x01
#define POINTER_SECONDARY_BUTTON 0x02
#define POINTER_THIRD_BUTTON 0x04

// Uses global variable
bool config_parse(const char* path);
void config_write(const char* path);

bool config_set_breeding(int32_t value);
bool config_set_dragging(int32_t value);
bool config_set_ie_interactions(int32_t value);
bool config_set_ie_throwing(int32_t value);
bool config_set_cursor_data(int32_t value);
bool config_set_mascot_limit(int32_t value);
bool config_set_ie_throw_policy(int32_t value);
bool config_set_allow_dismiss_animations(int32_t value);
bool config_set_per_mascot_interactions(int32_t value);
bool config_set_interpolation_framerate(int32_t value);
bool config_set_overlay_layer(int32_t value);
bool config_set_tablets_enabled(int32_t value);
bool config_set_pointer_left_button(int32_t value);
bool config_set_pointer_right_button(int32_t value);
bool config_set_pointer_middle_button(int32_t value);
bool config_set_on_tool_pen(int32_t value);
bool config_set_on_tool_eraser(int32_t value);
bool config_set_on_tool_brush(int32_t value);
bool config_set_on_tool_pencil(int32_t value);
bool config_set_on_tool_airbrush(int32_t value);
bool config_set_on_tool_finger(int32_t value);
bool config_set_on_tool_lens(int32_t value);
bool config_set_on_tool_mouse(int32_t value);
bool config_set_on_tool_button1(int32_t value);
bool config_set_on_tool_button2(int32_t value);
bool config_set_on_tool_button3(int32_t value);
bool config_set_allow_throwing_multihead(int32_t value);
bool config_set_allow_dragging_multihead(int32_t value);
bool config_set_unified_outputs(int32_t value);

int32_t config_get_breeding();
int32_t config_get_dragging();
int32_t config_get_ie_interactions();
int32_t config_get_ie_throwing();
int32_t config_get_cursor_data();
int32_t config_get_mascot_limit();
int32_t config_get_ie_throw_policy();
int32_t config_get_allow_dismiss_animations();
int32_t config_get_per_mascot_interactions();
int32_t config_get_interpolation_framerate();
int32_t config_get_overlay_layer();
int32_t config_get_tablets_enabled();
int32_t config_get_pointer_left_button();
int32_t config_get_pointer_right_button();
int32_t config_get_pointer_middle_button();
int32_t config_get_on_tool_pen();
int32_t config_get_on_tool_eraser();
int32_t config_get_on_tool_brush();
int32_t config_get_on_tool_pencil();
int32_t config_get_on_tool_airbrush();
int32_t config_get_on_tool_finger();
int32_t config_get_on_tool_lens();
int32_t config_get_on_tool_mouse();
int32_t config_get_on_tool_button1();
int32_t config_get_on_tool_button2();
int32_t config_get_on_tool_button3();
int32_t config_get_allow_throwing_multihead();
int32_t config_get_allow_dragging_multihead();
int32_t config_get_unified_outputs();

const char* config_get_prototypes_location();
const char* config_get_plugins_location();
const char* config_get_socket_location();

bool config_get_by_key(const char* key, char* dest, uint8_t size);
bool config_set_by_key(const char* key, const char* value);

__attribute((unused)) static const char* config_keys[] = {
    CONFIG_PARAM_BREEDING,
    CONFIG_PARAM_DRAGGING,
    CONFIG_PARAM_IE_INTERACTIONS,
    CONFIG_PARAM_IE_THROWING,
    CONFIG_PARAM_IE_THROW_POLICY,
    CONFIG_PARAM_CURSOR_DATA,
    CONFIG_PARAM_MASCOT_LIMIT,
    CONFIG_PARAM_ALLOW_THROWING_MULTIHEAD,
    CONFIG_PARAM_ALLOW_DRAGGING_MULTIHEAD,
    CONFIG_PARAM_UNIFIED_OUTPUTS,
    CONFIG_PARAM_ALLOW_DISMISS_ANIMATIONS,
    CONFIG_PARAM_PER_MASCOT_INTERACTIONS,
    CONFIG_PARAM_INTERPOLATION_FRAMERATE,
    CONFIG_PARAM_OVERLAY_LAYER,
    CONFIG_PARAM_TABLETS_ENABLED,
    CONFIG_PARAM_POINTER_LEFT_BUTTON,
    CONFIG_PARAM_POINTER_RIGHT_BUTTON,
    CONFIG_PARAM_POINTER_MIDDLE_BUTTON,
    CONFIG_PARAM_ON_TOOL_PEN,
    CONFIG_PARAM_ON_TOOL_ERASER,
    CONFIG_PARAM_ON_TOOL_BRUSH,
    CONFIG_PARAM_ON_TOOL_PENCIL,
    CONFIG_PARAM_ON_TOOL_AIRBRUSH,
    CONFIG_PARAM_ON_TOOL_FINGER,
    CONFIG_PARAM_ON_TOOL_LENS,
    CONFIG_PARAM_ON_TOOL_MOUSE,
    CONFIG_PARAM_ON_TOOL_BUTTON1,
    CONFIG_PARAM_ON_TOOL_BUTTON2,
    CONFIG_PARAM_ON_TOOL_BUTTON3,
    NULL
};

#endif
