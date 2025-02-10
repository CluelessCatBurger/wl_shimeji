/*
    config.c - wl_shimeji's config file parser

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

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "layer_surface.h"
#include "master_header.h"

struct config config = {0};

bool config_parse(const char* path)
{

    config.breeding = true;
    config.dragging = true;
    config.ie_interactions = false;
    config.ie_throwing = false;
    config.cursor_data = true;
    config.dismiss_animations = true;
    config.affordances = true;
    config.overlay_layer = LAYER_TYPE_OVERLAY;
    config.mascot_limit = 512;
    config.ie_throw_policy = 3;
    config.pointer_left_value = -1;
    config.pointer_right_value = -1;
    config.pointer_middle_value = -1;
    config.enable_tablets = true;
    config.on_tool_pen_value = -1;
    config.on_tool_eraser_value = -1;
    config.on_tool_brush_value = -1;
    config.on_tool_pencil_value = -1;
    config.on_tool_airbrush_value = -1;
    config.on_tool_finger_value = -1;
    config.on_tool_lens_value = -1;
    config.on_tool_mouse_value = -1;
    config.on_tool_button1_value = -1;
    config.on_tool_button2_value = -1;
    config.on_tool_button3_value = -1;
    config.allow_dragging_multihead = -1;
    config.allow_throwing_multihead = -1;
    config.unified_outputs = -1;


    FILE* file = fopen(path, "r");
    if (!file) {
        return false;
    }

    char line[256] = {0};
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#') {
            continue;
        }

        char* key = strtok(line, "=");
        char* value = strtok(NULL, "=");

        if (!key || !value) {
            continue;
        }

        if (strcmp(key, "breeding") == 0) {
            config_set_breeding(strncmp(value, "true", 4) == 0);
        } else if (strcmp(key, "dragging") == 0) {
            config_set_dragging(strncmp(value, "true", 4) == 0);
        } else if (strcmp(key, "ie_interactions") == 0) {
            config_set_ie_interactions(strncmp(value, "true", 4) == 0);
        } else if (strcmp(key, "ie_throwing") == 0) {
            config_set_ie_throwing(strncmp(value, "true", 4) == 0);
        } else if (strcmp(key, "cursor_data") == 0) {
            config_set_cursor_data(strncmp(value, "true", 4) == 0);
        } else if (strcmp(key, "mascot_limit") == 0) {
            config_set_mascot_limit(atoi(value));
        } else if (strcmp(key, "ie_throw_policy") == 0) {
            config_set_ie_throw_policy(atoi(value));
        } else if (strcmp(key, "allow_dismiss_animations") == 0) {
            config_set_allow_dismiss_animations(strncmp(value, "true", 4) == 0);
        } else if (strcmp(key, "per_mascot_interactions") == 0) {
            config_set_per_mascot_interactions(strncmp(value, "true", 4) == 0);
        } else if (strcmp(key, "interpolation_framerate") == 0) {
            config_set_framerate(atoi(value));
        } else if (strcmp(key, "overlay_layer") == 0) {
            config_set_overlay_layer(atoi(value));
        } else if (strcmp(key, "tablets_enabled") == 0) {
            config_set_tablets_enabled(strncmp(value, "true", 4) == 0);
        } else if (strcmp(key, "pointer_left_value") == 0) {
            config_set_pointer_left_button(atoi(value));
        } else if (strcmp(key, "pointer_right_value") == 0) {
            config_set_pointer_right_button(atoi(value));
        } else if (strcmp(key, "pointer_middle_value") == 0) {
            config_set_pointer_middle_button(atoi(value));
        } else if (strcmp(key, "on_tool_pen_value") == 0) {
            config_set_on_tool_pen(atoi(value));
        } else if (strcmp(key, "on_tool_eraser_value") == 0) {
            config_set_on_tool_eraser(atoi(value));
        } else if (strcmp(key, "on_tool_brush_value") == 0) {
            config_set_on_tool_brush(atoi(value));
        } else if (strcmp(key, "on_tool_pencil_value") == 0) {
            config_set_on_tool_pencil(atoi(value));
        } else if (strcmp(key, "on_tool_airbrush_value") == 0) {
            config_set_on_tool_airbrush(atoi(value));
        } else if (strcmp(key, "on_tool_finger_value") == 0) {
            config_set_on_tool_finger(atoi(value));
        } else if (strcmp(key, "on_tool_lens_value") == 0) {
            config_set_on_tool_lens(atoi(value));
        } else if (strcmp(key, "on_tool_mouse_value") == 0) {
            config_set_on_tool_mouse(atoi(value));
        } else if (strcmp(key, "on_tool_button1_value") == 0) {
            config_set_on_tool_button1(atoi(value));
        } else if (strcmp(key, "on_tool_button2_value") == 0) {
            config_set_on_tool_button2(atoi(value));
        } else if (strcmp(key, "on_tool_button3_value") == 0) {
            config_set_on_tool_button3(atoi(value));
        } else if (strcmp(key, "allow_throwing_multihead") == 0) {
            config_set_allow_throwing_multihead(strncmp(value, "true", 4) == 0);
        } else if (strcmp(key, "allow_dragging_multihead") == 0) {
            config_set_allow_dragging_multihead(strncmp(value, "true", 4) == 0);
        } else if (strcmp(key, "unified_outputs") == 0) {
            config_set_unified_outputs(strncmp(value, "true", 4) == 0);
        }
    }

    fclose(file);
    return true;
}

void config_write(const char* path)
{
    FILE* file = fopen(path, "w");
    if (!file) {
        return;
    }

    fprintf(file, "breeding=%s\n", config.breeding ? "true" : "false");
    fprintf(file, "dragging=%s\n", config.dragging ? "true" : "false");
    fprintf(file, "ie_interactions=%s\n", config.ie_interactions ? "true" : "false");
    fprintf(file, "ie_throwing=%s\n", config.ie_throwing ? "true" : "false");
    fprintf(file, "cursor_data=%s\n", config.cursor_data ? "true" : "false");
    fprintf(file, "mascot_limit=%u\n", config.mascot_limit);
    fprintf(file, "ie_throw_policy=%d\n", config.ie_throw_policy);
    fprintf(file, "allow_dismiss_animations=%s\n", config.dismiss_animations ? "true" : "false");
    fprintf(file, "per_mascot_interactions=%s\n", config.affordances ? "true" : "false");
    if (config.framerate) fprintf(file, "interpolation_framerate=%d\n", config.framerate);
    fprintf(file, "overlay_layer=%d\n", config.overlay_layer);
    fprintf(file, "tablets_enabled=%s\n", config.enable_tablets ? "true" : "false");
    if (config.pointer_left_value != -1) fprintf(file, "pointer_left_value=%d\n", config.pointer_left_value);
    if (config.pointer_right_value != -1) fprintf(file, "pointer_right_value=%d\n", config.pointer_right_value);
    if (config.pointer_middle_value != -1) fprintf(file, "pointer_middle_value=%d\n", config.pointer_middle_value);
    if (config.on_tool_pen_value != -1) fprintf(file, "on_tool_pen_value=%d\n", config.on_tool_pen_value);
    if (config.on_tool_eraser_value != -1) fprintf(file, "on_tool_eraser_value=%d\n", config.on_tool_eraser_value);
    if (config.on_tool_brush_value != -1) fprintf(file, "on_tool_brush_value=%d\n", config.on_tool_brush_value);
    if (config.on_tool_pencil_value != -1) fprintf(file, "on_tool_pencil_value=%d\n", config.on_tool_pencil_value);
    if (config.on_tool_airbrush_value != -1) fprintf(file, "on_tool_airbrush_value=%d\n", config.on_tool_airbrush_value);
    if (config.on_tool_finger_value != -1) fprintf(file, "on_tool_finger_value=%d\n", config.on_tool_finger_value);
    if (config.on_tool_lens_value != -1) fprintf(file, "on_tool_lens_value=%d\n", config.on_tool_lens_value);
    if (config.on_tool_mouse_value != -1) fprintf(file, "on_tool_mouse_value=%d\n", config.on_tool_mouse_value);
    if (config.on_tool_button1_value != -1) fprintf(file, "on_tool_button1_value=%d\n", config.on_tool_button1_value);
    if (config.on_tool_button2_value != -1) fprintf(file, "on_tool_button2_value=%d\n", config.on_tool_button2_value);
    if (config.on_tool_button3_value != -1) fprintf(file, "on_tool_button3_value=%d\n", config.on_tool_button3_value);
    if (config.allow_throwing_multihead != -1) fprintf(file, "allow_throwing_multihead=%s\n", config.allow_throwing_multihead ? "true" : "false");
    if (config.allow_dragging_multihead != -1) fprintf(file, "allow_dragging_multihead=%s\n", config.allow_dragging_multihead ? "true" : "false");
    if (config.unified_outputs != -1) fprintf(file, "unified_outputs=%s\n", config.unified_outputs ? "true" : "false");

    fclose(file);
}

bool config_set_breeding(bool value)
{
    config.breeding = value;
    return true;
}

bool config_set_dragging(bool value)
{
    config.dragging = value;
    return true;
}

bool config_set_ie_interactions(bool value)
{
    config.ie_interactions = value;
    return true;
}

bool config_set_ie_throwing(bool value)
{
    config.ie_throwing = value;
    return true;
}

bool config_set_cursor_data(bool value)
{
    config.cursor_data = value;
    return true;
}

bool config_set_mascot_limit(uint32_t value)
{
    config.mascot_limit = value;
    return true;
}

bool config_set_ie_throw_policy(int32_t value)
{
    config.ie_throw_policy = value;
    return true;
}

bool config_set_allow_dismiss_animations(bool value)
{
    config.dismiss_animations = value;
    return true;
}

bool config_set_per_mascot_interactions(bool value)
{
    config.affordances = value;
    return true;
}

bool config_set_framerate(int32_t value)
{
    config.framerate = value;
    return true;
}

bool config_set_overlay_layer(int32_t value)
{
    config.overlay_layer = value;
    return true;
}

bool config_get_breeding()
{
    return config.breeding;
}

bool config_get_dragging()
{
    return config.dragging;
}

bool config_get_ie_interactions()
{
    return config.ie_interactions;
}

bool config_get_ie_throwing()
{
    return config.ie_throwing;
}

bool config_get_cursor_data()
{
    return config.cursor_data;
}

uint32_t config_get_mascot_limit()
{
    return config.mascot_limit;
}

int32_t config_get_ie_throw_policy()
{
    return config.ie_throw_policy;
}

bool config_get_allow_dismiss_animations()
{
    return config.dismiss_animations;
}

bool config_get_per_mascot_interactions()
{
    return config.affordances;
}

int32_t config_get_framerate()
{
    return config.framerate;
}

int32_t config_get_overlay_layer()
{
    if (!config.overlay_layer) {
        return LAYER_TYPE_OVERLAY;
    }
    return config.overlay_layer;
}

bool config_get_tablets_enabled()
{
    return config.enable_tablets;
}

uint32_t config_get_pointer_left_button()
{
    if (config.pointer_left_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.pointer_left_value;
}

uint32_t config_get_pointer_right_button()
{
    if (config.pointer_right_value == -1) return POINTER_SECONDARY_BUTTON;
    return config.pointer_right_value;
}

uint32_t config_get_pointer_middle_button()
{
    if (config.pointer_middle_value == -1) return POINTER_THIRD_BUTTON;
    return config.pointer_middle_value;
}

uint32_t config_get_on_tool_pen()
{
    if (config.on_tool_pen_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_pen_value;
}

uint32_t config_get_on_tool_eraser()
{
    if (config.on_tool_eraser_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_eraser_value;
}

uint32_t config_get_on_tool_brush()
{
    if (config.on_tool_brush_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_brush_value;
}

uint32_t config_get_on_tool_pencil()
{
    if (config.on_tool_pencil_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_pencil_value;
}

uint32_t config_get_on_tool_airbrush()
{
    if (config.on_tool_airbrush_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_airbrush_value;
}

uint32_t config_get_on_tool_finger()
{
    if (config.on_tool_finger_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_finger_value;
}

uint32_t config_get_on_tool_lens()
{
    if (config.on_tool_lens_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_lens_value;
}

uint32_t config_get_on_tool_mouse()
{
    if (config.on_tool_mouse_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_mouse_value;
}

uint32_t config_get_on_tool_button1()
{
    if (config.on_tool_button1_value == -1) return POINTER_SECONDARY_BUTTON;
    return config.on_tool_button1_value;
}

uint32_t config_get_on_tool_button2()
{
    if (config.on_tool_button2_value == -1) return POINTER_SECONDARY_BUTTON;
    return config.on_tool_button2_value;
}

uint32_t config_get_on_tool_button3()
{
    if (config.on_tool_button3_value == -1) return POINTER_SECONDARY_BUTTON;
    return config.on_tool_button3_value;
}


bool config_set_tablets_enabled(bool value)
{
    config.enable_tablets = value;
    return true;
}

bool config_set_pointer_left_button(int32_t value)
{
    if (value == -1) value = POINTER_PRIMARY_BUTTON;
    if ((uint32_t)value > POINTER_THIRD_BUTTON) {
        return false;
    }
    config.pointer_left_value = value;
    return true;
}

bool config_set_pointer_right_button(int32_t value)
{
    if (value == -1) value = POINTER_SECONDARY_BUTTON;
    if ((uint32_t)value > POINTER_THIRD_BUTTON) {
        return false;
    }
    config.pointer_right_value = value;
    return true;
}

bool config_set_pointer_middle_button(int32_t value)
{
    if (value == -1) value = POINTER_THIRD_BUTTON;
    if ((uint32_t)value > POINTER_THIRD_BUTTON) {
        return false;
    }
    config.pointer_middle_value = value;
    return true;
}

bool config_set_on_tool_pen(int32_t value)
{
    if (value == -1) value = POINTER_PRIMARY_BUTTON;
    if ((uint32_t)value > POINTER_THIRD_BUTTON) {
        return false;
    }
    config.on_tool_pen_value = value;
    return true;
}

bool config_set_on_tool_eraser(int32_t value)
{
    if (value == -1) value = POINTER_PRIMARY_BUTTON;
    if ((uint32_t)value > POINTER_THIRD_BUTTON) {
        return false;
    }
    config.on_tool_eraser_value = value;
    return true;
}

bool config_set_on_tool_brush(int32_t value)
{
    if (value == -1) value = POINTER_PRIMARY_BUTTON;
    if ((uint32_t)value > POINTER_THIRD_BUTTON) {
        return false;
    }
    config.on_tool_brush_value = value;
    return true;
}

bool config_set_on_tool_pencil(int32_t value)
{
    if (value == -1) value = POINTER_PRIMARY_BUTTON;
    if ((uint32_t)value > POINTER_THIRD_BUTTON) {
        return false;
    }
    config.on_tool_pencil_value = value;
    return true;
}

bool config_set_on_tool_airbrush(int32_t value)
{
    if (value == -1) value = POINTER_PRIMARY_BUTTON;
    if ((uint32_t)value > POINTER_THIRD_BUTTON) {
        return false;
    }
    config.on_tool_airbrush_value = value;
    return true;
}

bool config_set_on_tool_finger(int32_t value)
{
    if (value == -1) value = POINTER_PRIMARY_BUTTON;
    if ((uint32_t)value > POINTER_THIRD_BUTTON) {
        return false;
    }
    config.on_tool_finger_value = value;
    return true;
}

bool config_set_on_tool_lens(int32_t value)
{
    if (value == -1) value = POINTER_PRIMARY_BUTTON;
    if ((uint32_t)value > POINTER_THIRD_BUTTON) {
        return false;
    }
    config.on_tool_lens_value = value;
    return true;
}

bool config_set_on_tool_mouse(int32_t value)
{
    if (value == -1) value = POINTER_PRIMARY_BUTTON;
    if ((uint32_t)value > POINTER_THIRD_BUTTON) {
        return false;
    }
    config.on_tool_mouse_value = value;
    return true;
}

bool config_set_on_tool_button1(int32_t value)
{
    if (value == -1) value = POINTER_SECONDARY_BUTTON;
    if ((uint32_t)value > POINTER_THIRD_BUTTON) {
        return false;
    }
    config.on_tool_button1_value = value;
    return true;
}

bool config_set_on_tool_button2(int32_t value)
{
    if (value == -1) value = POINTER_SECONDARY_BUTTON;
    if ((uint32_t)value > POINTER_THIRD_BUTTON) {
        return false;
    }
    config.on_tool_button2_value = value;
    return true;
}

bool config_set_on_tool_button3(int32_t value)
{
    if (value == -1) value = POINTER_SECONDARY_BUTTON;
    if ((uint32_t)value > POINTER_THIRD_BUTTON) {
        return false;
    }
    config.on_tool_button3_value = value;
    return true;
}

bool config_set_allow_throwing_multihead(bool value)
{
    config.allow_throwing_multihead = value;
    return true;
}

bool config_set_allow_dragging_multihead(bool value)
{
    config.allow_dragging_multihead = value;
    return true;
}

bool config_get_allow_throwing_multihead()
{
    if (config.allow_throwing_multihead == -1) return false;
    return config.allow_throwing_multihead;
}

bool config_get_allow_dragging_multihead()
{
    if (config.allow_dragging_multihead == -1) return true;
    return config.allow_dragging_multihead;
}

bool config_set_unified_outputs(bool value)
{
    config.unified_outputs = value;
    return true;
}

bool config_get_unified_outputs()
{
    if (config.unified_outputs == -1) return false;
    return config.unified_outputs;
}
