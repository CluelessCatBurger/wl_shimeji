#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "master_header.h"

struct config config = {0};

bool config_parse(const char* path)
{
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
