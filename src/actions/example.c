/*
    example.c - wl_shimeji's example action implementation

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

#include "example.h"

enum mascot_tick_result example_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    /*
       This is initializator of action.
       It will be called on action start and should perform all necessary
       steps to prepare mascot for action execution.

        This function should return one of the following values:
        - mascot_tick_ok: Action is initialized successfully and can be processed
        - mascot_tick_next: Action is cannot be initialized successfully because of condition
        checks or if action is should return immediately (offset, look).
        - mascot_tick_error: Action is cannot be initialized because of some error.
    */

    // Usually you want to check action "length" - count of subactions/animations in action
    // Transient actions usually have no length and you may want to return error.
    // Some actions like simple actions should contain at least one animation to render actual animation.
    if (!actionref->action->length) {
        WARN("<Mascot:%s:%u> Example action has no length", mascot->prototype->name, mascot->id);
        return mascot_tick_error;
    }

    // In some actions you may want to perform ground check to ensure that mascot is on proper border type.
    // That action will automatically set action to "Fall" if mascot is proper border type and clean up action.
    enum mascot_tick_result ground_check = mascot_ground_check(mascot, actionref, example_action_clean);
    if (ground_check != mascot_tick_ok) {
        return ground_check;
    }

    // After ground check you usually want to ensure that action conditions are met.
    // We do it after ground check as ground check costs less than condition check.
    // Condition check is complex expressions that are evaluated using virtual machine,
    // I already optimized it to be fast, but it still costs more than ground check.
    enum mascot_tick_result actionref_cond = mascot_check_condition(mascot, actionref->condition); // Condition can be on action reference too
    enum mascot_tick_result action_cond = mascot_check_condition(mascot, actionref->action->condition);

    // mascot_check_condition returns mascot_tick_ok in case of success evaluation (result != 0)
    // or if condition is not set (NULL).
    // In case of error it will return mascot_tick_error.
    // In case of condition is not met it will return mascot_tick_next.

    if (actionref_cond == mascot_tick_error || action_cond == mascot_tick_error) {
        return mascot_tick_error;
    }
    if (actionref_cond == mascot_tick_next || action_cond == mascot_tick_next) {
        return mascot_tick_next;
    }

    // Copy pointer to condition to mascot->current_condition
    const struct mascot_expression* cond = actionref->condition ? actionref->condition : actionref->action->condition;
    mascot->current_condition.expression_prototype = (struct mascot_expression*)cond;
    mascot->current_condition.evaluated = cond ? cond->evaluate_once : 0; // Mark it as not evaluated if condition is not ment to be evaluated once

    // Some subactions may be duration limited by their callers.
    // In this case we should execute duration limit expression and set action duration.
    if (actionref->duration_limit) {
        DEBUG("Executing duration limit for action \"%s\"", actionref->action->name);
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
        DEBUG("Duration limit for action \"%s\" is %f", actionref->action->name, vmres);
    }

    // Reset commonly used things like action index, frame index, next frame tick, animation index
    // It best to do that in all non-transient actions.
    mascot->action_index = 0; // Subaction index
    mascot->frame_index = 0; // Current frame index
    mascot->next_frame_tick = 0; // Tick on which next frame should be rendered
    mascot->animation_index = 0; // Current animation index

    // Change mascot state
    mascot->state = mascot_state_stay;

    // Announce affordance of that action
    mascot_announce_affordance(mascot, actionref->action->affordance);

    return mascot_tick_ok;
}

struct mascot_action_next example_action_next(struct mascot* mascot, struct mascot_action_reference *actionref, uint32_t tick)
{

    /*
         This is main function of action.
         It will be called on every tick and should perform all necessary
         steps to render action.
         For example it will check if border conditions is still met,
         check if action conditions is still met, decide when to switch to next frame
         and finally when action should be finished.

          This function should return struct mascot_action_next with the following values:
          - status: One of the following values:
                - mascot_tick_ok: Action is processed successfully and can be processed further
                - mascot_tick_next: Action is ended for whatever reason and should proceed to next action
                - mascot_tick_reenter: Action is not fully processed and should be called again on same tick
                - mascot_tick_error: Action is encountered some error and mascot should be stopped
          - next_action:
            Action reference to action that should be set as current action.
          - next_animation:
            Animation index that should be set as current animation.
          - next_frame:
            Frame index that should be set as current frame.
    */

    struct mascot_action_next result = {0}; // Initialize result

    // Check if duration limit is set and we reached it
    if (mascot->action_duration && tick >= mascot->action_duration) {
        // We reached duration limit, we should proceed to next action
        result.status = mascot_tick_next;
        return result;
    }

    // Check if action is still on ground
    enum mascot_tick_result ground_check = mascot_ground_check(mascot, actionref, example_action_clean);
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

    // Reference implementation for conditioned animation switching
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

    // After calculations, we found no new animation candidates to switch to
    if (!new_animation) {
        result.status = mascot_tick_next;
        return result;
    }

    // Current animation is different from new animation, we should switch
    // to new animation and reset some values
    if (current_animation != new_animation) {
        result.next_animation = new_animation;
        mascot->frame_index = 0;
        mascot->next_frame_tick = tick;
        result.status = mascot_tick_reenter;
        return result;
    }

    // Check if we should switch to next frame
    // If we in loop or action duration is set, after last frame we should switch to first frame
    if (mascot->next_frame_tick <= tick) {
        if (mascot->current_animation) {
            if (mascot->frame_index >= mascot->current_animation->frame_count) {
                if (mascot->action_duration || actionref->action->loop) {
                    mascot->frame_index = 0;
                } else {
                    result.status = mascot_tick_next;
                    return result;
                }
            }
            result.next_pose = mascot->current_animation->frames[mascot->frame_index++];
        }
    }

    // Okayeg
    result.status = mascot_tick_ok;
    return result;
}

enum mascot_tick_result example_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(tick);
    UNUSED(actionref);

    /*
            This is tick function of action.
            It will be called only if action_next returned mascot_tick_ok.
            This function should perform calculation to decide new position of mascot
    */

    // Are we out of bounds?
    enum mascot_tick_result oob_check = mascot_out_of_bounds_check(mascot);
    if (oob_check != mascot_tick_ok) {
        return oob_check;
    }

    // Simpliest movement implementation
    float velx = mascot->VelocityX->value.f;
    float vely = mascot->VelocityY->value.f;
    if (velx != 0.0 || vely != 0.0) {
        int32_t new_x = mascot->X->value.i;
        int32_t new_y = mascot->Y->value.i;
        int32_t x = new_x;
        int32_t y = new_y;
        bool looking_right = mascot->LookingRight->value.i;

        if (velx != 0.0) {
            if (looking_right) {
                new_x += velx;
            } else {
                new_x -= velx;
            }
        }

        if (vely != 0.0) {
            new_y -= vely;
        }

        if (x != new_x || y != new_y) {
            // When we decided that we should move, we call that function, passing subsurface of mascot and new positions
            // Last argument is told environment subsystem that it should notify mascot about new position
            environment_subsurface_move(mascot->subsurface, new_x, new_y, true, true);
        }
    }
    return mascot_tick_ok;
}

void example_action_clean(struct mascot *mascot)
{
    // Clean up function for action
    mascot->animation_index = 0;
    mascot->frame_index = 0;
    mascot->next_frame_tick = 0;
    mascot->action_duration = 0;
    mascot_announce_affordance(mascot, NULL);
}
