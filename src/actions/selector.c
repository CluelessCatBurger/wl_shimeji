/*
    selector.c - wl_shimeji's selector action implementation

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

#include "selector.h"
#include "actionbase.h"

enum mascot_tick_result selector_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(tick);
    if (!actionref->action->length) {
        WARN("<Mascot:%s:%u> Selector action has no length", mascot->prototype->name, mascot->id);
        return mascot_tick_error;
    }

    // Check if action border requirements are met
    if (actionref->action->border_type != environment_border_type_any) {
        if (
            environment_get_border_type(mascot->environment, mascot->X->value.i, mascot->Y->value.i)
            != actionref->action->border_type
        ) return mascot_tick_next;
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

    // Reset action index, frame and animation index
    mascot->action_index = 0;

    // Unannounce affordance
    mascot_announce_affordance(mascot, NULL);

    return mascot_tick_ok;
}

struct mascot_action_next selector_action_next(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(tick);
    struct mascot_action_next result = {0};

    enum mascot_tick_result actionref_cond = mascot_recheck_condition(mascot, actionref->condition);
    enum mascot_tick_result action_cond = mascot_recheck_condition(mascot, actionref->action->condition);

    if (actionref_cond == mascot_tick_error || action_cond == mascot_tick_error) {
        result.status = mascot_tick_error;
        return result;
    }

    if (actionref_cond == mascot_tick_next || action_cond == mascot_tick_next) {
        result.status = mascot_tick_next;
        return result;
    }

    uint16_t current_subaction = mascot->action_index;

    for (current_subaction = 0; current_subaction < actionref->action->length; current_subaction++) {
        struct mascot_action_reference subactionref = {0};

        // Ensure content kind is not animation
        if (actionref->action->content[current_subaction].kind == mascot_action_content_type_animation) {
            LOG("ERROR", RED, "<Mascot:%s:%u> Sequence action contains animation in subaction %u", mascot->prototype->name, mascot->id, current_subaction);
            result.status = mascot_tick_error;
            return result;
        }

        if (actionref->action->content[current_subaction].kind == mascot_action_content_type_action) {
            subactionref.action = actionref->action->content[current_subaction].value.action;
            subactionref.condition = (struct mascot_expression*)actionref->action->content[current_subaction].value.action->condition;
        } else {
            subactionref = *(actionref->action->content[current_subaction].value.action_reference);
        }

        // Check borders first
        if (subactionref.action->border_type != environment_border_type_any) {
            if (
                environment_get_border_type(mascot->environment, mascot->X->value.i, mascot->Y->value.i)
                != subactionref.action->border_type
            ) continue;
        }


        enum mascot_tick_result subactionref_cond = mascot_check_condition(mascot, subactionref.condition);
        enum mascot_tick_result subaction_cond = mascot_check_condition(mascot, subactionref.action->condition);

        if (subactionref_cond == mascot_tick_error || subaction_cond == mascot_tick_error) {
            result.status = mascot_tick_error;
            return result;
        }

        if (subactionref_cond == mascot_tick_next || subaction_cond == mascot_tick_next) {
            continue;
        }

        result.next_action = subactionref;
        result.status = mascot_tick_reenter;
        return result;
    }
    result.status = mascot_tick_next;
    return result;
}

enum mascot_tick_result selector_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(tick);
    LOG("ERROR", RED, "<Mascot:%s:%u> Tick called on selector action %s", mascot->prototype->name, mascot->id, actionref->action->name);
    return mascot_tick_error;
}

void selector_action_clean(struct mascot *mascot)
{
    mascot->action_index = 0;
}
