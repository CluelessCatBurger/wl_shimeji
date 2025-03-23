/*
    scanjump.c - wl_shimeji's scanjump action implementation

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

#include "scanjump.h"
#include "environment.h"
#include <stdint.h>

enum mascot_tick_result scanjump_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    if (!actionref->action->length) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Scanjump action has no length", mascot->prototype->name, mascot->id);
        return mascot_tick_error;
    }

    // FIRST THAT WE NEED TO CHECK: is our target exists?
    struct mascot* new_target = mascot_get_target_by_affordance(mascot, actionref->action->affordance);
    if (!new_target) {
        DEBUG("<Mascot:%s:%u> Scanjump action has no target", mascot->prototype->name, mascot->id);
        return mascot_tick_next;
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

    mascot->VelocityParam->value.f = 0.0;

    struct mascot_local_variable* velocity_param = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_VELOCITYPARAM_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_VELOCITYPARAM_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_VELOCITYPARAM_ID];

    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_VELOCITYPARAM_ID, velocity_param) == mascot_tick_error) return mascot_tick_error;
    if (!velocity_param->used) mascot->VelocityParam->value.f = 20.0;

    mascot->state = mascot_state_jump;

    mascot_announce_affordance(mascot, NULL);
    mascot->target_mascot = new_target;

    return mascot_tick_ok;
}

struct mascot_action_next scanjump_action_next(struct mascot* mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    struct mascot_action_next result = {0};
    result.next_action = *actionref;

    // Ensure border conditions are still met
    if (actionref->action->border_type != environment_border_type_any) {
        if (
            environment_get_border_type(mascot->environment, mascot->X->value.i, mascot->Y->value.i)
            != actionref->action->border_type
        ) {
            result.status = mascot_tick_next;
            return result;
        }
    }

    int32_t distance = sqrt((mascot->target_mascot->X->value.i - mascot->X->value.i) * (mascot->target_mascot->X->value.i - mascot->X->value.i) + (mascot->target_mascot->Y->value.i - mascot->Y->value.i) * (mascot->target_mascot->Y->value.i - mascot->Y->value.i));
    int32_t target_velocity = sqrt(mascot->target_mascot->VelocityX->value.f * mascot->target_mascot->VelocityX->value.f + mascot->target_mascot->VelocityY->value.f * mascot->target_mascot->VelocityY->value.f)*2;
    int32_t my_velocity = sqrt(mascot->VelocityX->value.f * mascot->VelocityX->value.f + mascot->VelocityY->value.f * mascot->VelocityY->value.f)*2;

    // Destination is reached if distance is less than or equal mascots velocity*2
    if (distance <= fmax(target_velocity, my_velocity) && mascot->environment == mascot->target_mascot->environment) {
        struct mascot* target = mascot->target_mascot;
        scanjump_action_clean(mascot);
        bool interaction_result = mascot_interact(mascot, target, actionref->action->affordance, actionref->action->behavior, actionref->action->target_behavior);
        if (!interaction_result) {
            result.status = mascot_tick_next;
            return result;
        }
        result.status = mascot_tick_reenter;
        return result;
    }

    // Check if our target still have same affordance
    const char* affordance = mascot->target_mascot->current_affordance;
    if (affordance) {
        if (strcasecmp(actionref->action->affordance, affordance) || strlen(affordance) != strlen(actionref->action->affordance)) {
            affordance = NULL;
            mascot->target_mascot = NULL;
        }
    }


    if (!affordance) {
        // Try to find new target
        struct mascot* new_target = mascot_get_target_by_affordance(mascot, actionref->action->affordance);
        if (!new_target) {
            result.status = mascot_tick_next;
            return result;
        }
        mascot->target_mascot = new_target;
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

enum mascot_tick_result scanjump_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(tick);
    UNUSED(actionref);

    enum mascot_tick_result oob_check = mascot_out_of_bounds_check(mascot);
    if (oob_check != mascot_tick_ok) {
        return oob_check;
    }

    int32_t env_diff_x, env_diff_y;
    environment_global_coordinates_delta(mascot->target_mascot->environment, mascot->environment, &env_diff_x, &env_diff_y);

    int32_t target_x = mascot->target_mascot->X->value.i;
    int32_t target_y = mascot->target_mascot->Y->value.i;

    target_x += env_diff_x;
    target_y = mascot_screen_y_to_mascot_y(mascot->target_mascot, target_y);
    target_y += env_diff_y;
    target_y = mascot_screen_y_to_mascot_y(mascot, target_y);

    int32_t posx = mascot->X->value.i;
    int32_t posy = mascot->Y->value.i;
    float velocity_x = mascot->VelocityX->value.f;
    float velocity_y = mascot->VelocityY->value.f;
    float velocity = mascot->VelocityParam->value.f;

    // Expose also target's position in TargetX and TargetY (1.0.21.3)
    mascot->TargetX->value.i = target_x;
    mascot->TargetY->value.i = target_y;

    bool looking_right = posx < target_x;

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

    // Stop movement when reaching the target or sliding to the edge
    if (abs(target_x - posx) < fabs(velocity_x) && abs(target_y - posy) < fabs(velocity_y)) {
        posx = target_x;
        posy = target_y;
        velocity_x = 0;
        velocity_y = 0; // Reset velocities when target is reached
    }

    DEBUG("<Mascot:%s:%u> SCANJUMP: moving to %d, %d", mascot->prototype->name, mascot->id, target_x, target_y);
    DEBUG("<Mascot:%s:%u> SCANJUMP: moving by %f, %f", mascot->prototype->name, mascot->id, velocity_x, velocity_y);
    DEBUG("<Mascot:%s:%u> SCANJUMP: distances %f", mascot->prototype->name, mascot->id, distance_x);

    if (distance_x == 0) {
        return mascot_tick_reenter;
    }

    if (looking_right != mascot->LookingRight->value.i) {
        mascot->LookingRight->value.i = looking_right;
        mascot_reattach_pose(mascot);
    }

    if (posx != mascot->X->value.i || posy != mascot->Y->value.i) {
        environment_subsurface_move(mascot->subsurface, posx, posy, true, true);
    }

    return mascot_tick_ok;
}

void scanjump_action_clean(struct mascot *mascot)
{
    mascot->VelocityParam->value.f = 0.0;
    mascot->TargetX->value.i = 0;
    mascot->TargetY->value.i = 0;
    mascot->target_mascot = NULL;
    mascot_announce_affordance(mascot, NULL);
}
