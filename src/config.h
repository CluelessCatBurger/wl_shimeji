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

#define CONFIG_PARAM_BREEDING_ID 0
#define CONFIG_PARAM_DRAGGING_ID 1
#define CONFIG_PARAM_IE_INTERACTIONS_ID 2
#define CONFIG_PARAM_IE_THROWING_ID 3
#define CONFIG_PARAM_CURSOR_DATA_ID 4
#define CONFIG_PARAM_MASCOT_LIMIT_ID 5
#define CONFIG_PARAM_IE_THROW_POLICY_ID 6
#define CONFIG_PARAM_ALLOW_DISMISS_ANIMATIONS_ID 7
#define CONFIG_PARAM_PER_MASCOT_INTERACTIONS_ID 8
#define CONFIG_PARAM_INTERPOLATION_FRAMERATE_ID 9
#define CONFIG_PARAM_OVERLAY_LAYER 10
#define CONFIG_PARAM_TABLETS_ENABLED 11
#define CONFIG_PARAM_POINTER_LEFT_BUTTON 12
#define CONFIG_PARAM_POINTER_RIGHT_BUTTON 13
#define CONFIG_PARAM_POINTER_MIDDLE_BUTTON 14
#define CONFIG_PARAM_ON_TOOL_PEN 15
#define CONFIG_PARAM_ON_TOOL_ERASER 16
#define CONFIG_PARAM_ON_TOOL_BRUSH 17
#define CONFIG_PARAM_ON_TOOL_PENCIL 18
#define CONFIG_PARAM_ON_TOOL_AIRBRUSH 19
#define CONFIG_PARAM_ON_TOOL_FINGER 20
#define CONFIG_PARAM_ON_TOOL_LENS 21
#define CONFIG_PARAM_ON_TOOL_MOUSE 22
#define CONFIG_PARAM_ON_TOOL_BUTTON1 23
#define CONFIG_PARAM_ON_TOOL_BUTTON2 24
#define CONFIG_PARAM_ON_TOOL_BUTTON3 25
#define CONFIG_PARAM_ALLOW_THROWING_MULTIHEAD 26
#define CONFIG_PARAM_ALLOW_DRAGGING_MULTIHEAD 27
#define CONFIG_PARAM_UNIFIED_OUTPUTS 28
#define CONFIG_PARAM_COUNT 29

#define POINTER_PRIMARY_BUTTON 0x01
#define POINTER_SECONDARY_BUTTON 0x02
#define POINTER_THIRD_BUTTON 0x04

struct config {
    bool breeding;
    bool dragging;
    bool ie_interactions;
    bool ie_throwing;
    bool cursor_data;
    bool dismiss_animations;
    bool affordances;
    int32_t overlay_layer;

    int32_t framerate;

    uint32_t mascot_limit;
    int32_t ie_throw_policy;

    // Button mappings
    int32_t pointer_left_value;
    int32_t pointer_right_value;
    int32_t pointer_middle_value;

    // Tablets config
    bool enable_tablets;

    // Mappings for tool up/down events
    int32_t on_tool_pen_value;
    int32_t on_tool_eraser_value;
    int32_t on_tool_brush_value;
    int32_t on_tool_pencil_value;
    int32_t on_tool_airbrush_value;
    int32_t on_tool_finger_value;
    int32_t on_tool_lens_value;
    int32_t on_tool_mouse_value;

    // Mappings for tool button events
    int32_t on_tool_button1_value;
    int32_t on_tool_button2_value;
    int32_t on_tool_button3_value;

    // Multi-head support
    int32_t allow_throwing_multihead;
    int32_t allow_dragging_multihead;
    int32_t unified_outputs;
};

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

typedef bool (*config_param_setter)(int32_t value);
typedef int32_t (*config_param_getter)();

bool config_setter_table(uint32_t key, int32_t value);
int32_t config_getter_table(uint32_t key);

#endif
