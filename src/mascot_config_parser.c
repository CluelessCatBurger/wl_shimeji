/*
    mascot_config_parser.c - wl_shimeji's mascot configuration parser

    Copyright (C) 2024  CluelessCatBurger <github.com/CluelessCatBurger>

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

#include "mascot_config_parser.h"
#include "environment.h"
#include "expressions.h"
#include "mascot.h"
#include "mascot_atlas.h"
#include "third_party/json.h/json.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include "global_symbols.h"

#include "wayland_includes.h"

struct mascot_prototype_store_ {
    struct mascot_prototype** prototypes;
    uint32_t size;
    uint32_t count;
};

struct mascot_prototype* mascot_prototype_new()
{
    struct mascot_prototype* prototype = (struct mascot_prototype*)calloc(1,sizeof(struct mascot_prototype));
    if (!prototype) {
        return NULL;
    }
    return prototype;
}

void mascot_prototype_link(const struct mascot_prototype* prototype)
{
    if (!prototype) {
        return;
    }
    ((struct mascot_prototype*)prototype)->reference_count++;
}

void mascot_prototype_unlink(const struct mascot_prototype* prototype)
{
    if (!prototype) {
        return;
    }
    struct mascot_prototype* p = (struct mascot_prototype*)prototype;
    p->reference_count--;
    if (p->reference_count == 0) {
        free(p);
    }
}

mascot_prototype_store* mascot_prototype_store_new()
{
    mascot_prototype_store* store = (mascot_prototype_store*)calloc(1,sizeof(mascot_prototype_store));
    if (!store) {
        return NULL;
    }

    store->size = 16;
    store->prototypes = (struct mascot_prototype**)calloc(store->size,sizeof(struct mascot_prototype*));

    return store;
}

bool mascot_prototype_store_add(mascot_prototype_store* store, const struct mascot_prototype* prototype)
{
    if (!store || !prototype) {
        return false;
    }

    if (store->count >= store->size) {
        store->size *= 2;
        store->prototypes = (struct mascot_prototype**)realloc(store->prototypes, store->size * sizeof(struct mascot_prototype*));
    }

    // Ensure the prototype is not already in the store or name is not already in the store
    for (size_t i = 0; i < store->size; i++) {
        if (store->prototypes[i] == prototype) {
            return false;
        }
        if (store->prototypes[i] && strcmp(store->prototypes[i]->name, prototype->name) == 0) {
            return false;
        }
    }

    // Find an empty slot
    for (size_t i = 0; i < store->size; i++) {
        if (!store->prototypes[i]) {
            store->prototypes[i] = (struct mascot_prototype*)prototype;
            store->count++;
            mascot_prototype_link(prototype);
            ((struct mascot_prototype*)prototype)->prototype_store = store;
            return true;
        }
    }

    // How
    return false;
}

bool mascot_prototype_store_remove(mascot_prototype_store* store, const struct mascot_prototype* prototype)
{
    if (!store || !prototype) {
        return false;
    }

    for (size_t i = 0; i < store->size; i++) {
        if (store->prototypes[i] == prototype) {
            store->prototypes[i] = NULL;
            store->count--;
            mascot_prototype_unlink(prototype);
            ((struct mascot_prototype*)prototype)->prototype_store = NULL;
            return true;
        }
    }

    return false;
}

struct mascot_prototype* mascot_prototype_store_get(mascot_prototype_store* store, const char* name)
{
    if (!store || !name) {
        return NULL;
    }

    const char* default_name_template = "Shimeji.%s";
    char default_name[256] = {0};
    snprintf(default_name, 256, default_name_template, name);

    DEBUG("Looking for prototype %s, fallback name %s", name, default_name);

    for (size_t i = 0; i < store->size; i++) {
        if (store->prototypes[i] && strcasecmp(store->prototypes[i]->name, name) == 0 && strlen(store->prototypes[i]->name) == strlen(name)) {
            DEBUG("Found prototype %s at index %zu, internal_name %s", name, i, store->prototypes[i]->name);
            return store->prototypes[i];
        }
    }
    for (size_t i = 0; i < store->size; i++) {
        if (store->prototypes[i] && strcasecmp(store->prototypes[i]->name, default_name) == 0 && strlen(store->prototypes[i]->name) == strlen(default_name)) {
            DEBUG("Found prototype %s at index %zu, internal_name %s", name, i, store->prototypes[i]->name);
            return store->prototypes[i];
        }
    }

    DEBUG("Prototype %s not found", name);

    return NULL;
}

void mascot_prototype_store_free(mascot_prototype_store* store)
{
    if (!store) {
        return;
    }

    for (size_t i = 0; i < store->size; i++) {
        if (store->prototypes[i]) {
            mascot_prototype_unlink(store->prototypes[i]);
        }
    }

    free(store->prototypes);
    free(store);
}

int mascot_prototype_store_count(mascot_prototype_store* store)
{
    return store->count;
}

struct mascot_prototype* mascot_prototype_store_get_index(mascot_prototype_store* store, int index)
{
    if (index < 0 || index >= (int32_t)store->size) {
        return NULL;
    }
    return store->prototypes[index];
}


// Loader starts here

struct config_program_loader_result {
    struct mascot_expression** expressions;
    size_t count;
    bool ok;
};

struct config_action_parse_result {
    struct mascot_action* action;
    enum {
        MASCOT_ACTION_PARSE_OK,
        MASCOT_ACTION_PARSE_ERROR_GENERAL,
        MASCOT_ACTION_PARSE_ERROR_NOT_DEFINED,
        MASCOT_ACTION_PARSE_ERROR_NOT_IMPLEMENTED,
        MASCOT_ACTION_PARSE_ERROR_BAD_ACTION,
    } status;

};

struct config_action_loader_result {
    struct mascot_action** actions;
    size_t count;
    bool ok;

};

struct config_behavior_loader_result {
    struct mascot_behavior** behaviors;
    struct mascot_behavior_reference* root_list;
    size_t count, root_list_count;
    bool ok;
};

struct string_int_int {
    const char* string;
    int value;
    enum mascot_local_variable_kind kind;
};

struct string_ptr_pair {
    const char* string;
    void* value;
};

struct string_int_int locals[] = {
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_X_VALUE, MASCOT_LOCAL_VARIABLE_X_ID, MASCOT_LOCAL_VARIABLE_X_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_Y_VALUE, MASCOT_LOCAL_VARIABLE_Y_ID, MASCOT_LOCAL_VARIABLE_Y_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_TARGETX_VALUE, MASCOT_LOCAL_VARIABLE_TARGETX_ID, MASCOT_LOCAL_VARIABLE_TARGETX_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_TARGETY_VALUE, MASCOT_LOCAL_VARIABLE_TARGETY_ID, MASCOT_LOCAL_VARIABLE_TARGETY_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_GRAVITY_VALUE, MASCOT_LOCAL_VARIABLE_GRAVITY_ID, MASCOT_LOCAL_VARIABLE_GRAVITY_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_LOOKINGRIGHT_VALUE, MASCOT_LOCAL_VARIABLE_LOOKINGRIGHT_ID, MASCOT_LOCAL_VARIABLE_LOOKINGRIGHT_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_AIRDRAGX_VALUE, MASCOT_LOCAL_VARIABLE_AIRDRAGX_ID, MASCOT_LOCAL_VARIABLE_AIRDRAGX_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_AIRDRAGY_VALUE, MASCOT_LOCAL_VARIABLE_AIRDRAGY_ID, MASCOT_LOCAL_VARIABLE_AIRDRAGY_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_VELOCITYX_VALUE, MASCOT_LOCAL_VARIABLE_VELOCITYX_ID, MASCOT_LOCAL_VARIABLE_VELOCITYX_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_VELOCITYY_VALUE, MASCOT_LOCAL_VARIABLE_VELOCITYY_ID, MASCOT_LOCAL_VARIABLE_VELOCITYY_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_BORNX_VALUE, MASCOT_LOCAL_VARIABLE_BORNX_ID, MASCOT_LOCAL_VARIABLE_BORNX_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_BORNY_VALUE, MASCOT_LOCAL_VARIABLE_BORNY_ID, MASCOT_LOCAL_VARIABLE_BORNY_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_INITIALVELX_VALUE, MASCOT_LOCAL_VARIABLE_INITIALVELX_ID, MASCOT_LOCAL_VARIABLE_INITIALVELX_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_INITIALVELY_VALUE, MASCOT_LOCAL_VARIABLE_INITIALVELY_ID, MASCOT_LOCAL_VARIABLE_INITIALVELY_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_VELOCITYPARAM_VALUE, MASCOT_LOCAL_VARIABLE_VELOCITYPARAM_ID, MASCOT_LOCAL_VARIABLE_VELOCITYPARAM_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_FOOTX_VALUE, MASCOT_LOCAL_VARIABLE_FOOTX_ID, MASCOT_LOCAL_VARIABLE_FOOTX_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_FOOTDX_VALUE, MASCOT_LOCAL_VARIABLE_FOOTDX_ID, MASCOT_LOCAL_VARIABLE_FOOTDX_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_MODX_VALUE, MASCOT_LOCAL_VARIABLE_MODX_ID, MASCOT_LOCAL_VARIABLE_MODX_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_MODY_VALUE, MASCOT_LOCAL_VARIABLE_MODY_ID, MASCOT_LOCAL_VARIABLE_MODY_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_GAP_VALUE, MASCOT_LOCAL_VARIABLE_GAP_ID, MASCOT_LOCAL_VARIABLE_GAP_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_BORNINTERVAL_VALUE, MASCOT_LOCAL_VARIABLE_BORNINTERVAL_ID, MASCOT_LOCAL_VARIABLE_BORNINTERVAL_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_BORNCOUNT_VALUE, MASCOT_LOCAL_VARIABLE_BORNCOUNT_ID, MASCOT_LOCAL_VARIABLE_BORNCOUNT_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_IEOFFSETX_VALUE, MASCOT_LOCAL_VARIABLE_IEOFFSETX_ID, MASCOT_LOCAL_VARIABLE_IEOFFSETX_TYPE},
    (struct string_int_int){MASCOT_LOCAL_VARIABLE_IEOFFSETY_VALUE, MASCOT_LOCAL_VARIABLE_IEOFFSETY_ID, MASCOT_LOCAL_VARIABLE_IEOFFSETY_TYPE}
};

struct string_ptr_pair globals_n_funcs[] = {
    (struct string_ptr_pair)GLOBAL_SYM_MATH_E,
    (struct string_ptr_pair)GLOBAL_SYM_MATH_LN10,
    (struct string_ptr_pair)GLOBAL_SYM_MATH_LN2,
    (struct string_ptr_pair)GLOBAL_SYM_MATH_LOG2E,
    (struct string_ptr_pair)GLOBAL_SYM_MATH_LOG10E,
    (struct string_ptr_pair)GLOBAL_SYM_MATH_PI,
    (struct string_ptr_pair)GLOBAL_SYM_MATH_SQRT1_2,
    (struct string_ptr_pair)GLOBAL_SYM_MATH_SQRT2,
    (struct string_ptr_pair)FUNC_MATH_ABS,
    (struct string_ptr_pair)FUNC_MATH_ACOS,
    (struct string_ptr_pair)FUNC_MATH_ACOSH,
    (struct string_ptr_pair)FUNC_MATH_ASIN,
    (struct string_ptr_pair)FUNC_MATH_ASINH,
    (struct string_ptr_pair)FUNC_MATH_ATAN,
    (struct string_ptr_pair)FUNC_MATH_ATANH,
    (struct string_ptr_pair)FUNC_MATH_CBRT,
    (struct string_ptr_pair)FUNC_MATH_CEIL,
    (struct string_ptr_pair)FUNC_MATH_CLZ32,
    (struct string_ptr_pair)FUNC_MATH_COS,
    (struct string_ptr_pair)FUNC_MATH_COSH,
    (struct string_ptr_pair)FUNC_MATH_EXP,
    (struct string_ptr_pair)FUNC_MATH_EXPM1,
    (struct string_ptr_pair)FUNC_MATH_FLOOR,
    (struct string_ptr_pair)FUNC_MATH_FROUND,
    (struct string_ptr_pair)FUNC_MATH_LOG,
    (struct string_ptr_pair)FUNC_MATH_LOG1P,
    (struct string_ptr_pair)FUNC_MATH_LOG2,
    (struct string_ptr_pair)FUNC_MATH_LOG10,
    (struct string_ptr_pair)FUNC_MATH_MAX,
    (struct string_ptr_pair)FUNC_MATH_MIN,
    (struct string_ptr_pair)FUNC_MATH_POW,
    (struct string_ptr_pair)FUNC_MATH_RANDOM,
    (struct string_ptr_pair)FUNC_MATH_ROUND,
    (struct string_ptr_pair)FUNC_MATH_SIGN,
    (struct string_ptr_pair)FUNC_MATH_SIN,
    (struct string_ptr_pair)FUNC_MATH_SINH,
    (struct string_ptr_pair)FUNC_MATH_SQRT,
    (struct string_ptr_pair)FUNC_MATH_TAN,
    (struct string_ptr_pair)FUNC_MATH_TANH,
    (struct string_ptr_pair)FUNC_MATH_TRUNC,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ANCHOR,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ANCHOR_X,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ANCHOR_Y,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_CURSOR_X,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_CURSOR_Y,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_CURSOR_DX,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_CURSOR_DY,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_SCREEN_WIDTH,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_SCREEN_HEIGHT,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_WORK_AREA_WIDTH,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_WORK_AREA_HEIGHT,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_WORK_AREA_LEFT,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_WORK_AREA_TOP,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_WORK_AREA_RIGHT,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_WORK_AREA_BOTTOM,
    (struct string_ptr_pair)FUNC_MASCOT_ENVIRONMENT_FLOOR_ISON,
    (struct string_ptr_pair)FUNC_MASCOT_ENVIRONMENT_CEILING_ISON,
    (struct string_ptr_pair)FUNC_MASCOT_ENVIRONMENT_WALL_ISON,
    (struct string_ptr_pair)FUNC_MASCOT_ENVIRONMENT_LEFT_ISON,
    (struct string_ptr_pair)FUNC_MASCOT_ENVIRONMENT_RIGHT_ISON,
    (struct string_ptr_pair)FUNC_MASCOT_ENVIRONMENT_WORK_AREA_LEFT_BORDER_ISON,
    (struct string_ptr_pair)FUNC_MASCOT_ENVIRONMENT_WORK_AREA_RIGHT_BORDER_ISON,
    (struct string_ptr_pair)FUNC_MASCOT_ENVIRONMENT_WORK_AREA_CEILING_BORDER_ISON,
    (struct string_ptr_pair)FUNC_MASCOT_ENVIRONMENT_WORK_AREA_FLOOR_BORDER_ISON,

    // IE's
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_ACTIVE_IE_VISIBLE,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_ACTIVE_IE_WIDTH,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_ACTIVE_IE_HEIGHT,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_ACTIVE_IE_RIGHT,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_ACTIVE_IE_LEFT,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_ACTIVE_IE_TOP,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_ENVIRONMENT_ACTIVE_IE_BOTTOM,
    (struct string_ptr_pair)FUNC_MASCOT_ENVIRONMENT_ACTIVE_IE_TOP_BORDER_ISON,
    (struct string_ptr_pair)FUNC_MASCOT_ENVIRONMENT_ACTIVE_IE_BOTTOM_BORDER_ISON,
    (struct string_ptr_pair)FUNC_MASCOT_ENVIRONMENT_ACTIVE_IE_LEFT_BORDER_ISON,
    (struct string_ptr_pair)FUNC_MASCOT_ENVIRONMENT_ACTIVE_IE_RIGHT_BORDER_ISON,
    (struct string_ptr_pair)FUNC_MASCOT_ENVIRONMENT_ACTIVE_IE_BORDER_ISON,

    // Utility
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_COUNT,
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_COUNT_TOTAL,

    // Affordance
    (struct string_ptr_pair)GLOBAL_SYM_TARGET_ANCHOR,
    (struct string_ptr_pair)GLOBAL_SYM_TARGET_ANCHOR_X,
    (struct string_ptr_pair)GLOBAL_SYM_TARGET_ANCHOR_Y,

    // Noop
    (struct string_ptr_pair)GLOBAL_SYM_MASCOT_NOOP,
};

uint8_t GLOBAL_SYMS_COUNT = sizeof(globals_n_funcs) / sizeof(struct string_ptr_pair);

struct mascot_expression* parse_program(struct json_object_s* program)
{
    struct expression_prototype* prototype = NULL;
    struct mascot_expression* expression = NULL;

    uint8_t mascot_vars[128] = {0};
    uint8_t mascot_vars_count = 0;
    bool    mascot_vars_found = false;

    void*   global_getters[128] = {0};
    uint8_t globals_count = 0;
    bool    globals_found = false;

    void* function_getters[128] = {0};
    uint8_t functions_count = 0;
    bool    functions_found = false;

    char bytecode[1024] = {0};
    uint16_t bytecode_size = 0;
    bool insructions_found = false;

    bool evaluate_once = true;

    struct json_object_element_s* element = program->start;
    while (element) {
        if (!strncmp(element->name->string, "instructions", element->name->string_size) && strlen("instructions") == element->name->string_size) {
            if (element->value->type != json_type_string) {
                WARN("Instructions must be a string");
                return NULL;
            }
            struct json_string_s* instructions = (struct json_string_s*)element->value->payload;
            if (instructions->string_size > 1024) {
                WARN("Instructions string too long");
                return NULL;
            }
            insructions_found = true;
            memcpy(bytecode, instructions->string, instructions->string_size);
            bytecode_size = instructions->string_size;
        }
        if (!strncmp(element->name->string, "symtab_l", element->name->string_size) && strlen("symtab_l") == element->name->string_size) {
            if (element->value->type != json_type_array) {
                WARN("Local symbol table must be an array");
                return NULL;
            }
            mascot_vars_found = true;
            struct json_array_s* symtab_l = (struct json_array_s*)element->value->payload;
            struct json_array_element_s* symtab_l_element = symtab_l->start;
            while (symtab_l_element) {
                if (symtab_l_element->value->type != json_type_string) {
                    WARN("Local symbol table elements must be strings");
                    return NULL;
                }
                struct json_string_s* var = (struct json_string_s*)symtab_l_element->value->payload;
                symtab_l_element = symtab_l_element->next;
                if (mascot_vars_count >= 128) {
                    WARN("Too many local variables");
                    return NULL;
                }
                for (int i = 0; i < MASCOT_LOCAL_VARIABLE_COUNT; i++) {
                    if (!strncasecmp(var->string, locals[i].string, var->string_size)) {
                        mascot_vars[mascot_vars_count++] = locals[i].value;
                        break;
                    }
                }
            }
        }
        if (!strncmp(element->name->string, "symtab_g", element->name->string_size) && strlen("symtab_g") == element->name->string_size) {
            if (element->value->type != json_type_array) {
                WARN("Global symbol table must be an array");
                return NULL;
            }
            globals_found = true;
            struct json_array_s* symtab_g = (struct json_array_s*)element->value->payload;
            struct json_array_element_s* symtab_g_element = symtab_g->start;
            while (symtab_g_element) {
                if (symtab_g_element->value->type != json_type_string) {
                    WARN("Global symbol table elements must be strings");
                    return NULL;
                }
                struct json_string_s* var = (struct json_string_s*)symtab_g_element->value->payload;
                symtab_g_element = symtab_g_element->next;
                if (globals_count >= 128) {
                    WARN("Too many global variables");
                    return NULL;
                }
                for (int i = 0; i < GLOBAL_SYMS_COUNT+1; i++) {
                    if (i == GLOBAL_SYMS_COUNT) {
                        WARN("Unknown global variable %s", var->string);
                        global_getters[globals_count++] = mascot_noop;
                        break;
                    }
                    if (!strncasecmp(var->string, globals_n_funcs[i].string, var->string_size) && strlen(globals_n_funcs[i].string) == var->string_size) {
                        global_getters[globals_count++] = globals_n_funcs[i].value;
                        break;
                    }
                }
            }
        }
        if (!strncmp(element->name->string, "symtab_f", element->name->string_size) && strlen("symtab_f") == element->name->string_size) {
            if (element->value->type != json_type_array) {
                WARN("Function symbol table must be an array");
                return NULL;
            }
            functions_found = true;
            struct json_array_s* symtab_f = (struct json_array_s*)element->value->payload;
            struct json_array_element_s* symtab_f_element = symtab_f->start;
            while (symtab_f_element) {
                if (symtab_f_element->value->type != json_type_string) {
                    WARN("Function symbol table elements must be strings");
                    return NULL;
                }
                struct json_string_s* var = (struct json_string_s*)symtab_f_element->value->payload;
                symtab_f_element = symtab_f_element->next;
                if (functions_count >= 128) {
                    WARN("Too many functions");
                    return NULL;
                }
                for (int i = 0; i < GLOBAL_SYMS_COUNT+1; i++) {
                    if (i == GLOBAL_SYMS_COUNT) {
                        WARN("Unknown function %s", var->string);
                        function_getters[functions_count++] = mascot_noop;
                        break;
                    }
                    if (!strncasecmp(var->string, globals_n_funcs[i].string, var->string_size)) {
                        function_getters[functions_count++] = globals_n_funcs[i].value;
                        break;
                    }
                }
            }
        }
        if (!strncmp(element->name->string, "evaluate_once", element->name->string_size) && strlen("evaluate_once") == element->name->string_size) {
            if (element->value->type == json_type_null) {
               element = element->next;
               evaluate_once = true;
               continue;
            }
            if (element->value->type != json_type_true && element->value->type != json_type_false) {
                WARN("evaluate_once must be a boolean");
                return NULL;
            }
            evaluate_once = element->value->type == json_type_true;
        }

        element = element->next;
    }

    if (!insructions_found) {
        WARN("Instructions not found");
        return NULL;
    }
    if (!mascot_vars_found) {
        WARN("Local symbol table not found");
        return NULL;
    }
    if (!globals_found) {
        WARN("Global symbol table not found");
        return NULL;
    }
    if (!functions_found) {
        WARN("Function symbol table not found");
        return NULL;
    }

    prototype = expression_prototype_new();
    if (!prototype) {
        WARN("Failed to create prototype");
        return NULL;
    }

    // Check if instructions len is a multiple of 2
    if (bytecode_size % 2 != 0) {
        WARN("Instructions string length must be a multiple of 2");
        return NULL;
    }

    // Load into prototype
    expression_prototype_load_mascot_vars(prototype, mascot_vars, mascot_vars_count);
    expression_prototype_load_global_getters(prototype, global_getters, globals_count);
    expression_prototype_load_function_ptrs(prototype, function_getters, functions_count);

    // load bytecode
    if (!expression_prototype_load_bytecode(prototype, (uint8_t*)bytecode, bytecode_size)) {
        WARN("Failed to load bytecode");
        return NULL;
    }

    expression = (struct mascot_expression*)calloc(1, sizeof(struct mascot_expression));
    if (!expression) {
        WARN("Failed to allocate memory for expression");
        return NULL;
    }

    expression->body = prototype;
    expression->evaluate_once = evaluate_once;

    return expression;
}

struct config_program_loader_result load_programs(struct json_object_s* programs_root)
{
    struct config_program_loader_result result = {0};
    result.expressions = calloc(128, sizeof(struct mascot_expression*));
    int32_t size = 128;

    if (!programs_root) {
        free(result.expressions);
        result.expressions = NULL;
        return result;
    }

    struct json_object_element_s* programs = programs_root->start;
    if (programs->value->type != json_type_array || strcmp(programs->name->string, "programs") != 0) {
        WARN("Invalid syntax in programs.json: first element should be named \"programs\" and be an array");
        free(result.expressions);
        result.expressions = NULL;
        return result;
    }

    struct json_array_s* programs_array = (struct json_array_s*)programs->value->payload;
    struct json_array_element_s* program = programs_array->start;
    int i = 0;
    while (program) {
        if (i == size) {
            size *= 2;
            result.expressions = realloc(result.expressions, size * sizeof(struct mascot_expression*));
        }
        if (program->value->type != json_type_object) {
            WARN("Invalid syntax in programs.json: program should be an object");
            return result;
        }

        struct mascot_expression* expression = parse_program((struct json_object_s*)program->value->payload);
        if (!expression) {
            program = program->next;
            WARN("Failed to parse program num %d", i++);
            continue;
        }
        result.expressions[result.count++] = expression;
        expression->body->id = i;

        program = program->next;
        i++;
    }

    result.ok = true;
    return result;
}

struct config_actionref_parse_result {
    struct mascot_action_reference* actionref;
    enum {
        CONFIG_ACTIONREF_PARSE_OK,
        CONFIG_ACTIONREF_PARSE_ERROR_GENERAL,
        CONFIG_ACTIONREF_PARSE_ERROR_ACTION_NOT_FOUND,
        CONFIG_ACTIONREF_PARSE_ERROR_LOCAL_NOT_FOUND,
        CONFIG_ACTIONREF_INVALID_JSON
    } status;
};

struct config_actionref_parse_result parse_action_reference(struct mascot_prototype* prototype, struct json_object_s* action, struct mascot_action** action_definitions, size_t* count)
{
    struct config_actionref_parse_result result = {0};
    result.status = CONFIG_ACTIONREF_PARSE_ERROR_GENERAL;
    struct mascot_action_reference* actionref_obj = (struct mascot_action_reference*)calloc(1, sizeof(struct mascot_action_reference));
    if (!actionref_obj) {
        WARN("Failed to allocate memory for action");
        return result;
    }

    bool action_name_set = false;
    bool local_overrides_count_set = false;

    actionref_obj->overwritten_locals = calloc(128, sizeof(struct mascot_local_variable*));
    for (int i = 0; i < 128; i++) {
        actionref_obj->overwritten_locals[i] = (struct mascot_local_variable*)calloc(1, sizeof(struct mascot_local_variable));
    }

    result.status = CONFIG_ACTIONREF_INVALID_JSON;
    struct json_object_element_s* element = action->start;
    while (element) {
        struct json_object_element_s* celement = element;
        element = element->next;

        if (!strncmp(celement->name->string, "action_name", celement->name->string_size) && strlen("action_name") == celement->name->string_size) {
            // Should be string, cannot be null
            if (celement->value->type != json_type_string) {
                WARN("action_name must be a string");
                goto actionref_generator_fail;
            }

            // Now we need to find the action in the action_definitions
            struct json_string_s* action_name = (struct json_string_s*)celement->value->payload;
            for (size_t i = 0; i < *count; i++) {
                if (!strncmp(action_definitions[i]->name, action_name->string, action_name->string_size) && strlen(action_definitions[i]->name) == action_name->string_size) {
                    actionref_obj->action = action_definitions[i];
                    action_name_set = true;
                    break;
                }
            }
            if (!action_name_set) {
                WARN("Action %.*s is not defined!", action_name->string_size, action_name->string);
                result.status = CONFIG_ACTIONREF_PARSE_ERROR_ACTION_NOT_FOUND;
                goto actionref_generator_fail;
            }
        } else if (!strncmp(celement->name->string, "locals_count", celement->name->string_size) && strlen("locals_count") == celement->name->string_size) {
            // Should be number, cannot be null
            if (celement->value->type != json_type_number) {
                WARN("locals_count must be a number");
                goto actionref_generator_fail;
            }

            local_overrides_count_set = true;
        }
        else if (!strncmp("locals_overrides", celement->name->string, celement->name->string_size) && strlen("locals_overrides") == celement->name->string_size) {
            if (celement->value->type != json_type_object) {
                WARN("Local vars must be an object");
                goto actionref_generator_fail;
            }
            struct json_object_s* local_vars = (struct json_object_s*)celement->value->payload;
            struct json_object_element_s* local_var = local_vars->start;
            while (local_var) {
                struct json_object_element_s* clocal_var = local_var;
                local_var = local_var->next;

                // Check if value is a number
                if (clocal_var->value->type != json_type_number) {
                    WARN("Local var value must be a number");
                    goto actionref_generator_fail;
                }

                struct json_number_s* local_var_value = (struct json_number_s*)clocal_var->value->payload;
                if (clocal_var->name->string_size > 127) {
                    WARN("Local var name is too long");
                    goto actionref_generator_fail;
                }
                for (int i = 0; i < MASCOT_LOCAL_VARIABLE_COUNT; i++) {
                    if (!strncasecmp(clocal_var->name->string, locals[i].string, clocal_var->name->string_size)) {
                        for (uint16_t j = 0; j < prototype->expressions_count; j++) {
                            if (prototype->expression_definitions[j]->body->id == atoi(local_var_value->number)) {
                                actionref_obj->overwritten_locals[i]->used = true;
                                actionref_obj->overwritten_locals[i]->expr.expression_prototype = (struct mascot_expression*)prototype->expression_definitions[j];
                                actionref_obj->overwritten_locals[i]->expr.kind = locals[i].kind;
                                actionref_obj->overwritten_locals[i]->kind = locals[i].kind;
                                break;
                            }
                        }
                    }
                }
            }
        }
        // Get condition
        else if (!strncmp(celement->name->string, "condition", celement->name->string_size) && strlen("condition") == celement->name->string_size) {
            // Should be number, can be null

            if (celement->value->type == json_type_null) {
                actionref_obj->condition = NULL;
                continue;
            }

            if (celement->value->type != json_type_number) {
                WARN("Condition must be a number");
                goto actionref_generator_fail;
            }

            struct json_number_s* condition = (struct json_number_s*)celement->value->payload;
            // Find the condition in the prototype expression definitions
            for (uint16_t i = 0; i < prototype->expressions_count; i++) {
                if (prototype->expression_definitions[i]->body->id == atoi(condition->number)) {
                    actionref_obj->condition = (struct mascot_expression*)prototype->expression_definitions[i];
                    break;
                }
            }
        }
        // Get duration
        else if (!strncmp(celement->name->string, "duration", celement->name->string_size) && strlen("duration") == celement->name->string_size) {
            // Should be number, can be null

            if (celement->value->type == json_type_null) {
                actionref_obj->condition = NULL;
                continue;
            }

            if (celement->value->type != json_type_number) {
                WARN("Duration must be a number");
                goto actionref_generator_fail;
            }

            struct json_number_s* condition = (struct json_number_s*)celement->value->payload;
            // Find the condition in the prototype expression definitions
            for (uint16_t i = 0; i < prototype->expressions_count; i++) {
                if (prototype->expression_definitions[i]->body->id == atoi(condition->number)) {
                    actionref_obj->duration_limit = (struct mascot_expression*)prototype->expression_definitions[i];
                    break;
                }
            }
        }
    }

    if (!action_name_set) {
        WARN("Action name not set");
        goto actionref_generator_fail;
    }

    if (!local_overrides_count_set) {
        WARN("local_overrides count not set");
        goto actionref_generator_fail;
    }

    result.actionref = actionref_obj;
    result.status = CONFIG_ACTIONREF_PARSE_OK;
    return result;

actionref_generator_fail:
    if (actionref_obj->overwritten_locals) {
        for (size_t i = 0; i < 128; i++) {
            if (actionref_obj->overwritten_locals[i]) {
                free(actionref_obj->overwritten_locals[i]);
            }
        }
        free(actionref_obj->overwritten_locals);
    }
    free(actionref_obj);
    return result;
}

struct mascot_hotspot* parse_hotspot(struct json_object_s* hotspot)
{
    struct mascot_hotspot* hotspot_obj = (struct mascot_hotspot*)calloc(1, sizeof(struct mascot_hotspot));
    if (!hotspot_obj) {
        WARN("Failed to allocate memory for hotspot");
        return NULL;
    }

    char behavior[128] = {0};

    hotspot_obj->button = mascot_hotspot_button_middle;
    hotspot_obj->cursor = mascot_hotspot_cursor_hand;

    struct json_object_element_s* element = hotspot->start;
    while (element) {
        struct json_object_element_s* celement = element;
        element = element->next;

        // Get behavior
        if (!strncmp(celement->name->string, "behavior", celement->name->string_size) && strlen("behavior") == celement->name->string_size) {
            if (celement->value->type == json_type_null) {
                continue;
            }
            if (celement->value->type != json_type_string) {
                WARN("Behavior must be a string");
                goto hotspot_generator_fail;
            }

            struct json_string_s* behavior_str = (struct json_string_s*)celement->value->payload;
            if (behavior_str->string_size > 127) {
                WARN("Behavior string is too long");
                goto hotspot_generator_fail;
            }
            strncpy(behavior, behavior_str->string, behavior_str->string_size);
        }
        else if (!strncmp("x", celement->name->string, celement->name->string_size) && strlen("x") == celement->name->string_size) {
            if (celement->value->type != json_type_number) {
                WARN("X must be a number");
                goto hotspot_generator_fail;
            }

            struct json_number_s* x = (struct json_number_s*)celement->value->payload;
            hotspot_obj->x = atoi(x->number);
        }
        else if (!strncmp("y", celement->name->string, celement->name->string_size) && strlen("y") == celement->name->string_size) {
            if (celement->value->type != json_type_number) {
                WARN("Y must be a number");
                goto hotspot_generator_fail;
            }

            struct json_number_s* y = (struct json_number_s*)celement->value->payload;
            hotspot_obj->y = atoi(y->number);
        }
        else if (!strncmp("width", celement->name->string, celement->name->string_size) && strlen("width") == celement->name->string_size) {
            if (celement->value->type != json_type_number) {
                WARN("Width must be a number");
                goto hotspot_generator_fail;
            }

            struct json_number_s* width = (struct json_number_s*)celement->value->payload;
            hotspot_obj->width = atoi(width->number);
        }
        else if (!strncmp("height", celement->name->string, celement->name->string_size) && strlen("height") == celement->name->string_size) {
            if (celement->value->type != json_type_number) {
                WARN("Height must be a number");
                goto hotspot_generator_fail;
            }

            struct json_number_s* height = (struct json_number_s*)celement->value->payload;
            hotspot_obj->height = atoi(height->number);
        }
        else if (!strncmp("shape", celement->name->string, celement->name->string_size) && strlen("shape") == celement->name->string_size) {
            if (celement->value->type != json_type_string) {
                WARN("shape must be a string");
                goto hotspot_generator_fail;
            }

            struct json_string_s* type = (struct json_string_s*)celement->value->payload;
            if (strncasecmp("Ellipse", type->string, type->string_size) == 0) {
                hotspot_obj->shape = mascot_hotspot_shape_ellipse;
            } else if (strncasecmp("Rectangle", type->string, type->string_size) == 0) {
                hotspot_obj->shape = mascot_hotspot_shape_rectangle;
            } else {
                WARN("Unknown shape type");
                goto hotspot_generator_fail;
            }
        }
        else if (!strncmp("button", celement->name->string, celement->name->string_size) && strlen("button") == celement->name->string_size) {
            if (celement->value->type != json_type_string) {
                WARN("button must be a string");
                goto hotspot_generator_fail;
            }

            struct json_string_s* type = (struct json_string_s*)celement->value->payload;
            if (strncasecmp("Left", type->string, type->string_size) == 0) {
                hotspot_obj->button = mascot_hotspot_button_left;
            } else if (strncasecmp("Middle", type->string, type->string_size) == 0) {
                hotspot_obj->button = mascot_hotspot_button_middle;
            } else if (strncasecmp("Right", type->string, type->string_size) == 0) {
                hotspot_obj->button = mascot_hotspot_button_right;
            } else {
                WARN("Unknown button type");
                goto hotspot_generator_fail;
            }
        }
        else if (!strncmp("cursor", celement->name->string, celement->name->string_size) && strlen("cursor") == celement->name->string_size) {
            if (celement->value->type != json_type_string) {
                WARN("cursor must be a string");
                goto hotspot_generator_fail;
            }

            struct json_string_s* type = (struct json_string_s*)celement->value->payload;
            if (strncasecmp("Default", type->string, type->string_size) == 0) {
                hotspot_obj->cursor = mascot_hotspot_cursor_hand;
            } else if (strncasecmp("Hand", type->string, type->string_size) == 0) {
                hotspot_obj->cursor = mascot_hotspot_cursor_hand;
            } else if (strncasecmp("Crosshair", type->string, type->string_size) == 0) {
                hotspot_obj->cursor = mascot_hotspot_cursor_crosshair;
            } else if (strncasecmp("Move", type->string, type->string_size) == 0) {
                hotspot_obj->cursor = mascot_hotspot_cursor_move;
            } else if (strncasecmp("Text", type->string, type->string_size) == 0) {
                hotspot_obj->cursor = mascot_hotspot_cursor_text;
            } else if (strncasecmp("Wait", type->string, type->string_size) == 0) {
                hotspot_obj->cursor = mascot_hotspot_cursor_wait;
            } else if (strncasecmp("Help", type->string, type->string_size) == 0) {
                hotspot_obj->cursor = mascot_hotspot_cursor_help;
            } else if (strncasecmp("Progress", type->string, type->string_size) == 0) {
                hotspot_obj->cursor = mascot_hotspot_cursor_progress;
            } else if (strncasecmp("NotAllowed", type->string, type->string_size) == 0) {
                hotspot_obj->cursor = mascot_hotspot_cursor_deny;
            } else {
                WARN("Unknown cursor type");
                goto hotspot_generator_fail;
            }
        }
    }

    if (strlen(behavior)) {
        hotspot_obj->behavior = strdup(behavior);
    }

    return hotspot_obj;

hotspot_generator_fail:
    free(hotspot_obj);
    return NULL;

}

struct mascot_pose* parse_pose(struct mascot_prototype* prototype, struct json_object_s* pose)
{
    struct mascot_pose* pose_obj = (struct mascot_pose*)calloc(1, sizeof(struct mascot_pose));
    if (!pose_obj) {
        WARN("Failed to allocate memory for pose");
        return NULL;
    }

    bool sprite_set = false;
    bool sprite_right_set = false;
    bool duration_set = false;

    char image[128] = {0};
    char image_right[128] = {0};

    struct json_object_element_s* element = pose->start;
    while (element) {
        struct json_object_element_s* celement = element;
        element = element->next;

        // Get sprite
        if (!strncmp(celement->name->string, "image", celement->name->string_size) && strlen("image") == celement->name->string_size) {
            if (celement->value->type == json_type_null) {
                continue;
            }

            if (celement->value->type != json_type_string) {
                WARN("Sprite must be a string");
                goto pose_generator_fail;
            }

            struct json_string_s* sprite = (struct json_string_s*)celement->value->payload;
            snprintf(image, sprite->string_size+1, "%s", sprite->string);
            sprite_set = true;

        }

        if (!strncmp(celement->name->string, "image_right", celement->name->string_size) && strlen("image_right") == celement->name->string_size) {
            if (celement->value->type == json_type_null) {
                continue;
            }
            if (celement->value->type != json_type_string) {
                WARN("Sprite must be a string");
                goto pose_generator_fail;
            }

            struct json_string_s* sprite = (struct json_string_s*)celement->value->payload;
            snprintf(image_right, sprite->string_size+1, "%s", sprite->string);
            sprite_right_set = true;
        }

        // Get duration
        else if (!strncmp(celement->name->string, "duration", celement->name->string_size) && strlen("duration") == celement->name->string_size) {
            if (celement->value->type != json_type_number) {
                WARN("Duration must be a number");
                goto pose_generator_fail;
            }

            struct json_number_s* duration = (struct json_number_s*)celement->value->payload;
            pose_obj->duration = atoi(duration->number);
            duration_set = true;
        }

        // Get anchor_x
        else if (!strncmp(celement->name->string, "image_anchor_x", celement->name->string_size) && strlen("image_anchor_x") == celement->name->string_size) {
            if (celement->value->type != json_type_number) {
                WARN("Anchor X must be a number");
                goto pose_generator_fail;
            }

            struct json_number_s* anchor_x = (struct json_number_s*)celement->value->payload;
            pose_obj->anchor_x = -atoi(anchor_x->number);
        }

        // Get anchor_y
        else if (!strncmp(celement->name->string, "image_anchor_y", celement->name->string_size) && strlen("image_anchor_y") == celement->name->string_size) {
            if (celement->value->type != json_type_number) {
                WARN("Anchor Y must be a number");
                goto pose_generator_fail;
            }

            struct json_number_s* anchor_y = (struct json_number_s*)celement->value->payload;
            pose_obj->anchor_y = -atoi(anchor_y->number);
        }

        // Get velocity_X
        else if (!strncmp(celement->name->string, "velocity_x", celement->name->string_size) && strlen("velocity_x") == celement->name->string_size) {
            if (celement->value->type != json_type_number) {
                WARN("Velocity X must be a number");
                goto pose_generator_fail;
            }

            struct json_number_s* velocity_x = (struct json_number_s*)celement->value->payload;
            pose_obj->velocity_x = atoi(velocity_x->number);
        }

        // Get velocity_Y
        else if (!strncmp(celement->name->string, "velocity_y", celement->name->string_size) && strlen("velocity_y") == celement->name->string_size) {
            if (celement->value->type != json_type_number) {
                WARN("Velocity Y must be a number");
                goto pose_generator_fail;
            }

            struct json_number_s* velocity_y = (struct json_number_s*)celement->value->payload;
            pose_obj->velocity_y = atoi(velocity_y->number);
        }
    }

    if (!duration_set) {
        WARN("Duration not set");
        goto pose_generator_fail;
    }

    if (sprite_set) {
        uint16_t sprite_index = mascot_atlas_get_name_index(prototype->atlas, image);
        pose_obj->sprite[0] = mascot_atlas_get(prototype->atlas, sprite_index, false);
        if (!pose_obj->sprite[0]) {
            WARN("Failed to get sprite");
            goto pose_generator_fail;
        }

        if (sprite_right_set) {
            sprite_index = mascot_atlas_get_name_index(prototype->atlas, image_right);
            pose_obj->sprite[1] = mascot_atlas_get(prototype->atlas, sprite_index, false);
            if (!pose_obj->sprite[1]) {
                WARN("Failed to get sprite");
                goto pose_generator_fail;
            }
        } else {
            pose_obj->sprite[1] = mascot_atlas_get(prototype->atlas, sprite_index, true);
        }
    }

    return pose_obj;


pose_generator_fail:

    free(pose_obj);
    return NULL;

}

struct mascot_animation* parse_animation(struct mascot_prototype* prototype, struct json_object_s* animation)
{
    struct mascot_animation* animation_obj = (struct mascot_animation*)calloc(1, sizeof(struct mascot_animation));
    if (!animation_obj) {
        WARN("Failed to allocate memory for animation");
        return NULL;
    }

    animation_obj->frames = calloc(8, sizeof(struct mascot_pose*));
    animation_obj->hotspots = calloc(2, sizeof(struct mascot_hotspot*));
    size_t size = 8;
    size_t hsize = 2;

    bool frame_count_set = false;
    bool frames_set = false;

    struct json_object_element_s* element = animation->start;
    while (element) {
        struct json_object_element_s* celement = element;
        element = element->next;

        // Get frame count
        if (!strncmp(celement->name->string, "frame_count", celement->name->string_size) && strlen("frame_count") == celement->name->string_size) {
            if (celement->value->type != json_type_number) {
                WARN("Frame count must be a number");
                goto animation_parser_fail;
            }

            struct json_number_s* frame_count = (struct json_number_s*)celement->value->payload;
            animation_obj->frame_count = atoi(frame_count->number);
            frame_count_set = true;
        }
        // Get frames
        else if (!strncmp(celement->name->string, "frames", celement->name->string_size) && strlen("frames") == celement->name->string_size) {
            if (celement->value->type != json_type_array) {
                WARN("Frames must be an array");
                goto animation_parser_fail;
            }

            struct json_array_s* frames = (struct json_array_s*)celement->value->payload;
            struct json_array_element_s* frame = frames->start;
            size_t i = 0;
            while (frame) {

                if (frame->value->type != json_type_object) {
                    WARN("Frame must be an object");
                    goto animation_parser_fail;
                }

                if (i >= size) {
                    size *= 2;
                    animation_obj->frames = realloc(animation_obj->frames, size * sizeof(struct mascot_pose*));
                }

                struct json_object_s* frame_obj = (struct json_object_s*)frame->value->payload;
                animation_obj->frames[i] = parse_pose(prototype, frame_obj);
                if (!animation_obj->frames[i]) {
                    WARN("Failed to parse frame");
                    goto animation_parser_fail;
                }

                frame = frame->next;
                i++;
            }

            frames_set = true;
        }
        // Get hotspots
        else if (!strncmp(celement->name->string, "hotspots", celement->name->string_size) && strlen("hotspots") == celement->name->string_size) {
            if (celement->value->type != json_type_array) {
                WARN("Hotspots must be an array");
                goto animation_parser_fail;
            }

            struct json_array_s* hotspots = (struct json_array_s*)celement->value->payload;
            struct json_array_element_s* hotspot = hotspots->start;
            size_t i = 0;
            while (hotspot) {

                if (hotspot->value->type != json_type_object) {
                    WARN("Hotspot must be an object");
                    goto animation_parser_fail;
                }

                if (i >= hsize) {
                    hsize *= 2;
                    animation_obj->hotspots = realloc(animation_obj->frames, hsize * sizeof(struct mascot_hotspot*));
                }

                struct json_object_s* hotspot_obj = (struct json_object_s*)hotspot->value->payload;
                animation_obj->hotspots[i] = parse_hotspot(hotspot_obj);
                if (!animation_obj->hotspots[i]) {
                    WARN("Failed to parse hotspot");
                    goto animation_parser_fail;
                }

                animation_obj->hotspots_count++;

                hotspot = hotspot->next;
                i++;
            }
        }
        // Get condition
        else if (!strncmp(celement->name->string, "condition", celement->name->string_size) && strlen("condition") == celement->name->string_size) {
            // Should be number, can be null

            if (celement->value->type == json_type_null) {
                animation_obj->condition = NULL;
                continue;
            }

            if (celement->value->type != json_type_number) {
                WARN("Condition must be a number");
                goto animation_parser_fail;
            }

            struct json_number_s* condition = (struct json_number_s*)celement->value->payload;
            // Find the condition in the prototype expression definitions
            for (uint16_t i = 0; i < prototype->expressions_count; i++) {
                if (prototype->expression_definitions[i]->body->id == atoi(condition->number)) {
                    animation_obj->condition = (struct mascot_expression*)prototype->expression_definitions[i];
                    break;
                }
            }
        }
    }

    if (!frame_count_set) {
        WARN("Frame count not set");
        goto animation_parser_fail;
    }

    if (!frames_set) {
        WARN("Frames not set");
        goto animation_parser_fail;
    }

    return animation_obj;

animation_parser_fail:
    for (size_t i = 0; i < size; i++) {
        if (animation_obj->frames[i]) {
            free(animation_obj->frames[i]);
        }
    }
    free(animation_obj->frames);
    free(animation_obj);
    return NULL;
}
struct config_action_parse_result action_parse(struct mascot_prototype* prototype, struct json_object_s* action, struct mascot_action** action_definitions, size_t* count)
{
    struct config_action_parse_result result = {0};
    result.status = MASCOT_ACTION_PARSE_ERROR_GENERAL;
    struct mascot_action* action_obj = (struct mascot_action*)calloc(1, sizeof(struct mascot_action));
    if (!action_obj) {
        WARN("Failed to allocate memory for action");
        return result;
    }

    bool name_set = false;
    bool content_count_set = false;
    bool local_var_count_set = false;
    uint16_t local_vars_count = 0;
    enum mascot_action_embedded_property embedded_type = mascot_action_embedded_property_none;
    enum mascot_action_type type = mascot_action_type_stay;

    action_obj->variables = calloc(128, sizeof(struct mascot_local_variable*));
    if (!action_obj->variables) {
        WARN("Failed to allocate memory for local variables");
        goto action_generator_fail;
    }

    for (int i = 0; i < 128; i++)
        action_obj->variables[i] = calloc(1, sizeof(struct mascot_local_variable));

    struct json_object_element_s* element = action->start;

    result.status = MASCOT_ACTION_PARSE_ERROR_BAD_ACTION;
    while (element)
    {
        struct json_object_element_s* celement = element;
        element = element->next;

        if (!strncmp(celement->name->string, "name", celement->name->string_size) && strlen("name") == celement->name->string_size) {
            if (name_set) {
                WARN("Name already set");
                goto action_generator_fail;
            }
            if (celement->value->type != json_type_string) {
                WARN("Name must be a string");
                goto action_generator_fail;
            }
            action_obj->name = strndup(((struct json_string_s*)celement->value->payload)->string, ((struct json_string_s*)celement->value->payload)->string_size);
            name_set = true;
        }
        if (!strncmp("type", celement->name->string, celement->name->string_size) && strlen("type") == celement->name->string_size) {
            if (celement->value->type != json_type_string) {
                WARN("Type must be a string");
                continue;
            }
            char* type_str = strndup(((struct json_string_s*)celement->value->payload)->string, ((struct json_string_s*)celement->value->payload)->string_size);
            size_t type_str_len = strlen(type_str);
            if (!strncasecmp("stay", type_str, type_str_len)) {
                type = mascot_action_type_stay;
            } else if (!strncasecmp("move", type_str, type_str_len)) {
                type = mascot_action_type_move;
            } else if (!strncasecmp("embedded", type_str, type_str_len)) {
                type = mascot_action_type_embedded;
            } else if (!strncasecmp("animate", type_str, type_str_len)) {
                type = mascot_action_type_animate;
            } else if (!strncasecmp("sequence", type_str, type_str_len)) {
                type = mascot_action_type_sequence;
            } else if (!strncasecmp("select", type_str, type_str_len)) {
                type = mascot_action_type_select;
            } else {
                WARN("Unknown action type: %s", type_str);
                continue;
            }
        }
        if (!strncmp("content_count", celement->name->string, celement->name->string_size) && strlen("content_count") == celement->name->string_size) {
            if (content_count_set) {
                WARN("Content count already set");
                goto action_generator_fail;
            }
            if (celement->value->type != json_type_number) {
                WARN("Content count must be a number, but %d received", celement->value->type);;
                goto action_generator_fail;
            }
            action_obj->length = atoi(((struct json_number_s*)celement->value->payload)->number);
            content_count_set = true;
        }
        if (!strncmp("local_variables_count", celement->name->string, celement->name->string_size) && strlen("local_variables_count") == celement->name->string_size) {
            if (local_var_count_set) {
                WARN("Local var count already set");
                goto action_generator_fail;
            }
            if (celement->value->type != json_type_number) {
                WARN("Local var count must be a number");
                goto action_generator_fail;
            }
            local_vars_count = atoi(((struct json_number_s*)celement->value->payload)->number);
            local_var_count_set = true;
        }
        if (!strncmp("embedded_type", celement->name->string, celement->name->string_size) && strlen("embedded_type") == celement->name->string_size) {
            if (embedded_type != mascot_action_embedded_property_none) {
                WARN("Embedded type already set");
                goto action_generator_fail;
            }
            if (celement->value->type == json_type_null) {
                continue;
            }
            if (celement->value->type != json_type_string) {
                WARN("Embedded type must be a string");
                goto action_generator_fail;
            }
            char* embedded_type_str = strndup(((struct json_string_s*)celement->value->payload)->string, ((struct json_string_s*)celement->value->payload)->string_size);
            size_t embedded_type_str_len = strlen(embedded_type_str);
            if (!strncasecmp("none", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_none;
            } else if (!strncasecmp("look", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_look;
            } else if (!strncasecmp("offset", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_offset;
            } else if (!strncasecmp("jump", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_jump;
            } else if (!strncasecmp("fall", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_fall;
            } else if (!strncasecmp("dragged", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_drag;
            } else if (!strncasecmp("resist", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_drag_resist;
            } else if (!strncasecmp("breed", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_clone;
            } else if (!strncasecmp("broadcast", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_broadcast;
            } else if (!strncasecmp("scanmove", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_scanmove;
            } else if (!strncasecmp("scanjump", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_scanjump;
            } else if (!strncasecmp("interact", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_interact;
            } else if (!strncasecmp("dispose", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_dispose;
            } else if (!strncasecmp("transform", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_transform;
            } else if (!strncasecmp("thrown", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_thrown;
            } else if (!strncasecmp("walkwithie", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_walkwithie;
            } else if (!strncasecmp("fallwithie", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_fallwithie;
            } else if (!strncasecmp("throwie", embedded_type_str, embedded_type_str_len)) {
                embedded_type = mascot_action_embedded_property_throwie;
            } else {
                WARN("Unknown embedded type: %s", embedded_type_str);
                embedded_type = mascot_action_embedded_property_unknown;
                continue;
            }
        }
        if (!strncmp("condition", celement->name->string, celement->name->string_size) && strlen("condition") == celement->name->string_size) {
            if (celement->value->type == json_type_null) {
                continue;
            }
            if (celement->value->type != json_type_number) {
                WARN("Condition must be a string");
                goto action_generator_fail;
            }
            int16_t condition_name = atoi(((struct json_number_s*)celement->value->payload)->number);
            if (condition_name < 0 || condition_name > 127) {
                WARN("Condition name must be between 0 and 127");
                continue;
            }
            for (uint16_t i = 0; i < prototype->expressions_count; i++) {
                if (prototype->expression_definitions[i]->body->id == condition_name) {
                    action_obj->condition = prototype->expression_definitions[i];
                    break;
                }
            }
        }
        // loop parameter, can be null
        if (!strncmp("loop", celement->name->string, celement->name->string_size) && strlen("loop") == celement->name->string_size) {
            if (celement->value->type == json_type_null) {
                action_obj->loop = true;
                continue;
            }
            if (celement->value->type != json_type_true && celement->value->type != json_type_false) {
                WARN("Loop must be a boolean");
                goto action_generator_fail;
            }
            action_obj->loop = celement->value->type == json_type_true;
        }
        // local variables, required if local_vars_count is set
        if (!strncmp("local_variables", celement->name->string, celement->name->string_size) && strlen("local_variables") == celement->name->string_size) {
            if (celement->value->type != json_type_object) {
                WARN("Local vars must be an object");
                goto action_generator_fail;
            }
            struct json_object_s* local_vars = (struct json_object_s*)celement->value->payload;
            struct json_object_element_s* local_var = local_vars->start;
            while (local_var) {
                struct json_object_element_s* curr_local_var = local_var;
                local_var = local_var->next;

                // Check if value is a number
                if (curr_local_var->value->type != json_type_number) {
                    WARN("Local var value must be a number");
                    goto action_generator_fail;
                }

                struct json_number_s* local_var_value = (struct json_number_s*)curr_local_var->value->payload;
                if (curr_local_var->name->string_size > 127) {
                    WARN("Local var name is too long");
                    goto action_generator_fail;
                }

                for (int i = 0; i < MASCOT_LOCAL_VARIABLE_COUNT; i++) {
                    if (!strncasecmp(curr_local_var->name->string, locals[i].string, strlen(locals[i].string))) {
                        for (uint16_t j = 0; j < prototype->expressions_count; j++) {
                            if (prototype->expression_definitions[j]->body->id == atoi(local_var_value->number)) {
                                action_obj->variables[i]->used = true;
                                action_obj->variables[i]->expr.expression_prototype = (struct mascot_expression*)prototype->expression_definitions[j];
                                action_obj->variables[i]->expr.kind = locals[i].kind;
                                action_obj->variables[i]->kind = locals[i].kind;
                                break;
                            }
                        }
                    }
                }
            }
        }
        // Get target_behavior string
        if (!strncmp("target_behavior", celement->name->string, celement->name->string_size) && strlen("target_behavior") == celement->name->string_size) {
            if (celement->value->type != json_type_string) {
                WARN("Target behavior must be a string");
                goto action_generator_fail;
            }
            struct json_string_s* target_behavior = (struct json_string_s*)celement->value->payload;
            action_obj->target_behavior = strndup(target_behavior->string, target_behavior->string_size);
        }
        if (!strncmp("behavior", celement->name->string, celement->name->string_size) && strlen("behavior") == celement->name->string_size) {
            if (celement->value->type != json_type_string) {
                WARN("Behavior must be a string");
                goto action_generator_fail;
            }
            struct json_string_s* behavior = (struct json_string_s*)celement->value->payload;
            action_obj->behavior = strndup(behavior->string, behavior->string_size);
        }
        // Get select_behavior string
        if (!strncmp("select_behavior", celement->name->string, celement->name->string_size) && strlen("select_behavior") == celement->name->string_size) {
            if (celement->value->type != json_type_string) {
                WARN("Select behavior must be a string");
                goto action_generator_fail;
            }
            struct json_string_s* select_behavior = (struct json_string_s*)celement->value->payload;
            action_obj->select_behavior = strndup(select_behavior->string, select_behavior->string_size);
        }
        // Get born_behavior string
        if (!strncmp("born_behavior", celement->name->string, celement->name->string_size) && strlen("born_behavior") == celement->name->string_size) {

            if (celement->value->type != json_type_string) {
                WARN("Born behavior must be a string");
                goto action_generator_fail;
            }
            struct json_string_s* born_behavior = (struct json_string_s*)celement->value->payload;
            action_obj->born_behavior = strndup(born_behavior->string, born_behavior->string_size);
        }
        if (!strncmp("born_mascot", celement->name->string, celement->name->string_size) && strlen("born_mascot") == celement->name->string_size) {

            if (celement->value->type != json_type_string) {
                WARN("Born mascot must be a string");
                goto action_generator_fail;
            }
            struct json_string_s* born_behavior = (struct json_string_s*)celement->value->payload;
            action_obj->born_mascot = strndup(born_behavior->string, born_behavior->string_size);
        }
        // Get affordance string
        if (!strncmp("affordance", celement->name->string, celement->name->string_size) && strlen("affordance") == celement->name->string_size) {
            if (celement->value->type != json_type_string) {
                WARN("Affordance must be a string");
                goto action_generator_fail;
            }
            struct json_string_s* affordance = (struct json_string_s*)celement->value->payload;
            action_obj->affordance = strndup(affordance->string, affordance->string_size);
        }
        // Get transform_target
        if (!strncmp("transform_target", celement->name->string, celement->name->string_size) && strlen("transform_target") == celement->name->string_size) {
            if (celement->value->type != json_type_string) {
                WARN("Transform target must be a string");
                goto action_generator_fail;
            }
            struct json_string_s* transform_target = (struct json_string_s*)celement->value->payload;
            action_obj->transform_target = strndup(transform_target->string, transform_target->string_size);
        }
        // Get target_look
        if (!strncmp("target_look", celement->name->string, celement->name->string_size) && strlen("target_look") == celement->name->string_size) {
            if (celement->value->type != json_type_true && celement->value->type != json_type_false) {
                WARN("Target look target must be a boolean");
                goto action_generator_fail;
            }
            action_obj->target_look = celement->value->type == json_type_true;
        }
        // Get content array, content is array of objects
        if (!strncmp("content", celement->name->string, celement->name->string_size) && strlen("content") == celement->name->string_size) {
            if (celement->value->type != json_type_array) {
                WARN("Content must be an array");
                goto action_generator_fail;
            }
            struct json_array_s* content = (struct json_array_s*)celement->value->payload;
            struct json_array_element_s* content_element = content->start;
            int j = 0;
            while (content_element) {
                struct json_array_element_s* content_element_current = content_element;
                content_element = content_element->next;

                if (j >= 64 || (local_var_count_set && j >= local_vars_count)) {
                    WARN("Content array is too long");
                    goto action_generator_fail;
                }
                if (content_element_current->value->type != json_type_object) {
                    WARN("Content element must be an object");
                    goto action_generator_fail;
                }
                struct json_object_s* content_obj = (struct json_object_s*)content_element_current->value->payload;
                struct json_object_element_s* content_obj_element = content_obj->start;
                while (content_obj_element) {
                    struct json_object_element_s* content_obj_element_current = content_obj_element;
                    content_obj_element = content_obj_element->next;
                    if (!strncmp("type", content_obj_element_current->name->string, content_obj_element_current->name->string_size)) {
                        if (content_obj_element_current->value->type != json_type_string) {
                            WARN("Content name must be a string");
                            goto action_generator_fail;
                        }
                        struct json_string_s* content_type = (struct json_string_s*)content_obj_element_current->value->payload;
                        if (!strncasecmp("ActionReference", content_type->string, content_type->string_size)) {
                            struct config_actionref_parse_result action_reference = parse_action_reference(prototype, content_obj, action_definitions, count);
                            if (action_reference.status != CONFIG_ACTIONREF_PARSE_OK) {
                                WARN("Failed to parse action reference");
                                if (action_reference.status == CONFIG_ACTIONREF_PARSE_ERROR_ACTION_NOT_FOUND) {
                                    result.status = MASCOT_ACTION_PARSE_ERROR_NOT_DEFINED;
                                }
                                goto action_generator_fail;
                            }
                            action_obj->content[j].kind = mascot_action_content_type_action_reference;
                            action_obj->content[j].value.action_reference = action_reference.actionref;
                            j++;
                        } else if (!strncasecmp("Animation", content_type->string, content_type->string_size)) {
                            struct mascot_animation* animation = parse_animation(prototype, content_obj);
                            if (!animation) {
                                WARN("Failed to parse animation");
                                goto action_generator_fail;
                            }
                            action_obj->content[j].kind = mascot_action_content_type_animation;
                            action_obj->content[j].value.animation = animation;
                            j++;
                        } else {
                            WARN("Unknown content type");
                            goto action_generator_fail;
                        }
                    }
                }
            }
        }
        // Get border type
        if (!strncmp("border_type", celement->name->string, celement->name->string_size) && strlen("border_type") == celement->name->string_size) {
            if (celement->value->type != json_type_string) {
                WARN("Border type must be a string");
                goto action_generator_fail;
            }
            struct json_string_s* border_type = (struct json_string_s*)celement->value->payload;
            if (!strncasecmp("None", border_type->string, border_type->string_size)) {
                action_obj->border_type = environment_border_type_none;
            } else if (!strncasecmp("Wall", border_type->string, border_type->string_size)) {
                action_obj->border_type = environment_border_type_wall;
            } else if (!strncasecmp("Floor", border_type->string, border_type->string_size)) {
                action_obj->border_type = environment_border_type_floor;
            } else if (!strncasecmp("Ceiling", border_type->string, border_type->string_size)) {
                action_obj->border_type = environment_border_type_ceiling;
            } else {
                action_obj->border_type = environment_border_type_any;
            }
        }

    }

    if (!name_set) {
        WARN("Action must have a name");
        goto action_generator_fail;
    }

    if (!content_count_set) {
        WARN("Action must have a content count");
        goto action_generator_fail;
    }

    if (!local_var_count_set) {
        WARN("Action must have a local variable count");
        goto action_generator_fail;
    }

    if (embedded_type == mascot_action_embedded_property_clone) {
        if (action_obj->length) {
            if (action_obj->content[0].kind == mascot_action_content_type_animation) {
                action_obj->content[0].value.animation->frames[0]->anchor_x -= 32;
            }
        }
    }

    action_obj->type = type;
    action_obj->embedded_type = embedded_type;

    result.status = MASCOT_ACTION_PARSE_OK;
    result.action = action_obj;
    return result;

action_generator_fail:

    if (name_set) {
        WARN("Failed to load action named %s: %d", action_obj->name, result.status);
    } else {
        WARN("Failed to load action");
    }

    if (action_obj->variables) {
        for (size_t i = 0; i < 128; i++) {
            free(action_obj->variables[i]);
        }
    }
    free(action_obj->variables);

    free(action_obj->name);
    free(action_obj);
    return result;

}


struct config_action_loader_result load_actions(struct mascot_prototype* prototype, struct json_array_s* actions)
{
    struct config_action_loader_result result = {0};
    result.actions = calloc(128, sizeof(struct mascot_action*));
    size_t size = 128;

    if (!actions) {
        free(result.actions);
        return result;
    }

    struct json_object_s** deferred_resolving = calloc(actions->length, sizeof(struct json_object_s*));
    int deferred_count = 0;

    struct json_array_element_s* action = actions->start;
    while (action) {
        if (result.count == size) {
            size *= 2;
            result.actions = realloc(result.actions, size * sizeof(struct mascot_action*));
        }
        if (action->value->type != json_type_object) {
            WARN("Invalid syntax in actions.json: action should be an object");
            free(deferred_resolving);
            return result;
        }

        struct config_action_parse_result action_obj = action_parse(prototype, (struct json_object_s*)action->value->payload, result.actions, &result.count);
        if (action_obj.status != MASCOT_ACTION_PARSE_OK) {
            if (action_obj.status == MASCOT_ACTION_PARSE_ERROR_NOT_DEFINED) {
                deferred_resolving[deferred_count++] = (struct json_object_s*)action->value->payload;
            }
            action = action->next;
            continue;
        }
        DEBUG("Loaded action %s", action_obj.action->name);
        result.actions[result.count++] = action_obj.action;

        action = action->next;
    }

    DEBUG("Retrying load action with unresolved action names (%d)", deferred_count);

    for (int i = 0; i < deferred_count; i++) {
        if (result.count == size) {
            size *= 2;
            result.actions = realloc(result.actions, size * sizeof(struct mascot_action*));
        }
        struct config_action_parse_result action_obj = action_parse(prototype, deferred_resolving[i], result.actions, &result.count);
        if (action_obj.status != MASCOT_ACTION_PARSE_OK) {
            continue;
        }
        DEBUG("Loaded action %s", action_obj.action->name);
        result.actions[result.count++] = action_obj.action;
    }

    free(deferred_resolving);

    result.ok = true;
    return result;
}

struct config_behavior_parse_result {
    struct mascot_behavior* behavior;
    enum {
        MASCOT_BEHAVIOR_PARSE_OK,
        MASCOT_BEHAVIOR_PARSE_ERROR_NOT_DEFINED,
        MASCOT_BEHAVIOR_PARSE_ERROR_ACTION_NOT_DEFINED,
        MASCOT_BEHAVIOR_PARSE_ERROR,
    } status;
};

struct config_behaviorref_parse_result {
    struct mascot_behavior_reference behavior;
    enum {
        MASCOT_BEHAVIORREF_PARSE_OK,
        MASCOT_BEHAVIORREF_PARSE_ERROR_BEHAVIOR_NOT_DEFINED,
        MASCOT_BEHAVIORREF_PARSE_ERROR,
    } status;
};

struct mascot_temporal_behavior_reference {
    const char* name;
    uint64_t frequency;
    void* _condition;
};

struct config_behaviorref_parse_result parse_behaviorref(struct mascot_prototype* prototype, struct json_object_s* behaviorref, struct mascot_behavior** behaviors_definitions, size_t* bcount, bool do_temporal)
{
    UNUSED(prototype);
    struct config_behaviorref_parse_result result = {0};
    result.status = MASCOT_BEHAVIORREF_PARSE_ERROR;

    bool name_set = false;

    struct mascot_temporal_behavior_reference tempref = {0};

    struct json_object_element_s* element = behaviorref->start;
    while (element) {
        struct json_object_element_s* celement = element;
        element = element->next;

        if (!strncmp("name", celement->name->string, celement->name->string_size) && celement->name->string_size == 4) {
            if (celement->value->type != json_type_string) {
                WARN("Behavior reference name must be a string");
                return result;
            }

            if (do_temporal) {
                tempref.name = strndup(((struct json_string_s*)celement->value->payload)->string, ((struct json_string_s*)celement->value->payload)->string_size);
                name_set = true;
            } else {
                struct json_string_s* name = (struct json_string_s*)celement->value->payload;
                for (size_t i = 0; i < *bcount; i++) {
                    if (!strncmp(name->string, behaviors_definitions[i]->name, name->string_size)) {
                        result.behavior.behavior = behaviors_definitions[i];
                        name_set = true;
                    }
                }
                if (!name_set) {
                    WARN("Behavior %.*s not found", name->string_size, name->string);
                    result.status = MASCOT_BEHAVIORREF_PARSE_ERROR_BEHAVIOR_NOT_DEFINED;
                    return result;
                }
            }
        }
        else if (!strncmp("frequency", celement->name->string, celement->name->string_size) && celement->name->string_size == 9) {
            if (celement->value->type != json_type_number) {
                WARN("Behavior reference frequency must be a number");
                return result;
            }

            struct json_number_s* frequency = (struct json_number_s*)celement->value->payload;
            result.behavior.frequency = atoll(frequency->number);
            tempref.frequency = result.behavior.frequency;
        }
    }
    if (!name_set) {
        WARN("Behavior reference name not set");
        return result;
    }

    if (do_temporal) {
        *(struct mascot_temporal_behavior_reference*)(void*)&result.behavior = tempref;
    }

    result.status = MASCOT_BEHAVIORREF_PARSE_OK;
    return result;
}

bool resolve_temporal_behavior_reference(struct mascot_prototype* prototype, struct mascot_behavior* behavior, struct mascot_behavior** behaviors_definitions, size_t bcount)
{
    UNUSED(prototype);
    for (uint16_t i = 0; i < behavior->next_behaviors_count; i++) {
        struct mascot_temporal_behavior_reference ref = *(struct mascot_temporal_behavior_reference*)(void*)&behavior->next_behavior_list[i];
        bool replaced = false;
        DEBUG("Resolving behavior ref %s", ref.name);
        for (uint16_t j = 0; j < bcount; j++) {
            if (!strncmp(ref.name, behaviors_definitions[j]->name, strlen(behaviors_definitions[j]->name)) && strlen(ref.name) == strlen(behaviors_definitions[j]->name)) {
                behavior->next_behavior_list[i] = (struct mascot_behavior_reference){.behavior = behaviors_definitions[j], .frequency = ref.frequency, .condition = behavior->is_condition ? behavior->condition : NULL};
                replaced = true;
                break;
            }
        }
        if (!replaced) WARN("Could not resolve behavior reference %s", ref.name);
        free((void*)ref.name);
        if (!replaced) {
            return false;
        }

    }
    return false;
}

struct config_behavior_parse_result parse_behavior(struct mascot_prototype* prototype, struct json_object_s* behavior, struct mascot_behavior** behaviors_definitions, size_t* bcount)
{
    struct config_behavior_parse_result result = {0};
    result.status = MASCOT_BEHAVIOR_PARSE_ERROR;

    struct mascot_behavior* behavior_obj = calloc(1, sizeof(struct mascot_behavior));

    bool name_set = false;
    bool next_behaviors_count_set = false;
    struct json_string_s* behavior_name = NULL;
    struct json_string_s* action_name = NULL;

    struct json_object_element_s* element = behavior->start;
    while (element) {
        struct json_object_element_s* celement = element;
        element = element->next;

        if (!strcmp(celement->name->string, "name") && celement->name->string_size == strlen("name")) {
            if (celement->value->type != json_type_string) {
                WARN("Invalid syntax in behaviors.json: name should be a string");
                goto behavior_parser_failed;
            }
            behavior_name = (struct json_string_s*)celement->value->payload;
            name_set = true;
        } else if (!strcmp(celement->name->string, "action") && celement->name->string_size == strlen("action")) {
            if (celement->value->type == json_type_null) {
                continue;
            }
            if (celement->value->type != json_type_string) {
                WARN("Invalid syntax in behaviors.json: name should be a string");
                goto behavior_parser_failed;
            }
            action_name = (struct json_string_s*)celement->value->payload;
        } else if (!strcmp(celement->name->string, "next_behavior_list_count") && celement->name->string_size == strlen("next_behavior_list_count")) {
            if (celement->value->type != json_type_number) {
                WARN("Invalid syntax in behaviors.json: next_behavior_list_count should be a number");
                goto behavior_parser_failed;
            }
            behavior_obj->next_behaviors_count = atoi(((struct json_number_s*)celement->value->payload)->number);
            next_behaviors_count_set = true;
        } else if (!strcmp(celement->name->string, "hidden") && celement->name->string_size == strlen("hidden")) {
            if (celement->value->type == json_type_null) {
                continue;
            }
            if (celement->value->type != json_type_true && celement->value->type != json_type_false) {
                WARN("Invalid syntax in behaviors.json: hidden should be a boolean");
                goto behavior_parser_failed;
            }
            behavior_obj->hidden = celement->value->type == json_type_true;
        } else if (!strcmp(celement->name->string, "is_conditioner") && celement->name->string_size == strlen("is_conditioner")) {
            if (celement->value->type == json_type_null) {
                continue;
            }
            if (celement->value->type != json_type_true && celement->value->type != json_type_false) {
                WARN("Invalid syntax in behaviors.json: is_conditioner should be a boolean");
                goto behavior_parser_failed;
            }
            behavior_obj->is_condition = celement->value->type == json_type_true;
        } else if (!strcmp(celement->name->string, "frequency") && celement->name->string_size == strlen("frequency")) {
            if (celement->value->type == json_type_null) {
                continue;
            }
            if (celement->value->type != json_type_number) {
                WARN("Invalid syntax in behaviors.json: frequency should be a number");
                goto behavior_parser_failed;
            }
            behavior_obj->frequency = atoll(((struct json_number_s*)celement->value->payload)->number);
        } else if (!strcmp(celement->name->string, "condition") && celement->name->string_size == strlen("condition")) {
            if (celement->value->type == json_type_null) {
                continue;
            }
            if (celement->value->type != json_type_number) {
                WARN("Invalid syntax in behaviors.json: condition should be a number");
                goto behavior_parser_failed;
            }
            // Find expression by id
            for (size_t i = 0; i < prototype->expressions_count; i++) {
                if (prototype->expression_definitions[i]->body->id == atoi(((struct json_number_s*)celement->value->payload)->number)) {
                    behavior_obj->condition = prototype->expression_definitions[i];
                    break;
                }
            }
            if (!behavior_obj->condition) {
                WARN("Invalid syntax in behaviors.json: condition should be a valid expression id");
                goto behavior_parser_failed;
            }
        } else if (!strcmp(celement->name->string, "next_behavior_list") && strlen("next_behavior_list") == celement->name->string_size) {
            if (celement->value->type != json_type_array) {
                WARN("Invalid syntax in behaviors.json: next_behavior_list should be an array");
                goto behavior_parser_failed;
            }
            struct json_array_s* next_behavior_list = (struct json_array_s*)celement->value->payload;
            struct json_array_element_s* next_behavior_element = next_behavior_list->start;
            size_t local_cnt = 0;
            while (next_behavior_element) {
                if (next_behavior_element->value->type != json_type_object) {
                    WARN("Invalid syntax in behaviors.json: next_behavior_list should contain only objects");
                    goto behavior_parser_failed;
                }

                struct config_behaviorref_parse_result behaviorref_result = parse_behaviorref(prototype, (struct json_object_s*)next_behavior_element->value->payload, behaviors_definitions, bcount, true);
                if (behaviorref_result.status != MASCOT_BEHAVIORREF_PARSE_OK) {
                    if (behaviorref_result.status == MASCOT_BEHAVIORREF_PARSE_ERROR_BEHAVIOR_NOT_DEFINED) {
                        WARN("Invalid syntax in behaviors.json: behavior reference not defined");
                        result.status = MASCOT_BEHAVIOR_PARSE_ERROR_NOT_DEFINED;
                        goto behavior_parser_failed;
                    }
                    WARN("Invalid syntax in behaviors.json: invalid behavior reference");
                    goto behavior_parser_failed;
                }

                behavior_obj->next_behavior_list[local_cnt++] = behaviorref_result.behavior;
                next_behavior_element = next_behavior_element->next;
            }
        } else if (!strcmp(celement->name->string, "next_behavior_list_add") && strlen("next_behavior_list_add") == celement->name->string_size) {
            if (celement->value->type == json_type_null) {
                behavior_obj->add_behaviors = true;
                continue;
            }
            if (celement->value->type != json_type_true && celement->value->type != json_type_false) {
                WARN("Invalid syntax in behaviors.json: next_behavior_list_add should be a boolean");
                goto behavior_parser_failed;
            }
            behavior_obj->add_behaviors = celement->value->type == json_type_true;
        }
    }

    if (!name_set) {
        WARN("Invalid syntax in behaviors.json: name is required");
        goto behavior_parser_failed;
    }
    if (!next_behaviors_count_set) {
        WARN("Invalid syntax in behaviors.json: next_behavior_list_count is required");
        goto behavior_parser_failed;
    }
    if (!behavior_obj->is_condition) {
        if (!action_name) {
            // Find action by behavior name
            for (size_t i = 0; i < prototype->actions_count; i++) {
                if (!strcmp(prototype->action_definitions[i]->name, behavior_name->string) && strlen(prototype->action_definitions[i]->name) == behavior_name->string_size) {
                    behavior_obj->action = prototype->action_definitions[i];
                    break;
                }
            }
        } else {
            // Find action by action name
            for (size_t i = 0; i < prototype->actions_count; i++) {
                if (!strcmp(prototype->action_definitions[i]->name, action_name->string) && strlen(prototype->action_definitions[i]->name) == action_name->string_size) {
                    behavior_obj->action = prototype->action_definitions[i];
                    break;
                }
            }
        }
        if (!behavior_obj->action) {
            WARN("Invalid syntax in behaviors.json: Could not find action by name");
            result.status = MASCOT_BEHAVIOR_PARSE_ERROR_ACTION_NOT_DEFINED;
            goto behavior_parser_failed;
        }
    }

    behavior_obj->name = strndup(behavior_name->string, behavior_name->string_size);
    if (!behavior_obj->name) {
        WARN("Failed to allocate memory for behavior name");
        goto behavior_parser_failed;
    }

    result.status = MASCOT_BEHAVIOR_PARSE_OK;
    result.behavior = behavior_obj;
    return result;

behavior_parser_failed:
    if (behavior_obj->name) {
        WARN("Failed to load behavior named %s: %d", behavior_obj->name, result.status);
    } else {
        WARN("Failed to load behavior");
    }
    free((void*)behavior_obj->name);
    free(behavior_obj);
    return result;
}

struct config_behavior_loader_result load_behaviors(struct mascot_prototype* prototype, struct json_object_s* behaviors_root)
{
    struct config_behavior_loader_result result = {0};
    result.behaviors = calloc(128, sizeof(struct mascot_behavior*));
    size_t size = 128;

    if (!behaviors_root) {
        free(result.behaviors);
        return result;
    }

    struct json_object_s** deffered_resolving = calloc(behaviors_root->length, sizeof(struct json_object_s*));
    int deffered_count = 0;

    struct json_object_element_s* element = behaviors_root->start;
    while (element) {
        DEBUG("Loading behavior definitions");
        if (!strncmp("definitions", element->name->string, element->name->string_size) && element->name->string_size == 11) {
            if (element->value->type != json_type_array) {
                WARN("Invalid syntax in behaviors.json: definitions should be an array");
                free(deffered_resolving);
                return result;
            }

            struct json_array_s* definitions = (struct json_array_s*)element->value->payload;
            struct json_array_element_s* definition = definitions->start;

            while (definition) {
                if (result.count == size) {
                    size *= 2;
                    result.behaviors = realloc(result.behaviors, size * sizeof(struct mascot_behavior*));
                }
                struct config_behavior_parse_result behavior_obj = parse_behavior(prototype, (struct json_object_s*)definition->value->payload, result.behaviors, &result.count);
                if (behavior_obj.status != MASCOT_BEHAVIOR_PARSE_OK) {
                    if (behavior_obj.status == MASCOT_BEHAVIOR_PARSE_ERROR_NOT_DEFINED) {
                        deffered_resolving[deffered_count++] = (struct json_object_s*)definition->value->payload;
                    } else if (behavior_obj.status == MASCOT_BEHAVIOR_PARSE_ERROR_ACTION_NOT_DEFINED) {
                        if (behavior_obj.behavior) WARN("Behavior %s has an action that is not defined", behavior_obj.behavior->name);
                        else WARN("Behavior has an action that is not defined");
                    }
                    definition = definition->next;
                    continue;
                }
                DEBUG("Loaded behavior %s", behavior_obj.behavior->name);
                result.behaviors[result.count++] = behavior_obj.behavior;
                definition = definition->next;
            }

            DEBUG("Retrying load behavior with unresolved behavior names (%d)", deffered_count);

            for (uint16_t i = 0; i < result.count; i++) {
                resolve_temporal_behavior_reference(prototype, result.behaviors[i], result.behaviors, result.count);
            }

            free(deffered_resolving);

        }

        if (!strncmp("root_behavior_list", element->name->string, element->name->string_size) && element->name->string_size == strlen("root_behavior_list")) {
            DEBUG("Loading behavior root list");
            if (element->value->type != json_type_array) {
                WARN("Invalid syntax in behaviors.json: root_behavior_list should be an array");
                return result;
            }
            result.root_list = calloc(128, sizeof(struct mascot_behavior_reference));
            size = 128;
            // Now build root behavior list
            struct json_array_s* root_behavior_list = (struct json_array_s*)element->value->payload;
            struct json_array_element_s* root_behavior = root_behavior_list->start;
            while (root_behavior) {
                if (result.root_list_count == size) {
                    size *= 2;
                    result.root_list = realloc(result.root_list, size * sizeof(struct mascot_behavior_reference));
                }
                struct config_behaviorref_parse_result behaviorref_obj = parse_behaviorref(prototype, (struct json_object_s*)root_behavior->value->payload, result.behaviors, &result.count, false);
                if (behaviorref_obj.status != MASCOT_BEHAVIORREF_PARSE_OK) {
                    if (behaviorref_obj.status == MASCOT_BEHAVIORREF_PARSE_ERROR_BEHAVIOR_NOT_DEFINED) {
                        WARN("Behavior reference has an undefined behavior");
                    }
                    root_behavior = root_behavior->next;
                    continue;
                }
                behaviorref_obj.behavior.frequency /= 2;
                result.root_list[result.root_list_count++] = behaviorref_obj.behavior;
                root_behavior = root_behavior->next;
            }
        }
        element = element->next;
    }

    result.ok = true;
    return result;

}

enum mascot_prototype_load_result mascot_prototype_load(struct mascot_prototype * prototype, const char *path)
{
    static int64_t minver = -1;
    static int64_t curver = -1;
    static bool version_constraints_processed = false;

    if (!version_constraints_processed) {
        minver = version_to_i64(WL_SHIMEJI_MASCOT_MIN_VER);
        curver = version_to_i64(WL_SHIMEJI_MASCOT_CUR_VER);
        if (minver > curver) {
            ERROR("Invalid version constraints: min version is greater than current version");
        }
        if (minver < 0) {
            ERROR("Invalid version constraints: min version is invalid");
        }
        if (curver < 0) {
            ERROR("Invalid version constraints: current version is invalid");
        }
        version_constraints_processed = true;
    }

    if (!prototype || !path) {
        return PROTOTYPE_LOAD_NULL_POINTER;
    }

    if (prototype->reference_count) {
        return PROTOTYPE_LOAD_ALREADY_LOADED;
    }

    char filename_buf[128] = {0};
    snprintf(filename_buf, 128, "%s/manifest.json", path);
    FILE* manifest = fopen(filename_buf, "r");
    if (!manifest) {
        if (errno == ENOENT) {
            WARN("Failed to load prototype from %s: manifest.json not found", path);
            return PROTOTYPE_LOAD_MANIFEST_NOT_FOUND;
        }
    }
    fseek(manifest, 0, SEEK_END);
    size_t size = ftell(manifest);

    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        ERROR("Cannot load prototype from %s: Out of memory", filename_buf);
    }

    fseek(manifest, 0, SEEK_SET);
    fread(buffer, 1, size, manifest);

    struct json_value_s* manifest_data = json_parse(buffer, size);
    if (!manifest_data) {
        ERROR("Cannot load prototype from %s: Invalid JSON or out of memory", filename_buf);
    }

    if (manifest_data->type != json_type_object) {
        WARN("Cannot load prototype from %s: Invalid JSON: Root expected to be an object", filename_buf);
        return PROTOTYPE_LOAD_MANIFEST_INVALID;
    }

    struct json_object_s* manifest_root = (struct json_object_s*)manifest_data->payload;

    struct json_object_element_s* element = manifest_root->start;
    char assets_path[128]    = {0};
    char behaviors_path[128] = {0};
    char actions_path[128]   = {0};
    char programs_path[128]  = {0};
    char version_str[128]     = {0};
    int64_t version = 0;
    while (element) {
        if (!strcmp(element->name->string, "name")) {
            if (element->value->type != json_type_string) {
                WARN("Cannot load prototype from %s: Invalid JSON: name expected to be a string", filename_buf);
                return PROTOTYPE_LOAD_MANIFEST_INVALID;
            }
            prototype->name = strdup(((struct json_string_s*)(element->value->payload))->string);
        } else if (!strcmp(element->name->string, "version")) {
            if (element->value->type != json_type_string) {
                WARN("Cannot load prototype from %s: Invalid JSON: version expected to be a string", filename_buf);
                return PROTOTYPE_LOAD_MANIFEST_INVALID;
            }
            version = version_to_i64(((struct json_string_s*)(element->value->payload))->string);
            if (version < 0) {
                WARN("Cannot load prototype from %s: Invalid JSON: Mascot config version is invalid");
                return PROTOTYPE_LOAD_MANIFEST_INVALID;
            }
            strncpy(version_str, ((struct json_string_s*)(element->value->payload))->string, 128);
        } else if (!strcmp(element->name->string, "display_name")) {
            if (element->value->type != json_type_string) {
                WARN("Cannot load prototype from %s: Invalid JSON: display_name expected to be a string", filename_buf);
                return PROTOTYPE_LOAD_MANIFEST_INVALID;
            }
            prototype->display_name = strdup(((struct json_string_s*)(element->value->payload))->string);
        } else if (!strcmp(element->name->string, "programs")) {
            if (element->value->type != json_type_string) {
                WARN("Cannot load prototype from %s: Invalid JSON: programs expected to be a string", filename_buf);
                return PROTOTYPE_LOAD_MANIFEST_INVALID;
            }
            snprintf(programs_path, 128, "%s/%s", path, ((struct json_string_s*)(element->value->payload))->string);
        } else if (!strcmp(element->name->string, "assets")) {
            if (element->value->type != json_type_string) {
                WARN("Cannot load prototype from %s: Invalid JSON: assets expected to be a string", filename_buf);
                return PROTOTYPE_LOAD_MANIFEST_INVALID;
            }
            snprintf(assets_path, 128, "%s/%s", path, ((struct json_string_s*)(element->value->payload))->string);
        } else if (!strcmp(element->name->string, "actions")) {
            if (element->value->type != json_type_string) {
                WARN("Cannot load prototype from %s: Invalid JSON: actions expected to be a string", filename_buf);
                return PROTOTYPE_LOAD_MANIFEST_INVALID;
            }
            snprintf(actions_path, 128, "%s/%s", path, ((struct json_string_s*)(element->value->payload))->string);
        } else if (!strcmp(element->name->string, "behaviors")) {
            if (element->value->type != json_type_string) {
                WARN("Cannot load prototype from %s: Invalid JSON: behaviors expected to be a string", filename_buf);
                return PROTOTYPE_LOAD_MANIFEST_INVALID;
            }
            snprintf(behaviors_path, 128, "%s/%s", path, ((struct json_string_s*)(element->value->payload))->string);
        }
        element = element->next;
    }

    if (version < minver) {
        WARN("Cannot load prototype from %s: Mascot config version is too old! Minimum supported version is %s but got %s", filename_buf, WL_SHIMEJI_MASCOT_MIN_VER, version_str);
        return PROTOTYPE_LOAD_VERSION_TOO_OLD;
    }

    if (version > curver) {
        WARN("Cannot load prototype from %s: Mascot config version is too new! Highest supported version is %s but got %s", filename_buf, WL_SHIMEJI_MASCOT_CUR_VER, version_str);
        return PROTOTYPE_LOAD_VERSION_TOO_NEW;
    }

    if (!prototype->name) {
        WARN("Cannot load prototype from %s: Invalid JSON: name is required", filename_buf);
        return PROTOTYPE_LOAD_MANIFEST_INVALID;
    }

    if (!prototype->display_name) {
        prototype->display_name = strdup(prototype->name);
    }

    if (!strlen(assets_path)) {
        WARN("Cannot load prototype from %s: Invalid JSON: assets_path is required", filename_buf);
        return PROTOTYPE_LOAD_MANIFEST_INVALID;
    }

    if (!strlen(actions_path)) {
        WARN("Cannot load prototype from %s: Invalid JSON: actions_path is required", filename_buf);
        return PROTOTYPE_LOAD_MANIFEST_INVALID;
    }

    struct mascot_atlas* atlas = mascot_atlas_new(environment_get_compositor(), environment_get_shm(), assets_path);
    prototype->atlas = atlas;
    if (!atlas) {
        WARN("Cannot load prototype from %s: Failed to create atlas", filename_buf);
        return PROTOTYPE_LOAD_ASSETS_FAILED;
    }

    // Open and load programs.json
    FILE* programs = fopen(programs_path, "r");
    if (!programs) {
        WARN("Cannot load prototype from %s: Failed to open %s", filename_buf, programs_path);
        return PROTOTYPE_LOAD_PROGRAMS_NOT_FOUND;
    }

    fseek(programs, 0, SEEK_END);
    size = ftell(programs);
    fseek(programs, 0, SEEK_SET);

    char* progbuf = (char*)calloc(1, size + 1);
    fread(progbuf, 1, size, programs);
    struct json_value_s* programs_data = json_parse(progbuf, size);

    if (!programs_data) {
        WARN("Cannot load prototype from %s: Failed to parse programs.json", filename_buf);
        return PROTOTYPE_LOAD_PROGRAMS_INVALID;
    }

    if (programs_data->type != json_type_object) {
        WARN("Cannot load prototype from %s: Invalid JSON: programs.json root expected to be an object", filename_buf);
        return PROTOTYPE_LOAD_PROGRAMS_INVALID;
    }

    struct json_object_s* programs_root = (struct json_object_s*)programs_data->payload;
    struct config_program_loader_result program_loader_result = load_programs(programs_root);
    if (!program_loader_result.ok) {
        WARN("Cannot load prototype from %s: Failed to load programs", filename_buf);
        return PROTOTYPE_LOAD_PROGRAMS_INVALID;
    }

    DEBUG("Prototype %s: loaded %d programs", prototype->name, program_loader_result.count);
    prototype->expression_definitions = (const struct mascot_expression**)program_loader_result.expressions;
    prototype->expressions_count = program_loader_result.count;

    free(programs_data);
    fclose(programs);
    free(progbuf);

    FILE* actions = fopen(actions_path, "r");
    if (!actions) {
        WARN("Cannot load prototype from %s: Failed to open %s", filename_buf, actions_path);
        return PROTOTYPE_LOAD_ACTIONS_NOT_FOUND;
    }

    fseek(actions, 0, SEEK_END);
    size = ftell(actions);
    fseek(actions, 0, SEEK_SET);

    char* actbuf = (char*)calloc(1, size + 1);
    fread(actbuf, 1, size, actions);
    struct json_value_s* actions_data = json_parse(actbuf, size);

    if (!actions_data) {
        WARN("Cannot load prototype from %s: Failed to parse actions.json", filename_buf);
        return PROTOTYPE_LOAD_ACTIONS_INVALID;
    }

    if (actions_data->type != json_type_array) {
        WARN("Cannot load prototype from %s: Invalid JSON: actions.json root expected to be an array", filename_buf);
        return PROTOTYPE_LOAD_ACTIONS_INVALID;
    }

    struct json_array_s* actions_root = (struct json_array_s*)actions_data->payload;
    struct config_action_loader_result actions_loader_result = load_actions(prototype, actions_root);
    if (!actions_loader_result.ok) {
        WARN("Cannot load prototype from %s: Failed to load actions", filename_buf);
        return PROTOTYPE_LOAD_ACTIONS_INVALID;
    }

    DEBUG("Prototype %s: loaded %d actions", prototype->name, actions_loader_result.count);
    prototype->action_definitions = (const struct mascot_action**)actions_loader_result.actions;
    prototype->actions_count = actions_loader_result.count;

    free(actions_data);
    fclose(actions);
    free(actbuf);

    FILE* behaviors = fopen(behaviors_path, "r");
    if (!behaviors) {
        WARN("Cannot load prototype from %s: Failed to open %s", filename_buf, behaviors_path);
        return PROTOTYPE_LOAD_BEHAVIORS_NOT_FOUND;
    }

    fseek(behaviors, 0, SEEK_END);
    size = ftell(behaviors);
    fseek(behaviors, 0, SEEK_SET);

    char* behbuf = (char*)calloc(1, size + 1);
    fread(behbuf, 1, size, behaviors);
    // struct json_value_s* behaviors_data = json_parse(behbuf, size);
    struct json_parse_result_s result;
    struct json_value_s* behaviors_data = json_parse_ex(behbuf, size, 0, NULL, NULL, &result);

    if (!behaviors_data) {
        WARN("Cannot load prototype from %s: Failed to parse behaviors.json", filename_buf);
        return PROTOTYPE_LOAD_BEHAVIORS_INVALID;
    }

    if (behaviors_data->type != json_type_object) {
        WARN("Cannot load prototype from %s: Invalid JSON: behaviors.json root expected to be an object", filename_buf);
        return PROTOTYPE_LOAD_BEHAVIORS_INVALID;
    }

    struct json_object_s* behaviors_root = (struct json_object_s*)behaviors_data->payload;
    struct config_behavior_loader_result behaviors_loader_result = load_behaviors(prototype, behaviors_root);
    if (!actions_loader_result.ok) {
        WARN("Cannot load prototype from %s: Failed to load behaviors", filename_buf);
        return PROTOTYPE_LOAD_BEHAVIORS_INVALID;
    }

    DEBUG("Prototype %s: loaded %d behaviors", prototype->name, behaviors_loader_result.count);
    prototype->behavior_definitions = (const struct mascot_behavior**)behaviors_loader_result.behaviors;
    prototype->root_behavior_list = (const struct mascot_behavior_reference*)behaviors_loader_result.root_list;
    prototype->behavior_count = behaviors_loader_result.count;
    prototype->root_behavior_list_count = behaviors_loader_result.root_list_count;

    // Set up some behavior shortcut pointers
    for (uint16_t i = 0; i < prototype->behavior_count; i++) {
        const struct mascot_behavior* behavior = prototype->behavior_definitions[i];
        if (!strcmp("Fall", behavior->name) && strlen("Fall") == strlen(behavior->name)) {
            prototype->fall_behavior = behavior;
        }
        else if (!strcmp("Dragged", behavior->name) && strlen("Dragged") == strlen(behavior->name)) {
            prototype->drag_behavior = behavior;
        }
        else if (!strcmp("Thrown", behavior->name) && strlen("Thrown") == strlen(behavior->name)) {
            prototype->thrown_behavior = behavior;
        }
    }

    // Find and set dismiss action shortcut (if present)
    for (uint16_t i = 0; i < prototype->actions_count; i++) {
        const struct mascot_action* action = prototype->action_definitions[i];
        if (!strcmp("Dismissed", action->name)) {
            prototype->dismiss_action = action;
        }
    }

    free(behaviors_data);
    fclose(behaviors);
    free(behbuf);

    prototype->local_variables_count = 128;
    prototype->path = strdup(path);

    return PROTOTYPE_LOAD_SUCCESS;
}

void mascot_attach_affordance_manager(struct mascot* mascot, struct mascot_affordance_manager* manager) {
    if (!mascot) ERROR("Cannot attach affordance manager to NULL mascot");
    mascot->affordance_manager = manager;
}
void mascot_detach_affordance_manager(struct mascot* mascot) {
    mascot_attach_affordance_manager(mascot, NULL);
}
