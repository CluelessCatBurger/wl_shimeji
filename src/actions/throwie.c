/*
    simple.c - wl_shimeji's simple action implementation

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

#include "throwie.h"
#include "actionbase.h"

enum mascot_tick_result throwie_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    if (!actionref->action->length) {
        WARN("<Mascot:%s:%u> ThrowIE action has no length", mascot->prototype->name, mascot->id);
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

    DEBUG("<Mascot:%s:%u> Initializing simple action \"%s\"", mascot->prototype->name, mascot->id, actionref->action->name);
    DEBUG("<Mascot:%s:%u> Simple actionref info:", mascot->prototype->name, mascot->id);
    DEBUG("<Mascot:%s:%u> - Condition: %p", mascot->prototype->name, mascot->id, actionref->condition);
    DEBUG("<Mascot:%s:%u> - Duration: %p", mascot->prototype->name, mascot->id, actionref->duration_limit);


    enum mascot_tick_result ground_check = mascot_ground_check(mascot, actionref, throwie_action_clean);
    if (ground_check != mascot_tick_ok) {
        return ground_check;
    }

    // Check if action condition requirements are met
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

    struct mascot_local_variable* initialvx = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_INITIALVELX_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_INITIALVELX_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_INITIALVELX_ID];
    struct mascot_local_variable* initialvy = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_INITIALVELY_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_INITIALVELY_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_INITIALVELY_ID];
    struct mascot_local_variable* gravity = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_GRAVITY_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_GRAVITY_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_GRAVITY_ID];

    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_INITIALVELX_ID, initialvx) == mascot_tick_error) return mascot_tick_error;
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_INITIALVELY_ID, initialvy) == mascot_tick_error) return mascot_tick_error;
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_GRAVITY_ID, gravity) == mascot_tick_error) return mascot_tick_error;

    if (!initialvx->used) mascot->InitialVelX->value.f = 32;
    if (!initialvy->used) mascot->InitialVelY->value.f = -10;
    if (!gravity->used) mascot->Gravity->value.f = 0.5;

    // Reset action index, frame and animation index
    mascot->action_index = 0;
    mascot->frame_index = 0;
    mascot->next_frame_tick = 0;
    mascot->animation_index = 0;

    mascot->state = mascot_state_ie_throw;

    bool throw_ie = environment_ie_throw(mascot->environment, mascot->InitialVelX->value.f * (mascot->LookingRight->value.i ? 1 : -1), mascot->InitialVelY->value.f, mascot->Gravity->value.f, tick);

    if (!throw_ie) {
        WARN("<Mascot:%s:%u> Failed to throw mascot in IE", mascot->prototype->name, mascot->id);
        throwie_action_clean(mascot);
        return mascot_tick_next;
    }

    // Announce affordance
    mascot_announce_affordance(mascot, actionref->action->affordance);

    return mascot_tick_ok;
}

struct mascot_action_next throwie_action_next(struct mascot* mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    struct mascot_action_next result = {0};

    if (mascot->action_duration && tick >= mascot->action_duration) {
        result.status = mascot_tick_next;
        return result;
    }

    enum mascot_tick_result ground_check = mascot_ground_check(mascot, actionref, throwie_action_clean);
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

    struct ie_object* ie = environment_get_ie(mascot->environment);
    if (ie->state == IE_STATE_MOVED) {
        INFO("<Mascot:%s:%u> IE is moved", mascot->prototype->name, mascot->id);
        throwie_action_clean(mascot);
        result.status = mascot_tick_next;
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

    result.status = mascot_tick_ok;
    return result;
}

enum mascot_tick_result throwie_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(tick);
    UNUSED(actionref);

    enum mascot_tick_result oob_check = mascot_out_of_bounds_check(mascot);
    if (oob_check != mascot_tick_ok) {
        return oob_check;
    }

    float velx = mascot->VelocityX->value.f;
    float vely = mascot->VelocityY->value.f;
    bool looking_right = mascot->LookingRight->value.i;
    if (velx != 0.0 || vely != 0.0) {
        int32_t new_x = mascot->X->value.i;
        int32_t new_y = mascot->Y->value.i;
        int32_t x = new_x;
        int32_t y = new_y;

        if (velx != 0.0) {
            // new_x += velx;
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
            environment_subsurface_move(mascot->subsurface, new_x, new_y, true);
        }
    }
    return mascot_tick_ok;
}

void throwie_action_clean(struct mascot *mascot)
{
    mascot->animation_index = 0;
    mascot->frame_index = 0;
    mascot->next_frame_tick = 0;
    mascot->action_duration = 0;
    mascot->InitialVelX->value.f = 0;
    mascot->InitialVelY->value.f = 0;
    mascot->Gravity->value.f = 0;
    environment_ie_stop_movement(mascot->environment);
    mascot_announce_affordance(mascot, NULL);
}
