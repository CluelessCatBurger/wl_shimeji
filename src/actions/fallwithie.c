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

#include "fallwithie.h"
#include "actionbase.h"
#include "config.h"
#include "environment.h"

enum mascot_tick_result fallwithie_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{

    if (!config_get_ie_throwing()) {
        DEBUG("<Mascot:%s:%u> IE throwing is disabled, skipping action", mascot->prototype->name, mascot->id);
        mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
        return mascot_tick_reenter;
    }

    if (!actionref->action->length) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Fall action has no length", mascot->prototype->name, mascot->id);
        return mascot_tick_error;
    }

    struct ie_object* ie = environment_get_ie(mascot->environment);
    if (!ie) {
        DEBUG("<Mascot:%s:%u> No IE object found, skipping action", mascot->prototype->name, mascot->id);
        mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
        return mascot_tick_reenter;
    }

    if (!ie->active) {
        DEBUG("<Mascot:%s:%u> IE object is inactive, skipping action", mascot->prototype->name, mascot->id);
        mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
        return mascot_tick_reenter;
    }

    if (!environment_ie_allows_move(mascot->environment)) {
        DEBUG("<Mascot:%s:%u> IE object does not allow movement, skipping action", mascot->prototype->name, mascot->id);
        mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
        return mascot_tick_reenter;
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
    struct mascot_local_variable* ie_offt_x = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_IEOFFSETX_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_IEOFFSETX_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_IEOFFSETX_ID];
    struct mascot_local_variable* ie_offt_y = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_IEOFFSETY_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_IEOFFSETY_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_IEOFFSETY_ID];

    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_INITIALVELX_ID, initialvx) == mascot_tick_error) return mascot_tick_error;
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_INITIALVELY_ID, initialvy) == mascot_tick_error) return mascot_tick_error;
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_AIRDRAGX_ID, airdragx) == mascot_tick_error) return mascot_tick_error;
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_AIRDRAGY_ID, airdragy) == mascot_tick_error) return mascot_tick_error;
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_GRAVITY_ID, gravity) == mascot_tick_error) return mascot_tick_error;
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_IEOFFSETX_ID, ie_offt_x) == mascot_tick_error) return mascot_tick_error;
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_IEOFFSETY_ID, ie_offt_y) == mascot_tick_error) return mascot_tick_error;
    if (!airdragx->used) mascot->AirDragX->value.f = 0.05;
    if (!airdragy->used) mascot->AirDragY->value.f = 0.1;
    if (!gravity->used) mascot->Gravity->value.f = 2.0;

    mascot->VelocityX->value.f = mascot->InitialVelX->value.f;
    mascot->VelocityY->value.f = mascot->InitialVelY->value.f;

    mascot->state = mascot_state_ie_fall;

    mascot->action_duration = tick + 5; // Watchdog

    mascot_announce_affordance(mascot, actionref->action->affordance);
    return mascot_tick_ok;

}

enum mascot_tick_result fallwithie_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
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
    int32_t ie_offt_x = mascot_get_variable_i(mascot, MASCOT_LOCAL_VARIABLE_IEOFFSETX_ID) / environment_screen_scale(mascot->environment);
    int32_t ie_offt_y = mascot_get_variable_i(mascot, MASCOT_LOCAL_VARIABLE_IEOFFSETY_ID) / environment_screen_scale(mascot->environment);

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

    posx += new_x;
    posy -= new_y;

    INFO("FallWithIE TICK, modx mody newx newy velx vely posx posy %f %f %d %d %f %f %d %d", mod_x, mod_y, new_x, new_y, velocity_x, velocity_y, posx, posy);

    if (mascot->LookingRight->value.i != looking_right) {
        mascot->LookingRight->value.i = looking_right;
        mascot_reattach_pose(mascot);
    }

    if (posx != mascot->X->value.i || posy != mascot->Y->value.i) {
        enum environment_move_result move_result = environment_subsurface_move(mascot->subsurface, posx, posy, true, true);
        if (move_result == environment_move_clamped) {
            if (mascot->X->value.i == 0 && mascot->Y->value.i == 0) {
                mascot->X->value.i = 1;
            } else if (mascot->Y->value.i == -1 && mascot->X->value.i == (int32_t)environment_workarea_width(mascot->environment)) {
                mascot->X->value.i = environment_workarea_width(mascot->environment) - 1;
            }
            return mascot_tick_reenter;
        }
        mascot->action_duration = tick + 5;
        struct ie_object* ie = environment_get_ie(mascot->environment);
        if (ie) {
            if (ie->active) {
                enum environment_move_result move_res = environment_move_ok;
                if (looking_right) {
                    move_res = environment_ie_move(mascot->environment, posx - ie_offt_x, mascot_screen_y_to_mascot_y(mascot, posy) + ie_offt_y - ie->height);
                } else {
                    move_res = environment_ie_move(mascot->environment, posx + ie_offt_x - ie->width, mascot_screen_y_to_mascot_y(mascot, posy) + ie_offt_y - ie->height);
                }
                if (move_res == environment_move_invalid) {
                    mascot_set_behavior(mascot, mascot_fall_behavior(mascot));
                    return mascot_tick_reenter;
                }
            }
        }
    }

    return mascot_tick_ok;
}

struct mascot_action_next fallwithie_action_next(struct mascot* mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    struct mascot_action_next result = {0};
    result.next_action = *actionref;

    enum environment_border_type btype = environment_get_border_type(mascot->environment, mascot->X->value.i, mascot->Y->value.i);

    if (btype == environment_border_type_wall) {
        result.status = mascot_tick_next;
        return result;
    }

    struct ie_object* ie = environment_get_ie(mascot->environment);
    if (!ie) {
        WARN("<Mascot:%s:%u> Attached environment lost IE", mascot->prototype->name, mascot->id);
        mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
        fallwithie_action_clean(mascot);
        result.status = mascot_tick_next;
        return result;
    }
    if (!ie->active) {
        INFO("<Mascot:%s:%u> IE is no longer active", mascot->prototype->name, mascot->id);
        mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
        fallwithie_action_clean(mascot);
        result.status = mascot_tick_next;
        return result;
    }
    if (ie->state == IE_STATE_MOVED) {
        INFO("<Mascot:%s:%u> IE is moved", mascot->prototype->name, mascot->id);
        mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
        fallwithie_action_clean(mascot);
        result.status = mascot_tick_next;
        return result;
    }


    int32_t mascot_x = mascot->X->value.i;
    int32_t mascot_y = mascot_screen_y_to_mascot_y(mascot, mascot->Y->value.i);
    int32_t ie_offt_x = mascot_get_variable_i(mascot, MASCOT_LOCAL_VARIABLE_IEOFFSETX_ID) / environment_screen_scale(mascot->environment);
    int32_t ie_offt_y = mascot_get_variable_i(mascot, MASCOT_LOCAL_VARIABLE_IEOFFSETY_ID) / environment_screen_scale(mascot->environment);

    int32_t ie_corner_x = mascot->LookingRight->value.i ? ie->x : ie->x + ie->width;
    int32_t distance_x = abs((mascot_x + (mascot->LookingRight->value.i ? ie_offt_x : -ie_offt_x)) - ie_corner_x);
    int32_t distance_y = abs((mascot_y + ie_offt_y) - (ie->y + ie->height));

    if (distance_x > 50 || distance_y > 50) {
        mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
        fallwithie_action_clean(mascot);
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


    if (mascot->action_duration <= tick) {
        result.status = mascot_tick_next;
        return result;
    }

    // Check conditions
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
            result.next_pose = mascot->current_animation->frames[mascot->frame_index++];
        }
    }

    result.status = mascot_tick_ok;
    return result;
}

void fallwithie_action_clean(struct mascot *mascot)
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
