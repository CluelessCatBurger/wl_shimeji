/*
    offset.c - wl_shimeji's offset action implementation

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

#include "offset.h"
#include "actionbase.h"
#include "mascot.h"

enum mascot_tick_result offset_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(tick);
    if (actionref->action->length) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Offset action contains frames/subanimations", mascot->prototype->name, mascot->id);
        return mascot_tick_error;
    }

    enum mascot_tick_result actionref_cond = mascot_check_condition(mascot, actionref->condition);
    enum mascot_tick_result action_cond = mascot_check_condition(mascot, actionref->action->condition);

    if (actionref_cond == mascot_tick_error || action_cond == mascot_tick_error) {
        return mascot_tick_error;
    }
    if (actionref_cond == mascot_tick_next || action_cond == mascot_tick_next) {
        return mascot_tick_next;
    }

    const struct mascot_expression* cond = actionref->condition ? actionref->condition : actionref->action->condition;
    mascot->current_condition.expression_prototype = (struct mascot_expression*)cond;
    mascot->current_condition.evaluated = cond ? cond->evaluate_once : 0;

    // Save current position
    int32_t x = mascot->X->value.i;
    int32_t y = mascot->Y->value.i;

    struct mascot_local_variable* offset_x = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_X_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_X_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_X_ID];
    struct mascot_local_variable* offset_y = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_Y_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_Y_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_Y_ID];

    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_X_ID, offset_x) == mascot_tick_error) {
        return mascot_tick_error;
    }
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_Y_ID, offset_y) == mascot_tick_error) {
        return mascot_tick_error;
    }

    // Set new position
    environment_subsurface_set_position(mascot->subsurface, x + mascot->X->value.i, mascot_screen_y_to_mascot_y(mascot, y - mascot->Y->value.i));
    environment_subsurface_reset_interpolation(mascot->subsurface);
    mascot->X->value.i = x + mascot->X->value.i;
    mascot->Y->value.i = y - mascot->Y->value.i;
    return mascot_tick_next;

}


void offset_action_clean(struct mascot *mascot)
{
    UNUSED(mascot);
}
