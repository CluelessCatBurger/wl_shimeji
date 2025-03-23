/*
    scanmove.c - wl_shimeji's scanmove action implementation

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

#include "scanmove.h"
#include "actionbase.h"
#include "environment.h"
#include "mascot.h"
#include <string.h>
#include <strings.h>
#include <time.h>

enum mascot_tick_result scanmove_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    if (!actionref->action->length) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Scanmove action has no length", mascot->prototype->name, mascot->id);
        return mascot_tick_error;
    }

    // FIRST THAT WE NEED TO CHECK: is our target exists?
    struct mascot* new_target = mascot_get_target_by_affordance(mascot, actionref->action->affordance);
    if (!new_target) {
        DEBUG("<Mascot:%s:%u> Scanmove action has no target", mascot->prototype->name, mascot->id);
        return mascot_tick_next;
    }

    // Check if action border requirements are met
    enum environment_border_type border_type = environment_get_border_type(mascot->environment, mascot->X->value.i, mascot->Y->value.i);
    if (actionref->action->border_type != environment_border_type_any) {
        if (
            border_type
            != actionref->action->border_type
        ) {
            if (border_type != environment_border_type_floor) {
                mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
                scanmove_action_clean(mascot);
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

    mascot->VelocityX->value.i = 0;
    mascot->VelocityY->value.i = 0;

    mascot->state = mascot_state_scanmove;

    mascot_announce_affordance(mascot, NULL);
    mascot->target_mascot = new_target;

    return mascot_tick_ok;

}

struct mascot_action_next scanmove_action_next(struct mascot* mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    struct mascot_action_next result = {0};

    if (mascot->action_duration && tick >= mascot->action_duration) {
        result.status = mascot_tick_next;
        return result;
    }

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
        scanmove_action_clean(mascot);
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
                mascot->frame_index = 0;
            }
            result.next_pose = mascot->current_animation->frames[mascot->frame_index++];
        }
    }

    result.status = mascot_tick_ok;
    return result;
}

enum mascot_tick_result scanmove_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(actionref);
    UNUSED(tick);


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

    bool looking_right = mascot->LookingRight->value.i;

    // Expose also target's position in TargetX and TargetY (1.0.21.3)
    mascot->TargetX->value.i = target_x;
    mascot->TargetY->value.i = target_y;

    if (target_x != INT32_MAX && target_x != -1) {
        if (posx < target_x) looking_right = true;
        else if (posx > target_x) looking_right = false;
        if (target_x < (int)environment_workarea_left(mascot->environment)) {
            target_x = (int)environment_workarea_left(mascot->environment);
        }
        else if (target_x > (int32_t)environment_workarea_right(mascot->environment)) {
            target_x = (int32_t)environment_workarea_right(mascot->environment);
        }

        if (looking_right) {
            if (posx - velocity_x > target_x) {
                posx = target_x;
            } else {
                posx -= velocity_x;
            }
        } else {
            if (posx + velocity_x < target_x) {
                posx = target_x;
            } else {
                posx += velocity_x;
            }
        }
    }
    if (target_y != INT32_MAX && target_y != -1) {
        bool down = posy > target_y;

        if (target_y < (int32_t)environment_workarea_bottom(mascot->environment)) {
            mascot->TargetY->value.i = target_y = (int32_t)environment_workarea_bottom(mascot->environment);
        }
        else if (target_y > (int32_t)environment_workarea_top(mascot->environment)) {
            mascot->TargetY->value.i = target_y = (int32_t)environment_workarea_top(mascot->environment);
        }

        if (down) {
            if (posy - velocity_y <= target_y) {
                posy = target_y;
            } else {
                posy -= velocity_y;
            }
        } else {
            if (posy + velocity_y >= target_y) {
                posy = target_y;
            } else {
                posy += velocity_y;
            }
       }
    }

    if (mascot->LookingRight->value.i != looking_right) {
        mascot->LookingRight->value.i = looking_right;
        mascot_reattach_pose(mascot);
    }

    if ((posx == target_x || target_x == -1 ) && (posy == target_y || target_y == -1)) {
        DEBUG("<Mascot:%s:%u> Reached target, current pos (%d,%d), setting pos (%d,%d)", mascot->prototype->name, mascot->id, posx, posy, target_x, target_y);
        enum environment_move_result move_result = environment_subsurface_move(mascot->subsurface, posx, posy, true, true);
        if (move_result == environment_move_ok) return mascot_tick_reenter;
        else {
            mascot->TargetX->value.i = mascot->X->value.i;
            mascot->TargetY->value.i = mascot->Y->value.i;

            return mascot_tick_reenter;
        }
    }

    if (posx != mascot->X->value.i || posy != mascot->Y->value.i) {
        DEBUG("<Mascot:%s:%u> Scanmoving towards target, current pos (%d,%d), setting pos (%d,%d)", mascot->prototype->name, mascot->id, mascot->X->value.i, mascot->Y->value.i, posx, posy);
        environment_subsurface_move(mascot->subsurface, posx, posy, true, true);
    }
    return mascot_tick_ok;
}

void scanmove_action_clean(struct mascot *mascot) {
    mascot->TargetX->value.i = 0;
    mascot->TargetY->value.i = 0;
    mascot->VelocityX->value.f = 0;
    mascot->VelocityY->value.f = 0;
    mascot->target_mascot = NULL;
    mascot->state = mascot_state_none;
    mascot_announce_affordance(mascot, NULL);
}
