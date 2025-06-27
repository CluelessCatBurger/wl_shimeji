#include "walkwithie.h"
#include "environment.h"
#include "mascot.h"
#include "move.h"
#include "config.h"

enum mascot_tick_result walkwithie_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{

    if (!config_get_ie_throwing()) {
        DEBUG("<Mascot:%s:%u> IE throwing is disabled, skipping action", mascot->prototype->name, mascot->id);
        mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
        return mascot_tick_reenter;
    }

    if (!actionref->action->length) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Move action has no length", mascot->prototype->name, mascot->id);
        return mascot_tick_error;
    }

    // if (!ie->active) {
    //     DEBUG("<Mascot:%s:%u> IE object is inactive, skipping action", mascot->prototype->name, mascot->id);
    //     mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
    //     return mascot_tick_reenter;
    // }

    // if (!environment_ie_allows_move(mascot->environment)) {
    //     DEBUG("<Mascot:%s:%u> IE object does not allow movement, skipping action", mascot->prototype->name, mascot->id);
    //     mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
    //     return mascot_tick_reenter;
    // }

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
    mascot->VelocityX->value.i = 0;
    mascot->VelocityY->value.i = 0;

    // Execute variables variables TargetX, TargetY, IEOffsetX, IEOffsetY
    struct mascot_local_variable* target_x = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_TARGETX_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_TARGETX_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_TARGETX_ID];
    struct mascot_local_variable* target_y = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_TARGETY_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_TARGETY_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_TARGETY_ID];
    struct mascot_local_variable* ie_offt_x = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_IEOFFSETX_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_IEOFFSETX_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_IEOFFSETX_ID];
    struct mascot_local_variable* ie_offt_y = actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_IEOFFSETY_ID]->used ? actionref->overwritten_locals[MASCOT_LOCAL_VARIABLE_IEOFFSETY_ID] : actionref->action->variables[MASCOT_LOCAL_VARIABLE_IEOFFSETY_ID];

    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_TARGETX_ID, target_x) == mascot_tick_error) {
        return mascot_tick_error;
    }
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_TARGETY_ID, target_y) == mascot_tick_error) {
        return mascot_tick_error;
    }
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_IEOFFSETX_ID, ie_offt_x) == mascot_tick_error) {
        return mascot_tick_error;
    }
    if (mascot_assign_variable(mascot, MASCOT_LOCAL_VARIABLE_IEOFFSETY_ID, ie_offt_y) == mascot_tick_error) {
        return mascot_tick_error;
    }

    if (!target_x->used) mascot->TargetX->value.i = -1;
    if (!target_y->used) mascot->TargetY->value.i = -1;

    if (mascot->TargetX->value.i != -1) {
        if (mascot->TargetX->value.i < 0) {
            mascot->TargetX->value.i = 0;
        } else if (mascot->TargetX->value.i > (int32_t)environment_workarea_width(mascot->environment)) {
            mascot->TargetX->value.i = environment_workarea_width(mascot->environment);
        }
    }

    if (mascot->TargetY->value.i != -1) {
        if (mascot->TargetY->value.i < 0) {
            mascot->TargetY->value.i = 0;
        } else if (mascot->TargetY->value.i > (int32_t)environment_workarea_height(mascot->environment)) {
            mascot->TargetY->value.i = (int32_t)environment_workarea_height(mascot->environment);
        }
    }

    mascot->state = mascot_state_ie_walk;

    mascot_announce_affordance(mascot, actionref->action->affordance);

    return mascot_tick_ok;
}

struct mascot_action_next walkwithie_action_next(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
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
            DEBUG("<Mascot:%s:%u> Move action next, Border type not met", mascot->prototype->name, mascot->id);
            result.status = mascot_tick_next;
            return result;
        }
    }

    // if (!ie->active) {
    //     INFO("<Mascot:%s:%u> IE is no longer active", mascot->prototype->name, mascot->id);
    //     mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
    //     walkwithie_action_clean(mascot);
    //     result.status = mascot_tick_next;
    //     return result;
    // }
    // if (ie->state == IE_STATE_MOVED) {
    //     INFO("<Mascot:%s:%u> IE is moved", mascot->prototype->name, mascot->id);
    //     mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
    //     walkwithie_action_clean(mascot);
    //     result.status = mascot_tick_next;
    //     return result;
    // }

    // int32_t mascot_x = mascot->X->value.i;
    // int32_t mascot_y = mascot_screen_y_to_mascot_y(mascot, mascot->Y->value.i);
    // int32_t ie_offt_x = mascot_get_variable_i(mascot, MASCOT_LOCAL_VARIABLE_IEOFFSETX_ID) / environment_screen_scale(mascot->environment);
    // int32_t ie_offt_y = mascot_get_variable_i(mascot, MASCOT_LOCAL_VARIABLE_IEOFFSETY_ID) / environment_screen_scale(mascot->environment);

    // int32_t ie_corner_x = mascot->LookingRight->value.i ? ie->x : ie->x + ie->width;
    // int32_t distance_x = abs((mascot_x + (mascot->LookingRight->value.i ? ie_offt_x : -ie_offt_x)) - ie_corner_x);
    // int32_t distance_y = abs((mascot_y + ie_offt_y) - (ie->y + ie->height));

    // if (distance_x > 50 || distance_y > 50) {
    //     WARN("<Mascot:%s:%u> IE is too far away, values: %d %d, %d %d, %d %d, %d", mascot->prototype->name, mascot->id, mascot_x, mascot_y, ie_corner_x, ie->y + ie->height, distance_x, distance_y, mascot->LookingRight->value.i);
    //     mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
    //     walkwithie_action_clean(mascot);
    //     result.status = mascot_tick_next;
    //     return result;
    // }

    // DEBUG("<Mascot:%s:%u> WalkWithIE action next, TargetX, TargetY: %d, %d", mascot->prototype->name, mascot->id, mascot->TargetX->value.i, mascot->TargetY->value.i);
    // DEBUG("<Mascot:%s:%u> WalkWithIE action next, X, Y: %d, %d", mascot->prototype->name, mascot->id, mascot->X->value.i, mascot->Y->value.i);
    // // Check if target is reached
    // if ((mascot_x == mascot->TargetX->value.i || mascot->TargetX->value.i == -1) &&
    //     (mascot_y == mascot->TargetY->value.i || mascot->TargetY->value.i == -1)) {
    //     result.status = mascot_tick_next;
    //     return result;
    // }

    // // Ensure conditions are still met
    // enum mascot_tick_result actionref_cond = mascot_recheck_condition(mascot, actionref->condition);
    // enum mascot_tick_result action_cond = mascot_recheck_condition(mascot, actionref->action->condition);

    // if (action_cond != mascot_tick_ok)
    // {
    //     result.status = action_cond;
    //     return result;
    // }

    // if (actionref_cond != mascot_tick_ok)
    // {
    //     result.status = actionref_cond;
    //     return result;
    // }

    // const struct mascot_animation* current_animation = mascot->current_animation;
    // const struct mascot_animation* new_animation = NULL;

    // for (uint16_t i = 0; i < actionref->action->length; i++) {
    //     if (actionref->action->content[i].kind != mascot_action_content_type_animation) {
    //         LOG("ERROR", RED, "<Mascot:%s:%u> Simple action content is not an animation", mascot->prototype->name, mascot->id);
    //         result.status = mascot_tick_error;
    //         return result;
    //     }
    //     new_animation = actionref->action->content[i].value.animation;
    //     enum mascot_tick_result animcond = mascot_check_condition(mascot, new_animation->condition);
    //     if (animcond != mascot_tick_ok) {
    //         if (animcond == mascot_tick_error) {
    //             result.status = mascot_tick_error;
    //             return result;
    //         }
    //         new_animation = NULL;
    //         continue;
    //     }
    //     mascot->animation_index = i;
    //     break;
    // }

    // if (!new_animation) {
    //     result.status = mascot_tick_next;
    //     return result;
    // }

    // if (current_animation != new_animation) {
    //     result.next_animation = new_animation;
    //     mascot->frame_index = 0;
    //     mascot->next_frame_tick = tick;
    //     result.status = mascot_tick_reenter;
    //     return result;
    // }

    // if (mascot->next_frame_tick <= tick) {
    //     if (mascot->current_animation) {
    //         if (mascot->frame_index >= mascot->current_animation->frame_count) {
    //             mascot->frame_index = 0;
    //         }
    //         result.next_pose = mascot->current_animation->frames[mascot->frame_index++];
    //     }
    // }

    // result.status = mascot_tick_ok;
    return result;
}

enum mascot_tick_result walkwithie_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(actionref);
    UNUSED(tick);

    DEBUG("<Mascot:%s:%u> Move action tick, TargetX, TargetY: %d, %d", mascot->prototype->name, mascot->id, mascot->TargetX->value.i, mascot->TargetY->value.i);

    enum mascot_tick_result oob_check = mascot_out_of_bounds_check(mascot);
    if (oob_check != mascot_tick_ok) {
        return oob_check;
    }

    int32_t target_x = mascot->TargetX->value.i;
    int32_t target_y = mascot_screen_y_to_mascot_y(mascot, mascot->TargetY->value.i);
    int32_t posx = mascot->X->value.i;
    int32_t posy = mascot->Y->value.i;

    // int32_t ie_offt_y = mascot_get_variable_i(mascot, MASCOT_LOCAL_VARIABLE_IEOFFSETY_ID) / environment_screen_scale(mascot->environment);
    // int32_t ie_offt_x = mascot_get_variable_i(mascot, MASCOT_LOCAL_VARIABLE_IEOFFSETX_ID) / environment_screen_scale(mascot->environment);

    float velocity_x = mascot->VelocityX->value.f;
    float velocity_y = mascot->VelocityY->value.f;

    bool looking_right = mascot->LookingRight->value.i;

    if (target_x != INT32_MAX && target_x != -1) {
        if (posx < target_x) looking_right = true;
        else if (posx > target_x) looking_right = false;
        if (target_x < 0) {
            target_x = 0;
        }
        else if (target_x > (int32_t)environment_workarea_width(mascot->environment)) {
            target_x = (int32_t)environment_workarea_width(mascot->environment);
        }

        if (fabs(velocity_x) >= abs(posx - target_x)) {
            posx = target_x;
        } else {
            if (looking_right) posx -= velocity_x;
            else posx += velocity_x;
        }

    }
    if (target_y != INT32_MAX && target_y != -1) {
        bool down = posy > target_y;
        DEBUG("<Mascot:%s:%u> WalkWithIE action tick, posy, target_y, down: %d, %d, %d", mascot->prototype->name, mascot->id, posy, target_y, down);
        DEBUG("<Mascot:%s:%u> WalkWithIE action tick, velocity = %f,%f", mascot->prototype->name, mascot->id, velocity_x, velocity_y);
        if (target_y < 0) {
            mascot->TargetY->value.i = target_y = 0;
        }
        else if (target_y > (int32_t)environment_workarea_height(mascot->environment)) {
            mascot->TargetY->value.i = target_y = (int32_t)environment_workarea_height(mascot->environment);
        }

        if (fabs(velocity_y) >= abs(posy - target_y)) {
            posy = target_y;
        } else {
            // if (down) posy -= velocity_y;
            // else posy += velocity_y;
            posy -= velocity_y;
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
        DEBUG("<Mascot:%s:%u> Moving towards target, current pos (%d,%d), setting pos (%d,%d)", mascot->prototype->name, mascot->id, mascot->X->value.i, mascot->Y->value.i, posx, posy);
        enum environment_move_result move_result = environment_subsurface_move(mascot->subsurface, posx, posy, true, true);
        if (move_result == environment_move_clamped) {
            mascot->TargetX->value.i = -1;
            mascot->TargetY->value.i = -1;
            return mascot_tick_reenter;
        }
    }
    // environment_ie_t* ie = mascot_get_active_ie(mascot);
    // if (ie) {
    //     if (ie->active) {
    //         enum environment_move_result move_res = environment_move_ok;
    //         if (looking_right) {
    //             move_res = environment_ie_move(mascot->environment, posx - ie_offt_x, mascot_screen_y_to_mascot_y(mascot, posy) + ie_offt_y - ie->height);
    //         } else {
    //             move_res = environment_ie_move(mascot->environment, posx + ie_offt_x - ie->width, mascot_screen_y_to_mascot_y(mascot, posy) + ie_offt_y - ie->height);
    //         }
    //         if (move_res == environment_move_invalid) {
    //             mascot_set_behavior(mascot, mascot_fall_behavior(mascot));
    //             return mascot_tick_reenter;
    //         }
    //     }
    // }
    return mascot_tick_ok;
}

void walkwithie_action_clean(struct mascot *mascot)
{
    move_action_clean(mascot);
}
