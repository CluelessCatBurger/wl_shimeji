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

    // if (abs(mascot->TargetX->value.i - mascot->X->value.i) > (environment_screen_width(mascot->environment)/2)) {
    //     LOG("DEBUG", RED, "<Mascot:%s:%u> Jump action target is too far", mascot->prototype->name, mascot->id);
    //     int32_t random_num = rand();
    //     if (random_num < RAND_MAX / 2) {
    //         jump_action_clean(mascot);
    //         return mascot_tick_next;
    //     } else {
    //         if (random_num < RAND_MAX / 3) {
    //             if (mascot->X->value.i > 0) {
    //                 mascot->X->value.i -= 1;
    //             } else {
    //                 mascot->X->value.i += 1;
    //             }
    //             mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
    //             jump_action_clean(mascot);
    //             return mascot_tick_reenter;
    //         }
    //     }
    // }
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
        DEBUG("<Mascot:%s:%u> Destination met", mascot->prototype->name, mascot->id);
        result.status = mascot_tick_next;
        return result;
    }

    if (mascot->action_duration <= tick) {
        DEBUG("<Mascot:%s:%u> Jump action watchdog run out", mascot->prototype->name, mascot->id);
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

    enum mascot_tick_result oob_check = mascot_out_of_bounds_check(mascot);
    if (oob_check != mascot_tick_ok) {
        return oob_check;
    }

    int32_t posx = mascot->X->value.i;
    int32_t posy = mascot->Y->value.i;
    int32_t target_x = mascot->TargetX->value.i;
    int32_t target_y = mascot->TargetY->value.i;
    float velocity_x = mascot->VelocityX->value.f;
    float velocity_y = mascot->VelocityY->value.f;
    float velocity = mascot->VelocityParam->value.f;

    bool looking_right = posx < target_x;

    if (target_x < (int)environment_workarea_left(mascot->environment)) {
        mascot->TargetX->value.i = target_x = environment_workarea_left(mascot->environment);
    } else if (target_x > (int)environment_workarea_right(mascot->environment)) {
        mascot->TargetX->value.i = target_x = environment_workarea_right(mascot->environment);
    }

    if (target_y == -1) {
        mascot->TargetY->value.i = target_y = posy;
    } else if (target_y < (int)environment_workarea_bottom(mascot->environment)) {
        mascot->TargetY->value.i = target_y = environment_workarea_bottom(mascot->environment);
    } else if (target_y > (int)environment_workarea_top(mascot->environment)) {
        mascot->TargetY->value.i = target_y = environment_workarea_top(mascot->environment);
    }

    // Calculate distances
    float distance_x = target_x - posx;

    // Check if posy has reached the screen height and make the object slide
    if (posy >= (int32_t)environment_workarea_bottom(mascot->environment)) {
        posy = environment_workarea_bottom(mascot->environment); // Cap the y-coordinate
        velocity_y = 0; // Stop vertical movement to slide horizontally
    } else {
        // Calculate the vertical distance to the peak based on the current position
        float distance_y_to_target = target_y - posy;

        if (distance_x != 0) {
            // Adjust velocity_y to decelerate as it approaches the target
            velocity_x = velocity * (distance_x / sqrtf(distance_x * distance_x + distance_y_to_target * distance_y_to_target));
            velocity_y = velocity * (distance_y_to_target / sqrtf(distance_x * distance_x + distance_y_to_target * distance_y_to_target));

            // Ensure the object slows down as it approaches the target
            if (posy > target_y) {
                velocity_y = -fabs(velocity_y); // Move down if above the target
            } else {
                velocity_y = fabs(velocity_y); // Move up if below the target
            }

            // Update position
            posx += (int32_t)velocity_x;
            posy += (int32_t)velocity_y;

            // Store updated velocities for the next tick
            mascot->VelocityX->value.f = velocity_x;
            mascot->VelocityY->value.f = velocity_y;
        }
    }


    if (fabs(distance_x) < fabs(velocity_x)) {
        posx = target_x;
        posy = target_y;
    }

    DEBUG("<Mascot:%s:%u> JUMP: moving to %d, %d", mascot->prototype->name, mascot->id, target_x, target_y);
    DEBUG("<Mascot:%s:%u> JUMP: moving by %f, %f", mascot->prototype->name, mascot->id, velocity_x, velocity_y);
    DEBUG("<Mascot:%s:%u> JUMP: distances %f", mascot->prototype->name, mascot->id, distance_x);

    if (distance_x == 0) {
        return mascot_tick_reenter;
    }

    if (looking_right != mascot->LookingRight->value.i) {
        mascot->LookingRight->value.i = looking_right;
        mascot_reattach_pose(mascot);
    }

    if (posx != mascot->X->value.i || posy != mascot->Y->value.i) {
        enum environment_move_result move_res = environment_subsurface_move(mascot->subsurface, posx, posy, true, true);
        if (move_res == environment_move_clamped) {
            return mascot_tick_reenter;
        }
        mascot->action_duration = tick + 5;
    }

    return mascot_tick_ok;
}

void jump_action_clean(struct mascot *mascot)
{
    mascot->VelocityParam->value.f = 0.0;
    mascot->TargetX->value.i = 0;
    mascot->TargetY->value.i = 0;
    mascot_announce_affordance(mascot, NULL);
}
