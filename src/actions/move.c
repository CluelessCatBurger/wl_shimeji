/*
    move.c - wl_shimeji's move action implementation

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

#include "move.h"
#include "actionbase.h"

enum mascot_tick_result move_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    if (!actionref->action->length) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Move action has no length", mascot->prototype->name, mascot->id);
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
    mascot->VelocityX->value.i = 0;
    mascot->VelocityY->value.i = 0;

    // Execute variables variables TargetX, TargetY
    struct mascot_local_variable* target_x = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_TARGETX_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_TARGETX_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_TARGETX_ID];
    struct mascot_local_variable* target_y = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_TARGETY_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_TARGETY_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_TARGETY_ID];

    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_TARGETX_ID, target_x) == mascot_tick_error) {
        return mascot_tick_error;
    }
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_TARGETY_ID, target_y) == mascot_tick_error) {
        return mascot_tick_error;
    }

    if (!target_x->used) mascot->TargetX->value.i = -1;
    if (!target_y->used) mascot->TargetY->value.i = -1;

    if (mascot->TargetX->value.i != -1) {
        if (mascot->TargetX->value.i < 0) {
            mascot->TargetX->value.i = 0;
        } else if (mascot->TargetX->value.i > (int32_t)environment_screen_width(mascot->environment)) {
            mascot->TargetX->value.i = environment_screen_width(mascot->environment);
        }
    }

    if (mascot->TargetY->value.i != -1) {
        if (mascot->TargetY->value.i < 0) {
            mascot->TargetY->value.i = 0;
        } else if (mascot->TargetY->value.i > (int32_t)environment_screen_height(mascot->environment)) {
            mascot->TargetY->value.i = (int32_t)environment_screen_height(mascot->environment);
        }
        mascot->TargetY->value.i = mascot_screen_y_to_mascot_y(mascot, mascot->TargetY->value.i);
    }

    mascot->state = mascot_state_move;

    mascot_announce_affordance(mascot, actionref->action->affordance);

    return mascot_tick_ok;

}

struct mascot_action_next move_action_next(struct mascot* mascot, struct mascot_action_reference *actionref, uint32_t tick)
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

    DEBUG("<Mascot:%s:%u> Move action next, TargetX, TargetY: %d, %d", mascot->prototype->name, mascot->id, mascot->TargetX->value.i, mascot->TargetY->value.i);
    DEBUG("<Mascot:%s:%u> Move action next, X, Y: %d, %d", mascot->prototype->name, mascot->id, mascot->X->value.i, mascot->Y->value.i);
    // Check if target is reached
    if ((mascot->X->value.i == mascot->TargetX->value.i || mascot->TargetX->value.i == -1) &&
        (mascot->Y->value.i == mascot->TargetY->value.i || mascot->TargetY->value.i == -1)) {
        result.status = mascot_tick_next;
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
                mascot->frame_index = 0;
            }
            result.next_pose = mascot->current_animation->frames[mascot->frame_index++];
        }
    }

    result.status = mascot_tick_ok;
    return result;
}

enum mascot_tick_result move_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(actionref);
    UNUSED(tick);

    DEBUG("<Mascot:%s:%u> Move action tick, TargetX, TargetY: %d, %d", mascot->prototype->name, mascot->id, mascot->TargetX->value.i, mascot->TargetY->value.i);

    enum mascot_tick_result oob_check = mascot_out_of_bounds_check(mascot);
    if (oob_check != mascot_tick_ok) {
        return oob_check;
    }

    int32_t target_x = mascot->TargetX->value.i;
    int32_t target_y = mascot->TargetY->value.i;
    int32_t posx = mascot->X->value.i;
    int32_t posy = mascot->Y->value.i;

    float velocity_x = mascot->VelocityX->value.f;
    float velocity_y = mascot->VelocityY->value.f;

    bool looking_right = mascot->LookingRight->value.i;

    if (target_x != INT32_MAX && target_x != -1) {
        if (posx < target_x) looking_right = true;
        else if (posx > target_x) looking_right = false;
        if (target_x < 0) {
            target_x = 0;
        }
        else if (target_x > (int32_t)environment_screen_width(mascot->environment)) {
            target_x = (int32_t)environment_screen_width(mascot->environment);
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
        DEBUG("<Mascot:%s:%u> Move action tick, posy, target_y, down: %d, %d, %d", mascot->prototype->name, mascot->id, posy, target_y, down);

        if (target_y < 0) {
            mascot->TargetY->value.i = target_y = 0;
        }
        else if (target_y > (int32_t)environment_screen_height(mascot->environment)) {
            mascot->TargetY->value.i = target_y = (int32_t)environment_screen_height(mascot->environment);
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
        environment_subsurface_move(mascot->subsurface, posx, posy, true);
        return mascot_tick_reenter;
    }

    if (posx != mascot->X->value.i || posy != mascot->Y->value.i) {
        DEBUG("<Mascot:%s:%u> Moving towards target, current pos (%d,%d), setting pos (%d,%d)", mascot->prototype->name, mascot->id, mascot->X->value.i, mascot->Y->value.i, posx, posy);
        enum environment_move_result move_result = environment_subsurface_move(mascot->subsurface, posx, posy, true);
        if (move_result == environment_move_clamped) {
            mascot->TargetX->value.i = -1;
            mascot->TargetY->value.i = -1;
            return mascot_tick_reenter;
        }
    }
    return mascot_tick_ok;
}

void move_action_clean(struct mascot *mascot) {
    mascot->TargetX->value.i = 0;
    mascot->TargetY->value.i = 0;
    mascot->VelocityX->value.f = 0;
    mascot->VelocityY->value.f = 0;
    mascot_announce_affordance(mascot, NULL);
}
