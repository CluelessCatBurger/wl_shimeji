/*
    fall.c - wl_shimeji's fall action implementation

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

#include "fall.h"
#include "actionbase.h"
#include <stdint.h>

enum mascot_tick_result fall_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    if (!actionref->action->length) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Fall action has no length", mascot->prototype->name, mascot->id);
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
        mascot->action_duration = tick + vmres;
    }

    // Reset action index, frame and animation index
    mascot->action_index = 0;
    mascot->frame_index = 0;
    mascot->next_frame_tick = 0;
    mascot->animation_index = 0;

    mascot->VelocityX->value.f = 0.0;
    mascot->VelocityY->value.f = 0.0;
    mascot->AirDragX->value.f = 0.0;
    mascot->AirDragY->value.f = 0.0;
    mascot->InitialVelX->value.f = 0.0;
    mascot->InitialVelY->value.f = 0.0;


    // Execute variables variables InitialVelX and InitialVelY, AirDragX and AirDragY, Gravity
    struct mascot_local_variable* initialvx = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_INITIALVELX_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_INITIALVELX_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_INITIALVELX_ID];
    struct mascot_local_variable* initialvy = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_INITIALVELY_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_INITIALVELY_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_INITIALVELY_ID];
    struct mascot_local_variable* airdragx = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_AIRDRAGX_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_AIRDRAGX_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_AIRDRAGX_ID];
    struct mascot_local_variable* airdragy = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_AIRDRAGY_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_AIRDRAGY_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_AIRDRAGY_ID];
    struct mascot_local_variable* gravity = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_GRAVITY_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_GRAVITY_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_GRAVITY_ID];

    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_INITIALVELX_ID, initialvx) == mascot_tick_error) return mascot_tick_error;
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_INITIALVELY_ID, initialvy) == mascot_tick_error) return mascot_tick_error;
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_AIRDRAGX_ID, airdragx) == mascot_tick_error) return mascot_tick_error;
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_AIRDRAGY_ID, airdragy) == mascot_tick_error) return mascot_tick_error;
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_GRAVITY_ID, gravity) == mascot_tick_error) return mascot_tick_error;
    if (!airdragx->used) mascot->AirDragX->value.f = 0.05;
    if (!airdragy->used) mascot->AirDragY->value.f = 0.1;
    if (!gravity->used) mascot->Gravity->value.f = 2.0;


    if (mascot->InitialVelY->value.f < 0.0) {
        if (environment_get_border_type(mascot->environment, mascot->X->value.i, mascot->Y->value.i) == environment_border_type_ceiling) {
            fall_action_clean(mascot);
            return mascot_tick_next;
        }
    } else {
        if (environment_get_border_type(mascot->environment, mascot->X->value.i, mascot->Y->value.i) != environment_border_type_none) {
            fall_action_clean(mascot);
            return mascot_tick_next;
        }
    }

    if (mascot->InitialVelX->value.f == 0.0) {
        if (environment_get_border_type(mascot->environment, mascot->X->value.i, mascot->Y->value.i) == environment_border_type_wall) {
            fall_action_clean(mascot);
            return mascot_tick_next;
        }
    }

    mascot->VelocityX->value.f = mascot->InitialVelX->value.f;
    mascot->VelocityY->value.f = mascot->InitialVelY->value.f;

    mascot->state = mascot_state_fall;

    mascot->action_duration = tick + 5; // Watchdog

    mascot_announce_affordance(mascot, actionref->action->affordance);

    INFO("<Mascot:%s:%u> Initialized action %s", mascot->prototype->name, mascot->id, actionref->action->name);

    return mascot_tick_ok;

}

enum mascot_tick_result fall_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(actionref);

    enum mascot_tick_result oob_check = mascot_out_of_bounds_check(mascot);
    if (oob_check != mascot_tick_ok) {
        return oob_check;
    }

    float velocity_x = mascot->VelocityX->value.f;
    float velocity_y = mascot->VelocityY->value.f;
    float mod_x = mascot->ModX->value.f;
    float mod_y = mascot->ModY->value.f;
    int32_t new_x = mascot->X->value.i;
    int32_t new_y = mascot->Y->value.i;
    float air_drag_x = mascot->AirDragX->value.f;
    float air_drag_y = mascot->AirDragY->value.f;
    float gravity = mascot->Gravity->value.f;
    int32_t posx = mascot->X->value.i;
    int32_t posy = mascot->Y->value.i;

    bool looking_right = mascot->LookingRight->value.i;

    if (velocity_x != 0) {
        looking_right = velocity_x > 0;
    }
    mascot->VelocityX->value.f = velocity_x = (velocity_x - (velocity_x*air_drag_x));
    mascot->VelocityY->value.f = velocity_y = (velocity_y - (velocity_y*air_drag_y) + gravity);

    velocity_x = (int32_t)velocity_x;
    velocity_y = (int32_t)velocity_y;

    mod_x += fmod(velocity_x, 1);
    mod_y += fmod(velocity_y, 1);

    new_x = velocity_x + mod_x;
    new_y = velocity_y + mod_y;

    mascot->ModX->value.f = fmod(mod_x, 1.0);
    mascot->ModY->value.f = fmod(mod_y, 1.0);

    int32_t dev = (int32_t)fmax(abs(new_x), abs(new_y));
    if (dev < 1) dev = 1;

    posx += new_x;
    posy -= new_y;

    if (mascot->LookingRight->value.i != looking_right) {
        mascot->LookingRight->value.i = looking_right;
        mascot_reattach_pose(mascot);
    }

    if (posx != mascot->X->value.i || posy != mascot->Y->value.i) {
        enum environment_move_result move_result = environment_subsurface_move(mascot->subsurface, posx, posy, true);
        if (move_result == environment_move_clamped) {
            if (mascot->X->value.i == 0 && mascot->Y->value.i == 0) {
                mascot->X->value.i = 1;
            } else if (mascot->Y->value.i == -1 && mascot->X->value.i == (int32_t)environment_screen_width(mascot->environment)) {
                mascot->X->value.i = environment_screen_width(mascot->environment) - 1;
            }
            return mascot_tick_reenter;
        }
        mascot->action_duration = tick + 5;
    }

    return mascot_tick_ok;
}

struct mascot_action_next fall_action_next(struct mascot* mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    DEBUG("FALL NEXT");
    struct mascot_action_next result = {0};
    result.next_action = *actionref;

    enum environment_border_type btype = environment_get_border_type(mascot->environment, mascot->X->value.i, mascot->Y->value.i);

    if (btype == environment_border_type_wall) {
        result.status = mascot_tick_next;
        return result;
    }
    if (mascot->VelocityY->value.f != 0.0) {
        if (btype == environment_border_type_ceiling && mascot->VelocityY->value.f < 0.0) {
            result.status = mascot_tick_next;
            return result;
        } else if (btype == environment_border_type_floor && mascot->VelocityY->value.f > 0.0) {
            result.status = mascot_tick_next;
            return result;
        }
    } else {
        if (btype != environment_border_type_none) {
            result.status = mascot_tick_next;
            return result;
        }
    }

    DEBUG("FALL DURATION VS TICK %d %d", mascot->action_duration, tick);

    if (mascot->action_duration <= tick) {
        DEBUG("FALL WATCHDOG TRIGGERED");
        result.status = mascot_tick_next;
        return result;
    }

    // Check conditions
    enum mascot_tick_result actionref_cond = mascot_recheck_condition(mascot, actionref->condition);
    enum mascot_tick_result action_cond = mascot_recheck_condition(mascot, actionref->action->condition);

    if (actionref_cond == mascot_tick_next || action_cond == mascot_tick_next) {
        DEBUG("FALL COND NEXT");
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

        if (animcond == mascot_tick_next) {
            continue;
        }

        new_animation = actionref->action->content[i].value.animation;
        mascot->animation_index = i;
        break;
    }

    if (new_animation != current_animation) {
        DEBUG("FALL ASSIGNS NEW ANIMATION");
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

void fall_action_clean(struct mascot *mascot)
{
    mascot->VelocityX->value.f = 0;
    mascot->VelocityY->value.f = 0;
    mascot->ModX->value.f = 0;
    mascot->ModY->value.f = 0;
    mascot->AirDragX->value.f = 0;
    mascot->AirDragY->value.f = 0;
    mascot->Gravity->value.f = 0;
    mascot_announce_affordance(mascot, NULL);
}
