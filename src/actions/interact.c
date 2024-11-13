/*
    interact.c - wl_shimeji's interact action implementation

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

#include "interact.h"
#include "actionbase.h"

enum mascot_tick_result interact_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    if (!actionref->action->length) {
        WARN("<Mascot:%s:%u> Interact action has no length", mascot->prototype->name, mascot->id);
        return mascot_tick_error;
    }

    DEBUG("<Mascot:%s:%u> Initializing interact action \"%s\"", mascot->prototype->name, mascot->id, actionref->action->name);
    DEBUG("<Mascot:%s:%u> interact actionref info:", mascot->prototype->name, mascot->id);
    DEBUG("<Mascot:%s:%u> - Condition: %p", mascot->prototype->name, mascot->id, actionref->condition);
    DEBUG("<Mascot:%s:%u> - Duration: %p", mascot->prototype->name, mascot->id, actionref->duration_limit);


    enum mascot_tick_result ground_check = mascot_ground_check(mascot, actionref, interact_action_clean);
    if (ground_check != mascot_tick_ok) {
        return ground_check;
    }

    // Check if action condition requirements are met
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

    if (actionref->duration_limit) {
        DEBUG("Executing duration limit for action \"%s\"", actionref->action->name);
        float vmres = 0.0;
        enum expression_execution_result res = expression_vm_execute(
            actionref->duration_limit->body,
            mascot,
            &vmres
        );
        if (res == EXPRESSION_EXECUTION_ERROR) {
            LOG("ERROR", RED, "<Mascot:%s:%u> Duration errored for init in action \"%s\"", mascot->prototype->name, mascot->id, actionref->action->name);
        }
        mascot->action_duration = tick + vmres;
        DEBUG("Duration limit for action \"%s\" is %f", actionref->action->name, vmres);
    }

    // Reset action index, frame and animation index
    mascot->action_index = 0;
    mascot->frame_index = 0;
    mascot->next_frame_tick = 0;
    mascot->animation_index = 0;

    mascot->state = mascot_state_interact;

    // Announce affordance
    mascot_announce_affordance(mascot, actionref->action->affordance);

    return mascot_tick_ok;
}

void interact_action_clean(struct mascot *mascot)
{
    DEBUG("interact: clean");
    mascot->animation_index = 0;
    mascot->frame_index = 0;
    mascot->next_frame_tick = 0;
    mascot->action_duration = 0;
    mascot->state = mascot_state_interact;
    mascot_announce_affordance(mascot, NULL);
}
