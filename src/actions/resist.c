/*
    resist.c - wl_shimeji's resist action implementation

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
#include "dragging.h"

enum mascot_tick_result resist_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    if (!actionref->action->length) {
        WARN("<Mascot:%s:%u> Resist action has no length", mascot->prototype->name, mascot->id);
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

    if (actionref->duration_limit) {
        float vmres = 0.0;
        enum expression_execution_result res = expression_vm_execute(
            actionref->duration_limit->body,
            mascot,
            &vmres
        );
        if (res == EXPRESSION_EXECUTION_ERROR) {
            LOG("ERROR", RED, "<Mascot:%s:%u> Duration errored for init in action \"%s\"", mascot->prototype->name, mascot->id, actionref->action->name);
        }
        if (vmres == 0.0) {
            return mascot_tick_next;
        }
        mascot->action_duration = tick + vmres;
    }

    // Reset action index, frame and animation index
    mascot->action_index = 0;
    mascot->frame_index = 0;
    mascot->next_frame_tick = 0;
    mascot->animation_index = 0;

    mascot_announce_affordance(mascot, NULL);
    mascot->state = mascot_state_drag_resist;

    return mascot_tick_ok;
}

struct mascot_action_next resist_action_next(struct mascot* mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    struct mascot_action_next result = {0};
    struct mascot_animation* animation = NULL;

    const struct mascot_animation* current_animation = mascot->current_animation;

    for (uint16_t i = 0; i < actionref->action->length; i++) {
        if (actionref->action->content[i].kind != mascot_action_content_type_animation) {
            LOG("ERROR", RED, "<Mascot:%s:%u> Action content is not an animation", mascot->prototype->name, mascot->id);
            result.status = mascot_tick_error;
            return result;
        }

        enum mascot_tick_result animationcond = mascot_check_condition(mascot, actionref->action->content[i].value.animation->condition);

        if (animationcond == mascot_tick_next) {
            continue;
        }

        if (animationcond == mascot_tick_error) {
            result.status = mascot_tick_error;
            return result;
        }

        mascot->animation_index = i;
        animation = actionref->action->content[i].value.animation;
        break;
    }

    if (current_animation != animation) {
        result.next_animation = animation;
        mascot->frame_index = 0;
        mascot->next_frame_tick = tick;
        result.status = mascot_tick_reenter;
        return result;
    }

    if (mascot->next_frame_tick <= tick) {
        if (mascot->current_animation) {
            if (mascot->frame_index >= mascot->current_animation->frame_count) {
                if (mascot->action_duration || actionref->action->loop) {
                    mascot->frame_index = 0;
                } else {
                    result.status = mascot_tick_escape;
                    return result;
                }
            }
            result.next_pose = mascot->current_animation->frames[mascot->frame_index++];
        }
    }

    result.next_animation = animation;
    result.status = mascot_tick_ok;
    return result;
}

enum mascot_tick_result resist_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(actionref);

    int posx = mascot->X->value.i;
    int posy = mascot->Y->value.i;

    environment_pointer_update_delta(mascot->subsurface, tick);

    if (abs(mascot->X->value.i - posx) >= 5 || abs(mascot->Y->value.i - posy) >= 5) {
        mascot->dragged_tick = tick;
        mascot_set_behavior(mascot, mascot->prototype->drag_behavior);
        return mascot_tick_reenter;
    }
    return mascot_tick_ok;
}

void resist_action_clean(struct mascot *mascot)
{
    mascot->animation_index = 0;
    mascot->frame_index = 0;
    mascot->next_frame_tick = 0;
    mascot->action_duration = 0;
}
