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
#define CONFIG_PARAM_TICK_DELAY_ID 9

struct config {
    bool breeding;
    bool dragging;
    bool ie_interactions;
    bool ie_throwing;
    bool cursor_data;
    bool dismiss_animations;
    bool affordances;

    uint32_t tick_delay;

    uint32_t mascot_limit;
    int32_t ie_throw_policy;
};

// Uses global variable
bool config_parse(const char* path);
void config_write(const char* path);

bool config_set_breeding(bool value);
bool config_set_dragging(bool value);
bool config_set_ie_interactions(bool value);
bool config_set_ie_throwing(bool value);
bool config_set_cursor_data(bool value);
bool config_set_mascot_limit(uint32_t value);
bool config_set_ie_throw_policy(int32_t value);
bool config_set_allow_dismiss_animations(bool value);
bool config_set_per_mascot_interactions(bool value);
bool config_set_tick_delay(uint32_t value);

bool config_get_breeding();
bool config_get_dragging();
bool config_get_ie_interactions();
bool config_get_ie_throwing();
bool config_get_cursor_data();
uint32_t config_get_mascot_limit();
int32_t config_get_ie_throw_policy();
bool config_get_allow_dismiss_animations();
bool config_get_per_mascot_interactions();
uint32_t config_get_tick_delay();

#endif
