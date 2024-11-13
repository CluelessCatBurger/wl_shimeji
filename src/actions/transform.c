/*
    transform.c - wl_shimeji's transform action implementation

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

#include "transform.h"
#include "actionbase.h"
#include "simple.h"

enum mascot_tick_result transform_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{

    // Check is we know required prototype
    if (actionref->action->transform_target) {
        struct mascot_prototype* target = mascot_prototype_store_get(mascot->prototype->prototype_store, actionref->action->transform_target);
        if (!target) {
            LOG("ERROR", RED, "<Mascot:%s:%u> Transform target prototype \"%s\" not found", mascot->prototype->name, mascot->id, actionref->action->transform_target);
            return mascot_tick_next;
        }
    } else {
        LOG("ERROR", RED, "<Mascot:%s:%u> Transform target prototype not defined", mascot->prototype->name, mascot->id);
        return mascot_tick_next;
    }

    enum mascot_tick_result ground_check = mascot_ground_check(mascot, actionref, transform_action_clean);
    if (ground_check != mascot_tick_ok) {
        return ground_check;
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
    mascot->animation_index = -1;

    // Announce affordance
    mascot_announce_affordance(mascot, NULL);

    return mascot_tick_ok;
}

struct mascot_action_next transform_action_next(struct mascot* mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    struct mascot_action_next result = {0};

    if (mascot->action_duration && tick >= mascot->action_duration) {
        result.status = mascot_tick_transform;
        return result;
    }

    enum mascot_tick_result ground_check = mascot_ground_check(mascot, actionref, transform_action_clean);
    if (ground_check != mascot_tick_ok) {
        result.status = ground_check;
        return result;
    }

    // Ensure conditions are still met
    enum mascot_tick_result actionref_cond = mascot_recheck_condition(mascot, actionref->condition);
    enum mascot_tick_result action_cond = mascot_recheck_condition(mascot, actionref->action->condition);

    if (action_cond != mascot_tick_ok)
    {
        result.status = action_cond;
        return result;
    }

    if (actionref_cond != mascot_tick_ok)
    {
        result.status = actionref_cond;
        return result;
    }

    const struct mascot_animation* current_animation = mascot->current_animation;
    const struct mascot_animation* new_animation = NULL;

    for (uint16_t i = 0; i < actionref->action->length; i++) {
        if (actionref->action->content[i].kind != mascot_action_content_type_animation) {
            LOG("ERROR", RED, "<Mascot:%s:%u> Simple action content is not an animation", mascot->prototype->name, mascot->id);
            result.status = mascot_tick_error;
            return result;
        }
        new_animation = actionref->action->content[i].value.animation;
        enum mascot_tick_result animcond = mascot_check_condition(mascot, new_animation->condition);
        if (animcond != mascot_tick_ok) {
            if (animcond == mascot_tick_error) {
                result.status = mascot_tick_error;
                return result;
            }
            new_animation = NULL;
            continue;
        }
        mascot->animation_index = i;
        break;
    }

    if (!new_animation) {
        result.status = mascot_tick_next;
        return result;
    }

    if (current_animation != new_animation) {
        result.next_animation = new_animation;
        mascot->frame_index = 0;
        mascot->next_frame_tick = tick;
        result.status = mascot_tick_reenter;
        return result;
    }

    if (mascot->next_frame_tick <= tick) {
        if (mascot->current_animation) {
            if (mascot->frame_index >= mascot->current_animation->frame_count) {
                if (mascot->action_duration || actionref->action->loop)     {
                    mascot->frame_index = 0;
                } else {
                    result.status = mascot_tick_transform;
                    return result;
                }
            }
            result.next_pose = mascot->current_animation->frames[mascot->frame_index++];
        }
    }

    result.status = mascot_tick_ok;
    return result;
}

void transform_action_clean(struct mascot *mascot)
{
    mascot->animation_index = 0;
    mascot->frame_index = 0;
    mascot->next_frame_tick = 0;
    mascot->action_duration = 0;
}
