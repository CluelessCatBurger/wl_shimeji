/*
    jump.c - wl_shimeji's jump action implementation

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

#include "jump.h"
#include "environment.h"
#include "mascot.h"
#include <stdint.h>
#include <sys/types.h>

enum mascot_tick_result jump_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    if (!actionref->action->length) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Jump action has no length", mascot->prototype->name, mascot->id);
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

    mascot->action_duration = mascot_duration_limit(mascot, actionref->duration_limit, tick);

    // Reset action index, frame and animation index
    mascot->action_index = 0;
    mascot->frame_index = 0;
    mascot->next_frame_tick = 0;
    mascot->animation_index = 0;

    mascot->TargetX->value.i = 0;
    mascot->TargetY->value.i = 0;
    mascot->VelocityParam->value.f = 0.0;

    mascot->action_duration = tick + 5;

    // Execute variables variables InitialVelX and InitialVelY, AirDragX and AirDragY, Gravity
    struct mascot_local_variable* target_x = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_TARGETX_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_TARGETX_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_TARGETX_ID];
    struct mascot_local_variable* target_y = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_TARGETY_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_TARGETY_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_TARGETY_ID];
    struct mascot_local_variable* velocity_param = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_VELOCITYPARAM_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_VELOCITYPARAM_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_VELOCITYPARAM_ID];

    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_TARGETX_ID, target_x) == mascot_tick_error) return mascot_tick_error;
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_TARGETY_ID, target_y) == mascot_tick_error) return mascot_tick_error;
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_VELOCITYPARAM_ID, velocity_param) == mascot_tick_error) return mascot_tick_error;
    if (!velocity_param->used) mascot->VelocityParam->value.f = 20.0;

    if (mascot->TargetX->value.i == -1 || mascot->TargetY->value.i == -1) {
        jump_action_clean(mascot);
        return mascot_tick_next;
    }

    if (mascot->TargetY->value.i != -1) {
        mascot->TargetY->value.i = mascot_screen_y_to_mascot_y(mascot, mascot->TargetY->value.i);
    }

    mascot->state = mascot_state_jump;

    mascot_announce_affordance(mascot, actionref->action->affordance);

    return mascot_tick_ok;
}

struct mascot_action_next jump_action_next(struct mascot* mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    struct mascot_action_next result = {0};
    result.next_action = *actionref;

    if (mascot->TargetX->value.i == mascot->X->value.i) {
        result.status = mascot_tick_next;
        return result;
    }

    // Check conditions
    enum mascot_tick_result actionref_cond = mascot_recheck_condition(mascot, actionref->condition);
    enum mascot_tick_result action_cond = mascot_recheck_condition(mascot, actionref->action->condition);

    if (actionref_cond == mascot_tick_next || action_cond == mascot_tick_next) {
        DEBUG("<Mascot:%s:%u> Jump action next condition is failed", mascot->prototype->name, mascot->id);
        result.status = mascot_tick_next;
        return result;
    }

    if (actionref_cond == mascot_tick_error || action_cond == mascot_tick_error) {
        result.status = mascot_tick_error;
        return result;
    }

    const struct mascot_animation* current_animation = mascot->current_animation;
    const struct mascot_animation* new_animation = NULL;

    for (uint16_t i = 0; i < actionref->action->length; i++) {
        if (actionref->action->content[i].kind != mascot_action_content_type_animation) {
            LOG("ERROR", RED, "<Mascot:%s:%u> Action content is not an animation", mascot->prototype->name, mascot->id);
            result.status = mascot_tick_error;
            return result;
        }

        enum mascot_tick_result animcond = mascot_check_condition(mascot, actionref->action->content[i].value.animation->condition);

        if (animcond == mascot_tick_error) {
            result.status = mascot_tick_error;
            return result;
        }

        if (animcond == mascot_tick_next) continue;

        new_animation = actionref->action->content[i].value.animation;
        mascot->animation_index = i;
        break;
    }

    if (new_animation != current_animation) {
        result.next_animation = new_animation;
        mascot->next_frame_tick = tick;
        result.status = mascot_tick_reenter;
        mascot->frame_index = 0;
        return result;
    }

    if (mascot->next_frame_tick <= tick) {
        if (mascot->current_animation) {
            if (mascot->frame_index >= mascot->current_animation->frame_count) {
                mascot->frame_index = 0;
            }
            DEBUG("NEW POSE %p", mascot->current_animation->frames[mascot->frame_index]);
            result.next_pose = mascot->current_animation->frames[mascot->frame_index++];
        }
    }

    result.status = mascot_tick_ok;
    return result;
}

enum mascot_tick_result jump_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(actionref);
    UNUSED(tick);

    int32_t posx = mascot->X->value.i;
    int32_t posy = mascot->Y->value.i;
    int32_t target_x = mascot->TargetX->value.i;
    int32_t target_y = mascot->TargetY->value.i;
    float velocity_param = mascot->VelocityParam->value.f;

    bool looking_right = posx < target_x;

    float distance_x = target_x - posx;
    float distance_y = (target_y - posy) + fabsf(distance_x);
    float distance_abs = sqrtf(distance_x * distance_x + distance_y * distance_y);

    if (looking_right != mascot->LookingRight->value.i) {
        mascot->LookingRight->value.i = looking_right;
        mascot_reattach_pose(mascot);
    }

    if (distance_abs <= velocity_param || distance_abs == 0.0f) {
        environment_subsurface_move(mascot->subsurface, target_x, target_y, true, true);
        return mascot_tick_reenter;
    }

    float velocity_x = (distance_x / distance_abs) * velocity_param;
    float velocity_y = (distance_y / distance_abs) * velocity_param;

    environment_subsurface_move(mascot->subsurface,
                                (int32_t)(posx + velocity_x),
                                (int32_t)(posy + velocity_y),
                                true, true);

    return mascot_tick_ok;
}

void jump_action_clean(struct mascot *mascot)
{
    mascot->VelocityParam->value.f = 0.0;
    mascot->TargetX->value.i = 0;
    mascot->TargetY->value.i = 0;
    mascot_announce_affordance(mascot, NULL);
}
