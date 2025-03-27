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

    float mascot_opacity;
    float mascot_scale;

    char* prototypes_location;
    char* plugins_location;
    char* socket_location;
    char config_location[PATH_MAX];
} config = {0};

bool is_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n';
}

char* string_strip(char* str)
{
    if (!str)
        return NULL;

    char* end = str + strlen(str) - 1;

    while (end > str && is_whitespace(*end))
        end--;

    *(end + 1) = '\0';

    while (*str && is_whitespace(*str))
        str++;

    return str;
}

bool parse_bool(const char* str)
{
    if (!str)
        return false;

    if (!strcasecmp(str, "true") || !strcasecmp(str, "yes") || !strcasecmp(str, "on") || !strcasecmp(str, "1"))
        return true;

    if (!strcasecmp(str, "false") || !strcasecmp(str, "no") || !strcasecmp(str, "off") || !strcasecmp(str, "0"))
        return false;

    WARN("Unrecognized string in boolean field: %s. Assuming true", str);
    return true;
}

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
    config.mascot_opacity = -1.0f;
    config.mascot_scale = -1.0f;

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

        key = string_strip(key);
        value = string_strip(value);

        if (!key || !value) {
            continue;
        }

        if (strcasecmp(key, "breeding") == 0) {
            config_set_breeding(parse_bool(value));
        } else if (strcasecmp(key, "dragging") == 0) {
            config_set_dragging(parse_bool(value));
        } else if (strcasecmp(key, "ie_interactions") == 0) {
            config_set_ie_interactions(parse_bool(value));
        } else if (strcasecmp(key, "ie_throwing") == 0) {
            config_set_ie_throwing(parse_bool(value));
        } else if (strcasecmp(key, "cursor_data") == 0) {
            config_set_cursor_data(parse_bool(value));
        } else if (strcasecmp(key, "mascot_limit") == 0) {
            config_set_mascot_limit(atoi(value));
        } else if (strcasecmp(key, "ie_throw_policy") == 0) {
            config_set_ie_throw_policy(atoi(value));
        } else if (strcasecmp(key, "allow_dismiss_animations") == 0) {
            config_set_allow_dismiss_animations(parse_bool(value));
        } else if (strcasecmp(key, "per_mascot_interactions") == 0) {
            config_set_per_mascot_interactions(parse_bool(value));
        } else if (strcasecmp(key, "interpolation_framerate") == 0) {
            config_set_interpolation_framerate(atoi(value));
        } else if (strcasecmp(key, "overlay_layer") == 0) {
            config_set_overlay_layer(atoi(value));
        } else if (strcasecmp(key, "tablets_enabled") == 0) {
            config_set_tablets_enabled(parse_bool(value));
        } else if (strcasecmp(key, "pointer_left_value") == 0) {
            config_set_pointer_left_button(atoi(value));
        } else if (strcasecmp(key, "pointer_right_value") == 0) {
            config_set_pointer_right_button(atoi(value));
        } else if (strcasecmp(key, "pointer_middle_value") == 0) {
            config_set_pointer_middle_button(atoi(value));
        } else if (strcasecmp(key, "on_tool_pen_value") == 0) {
            config_set_on_tool_pen(atoi(value));
        } else if (strcasecmp(key, "on_tool_eraser_value") == 0) {
            config_set_on_tool_eraser(atoi(value));
        } else if (strcasecmp(key, "on_tool_brush_value") == 0) {
            config_set_on_tool_brush(atoi(value));
        } else if (strcasecmp(key, "on_tool_pencil_value") == 0) {
            config_set_on_tool_pencil(atoi(value));
        } else if (strcasecmp(key, "on_tool_airbrush_value") == 0) {
            config_set_on_tool_airbrush(atoi(value));
        } else if (strcasecmp(key, "on_tool_finger_value") == 0) {
            config_set_on_tool_finger(atoi(value));
        } else if (strcasecmp(key, "on_tool_lens_value") == 0) {
            config_set_on_tool_lens(atoi(value));
        } else if (strcasecmp(key, "on_tool_mouse_value") == 0) {
            config_set_on_tool_mouse(atoi(value));
        } else if (strcasecmp(key, "on_tool_button1_value") == 0) {
            config_set_on_tool_button1(atoi(value));
        } else if (strcasecmp(key, "on_tool_button2_value") == 0) {
            config_set_on_tool_button2(atoi(value));
        } else if (strcasecmp(key, "on_tool_button3_value") == 0) {
            config_set_on_tool_button3(atoi(value));
        } else if (strcasecmp(key, "allow_throwing_multihead") == 0) {
            config_set_allow_throwing_multihead(parse_bool(value));
        } else if (strcasecmp(key, "allow_dragging_multihead") == 0) {
            config_set_allow_dragging_multihead(parse_bool(value));
        } else if (strcasecmp(key, "unified_outputs") == 0) {
            config_set_unified_outputs(parse_bool(value));
        } else if (strcasecmp(key, "prototypes_location") == 0) {
            if (config.prototypes_location) {
                free(config.prototypes_location);
            }
            config.prototypes_location = strdup(value);
        } else if (strcasecmp(key, "plugins_location") == 0) {
            if (config.plugins_location) {
                free(config.plugins_location);
            }
            config.plugins_location = strdup(value);
        } else if (strcasecmp(key, "socket_location") == 0) {
            if (config.socket_location) {
                free(config.socket_location);
            }
            config.socket_location = strdup(value);
        } else if (strcasecmp(key, "mascot_opacity") == 0) {
            config_set_opacity(atof(value));
        } else if (strcasecmp(key, "mascot_scale") == 0) {
            config_set_mascot_scale(atof(value));
        }
    }
    strncpy(config.config_location, path, PATH_MAX);
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
    if (config.prototypes_location) fprintf(file, "prototypes_location=%s\n", config.prototypes_location);
    if (config.plugins_location) fprintf(file, "prototypes_location=%s\n", config.plugins_location);
    if (config.socket_location) fprintf(file, "prototypes_location=%s\n", config.socket_location);
    if (config.mascot_scale != -1.0f) fprintf(file, "mascot_scale=%f\n", config.mascot_scale);
    if (config.mascot_opacity != -1.0f) fprintf(file, "mascot_opacity=%f\n", config.mascot_opacity);


    fclose(file);
}

bool config_set_breeding(int32_t value)
{
    config.breeding = value;
    return true;
}

bool config_set_dragging(int32_t value)
{
    config.dragging = value;
    return true;
}

bool config_set_ie_interactions(int32_t value)
{
    config.ie_interactions = value;
    return true;
}

bool config_set_ie_throwing(int32_t value)
{
    config.ie_throwing = value;
    return true;
}

bool config_set_cursor_data(int32_t value)
{
    config.cursor_data = value;
    return true;
}

bool config_set_mascot_limit(int32_t value)
{
    config.mascot_limit = value;
    return true;
}

bool config_set_ie_throw_policy(int32_t value)
{
    config.ie_throw_policy = value;
    return true;
}

bool config_set_allow_dismiss_animations(int32_t value)
{
    config.dismiss_animations = value;
    return true;
}

bool config_set_per_mascot_interactions(int32_t value)
{
    config.affordances = value;
    return true;
}

bool config_set_interpolation_framerate(int32_t value)
{
    config.framerate = value;
    return true;
}

bool config_set_overlay_layer(int32_t value)
{
    config.overlay_layer = value;
    return true;
}

int32_t config_get_breeding()
{
    return config.breeding;
}

int32_t config_get_dragging()
{
    return config.dragging;
}

int32_t config_get_ie_interactions()
{
    return config.ie_interactions;
}

int32_t config_get_ie_throwing()
{
    return config.ie_throwing;
}

int32_t config_get_cursor_data()
{
    return config.cursor_data;
}

int32_t config_get_mascot_limit()
{
    return config.mascot_limit;
}

int32_t config_get_ie_throw_policy()
{
    return config.ie_throw_policy;
}

int32_t config_get_allow_dismiss_animations()
{
    return config.dismiss_animations;
}

int32_t config_get_per_mascot_interactions()
{
    return config.affordances;
}

int32_t config_get_interpolation_framerate()
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

int32_t config_get_tablets_enabled()
{
    return config.enable_tablets;
}

int32_t config_get_pointer_left_button()
{
    if (config.pointer_left_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.pointer_left_value;
}

int32_t config_get_pointer_right_button()
{
    if (config.pointer_right_value == -1) return POINTER_SECONDARY_BUTTON;
    return config.pointer_right_value;
}

int32_t config_get_pointer_middle_button()
{
    if (config.pointer_middle_value == -1) return POINTER_THIRD_BUTTON;
    return config.pointer_middle_value;
}

int32_t config_get_on_tool_pen()
{
    if (config.on_tool_pen_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_pen_value;
}

int32_t config_get_on_tool_eraser()
{
    if (config.on_tool_eraser_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_eraser_value;
}

int32_t config_get_on_tool_brush()
{
    if (config.on_tool_brush_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_brush_value;
}

int32_t config_get_on_tool_pencil()
{
    if (config.on_tool_pencil_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_pencil_value;
}

int32_t config_get_on_tool_airbrush()
{
    if (config.on_tool_airbrush_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_airbrush_value;
}

int32_t config_get_on_tool_finger()
{
    if (config.on_tool_finger_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_finger_value;
}

int32_t config_get_on_tool_lens()
{
    if (config.on_tool_lens_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_lens_value;
}

int32_t config_get_on_tool_mouse()
{
    if (config.on_tool_mouse_value == -1) return POINTER_PRIMARY_BUTTON;
    return config.on_tool_mouse_value;
}

int32_t config_get_on_tool_button1()
{
    if (config.on_tool_button1_value == -1) return POINTER_SECONDARY_BUTTON;
    return config.on_tool_button1_value;
}

int32_t config_get_on_tool_button2()
{
    if (config.on_tool_button2_value == -1) return POINTER_SECONDARY_BUTTON;
    return config.on_tool_button2_value;
}

int32_t config_get_on_tool_button3()
{
    if (config.on_tool_button3_value == -1) return POINTER_SECONDARY_BUTTON;
    return config.on_tool_button3_value;
}


bool config_set_tablets_enabled(int32_t value)
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

bool config_set_allow_throwing_multihead(int32_t value)
{
    config.allow_throwing_multihead = value;
    return true;
}

bool config_set_allow_dragging_multihead(int32_t value)
{
    config.allow_dragging_multihead = value;
    return true;
}

int32_t config_get_allow_throwing_multihead()
{
    if (config.allow_throwing_multihead == -1) return false;
    return config.allow_throwing_multihead;
}

int32_t config_get_allow_dragging_multihead()
{
    if (config.allow_dragging_multihead == -1) return true;
    return config.allow_dragging_multihead;
}

bool config_set_unified_outputs(int32_t value)
{
    config.unified_outputs = value;
    return true;
}

int32_t config_get_unified_outputs()
{
    if (config.unified_outputs == -1) return false;
    return config.unified_outputs;
}

const char* config_get_prototypes_location()
{
    return config.prototypes_location;
}

const char* config_get_plugins_location()
{
    return config.plugins_location;
}

const char* config_get_socket_location()
{
    return config.socket_location;
}

bool config_set_opacity(float value)
{
    if (value < 0.0f && value != -1.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    config.mascot_opacity = value;
    return true;
}

bool config_set_mascot_scale(float value)
{
    if (value < 0.25f && value != -1.0f) value = 0.25f;
    if (value > 2.0f) value = 2.0f;
    config.mascot_scale = value;
    return true;
}

float config_get_opacity()
{
    return config.mascot_opacity == -1.0f ? 1.0f : config.mascot_opacity;
}

float config_get_mascot_scale()
{
    return config.mascot_scale == -1.0f ? 1.0f : config.mascot_scale;
}

bool config_get_by_key(const char* key, char* dest, uint8_t size)
{
    if (!strcmp(key, CONFIG_PARAM_BREEDING)) {
        snprintf(dest, size, "%s", config.breeding ? "true" : "false");
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_DRAGGING)) {
        snprintf(dest, size, "%s", config.dragging ? "true" : "false");
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_IE_INTERACTIONS)) {
        snprintf(dest, size, "%s", config.ie_interactions ? "true" : "false");
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_IE_THROWING)) {
        snprintf(dest, size, "%s", config.ie_throwing ? "true" : "false");
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_IE_THROW_POLICY)) {
        if (config.ie_throw_policy == -1) {
            snprintf(dest, size, "looping");
            return true;
        } else if (config.ie_throw_policy == 0) {
            snprintf(dest, size, "none");
            return true;
        } else if (config.ie_throw_policy == 1) {
            snprintf(dest, size, "stop_at_borders");
            return true;
        } else if (config.ie_throw_policy == 2) {
            snprintf(dest, size, "bounce_at_borders");
            return true;
        } else if (config.ie_throw_policy == 3) {
            snprintf(dest, size, "looping");
            return true;
        } else if (config.ie_throw_policy == 4) {
            snprintf(dest, size, "close");
            return true;
        } else if (config.ie_throw_policy == 5) {
            snprintf(dest, size, "minimize");
            return true;
        } else if (config.ie_throw_policy == 6) {
            snprintf(dest, size, "keep_offscreen");
            return true;
        } else {
            snprintf(dest, size, "unknown");
            return true;
        }
    } else if (!strcmp(key, CONFIG_PARAM_CURSOR_DATA)) {
        snprintf(dest, size, "%s", config.cursor_data ? "true" : "false");
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_MASCOT_LIMIT)) {
        snprintf(dest, size, "%d", config.mascot_limit);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_ALLOW_THROWING_MULTIHEAD)) {
        snprintf(dest, size, "%s", config.allow_throwing_multihead ? "true" : "false");
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_ALLOW_DRAGGING_MULTIHEAD)) {
        snprintf(dest, size, "%s", config.allow_dragging_multihead ? "true" : "false");
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_UNIFIED_OUTPUTS)) {
        snprintf(dest, size, "%s", config.unified_outputs ? "true" : "false");
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_ALLOW_DISMISS_ANIMATIONS)) {
        snprintf(dest, size, "%s", config.dismiss_animations ? "true" : "false");
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_PER_MASCOT_INTERACTIONS)) {
        snprintf(dest, size, "%s", config.affordances ? "true" : "false");
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_INTERPOLATION_FRAMERATE)) {
        snprintf(dest, size, "%d", config.framerate);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_OVERLAY_LAYER)) {
        if (config.overlay_layer == -1) {
            snprintf(dest, size, "overlay");
        } else if (config.overlay_layer == 0) {
            snprintf(dest, size, "background");
        } else if (config.overlay_layer == 1) {
            snprintf(dest, size, "bottom");
        } else if (config.overlay_layer == 2) {
            snprintf(dest, size, "top");
        } else if (config.overlay_layer == 3) {
            snprintf(dest, size, "overlay");
        } else {
            snprintf(dest, size, "unknown");
        }
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_TABLETS_ENABLED)) {
        snprintf(dest, size, "%s", config.enable_tablets ? "true" : "false");
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_POINTER_LEFT_BUTTON)) {
        snprintf(dest, size, "%d", config.pointer_left_value);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_POINTER_RIGHT_BUTTON)) {
        snprintf(dest, size, "%d", config.pointer_right_value);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_POINTER_MIDDLE_BUTTON)) {
        snprintf(dest, size, "%d", config.pointer_middle_value);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_PEN)) {
        snprintf(dest, size, "%d", config.on_tool_pen_value);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_ERASER)) {
        snprintf(dest, size, "%d", config.on_tool_eraser_value);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_BRUSH)) {
        snprintf(dest, size, "%d", config.on_tool_brush_value);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_PENCIL)) {
        snprintf(dest, size, "%d", config.on_tool_pencil_value);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_AIRBRUSH)) {
        snprintf(dest, size, "%d", config.on_tool_airbrush_value);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_FINGER)) {
        snprintf(dest, size, "%d", config.on_tool_finger_value);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_LENS)) {
        snprintf(dest, size, "%d", config.on_tool_lens_value);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_MOUSE)) {
        snprintf(dest, size, "%d", config.on_tool_mouse_value);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_BUTTON1)) {
        snprintf(dest, size, "%d", config.on_tool_button1_value);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_BUTTON2)) {
        snprintf(dest, size, "%d", config.on_tool_button2_value);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_BUTTON3)) {
        snprintf(dest, size, "%d", config.on_tool_button3_value);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_OPACITY)) {
        snprintf(dest, size, "%f", config.mascot_opacity);
        return true;
    } else if (!strcmp(key, CONFIG_PARAM_MASCOT_SCALE)) {
        snprintf(dest, size, "%f", config.mascot_scale);
        return true;
    }
    return false;
}

bool config_set_by_key(const char* key, const char* value)
{
    bool res = false;
    if (!strcmp(key, CONFIG_PARAM_BREEDING)) {
        res = config_set_breeding(parse_bool(value));
    } else if (!strcmp(key, CONFIG_PARAM_DRAGGING)) {
        res = config_set_dragging(parse_bool(value));
    } else if (!strcmp(key, CONFIG_PARAM_IE_INTERACTIONS)) {
        res = config_set_ie_interactions(parse_bool(value));
    } else if (!strcmp(key, CONFIG_PARAM_IE_THROWING)) {
        res = config_set_ie_throwing(parse_bool(value));
    } else if (!strcmp(key, CONFIG_PARAM_IE_THROW_POLICY)) {
        int policy = 3; // Default to looping
        if (!strcasecmp(value, "none")) policy = 0;
        else if (!strcasecmp(value, "stop_at_borders")) policy = 1;
        else if (!strcasecmp(value, "bounce_at_borders")) policy = 2;
        else if (!strcasecmp(value, "looping")) policy = 3;
        else if (!strcasecmp(value, "close")) policy = 4;
        else if (!strcasecmp(value, "minimize")) policy = 5;
        else if (!strcasecmp(value, "keep_offscreen")) policy = 6;
        res = config_set_ie_throw_policy(policy);
    } else if (!strcmp(key, CONFIG_PARAM_CURSOR_DATA)) {
        res = config_set_cursor_data(parse_bool(value));
    } else if (!strcmp(key, CONFIG_PARAM_MASCOT_LIMIT)) {
        res = config_set_mascot_limit(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_ALLOW_THROWING_MULTIHEAD)) {
        res = config_set_allow_throwing_multihead(parse_bool(value));
    } else if (!strcmp(key, CONFIG_PARAM_ALLOW_DRAGGING_MULTIHEAD)) {
        res = config_set_allow_dragging_multihead(parse_bool(value));
    } else if (!strcmp(key, CONFIG_PARAM_UNIFIED_OUTPUTS)) {
        res = config_set_unified_outputs(parse_bool(value));
    } else if (!strcmp(key, CONFIG_PARAM_ALLOW_DISMISS_ANIMATIONS)) {
        res = config_set_allow_dismiss_animations(parse_bool(value));
    } else if (!strcmp(key, CONFIG_PARAM_PER_MASCOT_INTERACTIONS)) {
        res = config_set_per_mascot_interactions(parse_bool(value));
    } else if (!strcmp(key, CONFIG_PARAM_INTERPOLATION_FRAMERATE)) {
        res = config_set_interpolation_framerate(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_OVERLAY_LAYER)) {
        int layer = 3; // Default to overlay
        if (!strcasecmp(value, "background")) layer = 0;
        else if (!strcasecmp(value, "bottom")) layer = 1;
        else if (!strcasecmp(value, "top")) layer = 2;
        else if (!strcasecmp(value, "overlay")) layer = 3;
        res = config_set_overlay_layer(layer);
    } else if (!strcmp(key, CONFIG_PARAM_TABLETS_ENABLED)) {
        res = config_set_tablets_enabled(parse_bool(value));
    } else if (!strcmp(key, CONFIG_PARAM_POINTER_LEFT_BUTTON)) {
        res = config_set_pointer_left_button(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_POINTER_RIGHT_BUTTON)) {
        res = config_set_pointer_right_button(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_POINTER_MIDDLE_BUTTON)) {
        res = config_set_pointer_middle_button(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_PEN)) {
        res = config_set_on_tool_pen(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_ERASER)) {
        res = config_set_on_tool_eraser(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_BRUSH)) {
        res = config_set_on_tool_brush(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_PENCIL)) {
        res = config_set_on_tool_pencil(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_AIRBRUSH)) {
        res = config_set_on_tool_airbrush(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_FINGER)) {
        res = config_set_on_tool_finger(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_LENS)) {
        res = config_set_on_tool_lens(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_MOUSE)) {
        res = config_set_on_tool_mouse(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_BUTTON1)) {
        res = config_set_on_tool_button1(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_BUTTON2)) {
        res = config_set_on_tool_button2(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_ON_TOOL_BUTTON3)) {
        res = config_set_on_tool_button3(atoi(value));
    } else if (!strcmp(key, CONFIG_PARAM_MASCOT_SCALE)) {
        res = config_set_mascot_scale(atof(value));
    } else if (!strcmp(key, CONFIG_PARAM_OPACITY)) {
        res = config_set_opacity(atof(value));
    }
    if (res) config_write(config.config_location);
    return res;
}
