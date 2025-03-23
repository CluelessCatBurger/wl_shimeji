/*
    breed.c - wl_shimeji's breed action implementation

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


#include "breed.h"
#include "actionbase.h"
#include "config.h"

enum mascot_tick_result breed_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    if (!config_get_breeding()) {
        return mascot_tick_next;
    }

    if (config_get_mascot_limit() <= (int32_t)mascot_total_count) {
        return mascot_tick_next;
    }

    enum mascot_tick_result ground_check = mascot_ground_check(mascot, actionref, breed_action_clean);
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
    mascot->animation_index = 0;
    mascot->born_count = 0;
    mascot->born_tick = tick;

    mascot->BornX->value.i = 0;
    mascot->BornY->value.i = 0;
    mascot->BornInterval->value.i = 0;
    mascot->BornCount->value.i = 0;

    struct mascot_local_variable* born_x = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_BORNX_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_BORNX_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_BORNX_ID];
    struct mascot_local_variable* born_y = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_BORNY_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_BORNY_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_BORNY_ID];
    struct mascot_local_variable* born_count = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_BORNCOUNT_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_BORNCOUNT_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_BORNCOUNT_ID];
    struct mascot_local_variable* born_interval = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_BORNINTERVAL_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_BORNINTERVAL_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_BORNINTERVAL_ID];

    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_BORNX_ID, born_x) != mascot_tick_ok) {
        return mascot_tick_error;
    }

    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_BORNY_ID, born_y) != mascot_tick_ok) {
        return mascot_tick_error;
    }

    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_BORNCOUNT_ID, born_count) != mascot_tick_ok) {
        return mascot_tick_error;
    }

    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_BORNINTERVAL_ID, born_interval) != mascot_tick_ok) {
        return mascot_tick_error;
    }

    if (!born_count->used) {
        mascot->BornCount->value.i = 1;
    }

    // Announce affordance
    mascot_announce_affordance(mascot, NULL);

    return mascot_tick_ok;
}

struct mascot_action_next breed_action_next(struct mascot* mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    struct mascot_action_next result = {0};

    uint16_t current_frame = mascot->frame_index;
    const struct mascot_animation* new_animation = NULL;
    const struct mascot_animation* current_animation = mascot->current_animation;

    if (!actionref->action->length) {
        result.status = mascot_tick_clone;
        return result;
    }

    enum mascot_tick_result ground_check = mascot_ground_check(mascot, actionref, breed_action_clean);
    if (ground_check != mascot_tick_ok) {
        result.status = ground_check;
        return result;
    }

    // Check if conditions are met
    enum mascot_tick_result actionref_cond = mascot_recheck_condition(mascot, actionref->condition);
    enum mascot_tick_result action_cond = mascot_recheck_condition(mascot, actionref->action->condition);

    if (actionref_cond == mascot_tick_next || action_cond == mascot_tick_next) {
        result.status = mascot_tick_next;
        return result;
    }

    if (actionref_cond == mascot_tick_error || action_cond == mascot_tick_error) {
        result.status = mascot_tick_error;
        return result;
    }

    // Check duration
    if (mascot->action_duration && mascot->action_duration <= tick) {
        if (mascot->BornCount->value.i > mascot->born_count) {
            result.status = mascot_tick_clone;
            return result;
        }
        result.status = mascot_tick_next;
        return result;
    }

    for (uint16_t i = 0; i < actionref->action->length; i++) {
        if (actionref->action->content[i].kind != mascot_action_content_type_animation) {
            LOG("ERROR", RED, "<Mascot:%s:%u> Action content is not an animation", mascot->prototype->name, mascot->id);
            result.status = mascot_tick_error;
            return result;
        }

        enum mascot_tick_result anim_cond = mascot_check_condition(mascot, actionref->action->content[i].value.animation->condition);
        if (anim_cond == mascot_tick_next) {
            continue;
        }
        if (anim_cond == mascot_tick_error) {
            result.status = mascot_tick_error;
            return result;
        }

        new_animation = actionref->action->content[i].value.animation;
        mascot->animation_index = i;
        break;
    }
    if (current_animation != new_animation) {
        mascot->frame_index = 0;
        mascot->next_frame_tick = tick;
        result.status = mascot_tick_reenter;
        result.next_animation = new_animation;
        return result;
    }

    if (mascot->BornInterval->value.i > 0) {
        if ((int32_t)(tick - mascot->born_tick) > mascot->BornInterval->value.i) {
            if (mascot->born_count < mascot->BornCount->value.i) {
                result.status = mascot_tick_clone;
                return result;
            }
        }
    }

    if (mascot->next_frame_tick <= tick) {
        if (current_animation) {
            if (current_frame >= current_animation->frame_count) {
                if (actionref->action->loop || mascot->action_duration) {
                    mascot->frame_index = 0;
                } else {
                    if (mascot->BornCount->value.i > mascot->born_count) {
                        result.status = mascot_tick_clone;
                        return result;
                    }
                    result.status = mascot_tick_next;
                    return result;
                }
            }
            result.next_pose = current_animation->frames[mascot->frame_index++];
        }
    }

    result.status = mascot_tick_ok;
    return result;
}

enum mascot_tick_result breed_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(tick);

    enum mascot_tick_result oob_check = mascot_out_of_bounds_check(mascot);
    if (oob_check != mascot_tick_ok) {
        return oob_check;
    }

    // Check condition
    if (mascot->current_condition.expression_prototype && !mascot->current_condition.evaluated) {
        float vmres = 0.0;
        enum expression_execution_result res = expression_vm_execute(
            actionref->action->condition->body,
            mascot,
            &vmres
        );
        if (res == EXPRESSION_EXECUTION_ERROR) {
            LOG("ERROR", RED, "<Mascot:%s:%u> Condition errored for tick in action \"%s\"", mascot->prototype->name, mascot->id, actionref->action->name);
        }
        if (vmres == 0.0) {
            return mascot_tick_next;
        }
    }

    return mascot_tick_ok;
}

void breed_action_clean(struct mascot *mascot)
{
    mascot->animation_index = 0;
    mascot->frame_index = 0;
    mascot->next_frame_tick = 0;
    mascot->action_duration = 0;
}
