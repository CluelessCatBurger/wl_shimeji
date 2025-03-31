/*
    mascot.c - wl_shimeji's mascot processing routines

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

#include "mascot.h"
#include "environment.h"
#include "expressions.h"
#include "mascot_config_parser.h"
#include "config.h"
#include "physics.h"
#include "plugins.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef PLUGINSUPPORT_IMPLEMENTATION

#include "actions/actions.h"
#include "actions/actionbase.h"

#include "protocol/server.h"

uint32_t mascot_total_count = 0;
uint32_t new_mascot_id = 0;

static void mascot_init_(struct mascot* mascot, const struct mascot_prototype* prototype, bool save_vars);
void mascot_build_behavior_pool(struct mascot* mascot, const struct mascot_behavior *behavior, bool add);

const struct mascot_behavior* mascot_prototype_behavior_by_name(const struct mascot_prototype* prototype, const char* name)
{
    if (!prototype) ERROR("MascotPrototypeBehaviorByName: prototype is NULL");
    if (!name) return NULL;
    for (uint16_t i = 0; i < prototype->behavior_count; i++) {
        if (strcmp(prototype->behavior_definitions[i]->name, name) == 0
            && strlen(prototype->behavior_definitions[i]->name) == strlen(name)) {
            return prototype->behavior_definitions[i];
        }
    }
    return NULL;
}

struct mascot* mascot_get_target_by_affordance(struct mascot* mascot, const char* affordance)
{
    if (!affordance) return NULL;
    if (!config_get_per_mascot_interactions()) return NULL;
    if (!mascot->affordance_manager) return NULL;
    pthread_mutex_lock(&mascot->affordance_manager->mutex);
    if (!mascot->affordance_manager->occupied_slots_count) {
        pthread_mutex_unlock(&mascot->affordance_manager->mutex);
        return NULL;
    }
    struct mascot* candidate = NULL;
    float score = 0.0;
    for (uint32_t i = 0; i < mascot->affordance_manager->slot_count; i++) {
        if (mascot->affordance_manager->slot_state[i] && mascot->affordance_manager->slots[i]) {
            if (!mascot->affordance_manager->slots[i]->current_affordance) continue;
            if (strcmp(mascot->affordance_manager->slots[i]->current_affordance, affordance) == 0
                && strlen(mascot->affordance_manager->slots[i]->current_affordance) == strlen(affordance)
            ) {
                struct mascot* candidate_ = mascot->affordance_manager->slots[i];
                if (mascot->environment != candidate_->environment && !config_get_unified_outputs()) continue;
                float new_score = drand48();
                if (new_score > score) {
                    candidate = candidate_;
                    score = new_score;
                }
            }
        }
    }
    INFO("<Mascot:%s:%u> Target by affordance %s: %s:%u, score %f", mascot->prototype->name, mascot->id, affordance, candidate ? candidate->prototype->name : "(nil)", candidate ? candidate->id : 0, score);
    pthread_mutex_unlock(&mascot->affordance_manager->mutex);
    return candidate;
}

void mascot_announce_affordance(struct mascot* mascot, const char* affordance)
{
    if (!mascot) return;
    if (!mascot->affordance_manager) return;
    pthread_mutex_lock(&mascot->affordance_manager->mutex);
    mascot->current_affordance = affordance;
    DEBUG("<Mascot:%s:%u> Announcing affordance: %s", mascot->prototype->name, mascot->id, affordance ? affordance : "(nil)");
    if (affordance) {
        if (mascot->affordance_manager->occupied_slots_count == mascot->affordance_manager->slot_count) {
            pthread_mutex_unlock(&mascot->affordance_manager->mutex);
            return;
        }
        for (uint32_t i = 0; i < mascot->affordance_manager->slot_count; i++) {
            if (!mascot->affordance_manager->slot_state[i]) {
                mascot->affordance_manager->slots[i] = mascot;
                mascot->affordance_manager->slot_state[i] = true;
                mascot->affordance_manager->occupied_slots_count++;
                INFO("<Mascot:%s:%u> Affordance %s announced, slot %u", mascot->prototype->name, mascot->id, affordance, i);
                break;
            }
        }
    } else {
        for (uint32_t i = 0; i < mascot->affordance_manager->slot_count; i++) {
            if (mascot->affordance_manager->slots[i] == mascot) {
                mascot->affordance_manager->slots[i] = NULL;
                mascot->affordance_manager->slot_state[i] = false;
                mascot->affordance_manager->occupied_slots_count--;
                INFO("<Mascot:%s:%u> Affordance %s unannounced, slot %u", mascot->prototype->name, mascot->id, affordance, i);
                break;
            }
        }
    }

    pthread_mutex_unlock(&mascot->affordance_manager->mutex);
}

bool mascot_interact(struct mascot* mascot, struct mascot* target, const char* affordance, const char* my_behavior, const char* your_behavior) {
    if (!mascot) ERROR("MascotInteract: mascot is NULL");

    mascot_link(target);

    if (!target) {
        WARN("<Mascot:%s:%u> Interact: target is NULL", mascot->prototype->name, mascot->id);
        mascot_unlink(mascot);
        return false;
    }
    if (!affordance) {
        WARN("<Mascot:%s:%u> Interact: affordance is NULL", mascot->prototype->name, mascot->id);
        mascot_unlink(mascot);
        return false;
    }
    if (!my_behavior || !your_behavior) {
        WARN("<Mascot:%s:%u> Interact: one of behaviors is NULL", mascot->prototype->name, mascot->id);
        mascot_unlink(mascot);
        return false;
    }

    const struct mascot_behavior* my_behavior_ptr = mascot_prototype_behavior_by_name(mascot->prototype, my_behavior);
    if (!my_behavior_ptr) {
        WARN("<Mascot:%s:%u> Interact: my_behavior %s not found", mascot->prototype->name, mascot->id, my_behavior);
        mascot_unlink(mascot);
        return false;
    }
    const struct mascot_behavior* your_behavior_ptr = mascot_prototype_behavior_by_name(target->prototype, your_behavior);
    if (!your_behavior_ptr) {
        WARN("<Mascot:%s:%u> Interact: your_behavior %s not found", mascot->prototype->name, mascot->id, your_behavior);
        mascot_unlink(mascot);
        return false;
    }

    pthread_mutex_lock(&target->tick_lock);
    mascot_announce_affordance(target, NULL);
    target->X->value = mascot->X->value;
    target->Y->value = mascot->Y->value;

    DEBUG("<Mascot:%s:%u> Interact: I: %s, You: %s", mascot->prototype->name, mascot->id, my_behavior, your_behavior);
    DEBUG("<Mascot:%s:%u> Interact: My looking right: %d, Your looking right: %d", mascot->prototype->name, mascot->id, mascot->LookingRight->value.i, target->LookingRight->value.i);

    // Do it before mascot_set_behavior, as it clears mascot->current_action.action
    if (mascot->current_action.action->target_look && mascot->LookingRight->value.i == target->LookingRight->value.i) {
        target->LookingRight->value.i = !mascot->LookingRight->value.i;
    }

    mascot_set_behavior(mascot, my_behavior_ptr);
    mascot_set_behavior(target, your_behavior_ptr);

    DEBUG("<Mascot:%s:%u> Interact: My new looking right: %d, Your new looking right: %d", mascot->prototype->name, mascot->id, mascot->LookingRight->value.i, target->LookingRight->value.i);

    pthread_mutex_unlock(&target->tick_lock);
    mascot_unlink(target);
    return true;
}

void mascot_attach_pose(struct mascot* mascot, const struct mascot_pose* pose, uint32_t tick)
{
    if (!mascot) return;
    if (!pose) {
        DEBUG("<Mascot:%s:u> Detaching pose", mascot->prototype->name, mascot->id);
        environment_subsurface_unmap(mascot->subsurface);
        return;
    }
    environment_subsurface_attach(mascot->subsurface, pose);
    DEBUG("<Mascot:%s:%u> Attaching pose %d, with velocity = (%d,%d), anchor = (%d,%d)", mascot->prototype->name, mascot->id, mascot->frame_index ? mascot->frame_index -1 : 0, pose->velocity_x, pose->velocity_y, pose->anchor_x, pose->anchor_y);
    mascot->next_frame_tick = tick + pose->duration;
    if (mascot->state != mascot_state_fall && mascot->state != mascot_state_jump && mascot->state != mascot_state_ie_fall) {
        mascot->VelocityX->value.f = pose->velocity_x;
        mascot->VelocityY->value.f = pose->velocity_y;
    }
}

void mascot_reattach_pose(struct mascot* mascot) {
    if (!mascot->current_animation) return;
    if (mascot->frame_index == 0) return;
    const struct mascot_pose* pose = mascot->current_animation->frames[mascot->frame_index-1];
    if (!pose) return;
    environment_subsurface_attach(mascot->subsurface, pose);
}

struct action_funcs {
    mascot_action_init init;
    mascot_action_tick tick;
    mascot_action_next next;
    mascot_action_clean clean;
};

struct action_funcs null_funcs = {0};

struct action_funcs state_funcs[mascot_action_type_count] = {
    {simple_action_init, simple_action_tick, simple_action_next, simple_action_clean},
    {stay_action_init, stay_action_tick, stay_action_next, simple_action_clean},
    {move_action_init, move_action_tick, move_action_next, move_action_clean},
    {0},
    {sequence_action_init, sequence_action_tick, sequence_action_next, sequence_action_clean},
    {selector_action_init, selector_action_tick, selector_action_next, selector_action_clean},
};

struct action_funcs embedded_funcs[mascot_embedded_property_count] = {
    {0}, // None
    {look_action_init, NULL, NULL, look_action_clean}, // look
    {offset_action_init, NULL, NULL, offset_action_clean}, // offset
    {jump_action_init, jump_action_tick, jump_action_next, jump_action_clean}, // jump
    {fall_action_init, fall_action_tick, fall_action_next, fall_action_clean}, // fall
    {dragging_action_init, dragging_action_tick, dragging_action_next, dragging_action_clean}, // dragging
    {resist_action_init, resist_action_tick, resist_action_next, resist_action_clean}, // resist
    {breed_action_init, breed_action_tick, breed_action_next, breed_action_clean}, // breed
    {0},
    {scanmove_action_init, scanmove_action_tick, scanmove_action_next, scanmove_action_clean},
    {scanjump_action_init, scanjump_action_tick, scanjump_action_next, scanjump_action_clean},
    {interact_action_init, simple_action_tick, simple_action_next, interact_action_clean},
    {dispose_action_init, simple_action_tick, dispose_action_next, simple_action_clean},
    {transform_action_init, simple_action_tick, transform_action_next, transform_action_clean},

    {0},

    {walkwithie_action_init, walkwithie_action_tick, walkwithie_action_next, walkwithie_action_clean},
    {fallwithie_action_init, fallwithie_action_tick, fallwithie_action_next, fallwithie_action_clean},
    {throwie_action_init, throwie_action_tick, throwie_action_next, throwie_action_clean},


    {0},
    {0},
};

struct action_funcs* mascot_get_handlers(struct mascot* mascot) {
    if (!mascot->current_action.action) {
        return &null_funcs;
    }
    if (mascot->current_action.action->type == mascot_action_type_embedded) {
        return &embedded_funcs[mascot->current_action.action->embedded_type];
    }
    return &state_funcs[mascot->current_action.action->type];
}

enum action_set_result mascot_set_action_internal(struct mascot* mascot, struct mascot_action_reference* actionref, bool push_stack, uint32_t tick, bool clear_stack)
{
    if (!actionref->action) return ACTION_SET_PARAMS_INVALID;

    DEBUG("<Mascot:%s:%u> Setting action %s, push stack %d, clear stack %d", mascot->prototype->name, mascot->id, actionref->action->name, push_stack, clear_stack);
    if (mascot->current_action.action) {
        DEBUG("<Mascot:%s:%u> Current action %s", mascot->prototype->name, mascot->id, mascot->current_action.action->name);
    }

    mascot_action_init init = NULL;
    if (actionref->action->type == mascot_action_type_embedded) {
        init = embedded_funcs[actionref->action->embedded_type].init;
    } else {
        init = state_funcs[actionref->action->type].init;
    }

    if (!init) return ACTION_SET_CONDITION_NOT_MET;

    uint16_t action_index = mascot->action_index;
    struct mascot_action_reference old_action = mascot->current_action;
    mascot->current_action = *actionref;

    mascot_action_clean clean = NULL;
    if (old_action.action) {
        if (old_action.action->type == mascot_action_type_embedded) {
            clean = embedded_funcs[old_action.action->embedded_type].clean;
        } else {
            clean = state_funcs[old_action.action->type].clean;
        }
    }
    if (clean) clean(mascot);

    if (push_stack) {
        if (clear_stack) {
            mascot->as_p = 0;
        }
        if (mascot->as_p == 128) return ACTION_SET_ACTION_STACK_OVERFLOW;
        DEBUG("<Mascot:%s:%u> Pushing action %s to stack", mascot->prototype->name, mascot->id, old_action.action->name);
        DEBUG("<Mascot:%s:%u> Old stack pointer %d", mascot->prototype->name, mascot->id, mascot->as_p);
        DEBUG("<Mascot:%s:%u> Action index %d", mascot->prototype->name, mascot->id, action_index);
        mascot->action_stack[mascot->as_p] = old_action;
        mascot->action_index_stack[mascot->as_p++] = action_index;
    }

    enum mascot_tick_result res = init(mascot, actionref, tick);

    if (res == mascot_tick_error) return ACTION_SET_ERROR;
    if (res == mascot_tick_reenter) return ACTION_SET_ACTION_REENTER;
    if (res == mascot_tick_next) return ACTION_SET_ACTION_NEXT;

    mascot->action_tick = tick;

    return ACTION_SET_RESULT_OK;
}

enum action_set_result mascot_set_action(struct mascot* mascot, struct mascot_action_reference* actionref, bool push_stack, uint32_t tick)
{
    if (!mascot) return ACTION_SET_PARAMS_INVALID;
    if (!actionref) return ACTION_SET_PARAMS_INVALID;

    DEBUG("<Mascot:%s:%u> Setting action %s", mascot->prototype->name, mascot->id, actionref->action->name);

    enum action_set_result res = mascot_set_action_internal(mascot, actionref, push_stack, tick, true);

    if (res != ACTION_SET_RESULT_OK) return res;

    return ACTION_SET_RESULT_OK;
}

enum action_set_result mascot_pop_action_stack(struct mascot* mascot, uint32_t tick)
{
    if (!mascot) return ACTION_SET_PARAMS_INVALID;
    if (mascot->as_p == 0) return ACTION_SET_ACTION_STACK_EMPTY;

    DEBUG("<Mascot:%s:%u> Popping action stack", mascot->prototype->name, mascot->id);

    struct mascot_action_reference actionref = mascot->action_stack[--mascot->as_p];
    enum action_set_result res = mascot_set_action_internal(mascot, &actionref, false, tick, false);
    mascot->action_index = mascot->action_index_stack[mascot->as_p];
    return res;
}

// Selects behavior from pool based on frequency
const struct mascot_behavior* select_behavior_from_pool(struct mascot* mascot, const struct mascot_behavior_reference* pool, uint16_t pool_len)
{
    int64_t total_frequency = 0;
    for (uint16_t i = 0; i < pool_len; i++) {
        if (!pool[i].frequency) continue;
        if (mascot_check_condition(mascot, pool[i].condition) != mascot_tick_ok) continue;
        total_frequency += pool[i].frequency;
    }
    int64_t random = drand48() * (double)total_frequency;
    for (int16_t i = 0; i < pool_len; i++)
    {
        if (!pool[i].frequency) continue;
        if (mascot_check_condition(mascot, pool[i].condition) != mascot_tick_ok) continue;
        random -= (int64_t)pool[i].frequency;
        if (random <= 0) return pool[i].behavior;
    }
    return NULL;
}

static enum mascot_tick_result mascot_action_get_next(struct mascot* mascot, uint32_t tick)
{
    struct mascot_action_reference actionref = mascot->current_action;

    if (!actionref.action) {
        if (!mascot->current_behavior) {
            WARN("<Mascot:%s:%u> Action iterator called while behavior is NULL", mascot->prototype->name, mascot->id);
            return mascot_tick_error;
        }
        actionref.action = (struct mascot_action*)mascot->current_behavior->action;

        enum action_set_result action_set = mascot_set_action(mascot, &actionref, false, tick);
        if (action_set != ACTION_SET_RESULT_OK) {
            WARN("<Mascot:%s:%u> Failed to set action \"%s\" from behavior \"%s\"", mascot->prototype->name, mascot->id, actionref.action->name, mascot->current_behavior->name);
            return mascot_tick_error;
        }
    }

    if (!actionref.action) {
        WARN("<Mascot:%s:%u> Behavior \"%s\" has no corresponding action", mascot->prototype->name, mascot->id, mascot->current_behavior->name);
        return mascot_tick_error;
    }

    for (int x = 0; x < 16; x++) {
        struct mascot_action_reference actionref = mascot->current_action;

        // Get appropriate next handler
        mascot_action_next next_func = NULL;
        if (actionref.action) {
            if (actionref.action->type == mascot_action_type_embedded) {
                next_func = embedded_funcs[actionref.action->embedded_type].next;
            } else {
                next_func = state_funcs[actionref.action->type].next;
            }
        } else {
            return mascot_action_get_next(mascot, tick);
        }

        if (!next_func) {
            // WARN("<Mascot:%s:%u> Action \"%s\" has no next handler", mascot->prototype->name, mascot->id, actionref.action->name);
            return mascot_tick_next;
        }

        struct mascot_action_next result = next_func(mascot, &actionref, tick);
        const struct mascot_animation* animation = NULL;
        const struct mascot_pose* pose = NULL;

        actionref = result.next_action;
        animation = result.next_animation;
        pose = result.next_pose;

        switch (result.status) {
            case mascot_tick_clone:
                return mascot_tick_clone;
            case mascot_tick_next:
                if (mascot->as_p) {
                    enum action_set_result res = mascot_pop_action_stack(mascot, tick);
                    if (res != ACTION_SET_RESULT_OK && res != ACTION_SET_CONDITION_NOT_MET && res != ACTION_SET_BORDER_INVALID && res != ACTION_SET_ACTION_NEXT && res != ACTION_SET_ACTION_REENTER && res != ACTION_SET_ACTION_STACK_EMPTY) return mascot_tick_error;
                    if (res == ACTION_SET_CONDITION_NOT_MET || res == ACTION_SET_BORDER_INVALID) return mascot_tick_next;
                    if (res == ACTION_SET_ACTION_STACK_OVERFLOW) return mascot_tick_next;
                    continue;
                }
                mascot->current_action = (struct mascot_action_reference){0};
                mascot->current_condition = (struct mascot_expression_value){0};
                mascot->current_animation = NULL;
                return mascot_tick_next;
            case mascot_tick_escape:
                mascot->as_p = 0;
                resist_action_clean(mascot);
                return mascot_tick_escape;
            case mascot_tick_reenter:
            case mascot_tick_ok:
                break;
            case mascot_tick_error:
                return mascot_tick_error;
            default:
                return result.status;
        }
        if (animation) {
            if (animation != mascot->current_animation) {
                mascot->current_animation = animation;
                mascot->animation_index = 0;
                mascot->frame_index = 0;
            }
        }
        if (pose) {
            mascot_attach_pose(mascot, pose, tick);
        }

        bool nested = false;
        if (actionref.action && mascot->current_action.action != actionref.action) {
            nested = mascot->current_action.action ? mascot->current_action.action->type == mascot_action_type_sequence: false;
            enum action_set_result set_result = mascot_set_action_internal(
                mascot,
                &actionref,
                nested,
                tick, false);

            if (set_result != ACTION_SET_RESULT_OK) {
                if (mascot->as_p) {
                    enum action_set_result res = mascot_pop_action_stack(mascot, tick);
                    if (res != ACTION_SET_RESULT_OK && res != ACTION_SET_CONDITION_NOT_MET && res != ACTION_SET_BORDER_INVALID && res != ACTION_SET_ACTION_NEXT && res != ACTION_SET_ACTION_REENTER && res != ACTION_SET_ACTION_STACK_EMPTY) return mascot_tick_error;
                    if (res == ACTION_SET_CONDITION_NOT_MET || res == ACTION_SET_BORDER_INVALID) return mascot_tick_next;
                    if (res == ACTION_SET_ACTION_STACK_OVERFLOW) return mascot_tick_next;
                    continue;
                }
            }

            if (set_result == ACTION_SET_ERROR) return mascot_tick_error;
            if (set_result == ACTION_SET_CONDITION_NOT_MET) continue;
            if (set_result == ACTION_SET_BORDER_INVALID) continue;
            if (set_result == ACTION_SET_ACTION_NEXT) return mascot_tick_next;
        }
        if (result.status == mascot_tick_ok) return mascot_tick_ok;
    }
    LOG("ERROR", RED, "<Mascot:%s:%u> Action iterator reached maximum iterations", mascot->prototype->name, mascot->id);
    return mascot_tick_error;
}

// Behavior iterator
enum mascot_tick_result mascot_behavior_next(struct mascot* mascot, uint32_t tick)
{

    UNUSED(tick);

    if (mascot->dragged) {
        mascot_set_behavior(mascot, mascot->prototype->drag_behavior);
        return mascot_tick_ok;
    }

    if (mascot->hotspot_active) {
        mascot_set_behavior(mascot, mascot->hotspot_behavior);
        return mascot_tick_ok;
    }

    if (mascot->current_behavior) INFO("<Mascot:%s:%u> Completed behavior \"%s\"", mascot->prototype->name, mascot->id, mascot->current_behavior->name);

    mascot_build_behavior_pool(mascot, NULL, false);
    if (mascot->current_behavior) mascot_build_behavior_pool(mascot, mascot->current_behavior, mascot->current_behavior->add_behaviors);

    if (!mascot->behavior_pool_len) {
        // Should never happen
        WARN("<Mascot:%s:%u> Cannot select next behavior: behavior pull is empty!", mascot->prototype->name, mascot->id);
        mascot_build_behavior_pool(mascot, NULL, false);
        if (!mascot->behavior_pool_len) {
            WARN("<Mascot:%s:%u> Cannot select next behavior: behavior pull is still empty!", mascot->prototype->name, mascot->id);
        }
    }

    const struct mascot_behavior* next_behavior = select_behavior_from_pool(mascot, mascot->behavior_pool, mascot->behavior_pool_len);
    if (!next_behavior) {
        // Should never happen
        WARN("<Mascot:%s:%u> Selected behavior is NULL", mascot->prototype->name, mascot->id);
        return mascot_tick_error;
    }

    mascot_set_behavior(mascot, next_behavior);

    INFO("<Mascot:%s:%u> Initialized behavior \"%s\"", mascot->prototype->name, mascot->id, mascot->current_behavior->name);

    return mascot_tick_reenter;
}

struct mascot* mascot_clone(struct mascot* mascot) {
    if (!mascot) ERROR("MascotClone: mascot is NULL");
    const char* mascot_type = NULL;
    const char* born_behavior = NULL;
    int32_t born_x = mascot->BornX->value.i;
    int32_t born_y = mascot->BornY->value.i;
    DEBUG("<Mascot:%s:%u> Cloning mascot with offsets %d, %d", mascot->prototype->name, mascot->id, born_x, born_y);
    if (mascot->current_action.action) {
        mascot_type = mascot->current_action.action->born_mascot;
        born_behavior = mascot->current_action.action->born_behavior;
    }
    if (mascot_type && !mascot->prototype->prototype_store) {
        WARN("MascotClone: mascot type is defined but prototype store is NULL");
        return NULL;
    }

    const struct mascot_prototype* clone_prototype = NULL;

    if (mascot_type) {
        clone_prototype = mascot_prototype_store_get(mascot->prototype->prototype_store, mascot_type);
    } else {
        clone_prototype = mascot->prototype;
    }

    if (!clone_prototype) {
        WARN("MascotClone: clone prototype is NULL");
        return NULL;
    }
    INFO("<Mascot:%s:%u> Cloning mascot to prototype of type %s with born behavior %s at (%d,%d)", mascot->prototype->name, mascot->id, clone_prototype->name, born_behavior, mascot->X->value.i, mascot->Y->value.i);
    struct mascot* clone = mascot_new(
        clone_prototype,
        born_behavior,
        mascot->InitialVelX->value.f,
        mascot->InitialVelY->value.f,
        (mascot->X->value.i + born_x * (mascot->LookingRight->value.i ? -1 : 1) * environment_screen_scale(mascot->environment)),
        mascot->Y->value.i + born_y * environment_screen_scale(mascot->environment),
        mascot->Gravity->value.f,
        mascot->AirDragX->value.f,
        mascot->AirDragY->value.f,
        mascot->LookingRight->value.i,
        mascot->environment
    );
    return clone;
}

bool mascot_transform(struct mascot* mascot)
{
    if (!mascot) ERROR("MascotTransform: mascot is NULL");
    const char* mascot_type = NULL;
    const char* transform_behavior = NULL;
    if (mascot->current_action.action) {
        mascot_type = mascot->current_action.action->transform_target;
        transform_behavior = mascot->current_action.action->target_behavior;
    }

    INFO("<Mascot:%s:%u> Transforming mascot to type \"%s\" with target behavior %s", mascot->prototype->name, mascot->id, mascot_type, transform_behavior);

    if (!mascot_type) {
        WARN("MascotTransform: mascot type is NULL");
        return false;
    }

    const struct mascot_behavior* behavior = NULL;
    const struct mascot_prototype* prototype = NULL;

    if (mascot->prototype->prototype_store) {
        prototype = mascot_prototype_store_get(mascot->prototype->prototype_store, mascot_type);
        if (!prototype) {
            WARN("MascotTransform: prototype is NULL");
            return false;
        }
    }

    int32_t position_x = mascot->X->value.i;
    int32_t position_y = mascot->Y->value.i;
    bool looking_right = mascot->LookingRight->value.i;

    behavior = mascot_prototype_behavior_by_name(prototype, transform_behavior);
    mascot_init_(mascot, prototype, false);
    if (behavior) mascot_set_behavior(mascot, behavior);
    else mascot_behavior_next(mascot, 0);
    mascot->X->value.i = position_x;
    mascot->Y->value.i = position_y;
    mascot->LookingRight->value.i = looking_right;
    return true;
}

enum mascot_tick_result mascot_tick(struct mascot* mascot, uint32_t tick, struct mascot_tick_return *tick_return) {
    enum mascot_tick_result action_result = mascot_tick_reenter;
    *tick_return = (struct mascot_tick_return) {0};

    pthread_mutex_lock(&mascot->tick_lock);

    if (!mascot->current_behavior) {
        DEBUG("<Mascot:%s:%u> No behavior set, trying to set fall", mascot->prototype->name, mascot->id);
        if (mascot_fall_behavior(mascot)) {
            mascot_set_behavior(mascot, mascot_fall_behavior(mascot));
        } else {
            WARN("<Mascot:%s:%u> No behavior set", mascot->prototype->name, mascot->id);
            mascot_build_behavior_pool(mascot, NULL, false);
        }
    }

    int iteration = 0;
    while (tick_return->events_count < 128 && action_result != mascot_tick_ok && iteration++ < 16) {
        action_result = mascot_action_get_next(mascot, tick);
        mascot_action_tick tick_handler = mascot_get_handlers(mascot)->tick;
        if (tick_handler && action_result == mascot_tick_ok) {
            action_result = tick_handler(mascot, &mascot->current_action, tick);
        }

        if (action_result == mascot_tick_error) {
            WARN("<Mascot:%s:%u> Action tick error", mascot->prototype->name, mascot->id);
            break;
        } else if (action_result == mascot_tick_next) {
            action_result = mascot_behavior_next(mascot, tick);
        } else if (action_result == mascot_tick_clone || action_result == mascot_tick_clone_and_next) {
            struct mascot* clone = mascot_clone(mascot);
            if (!clone) {
                WARN("<Mascot:%s:%u> Cannot clone mascot", mascot->prototype->name, mascot->id);
                pthread_mutex_unlock(&mascot->tick_lock);
                return mascot_tick_error;
            }
            mascot->born_count++;
            mascot->born_tick = tick;
            if (action_result == mascot_tick_clone_and_next) {
                action_result = mascot_behavior_next(mascot, tick);
            }
            tick_return->events[tick_return->events_count].event_type = mascot_tick_clone;
            tick_return->events[tick_return->events_count++].event.mascot = clone;

        } else if (action_result == mascot_tick_transform) {
            if (!mascot_transform(mascot)) {
                WARN("<Mascot:%s:%u> Cannot transform mascot", mascot->prototype->name, mascot->id);
                pthread_mutex_unlock(&mascot->tick_lock);
                return mascot_tick_error;
            }
            action_result = mascot_tick_ok;
        } else if (action_result == mascot_tick_dispose) {
            pthread_mutex_unlock(&mascot->tick_lock);
            return mascot_tick_dispose;
        } else if (action_result == mascot_tick_escape) {
            environment_subsurface_release(mascot->subsurface);
            mascot_drag_ended(mascot, false);
        }
    }
    if (iteration >= 64) {
        WARN("<Mascot:%s:%u> Tick iteration limit reached, considered softlocked (changing behavior to fall)", mascot->prototype->name, mascot->id);
        for (int i = 0; i < mascot->behavior_pool_len; i++) {
            if (mascot->behavior_pool[i].behavior == mascot->current_behavior) {
                mascot->behavior_pool[i].frequency = 0;
                break;
            }
        }
        mascot_set_behavior(mascot, 0);
        pthread_mutex_unlock(&mascot->tick_lock);
        return mascot_tick_reenter;
    }
    if (tick_return->events_count == 128) {
        pthread_mutex_unlock(&mascot->tick_lock);
        return mascot_tick_reenter;
    }
    pthread_mutex_unlock(&mascot->tick_lock);
    return action_result;
}


// Try to start dragging the mascot
bool mascot_drag_started(struct mascot* mascot, environment_pointer_t* pointer)
{
    if (!config_get_dragging()) return false;
    if (mascot->state == mascot_state_interact || !mascot->prototype->drag_behavior) {
        return false;
    }
    mascot->dragged = true;
    mascot->dragged_tick = 0;
    mascot_set_behavior(mascot, mascot->prototype->drag_behavior);
    environment_subsurface_set_offset(mascot->subsurface, 0, 120);
    environment_subsurface_drag(mascot->subsurface, pointer);
    return true;
}

bool mascot_drag_ended(struct mascot* mascot, bool throw)
{
    mascot->dragged = false;
    if (throw) mascot_set_behavior(mascot, mascot_thrown_behavior(mascot));
    else mascot_set_behavior(mascot, mascot_fall_behavior(mascot));
    environment_subsurface_release(mascot->subsurface);
    environment_subsurface_set_offset(mascot->subsurface, 0, 0);
    environment_subsurface_move(mascot->subsurface, mascot->X->value.i, mascot->Y->value.i-120, true, false);
    return true;
}



// Set the environment of the mascot
bool mascot_environment_changed(struct mascot* mascot, environment_t* env)
{
    if (!env) ERROR("MascotEnvironmentChanged: mascot is NULL");
    INFO("<Mascot:%s:%u> Environment changed", mascot->prototype->name, mascot->id);
    mascot->environment = env;
    protocol_server_mascot_migrated(mascot, env);
    return environment_migrate_subsurface(mascot->subsurface, env);
}

struct mascot* mascot_new
    (
        const struct mascot_prototype* prototype, const char* starting_behavior,
        float velx, float vely, uint32_t posx, uint32_t posy, float_t gravity,
        float air_drag_x, float air_drag_y, bool looking_right, environment_t* env
    )
{
    if (!prototype) ERROR("Could not create mascot: Prototype is null");

    struct mascot* mascot = calloc(1, sizeof(struct mascot));
    if (!mascot) ERROR("Could not create mascot type %s(%s): Allocation failed");

    mascot->id = new_mascot_id++;
    mascot->environment = env;

    mascot_init_(mascot, prototype, false);

    mascot->LookingRight->value.i = looking_right;

    mascot->X->value.i = posx;
    mascot->Y->value.i = posy;
    mascot->VelocityX->value.f = velx;
    mascot->VelocityY->value.f = vely;
    mascot->Gravity->value.f = gravity;
    mascot->AirDragX->value.f = air_drag_x;
    mascot->AirDragY->value.f = air_drag_y;

    // Look up the starting behavior
    if (starting_behavior) {
        const struct mascot_behavior* starting_behavior_ptr = NULL;
        for (uint16_t i = 0; i < prototype->behavior_count; i++) {
            if (!strcmp(prototype->behavior_definitions[i]->name, starting_behavior) && strlen(prototype->behavior_definitions[i]->name) == strlen(starting_behavior)) {
                starting_behavior_ptr = prototype->behavior_definitions[i];
                break;
            }
        }
        if (starting_behavior_ptr) {
            mascot_set_behavior(mascot, starting_behavior_ptr);
        } else {
            WARN("Could not find starting behavior %s for mascot %s(%s)", starting_behavior, prototype->display_name, prototype->name);
        }
    }

    mascot->subsurface = environment_create_subsurface(env); // Get opaque surface from env
    environment_subsurface_associate_mascot(mascot->subsurface, mascot);
    environment_subsurface_set_position(mascot->subsurface, posx, environment_screen_height(env) - posy);
    mascot_total_count++;
    pthread_mutex_init(&mascot->tick_lock, NULL);
    INFO("<Mascot:%s:%u> Created new mascot of type \"%s\" at (%d,%d)", prototype->name, mascot->id, prototype->display_name, posx, posy);
    return mascot;

}

void mascot_link(struct mascot* mascot)
{
    if (!mascot) return;
    pthread_mutex_lock(&mascot->tick_lock);
    mascot->refcounter++;
    pthread_mutex_unlock(&mascot->tick_lock);
}

void mascot_unlink(struct mascot* mascot)
{
    if (!mascot) return;

    pthread_mutex_lock(&mascot->tick_lock);

    if (mascot->refcounter) mascot->refcounter--;
    if (mascot->refcounter > 0) {
        pthread_mutex_unlock(&mascot->tick_lock);
        return;
    };

    protocol_server_mascot_destroyed(mascot);

    if (mascot->prototype)
    {
        mascot_prototype_unlink((struct mascot_prototype*)mascot->prototype);
    }

    if (mascot->subsurface)
    {
        environment_destroy_subsurface(mascot->subsurface);
    }

    mascot_announce_affordance(mascot, NULL);
    if (mascot->associated_ie) {
        // plugin_execute_ie_detach_mascot(mascot->associated_ie->parent_plugin, mascot->associated_ie, mascot);
    }
    mascot_detach_affordance_manager(mascot);

    free(mascot->action_data);

    pthread_mutex_unlock(&mascot->tick_lock);
    pthread_mutex_destroy(&mascot->tick_lock);

    mascot_total_count--;

    free(mascot);
}

static void mascot_init_(struct mascot* mascot, const struct mascot_prototype* prototype, bool save_vars)
{
    if (!mascot || !prototype) return;

    if (mascot->prototype)
    {
        mascot_prototype_unlink((struct mascot_prototype*)mascot->prototype);
    }

    mascot->prototype = prototype;
    mascot_prototype_link(mascot->prototype);

    if (!save_vars)
    {
        for (uint16_t i = 0; i < prototype->local_variables_count; i++)
        {
            mascot->local_variables[i].expr = (struct mascot_expression_value){0};
            mascot->local_variables[i].value.i = 0;
            mascot->local_variables[i].used = false;
        }
        mascot->X = &mascot->local_variables[0];
        mascot->Y = &mascot->local_variables[1];
        mascot->TargetX = &mascot->local_variables[2];
        mascot->TargetY = &mascot->local_variables[3];
        mascot->Gravity = &mascot->local_variables[4];
        mascot->LookingRight = &mascot->local_variables[5];
        mascot->AirDragX = &mascot->local_variables[6];
        mascot->AirDragY = &mascot->local_variables[7];
        mascot->VelocityX = &mascot->local_variables[8];
        mascot->VelocityY = &mascot->local_variables[9];
        mascot->BornX = &mascot->local_variables[10];
        mascot->BornY = &mascot->local_variables[11];
        mascot->InitialVelX = &mascot->local_variables[12];
        mascot->InitialVelY = &mascot->local_variables[13];
        mascot->VelocityParam = &mascot->local_variables[14];
        mascot->FootX = &mascot->local_variables[15];
        mascot->FootDX = &mascot->local_variables[16];
        mascot->ModX = &mascot->local_variables[17];
        mascot->ModY = &mascot->local_variables[18];
        mascot->Gap = &mascot->local_variables[19];
        mascot->BornInterval = &mascot->local_variables[20];
        mascot->BornCount = &mascot->local_variables[21];

        mascot->X->kind = MASCOT_LOCAL_VARIABLE_X_TYPE;
        mascot->Y->kind = MASCOT_LOCAL_VARIABLE_Y_TYPE;
        mascot->TargetX->kind = MASCOT_LOCAL_VARIABLE_TARGETX_TYPE;
        mascot->TargetY->kind = MASCOT_LOCAL_VARIABLE_TARGETY_TYPE;
        mascot->Gravity->kind = MASCOT_LOCAL_VARIABLE_GRAVITY_TYPE;
        mascot->LookingRight->kind = MASCOT_LOCAL_VARIABLE_LOOKINGRIGHT_TYPE;
        mascot->AirDragX->kind = MASCOT_LOCAL_VARIABLE_AIRDRAGX_TYPE;
        mascot->AirDragY->kind = MASCOT_LOCAL_VARIABLE_AIRDRAGY_TYPE;
        mascot->VelocityX->kind = MASCOT_LOCAL_VARIABLE_VELOCITYX_TYPE;
        mascot->VelocityY->kind = MASCOT_LOCAL_VARIABLE_VELOCITYY_TYPE;
        mascot->BornX->kind = MASCOT_LOCAL_VARIABLE_BORNX_TYPE;
        mascot->BornY->kind = MASCOT_LOCAL_VARIABLE_BORNY_TYPE;
        mascot->InitialVelX->kind = MASCOT_LOCAL_VARIABLE_INITIALVELX_TYPE;
        mascot->InitialVelY->kind = MASCOT_LOCAL_VARIABLE_INITIALVELY_TYPE;
        mascot->VelocityParam->kind = MASCOT_LOCAL_VARIABLE_VELOCITYPARAM_TYPE;
        mascot->FootX->kind = MASCOT_LOCAL_VARIABLE_FOOTX_TYPE;
        mascot->FootDX->kind = MASCOT_LOCAL_VARIABLE_FOOTDX_TYPE;
        mascot->ModX->kind = MASCOT_LOCAL_VARIABLE_MODX_TYPE;
        mascot->ModY->kind = MASCOT_LOCAL_VARIABLE_MODY_TYPE;
        mascot->Gap->kind = MASCOT_LOCAL_VARIABLE_GAP_TYPE;
        mascot->BornInterval->kind = MASCOT_LOCAL_VARIABLE_BORNINTERVAL_TYPE;
        mascot->BornCount->kind = MASCOT_LOCAL_VARIABLE_BORNCOUNT_TYPE;
    }

    mascot->next_frame_tick = 0;
    mascot->dragged = false;
    mascot->state = mascot_state_none;
    mascot->current_affordance = NULL;
    memset(mascot->action_stack, 0, sizeof(const struct mascot_action*)*128);
    mascot->as_p = 0;
    memset(mascot->behavior_pool, 0, sizeof(const struct mascot_behavior*)*128);
    mascot->behavior_pool_len = 0;

    mascot->current_action.action = NULL;
    mascot->current_condition = (struct mascot_expression_value){0};
    mascot->current_behavior = NULL;
    mascot->current_animation = NULL;

    mascot->frame_index = 0;
    mascot->animation_index = 0;
    mascot->action_index = 0;

    mascot->hotspot_active = false;
    mascot->hotspot_behavior = NULL;

    mascot->associated_ie = NULL;

    free(mascot->action_data);
    mascot->action_data = NULL;

}

bool mascot_hotspot_click(struct mascot* mascot, int32_t x, int32_t y, enum mascot_hotspot_button button)
{

    if (!mascot) return false;
    if (!mascot->current_animation) return false;
    if (!mascot->current_animation->hotspots_count) return false;

    pthread_mutex_lock(&mascot->tick_lock);

    struct mascot_hotspot* hotspot = mascot_hotspot_by_pos(mascot, x, y);
    if (!hotspot) {
        pthread_mutex_unlock(&mascot->tick_lock);
        return false;
    }

    if (hotspot->button != button) {
        pthread_mutex_unlock(&mascot->tick_lock);
        return false;
    }

    const struct mascot_behavior* behavior = mascot_prototype_behavior_by_name(mascot->prototype, hotspot->behavior);
    if (!behavior) {
        pthread_mutex_unlock(&mascot->tick_lock);
        return false;
    }

    mascot->hotspot_behavior = behavior;
    mascot_set_behavior(mascot, behavior);

    pthread_mutex_unlock(&mascot->tick_lock);
    return true;
}


bool mascot_hotspot_hold(struct mascot* mascot, int32_t x, int32_t y, enum mascot_hotspot_button button, bool release) {
    if (!mascot) return false;

    if (release) {
        mascot->hotspot_active = false;
        mascot->hotspot_behavior = NULL;
        return true;
    }

    bool res = mascot_hotspot_click(mascot, x, y, button);
    if (res) {
        mascot->hotspot_active = true;
    }
    return res;
}

struct mascot_hotspot* mascot_hotspot_by_pos(struct mascot* mascot, int32_t x, int32_t y) {
    if (!mascot) return NULL;
    if (!mascot->current_animation) return NULL;
    if (!mascot->current_animation->hotspots_count) return NULL;

    for (uint16_t i = 0; i < mascot->current_animation->hotspots_count; i++)
    {
        struct mascot_hotspot* hotspot = mascot->current_animation->hotspots[i];
        if (hotspot->shape == mascot_hotspot_shape_rectangle) {
            if (!(x >= hotspot->x && x <= hotspot->x + hotspot->width && y >= hotspot->y && y <= hotspot->y + hotspot->height)) {
                continue;
            }
        } else if (hotspot->shape == mascot_hotspot_shape_ellipse) {
            // Check if x,y is inside the ellipse, if not, continue
            int32_t a = hotspot->width / 2;
            int32_t b = hotspot->height / 2;
            int32_t h = hotspot->x + a;
            int32_t k = hotspot->y + b;
            if (a == 0 || b == 0) {
                continue;
            }
            if (((x - h) * (x - h)) / (a * a) + ((y - k) * (y - k)) / (b * b) > 1) {
                continue;
            }
        }
        if (!hotspot->behavior) {
            continue;
        }
        const struct mascot_behavior* behavior = mascot_prototype_behavior_by_name(mascot->prototype, hotspot->behavior);
        if (!behavior) {
            continue;
        }
        return hotspot;
    }
    return NULL;
}

#endif

bool mascot_moved(struct mascot* mascot, int32_t x, int32_t y)
{
    mascot->X->value.i = x;
    mascot->Y->value.i = y;
    return true;
}

float mascot_get_variable_f(struct mascot *mascot, uint16_t id)
{
    if (!mascot) {
        return 0.0;
    }

    struct mascot_local_variable* var = &mascot->local_variables[id];
    if (var->kind == mascot_local_variable_int) {
        return (float)var->value.i;
    }
    return var->value.f;
}

int32_t mascot_get_variable_i(struct mascot *mascot, uint16_t id)
{
    if (!mascot) {
        return 0;
    }

    struct mascot_local_variable* var = &mascot->local_variables[id];
    if (var->kind == mascot_local_variable_float) {
        return (int32_t)var->value.f;
    }
    return var->value.i;
}

void mascot_set_variable_f(struct mascot *mascot, uint16_t id, float value)
{
    if (!mascot) {
        return;
    }

    struct mascot_local_variable* var = &mascot->local_variables[id];
    var->value.f = value;
}

void mascot_set_variable_i(struct mascot *mascot, uint16_t id, int32_t value)
{
    if (!mascot) {
        return;
    }

    struct mascot_local_variable* var = &mascot->local_variables[id];
    var->value.i = value;
}

const struct mascot_behavior* mascot_fall_behavior(struct mascot* mascot)
{
    if (!mascot) {
        return NULL;
    }

    return mascot->prototype->fall_behavior;
}

const struct mascot_behavior* mascot_thrown_behavior(struct mascot* mascot)
{
    if (!mascot) {
        return NULL;
    }

    return mascot->prototype->thrown_behavior;
}

void mascot_build_behavior_pool(struct mascot* mascot, const struct mascot_behavior* behavior, bool add) {
    if (!mascot) return;
    if (!add) {
        mascot->behavior_pool_len = 0;
        memset(mascot->behavior_pool, 0, sizeof(struct mascot_behavior_reference) * 128);
    }

    const struct mascot_behavior_reference* next_behavior_list;
    uint16_t behavior_count = 0;
    if (behavior) {
        next_behavior_list = behavior->next_behavior_list;
        behavior_count = behavior->next_behaviors_count;
    }
    else {
        next_behavior_list = (struct mascot_behavior_reference*)mascot->prototype->root_behavior_list;
        behavior_count = mascot->prototype->root_behavior_list_count;
    }

    for (uint16_t i = 0; i < behavior_count; i++) {
        const struct mascot_behavior_reference* behavior_ref = &next_behavior_list[i];
        if (behavior_ref->behavior->is_condition) {
            if (behavior_ref->behavior->condition) {
                float result = 0.0;
                enum expression_execution_result execution_status = expression_vm_execute(behavior_ref->behavior->condition->body, mascot, &result);
                if (execution_status == EXPRESSION_EXECUTION_ERROR) {
                    WARN("<Mascot:%s:%u> Error while executing condition expression", mascot->prototype->name, mascot->id);
                    continue;
                }
                if (result == 0.0) {
                    continue;
                }
            }
            mascot_build_behavior_pool(mascot, behavior_ref->behavior, true);
            continue;
        }
        if (behavior_ref->frequency == 0) continue;
        if (behavior_ref->behavior->action->border_type != environment_border_type_any) {
            if (environment_get_border_type(mascot->environment, mascot->X->value.i, mascot->Y->value.i) != behavior_ref->behavior->action->border_type) {
                continue;
            }
        }
        enum mascot_tick_result cond_res = mascot_check_condition(mascot, behavior_ref->behavior->condition);
        if (cond_res == mascot_tick_error) {
            return;
        }
        if (cond_res == mascot_tick_next) {
            continue;
        }
        if (mascot->behavior_pool_len >= 128) {
            break;
        }
        DEBUG("<Mascot:%s:%u> Adding behavior %s to pool", mascot->prototype->name, mascot->id, behavior_ref->behavior->name);
        mascot->behavior_pool[mascot->behavior_pool_len++] = (struct mascot_behavior_reference){.behavior = behavior_ref->behavior, .frequency = behavior_ref->frequency};
    }
}

// Set behavior (resets behavior pool and action stacks)
void mascot_set_behavior(struct mascot* mascot, const struct mascot_behavior* behavior)
{
#ifndef PLUGINSUPPORT_IMPLEMENTATION
    if (mascot->current_action.action) {
        struct action_funcs* funcs = mascot_get_handlers(mascot);
        if (funcs) {
            if (funcs->clean) {
                funcs->clean(mascot);
            }
        }
    }
#endif

    mascot->current_behavior = behavior;
    mascot->current_action = (struct mascot_action_reference){0};
    mascot->current_condition = (struct mascot_expression_value){0};
    mascot->current_animation = NULL;
    mascot->animation_index = 0;
    mascot->frame_index = 0;
    mascot->action_index = 0;
    mascot->as_p = 0;

    mascot_build_behavior_pool(mascot, NULL, false);
    if (behavior) {
        if (behavior->is_condition) {
            WARN("<Mascot:%s:%u> Behavior is a condition, not a behavior", mascot->prototype->name, mascot->id);
            return;
        }
        mascot_build_behavior_pool(mascot, behavior, behavior->add_behaviors);
    }
}

enum mascot_tick_result mascot_out_of_bounds_check(struct mascot* mascot)
{
    struct bounding_box* env_bbox = environment_local_geometry(mascot->environment);
    if (
        is_outside(env_bbox, mascot->X->value.i, mascot->Y->value.i)
        && mascot->state != mascot_state_jump
    ) {
        INFO("<Mascot:%s:%u> Mascot out of screen bounds (caught at %d,%d while allowed values are from 0,0 to %d,%d (Geometry offset is %d,%d)), respawning!", mascot->prototype->name, mascot->id, mascot->X->value.i, mascot->Y->value.i, env_bbox->width, env_bbox->height, env_bbox->x, env_bbox->y);
        mascot->X->value.i = rand() % environment_workarea_width(mascot->environment);
        mascot->Y->value.i = environment_workarea_height(mascot->environment) - 256;
        mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
        environment_subsurface_set_position(mascot->subsurface, mascot->X->value.i, mascot_screen_y_to_mascot_y(mascot, mascot->Y->value.i));
        environment_subsurface_reset_interpolation(mascot->subsurface);
        return mascot_tick_reenter;
    }
    return mascot_tick_ok;
}

enum mascot_tick_result mascot_ground_check(struct mascot* mascot, struct mascot_action_reference* actionref, void (*clean_func)(struct mascot*))
{
    // Check if action border requirements are met
    enum environment_border_type border_type = environment_get_border_type(mascot->environment, mascot->X->value.i, mascot->Y->value.i);
    if (actionref->action->border_type != environment_border_type_any) {
        if (
            border_type
            != actionref->action->border_type
        ) {
            if (border_type != environment_border_type_floor) {
                mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
                clean_func(mascot);
                return mascot_tick_reenter;
            }

            return mascot_tick_next;
        }
    }
    return mascot_tick_ok;
}

enum mascot_tick_result mascot_execute_variable(struct mascot *mascot, uint16_t variable_id)
{
    if (variable_id > MASCOT_LOCAL_VARIABLE_COUNT) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Variable %u out of bounds", mascot->prototype->name, mascot->id, variable_id);
        return mascot_tick_error;
    }
    if (mascot->local_variables[variable_id].used) {
        float vmres = 0.0;
        enum expression_execution_result res = expression_vm_execute(
            mascot->local_variables[variable_id].expr.expression_prototype->body,
            mascot,
            &vmres
        );
        if (res == EXPRESSION_EXECUTION_ERROR) {
            LOG("ERROR", RED, "<Mascot:%s:%u> Variable %u errored for init in action \"%s\"", mascot->prototype->name, mascot->id, variable_id, mascot->current_action.action->name);
            return mascot_tick_error;
        }
        if (mascot->local_variables[variable_id].kind == mascot_local_variable_float) {
            DEBUG("<Mascot:%s:%u> Variable %u set to %f", mascot->prototype->name, mascot->id, variable_id, vmres);
            mascot->local_variables[variable_id].value.f = vmres;
        } else if (mascot->local_variables[variable_id].kind == mascot_local_variable_int) {
            DEBUG("<Mascot:%s:%u> Variable %u set to %d", mascot->prototype->name, mascot->id, variable_id, (int)vmres);
            mascot->local_variables[variable_id].value.i = (int)vmres;
        } else {
            LOG("ERROR", RED, "<Mascot:%s:%u> Variable %u kind not supported", mascot->prototype->name, mascot->id, variable_id);
            return mascot_tick_error;
        }
    }
    return mascot_tick_ok;
}

enum mascot_tick_result mascot_assign_variable(struct mascot *mascot, uint16_t variable_id, struct mascot_local_variable* variable_data)
{
    if (variable_id > MASCOT_LOCAL_VARIABLE_COUNT) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Variable %u out of bounds", mascot->prototype->name, mascot->id, variable_id);
        return mascot_tick_error;
    }
    DEBUG("<Mascot:%s:%u> Variable %u assigned", mascot->prototype->name, mascot->id, variable_id);
    mascot->local_variables[variable_id] = (struct mascot_local_variable){
        .kind = mascot->local_variables[variable_id].kind,
        .used = variable_data->used,
        .expr = variable_data->expr,
        .value = variable_data->value
    };
    return mascot_execute_variable(mascot, variable_id);
}

enum mascot_tick_result mascot_check_condition(struct mascot *mascot, const struct mascot_expression* condition)
{
    if (!condition) {
        return mascot_tick_ok;
    }
    float vmres = 0.0;
    enum expression_execution_result res = expression_vm_execute(
        condition->body,
        mascot,
        &vmres
    );
    if (res == EXPRESSION_EXECUTION_ERROR) {
        LOG("ERROR", RED, "<Mascot:%s:%u> Condition errored for next in action \"%s\"", mascot->prototype->name, mascot->id, mascot->current_action.action->name);
        return mascot_tick_error;
    }
    if (vmres == 0.0) {
        return mascot_tick_next;
    }
    return mascot_tick_ok;
}

enum mascot_tick_result mascot_recheck_condition(struct mascot *mascot, const struct mascot_expression* condition)
{
    if (condition) {
        if (condition->evaluate_once) {
            return mascot_tick_ok;
        }
    }
    return mascot_check_condition(mascot, condition);
}

int32_t yconvat(environment_t* env, int32_t screen_y)
{
    if (screen_y == -1) return -1;
    return environment_workarea_height(env) - screen_y;
}


int32_t mascot_screen_y_to_mascot_y(struct mascot* mascot, int32_t screen_y)
{
    if (screen_y == -1) return -1;
    return yconvat(mascot->environment, screen_y);
}

bool mascot_is_on_workspace_border(struct mascot* mascot)
{
    return (
        mascot->X->value.i == (int32_t)environment_workarea_left(mascot->environment) ||
        mascot->X->value.i == (int32_t)environment_workarea_right(mascot->environment) ||
        mascot->Y->value.i == mascot_screen_y_to_mascot_y(mascot, environment_workarea_top(mascot->environment)) ||
        mascot->Y->value.i == mascot_screen_y_to_mascot_y(mascot, environment_workarea_bottom(mascot->environment))
    );
}

void mascot_apply_environment_position_diff(struct mascot* mascot, int32_t dx, int32_t dy, int32_t flags, environment_t* at_env)
{
    if (!mascot) return;
    if (!at_env) at_env = mascot->environment;

    int new_x = mascot->X->value.i;
    int new_y = mascot->Y->value.i;
    if (flags & DIFF_HORIZONTAL_MOVE) {
        new_x = mascot->X->value.i + dx;
        if (mascot->TargetX->value.i != -1) mascot->TargetX->value.i += dx;

    }
    if (flags & DIFF_VERTICAL_MOVE) {
        new_y = yconvat(mascot->environment, mascot->Y->value.i) + dy;
        if (mascot->TargetY->value.i != -1) mascot->TargetY->value.i += dy;
    }

    mascot_moved(mascot, new_x, yconvat(at_env, new_y));

}

environment_ie_t* mascot_get_active_ie(struct mascot* mascot) {
    if (!mascot) ERROR("mascot_get_active_ie: mascot is NULL");
    if (mascot->associated_ie) return mascot->associated_ie;
    return environment_get_front_ie(mascot->environment);
}
