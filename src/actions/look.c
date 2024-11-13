/*
    look.c - wl_shimeji's look action implementation

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

#include "look.h"

enum mascot_tick_result look_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(tick);
    if (actionref->action->length) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Look action contains frames/subanimations", mascot->prototype->name, mascot->id);
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

    struct mascot_local_variable* looking_right = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_LOOKINGRIGHT_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_LOOKINGRIGHT_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_LOOKINGRIGHT_ID];

    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_LOOKINGRIGHT_ID, looking_right) == mascot_tick_error) {
        return mascot_tick_error;
    }
    return mascot_tick_next;
}

void look_action_clean(struct mascot *mascot)
{
    UNUSED(mascot);
}
