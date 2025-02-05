/*
    dragging.c - wl_shimeji's drag action implementation

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

#include "dragging.h"
#include "environment.h"

struct dragging_aux_data {
    int32_t prev_x;
    int32_t prev_y;
};

enum mascot_tick_result dragging_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(tick);
    if (!actionref->action->length) {
        WARN("<Mascot:%s:%u> Dragging action has no length", mascot->prototype->name, mascot->id);
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

    mascot->dragged_tick = tick;

    mascot->state = mascot_state_drag;

    mascot_announce_affordance(mascot, NULL);

    free(mascot->action_data);
    mascot->action_data = calloc(1, sizeof(struct dragging_aux_data));
    struct dragging_aux_data* aux_data = (struct dragging_aux_data*)mascot->action_data;
    aux_data->prev_x = mascot->X->value.i;
    aux_data->prev_y = mascot->Y->value.i;
    return mascot_tick_ok;
}

struct mascot_action_next dragging_action_next(struct mascot* mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    struct mascot_action_next result = {0};

    const struct mascot_animation* current_animation = mascot->current_animation;
    const struct mascot_animation* new_animation = NULL;

    for (uint16_t i = 0; i < actionref->action->length; i++) {
        if (actionref->action->content[i].kind != mascot_action_content_type_animation) {
            LOG("ERROR", RED, "<Mascot:%s:%u> Action content is not an animation", mascot->prototype->name, mascot->id);
            result.status = mascot_tick_error;
            return result;
        }

        enum mascot_tick_result animcond = mascot_check_condition(mascot, actionref->action->content[i].value.animation->condition);
        if (animcond == mascot_tick_next) {
            continue;
        }
        if (animcond == mascot_tick_error) {
            result.status = mascot_tick_error;
            return result;
        }

        new_animation = actionref->action->content[i].value.animation;
        mascot->animation_index = i;
        break;
    }
    if (new_animation != mascot->current_animation) {
        result.status = mascot_tick_reenter;
        result.next_animation = new_animation;
        mascot->frame_index = 0;
        mascot->next_frame_tick = tick;
        return result;
    }

    if (mascot->next_frame_tick <= tick) {
        if (current_animation) {
            if (mascot->frame_index >= current_animation->frame_count) {
                mascot->frame_index = 0;
            }
            result.next_pose = current_animation->frames[mascot->frame_index++];
        }
    }

    if (tick - mascot->dragged_tick >= 250) {
        if ((float)rand() / (float)RAND_MAX > 0.1) {
            result.status = mascot_tick_next;
            return result;
        }
    }

    result.next_action = *actionref;
    result.status = mascot_tick_ok;
    return result;
}

enum mascot_tick_result dragging_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick)
{
    UNUSED(actionref);

    struct dragging_aux_data* aux_data = (struct dragging_aux_data*)mascot->action_data;

    int posx = aux_data->prev_x;
    int posy = aux_data->prev_y;

    aux_data->prev_x = mascot->X->value.i;
    aux_data->prev_y = mascot->Y->value.i;

    environment_pointer_update_delta(mascot->subsurface, tick);

    if (abs(mascot->X->value.i - posx) >= 5 || abs(mascot->Y->value.i - posy) >= 5) {
        mascot->dragged_tick = tick;
    }

    mascot->LookingRight->value.i = 0;
    int new_x = mascot->X->value.i;
    int foot_x = mascot->FootX->value.i;
    int foot_dx = mascot->FootDX->value.i;

    mascot->FootDX->value.i = (foot_dx + (new_x - foot_x) * 0.1) * 0.8;
    mascot->FootX->value.i = foot_x + (mascot->FootDX->value.i);

    return mascot_tick_ok;
}

void dragging_action_clean(struct mascot *mascot)
{
    mascot->animation_index = 0;
    mascot->frame_index = 0;
    mascot->next_frame_tick = 0;
    mascot->action_duration = 0;
    free(mascot->action_data);
    mascot->action_data = NULL;
}
