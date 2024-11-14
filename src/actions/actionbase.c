/*
    actionbase.c - wl_shimeji's useful action base functions and related definitions

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

#include "actionbase.h"

enum mascot_tick_result mascot_out_of_bounds_check(struct mascot* mascot)
{
    if (
        mascot->X->value.i < 0 || mascot->X->value.i > (int32_t)environment_screen_width(mascot->environment) ||
        mascot->Y->value.i < 0 || mascot->Y->value.i > (int32_t)environment_screen_height(mascot->environment)
    ) {
        INFO("<Mascot:%s:%u> Mascot out of screen bounds (caught at %d,%d while allowed values are from 0,0 to %d,%d), respawning", mascot->prototype->name, mascot->id, mascot->X->value.i, mascot->Y->value.i, environment_screen_width(mascot->environment), environment_screen_height(mascot->environment));
        mascot->X->value.i = rand() % environment_screen_width(mascot->environment);
        mascot->Y->value.i = environment_screen_height(mascot->environment) - 256;
        mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
        return mascot_tick_reenter;
    }
    return mascot_tick_ok;
}

enum mascot_tick_result mascot_ground_check(struct mascot* mascot, struct mascot_action_reference* actionref, mascot_action_clean clean_func)
{
    // Check if action border requirements are met
    enum environment_border_type border_type = environment_get_border_type(mascot->environment, mascot->X->value.i, mascot->Y->value.i);
    if (actionref->action->border_type != environment_border_type_any) {
        if (
            border_type
            != actionref->action->border_type
        ) {
            if (border_type != environment_border_type_floor) {
                mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
                clean_func(mascot);
                return mascot_tick_reenter;
            }

            return mascot_tick_next;
        }
    }
    return mascot_tick_ok;
}

enum mascot_tick_result mascot_execute_variable(struct mascot *mascot, uint16_t variable_id)
{
    if (variable_id > MASCOT_LOCAL_VARIABLE_COUNT) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Variable %u out of bounds", mascot->prototype->name, mascot->id, variable_id);
        return mascot_tick_error;
    }
    if (mascot->local_variables[variable_id].used) {
        float vmres = 0.0;
        enum expression_execution_result res = expression_vm_execute(
            mascot->local_variables[variable_id].expr.expression_prototype->body,
            mascot,
            &vmres
        );
        if (res == EXPRESSION_EXECUTION_ERROR) {
            LOG("ERROR", RED, "<Mascot:%s:%u> Variable %u errored for init in action \"%s\"", mascot->prototype->name, mascot->id, variable_id, mascot->current_action.action->name);
            return mascot_tick_error;
        }
        if (mascot->local_variables[variable_id].kind == mascot_local_variable_float) {
            DEBUG("<Mascot:%s:%u> Variable %u set to %f", mascot->prototype->name, mascot->id, variable_id, vmres);
            mascot->local_variables[variable_id].value.f = vmres;
        } else if (mascot->local_variables[variable_id].kind == mascot_local_variable_int) {
            DEBUG("<Mascot:%s:%u> Variable %u set to %d", mascot->prototype->name, mascot->id, variable_id, (int)vmres);
            mascot->local_variables[variable_id].value.i = (int)vmres;
        } else {
            LOG("ERROR", RED, "<Mascot:%s:%u> Variable %u kind not supported", mascot->prototype->name, mascot->id, variable_id);
            return mascot_tick_error;
        }
    }
    return mascot_tick_ok;
}

enum mascot_tick_result mascot_assign_variable(struct mascot *mascot, uint16_t variable_id, struct mascot_local_variable* variable_data)
{
    if (variable_id > MASCOT_LOCAL_VARIABLE_COUNT) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Variable %u out of bounds", mascot->prototype->name, mascot->id, variable_id);
        return mascot_tick_error;
    }
    DEBUG("<Mascot:%s:%u> Variable %u assigned", mascot->prototype->name, mascot->id, variable_id);
    mascot->local_variables[variable_id] = (struct mascot_local_variable){
        .kind = mascot->local_variables[variable_id].kind,
        .used = variable_data->used,
        .expr = variable_data->expr,
        .value = variable_data->value
    };
    return mascot_execute_variable(mascot, variable_id);
}

enum mascot_tick_result mascot_check_condition(struct mascot *mascot, const struct mascot_expression* condition)
{
    if (!condition) {
        return mascot_tick_ok;
    }
    float vmres = 0.0;
    enum expression_execution_result res = expression_vm_execute(
        condition->body,
        mascot,
        &vmres
    );
    if (res == EXPRESSION_EXECUTION_ERROR) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Condition errored for next in action \"%s\"", mascot->prototype->name, mascot->id, mascot->current_action.action->name);
        return mascot_tick_error;
    }
    if (vmres == 0.0) {
        return mascot_tick_next;
    }
    return mascot_tick_ok;
}

enum mascot_tick_result mascot_recheck_condition(struct mascot *mascot, const struct mascot_expression* condition)
{
    if (condition) {
        if (condition->evaluate_once) {
            return mascot_tick_ok;
        }
    }
    return mascot_check_condition(mascot, condition);
}

int32_t mascot_screen_y_to_mascot_y(struct mascot* mascot, int32_t screen_y)
{
    return environment_screen_height(mascot->environment) - screen_y;
}
