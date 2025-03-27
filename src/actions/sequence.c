/*
    sequence.c - wl_shimeji's sequence action implementation

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

#include "sequence.h"
#include "actionbase.h"

enum mascot_tick_result sequence_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    if (!actionref->action->length) {
        WARN("<Mascot:%s:%u> Sequence action has no length", mascot->prototype->name, mascot->id);
        return mascot_tick_error;
    }

    DEBUG("<Mascot:%s:%u> Sequence action \"%s\" init", mascot->prototype->name, mascot->id, actionref->action->name);

    // Check if action border requirements are met
    enum environment_border_type border_type = environment_get_border_type(mascot->environment, mascot->X->value.i, mascot->Y->value.i);
    if (actionref->action->border_type != environment_border_type_any) {
        if (
            border_type
            != actionref->action->border_type
        ) {
            if (border_type == environment_border_type_none) {
                mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
                sequence_action_clean(mascot);
                return mascot_tick_reenter;
            }
            return mascot_tick_next;
        }
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

    mascot->action_duration = 0;

    mascot->action_duration = mascot_duration_limit(mascot, actionref->duration_limit, tick);

    mascot->action_index = 0;

    // Announce affordance
    mascot_announce_affordance(mascot, NULL);

    return mascot_tick_ok;
}


struct mascot_action_next sequence_action_next(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    struct mascot_action_next result = {0};

    // Check conditions
    enum mascot_tick_result actionref_cond = mascot_recheck_condition(mascot, actionref->condition);
    enum mascot_tick_result action_cond = mascot_recheck_condition(mascot, actionref->action->condition);

    if (mascot->action_duration && tick >= mascot->action_duration) {
        DEBUG("<Mascot:%s:%u> Sequence action duration limit reached", mascot->prototype->name, mascot->id);
        result.status = mascot_tick_next;
        return result;
    }

    if (actionref_cond == mascot_tick_next || action_cond == mascot_tick_next) {
        DEBUG("<Mascot:%s:%u> Sequence action condition failed", mascot->prototype->name, mascot->id);
        result.status = mascot_tick_next;
        return result;
    }

    if (actionref_cond == mascot_tick_error || action_cond == mascot_tick_error) {
        WARN("<Mascot:%s:%u> Sequence action condition errored", mascot->prototype->name, mascot->id);
        result.status = mascot_tick_error;
        return result;
    }

    uint16_t current_subaction = mascot->action_index++;

    DEBUG("SEQUENCE NEXT SUBACTION ID == %d", current_subaction);

    if (current_subaction >= actionref->action->length) {
        if (actionref->action->loop || mascot->action_duration) {
            DEBUG("ACTION LOOPED");
            mascot->action_index = 0;
            current_subaction = 0;
        } else {
            result.status = mascot_tick_next;
            return result;
        }
    }

    // Ensure content kind is not animation
    if (actionref->action->content[current_subaction].kind == mascot_action_content_type_animation) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Sequence action contains animation in subaction %u", mascot->prototype->name, mascot->id, current_subaction);
        result.status = mascot_tick_error;
        return result;
    }


    if (actionref->action->content[current_subaction].kind == mascot_action_content_type_action) {
        result.next_action.action = actionref->action->content[current_subaction].value.action;
    } else {
        result.next_action = *(actionref->action->content[current_subaction].value.action_reference);
    }
    result.status = mascot_tick_reenter;
    return result;
}

enum mascot_tick_result sequence_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(tick);
    WARN("<Mascot:%s:%u> Tick called on sequenced action %s", mascot->prototype->name, mascot->id, actionref->action->name);
    return mascot_tick_ok;
}

void sequence_action_clean(struct mascot *mascot) {
    mascot->action_index = 0;
    mascot->action_duration = 0;
}
