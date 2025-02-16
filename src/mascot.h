/*
    mascot.h - wl_shimeji's mascot processing routines

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

#ifndef MASCOT_H
#define MASCOT_H

#include "master_header.h"
#include "wayland_includes.h"
#include <stdint.h>
#include <pthread.h>

struct mascot;
struct mascot_action;
struct mascot_behavior;
struct mascot_prototype;
struct mascot_tick_return;
struct mascot_pose;
struct mascot_affordance_manager;

#include "environment.h"
#include "mascot_atlas.h"
#include "expressions.h"

#include "mascot_config_parser.h"

#include "plugins.h"

extern uint32_t mascot_total_count;
extern uint32_t new_mascot_id;

enum action_set_result
{
    ACTION_SET_RESULT_OK,
    ACTION_SET_CONDITION_NOT_MET,
    ACTION_SET_CONDITION_EXECUTION_ERROR,
    ACTION_SET_BORDER_INVALID,
    ACTION_SET_ACTION_STACK_OVERFLOW,
    ACTION_SET_ACTION_STACK_EMPTY,
    ACTION_SET_ACTION_TRANSIENT,
    ACTION_SET_ACTION_REENTER,
    ACTION_SET_ACTION_NEXT,
    ACTION_SET_PARAMS_INVALID,
    ACTION_SET_ERROR
};


enum mascot_local_variable_kind {
    mascot_local_variable_int,
    mascot_local_variable_float
};

// Types of action for the mascot
enum mascot_action_type {
    mascot_action_type_animate, // ???
    mascot_action_type_stay, // Embedded stay action
    mascot_action_type_move, // Embedded move action
    mascot_action_type_embedded, // Embedded action???
    mascot_action_type_sequence, // Sequence of actions
    mascot_action_type_select, // Select action

    mascot_action_type_count
};

enum mascot_action_embedded_property {
    mascot_action_embedded_property_none,
    mascot_action_embedded_property_look, // investigate
    mascot_action_embedded_property_offset, // Move mascot immediately to offset set by X and Y
    mascot_action_embedded_property_jump,
    mascot_action_embedded_property_fall,
    mascot_action_embedded_property_drag,
    mascot_action_embedded_property_drag_resist, // After drag action is finished, mascot is escapes drag
    mascot_action_embedded_property_clone,
    mascot_action_embedded_property_broadcast, // todo
    mascot_action_embedded_property_scanmove,
    mascot_action_embedded_property_scanjump,
    mascot_action_embedded_property_interact, // todo
    mascot_action_embedded_property_dispose, // delete mascot
    mascot_action_embedded_property_transform, // transform mascot
    mascot_action_embedded_property_thrown,

    mascot_action_embedded_property_walkwithie,
    mascot_action_embedded_property_fallwithie,
    mascot_action_embedded_property_throwie,

    mascot_action_embedded_property_unsupported,
    mascot_action_embedded_property_unknown,

    mascot_embedded_property_count
};


#define MASCOT_LOCAL_VARIABLE_X_VALUE "mascot.x"
#define MASCOT_LOCAL_VARIABLE_X_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_X_ID    0

#define MASCOT_LOCAL_VARIABLE_Y_VALUE "mascot.y"
#define MASCOT_LOCAL_VARIABLE_Y_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_Y_ID    1

#define MASCOT_LOCAL_VARIABLE_TARGETX_VALUE "mascot.targetx"
#define MASCOT_LOCAL_VARIABLE_TARGETX_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_TARGETX_ID    2

#define MASCOT_LOCAL_VARIABLE_TARGETY_VALUE "mascot.targety"
#define MASCOT_LOCAL_VARIABLE_TARGETY_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_TARGETY_ID    3

#define MASCOT_LOCAL_VARIABLE_GRAVITY_VALUE "mascot.gravity"
#define MASCOT_LOCAL_VARIABLE_GRAVITY_TYPE  mascot_local_variable_float
#define MASCOT_LOCAL_VARIABLE_GRAVITY_ID    4

#define MASCOT_LOCAL_VARIABLE_LOOKINGRIGHT_VALUE "mascot.lookright"
#define MASCOT_LOCAL_VARIABLE_LOOKINGRIGHT_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_LOOKINGRIGHT_ID    5

#define MASCOT_LOCAL_VARIABLE_AIRDRAGX_VALUE "mascot.registancex"
#define MASCOT_LOCAL_VARIABLE_AIRDRAGX_TYPE  mascot_local_variable_float
#define MASCOT_LOCAL_VARIABLE_AIRDRAGX_ID    6

#define MASCOT_LOCAL_VARIABLE_AIRDRAGY_VALUE "mascot.registancey"
#define MASCOT_LOCAL_VARIABLE_AIRDRAGY_TYPE  mascot_local_variable_float
#define MASCOT_LOCAL_VARIABLE_AIRDRAGY_ID    7

#define MASCOT_LOCAL_VARIABLE_VELOCITYX_VALUE "mascot.velocityx"
#define MASCOT_LOCAL_VARIABLE_VELOCITYX_TYPE  mascot_local_variable_float
#define MASCOT_LOCAL_VARIABLE_VELOCITYX_ID    8

#define MASCOT_LOCAL_VARIABLE_VELOCITYY_VALUE "mascot.velocityy"
#define MASCOT_LOCAL_VARIABLE_VELOCITYY_TYPE  mascot_local_variable_float
#define MASCOT_LOCAL_VARIABLE_VELOCITYY_ID    9

#define MASCOT_LOCAL_VARIABLE_BORNX_VALUE "mascot.bornx"
#define MASCOT_LOCAL_VARIABLE_BORNX_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_BORNX_ID    10

#define MASCOT_LOCAL_VARIABLE_BORNY_VALUE "mascot.borny"
#define MASCOT_LOCAL_VARIABLE_BORNY_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_BORNY_ID    11

#define MASCOT_LOCAL_VARIABLE_INITIALVELX_VALUE "mascot.initialvx"
#define MASCOT_LOCAL_VARIABLE_INITIALVELX_TYPE  mascot_local_variable_float
#define MASCOT_LOCAL_VARIABLE_INITIALVELX_ID    12

#define MASCOT_LOCAL_VARIABLE_INITIALVELY_VALUE "mascot.initialvy"
#define MASCOT_LOCAL_VARIABLE_INITIALVELY_TYPE  mascot_local_variable_float
#define MASCOT_LOCAL_VARIABLE_INITIALVELY_ID    13

#define MASCOT_LOCAL_VARIABLE_VELOCITYPARAM_VALUE "mascot.velocityparam"
#define MASCOT_LOCAL_VARIABLE_VELOCITYPARAM_TYPE  mascot_local_variable_float
#define MASCOT_LOCAL_VARIABLE_VELOCITYPARAM_ID    14

#define MASCOT_LOCAL_VARIABLE_FOOTX_VALUE "mascot.footx"
#define MASCOT_LOCAL_VARIABLE_FOOTX_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_FOOTX_ID    15

#define MASCOT_LOCAL_VARIABLE_FOOTDX_VALUE "mascot.footdx"
#define MASCOT_LOCAL_VARIABLE_FOOTDX_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_FOOTDX_ID    16

#define MASCOT_LOCAL_VARIABLE_MODX_VALUE "mascot.modx"
#define MASCOT_LOCAL_VARIABLE_MODX_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_MODX_ID    17

#define MASCOT_LOCAL_VARIABLE_MODY_VALUE "mascot.mody"
#define MASCOT_LOCAL_VARIABLE_MODY_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_MODY_ID    18

#define MASCOT_LOCAL_VARIABLE_GAP_VALUE "mascot.gap"
#define MASCOT_LOCAL_VARIABLE_GAP_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_GAP_ID    19

#define MASCOT_LOCAL_VARIABLE_BORNINTERVAL_VALUE "mascot.borninterval"
#define MASCOT_LOCAL_VARIABLE_BORNINTERVAL_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_BORNINTERVAL_ID    20

#define MASCOT_LOCAL_VARIABLE_BORNCOUNT_VALUE "mascot.borncount"
#define MASCOT_LOCAL_VARIABLE_BORNCOUNT_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_BORNCOUNT_ID    21

#define MASCOT_LOCAL_VARIABLE_IEOFFSETX_VALUE "mascot.ieoffsetx"
#define MASCOT_LOCAL_VARIABLE_IEOFFSETX_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_IEOFFSETX_ID    22

#define MASCOT_LOCAL_VARIABLE_IEOFFSETY_VALUE "mascot.ieoffsety"
#define MASCOT_LOCAL_VARIABLE_IEOFFSETY_TYPE  mascot_local_variable_int
#define MASCOT_LOCAL_VARIABLE_IEOFFSETY_ID    23

#define MASCOT_LOCAL_VARIABLE_COUNT 24

struct mascot_behavior;
struct mascot_prototype;

// -----

// One frame of the animation
// Contains sprite and anchor point
// Also contains velocity and duration
struct mascot_pose {
    struct mascot_sprite* sprite[2];
    int32_t anchor_x, anchor_y;
    int32_t velocity_x, velocity_y;
    uint32_t duration; // Default duration of the frame is 1/40 of a second (??? maybe faster)
};

enum mascot_hotspot_cursor {
    mascot_hotspot_cursor_pointer,
    mascot_hotspot_cursor_hand,
    mascot_hotspot_cursor_crosshair,
    mascot_hotspot_cursor_move,
    mascot_hotspot_cursor_text,
    mascot_hotspot_cursor_wait,
    mascot_hotspot_cursor_help,
    mascot_hotspot_cursor_progress,
    mascot_hotspot_cursor_deny
};

enum mascot_hotspot_button {
    mascot_hotspot_button_left,
    mascot_hotspot_button_middle,
    mascot_hotspot_button_right
};

struct mascot_hotspot {
    enum {
        mascot_hotspot_shape_ellipse,
        mascot_hotspot_shape_rectangle
    } shape;
    int32_t x, y;
    int32_t width, height;
    const char* behavior;
    enum mascot_hotspot_cursor cursor;
    enum mascot_hotspot_button button;
};

// Animation is a sequence of frames, possibly with conditions
struct mascot_animation {
    struct mascot_expression* condition;
    struct mascot_pose** frames;
    struct mascot_hotspot** hotspots;
    uint16_t frame_count;
    uint16_t hotspots_count;
};

struct mascot_expression {
    bool evaluate_once:1;
    struct expression_prototype* body;
};

struct mascot_expression_value {
    bool evaluated:1;
    struct mascot_expression* expression_prototype;
    union {
        int32_t i;
        float_t f;
    } value;
    enum mascot_local_variable_kind kind;
};

struct mascot_local_variable {
    bool used;
    struct mascot_expression_value expr;
    enum mascot_local_variable_kind kind;
    union {
        int32_t i;
        float_t f;
    } value;
};

// References another action
struct mascot_action_reference {
    struct mascot_action* action;
    struct mascot_local_variable** overwritten_locals; // Local variables that are overwritten by this action
    struct mascot_expression* duration_limit; // Duration limit of the action
    struct mascot_expression* condition; // Condition of the action
};

enum mascot_action_content_type {
    mascot_action_content_type_none,
    mascot_action_content_type_animation,
    mascot_action_content_type_action_reference,
    mascot_action_content_type_action
};

struct mascot_action_content {
    union {
        struct mascot_animation * animation;
        struct mascot_action_reference * action_reference;
        struct mascot_action* action;
    } value;
    enum mascot_action_content_type kind;
};

// Action is a sequence of animations or other actions
struct mascot_action {
    char* name; // Name of the action
    bool loop; // Loop the action?
    enum mascot_action_type type; // Type of the action
    enum mascot_action_embedded_property embedded_type; // Embedded property of the action
    struct mascot_action_content content[64]; // Content of the action
    uint16_t length; // Length of the content
    enum environment_border_type border_type; // ??? (investigate)
    struct mascot_local_variable** variables; // Local variables that action sets
    const struct mascot_expression* condition; // Condition of the action
    const char* target_behavior; // Target behavior of the action
    const char* select_behavior; // Select behavior of the action
    const char* born_behavior; // Born behavior of the action
    const char* affordance; // Affordance of the action
    const char* transform_target; // prototype to transform to
    const char* born_mascot; // Mascot to be born
    const char* behavior; // Behavior to be set
    bool target_look; // Invert looking direction of target mascot if it matches ours
};

enum mascot_tick_result {
    mascot_tick_ok,
    mascot_tick_next,
    mascot_tick_clone,
    mascot_tick_clone_and_next,
    mascot_tick_dispose,
    mascot_tick_transform,
    mascot_tick_error,
    mascot_tick_reenter,
    mascot_tick_escape
};

// I don't remember what this is
struct mascot_tick_return {
    uint8_t events_count;
    struct {
        enum mascot_tick_result event_type;
        union {
            void* none;
            struct mascot* mascot;
        } event;
    } events[128];
};

struct mascot_behavior_reference {
    const struct mascot_behavior* behavior;
    uint64_t frequency;
    const struct mascot_expression* condition;
};

struct mascot_behavior {
    const char* name; // Name of the behavior

    bool hidden;
    const struct mascot_action* action;
    const struct mascot_expression* condition;
    bool add_behaviors;
    struct mascot_behavior_reference next_behavior_list[128];
    uint16_t next_behaviors_count;
    uint16_t frequency;
    bool is_condition;
};



// Mascot prototype
struct mascot_prototype {
    uint32_t id; // Prototype ID
    const char* name; // Internal name
    const char* display_name; // Display name
    const char* path; // Path to the prototype

    const struct mascot_action** action_definitions; // All defined actions
    const struct mascot_behavior** behavior_definitions; // All defined behaviors
    const struct mascot_local_variable** local_variables_definitions; // All variables that used in the prototype
    const struct mascot_expression** expression_definitions; // All scripts that used in the prototype
    const struct mascot_behavior_reference* root_behavior_list; // Root behavior list
    const struct mascot_atlas* atlas; // Mascot texture atlas

    const struct mascot_behavior* drag_behavior; // Drag behavior
    const struct mascot_behavior* thrown_behavior; // Thrown behavior
    const struct mascot_behavior* fall_behavior; // Fall behavior

    const struct mascot_action* dismiss_action; // Dismiss action (Always have type of dispose)

    uint16_t actions_count, behavior_count,
    local_variables_count, expressions_count, root_behavior_list_count;
    uint16_t reference_count;

    mascot_prototype_store* prototype_store;
};

enum mascot_state {
    mascot_state_none, // No state
    mascot_state_stay, // Stay on the surface
    mascot_state_animate, // Animate
    mascot_state_move, // Move
    mascot_state_fall, // Fall
    mascot_state_interact, // Interact between mascots
    mascot_state_jump, // Jump
    mascot_state_drag, // Drag (mascot is being dragged by the user)
    mascot_state_drag_resist, // Drag resist (mascot is resisting the drag)
    mascot_state_scanmove, // Scanmove (moving to target mascot for interaction)
    mascot_state_scanjump, // Scanjump (jumping to target mascot for interaction)
    mascot_state_ie_fall, // Interact with IE
    mascot_state_ie_walk,
    mascot_state_ie_throw
};

struct mascot_affordance_manager {
    struct mascot** slots;
    uint8_t* slot_state;
    uint32_t slot_count;
    uint32_t occupied_slots_count;
    pthread_mutex_t mutex;
};

struct mascot {
    uint32_t id; // Mascot ID
    const struct mascot_prototype* prototype;

    struct ie_object* associated_ie;

    uint32_t next_frame_tick; // Frame when the next tick should be
    uint32_t action_duration; // Duration of the current action

    bool dragged;
    bool force_pop_next_action;

    bool hotspot_active;
    const struct mascot_hotspot* hotspot;
    const struct mascot_behavior* hotspot_behavior;

    environment_t* environment; // opaque pointer to the environment
    environment_subsurface_t* subsurface; // opaque pointer to the mascot's surface

    enum mascot_state state;

    const char* current_affordance; // Current affordance of the mascot
    struct mascot* target_mascot; // Target mascot of the current action
    struct mascot_affordance_manager* affordance_manager; // Affordance manager of the mascot

    struct mascot_action_reference action_stack[128]; // Action stack
    uint16_t action_index_stack[128]; // Action index stack (don't remember actual usecase)
    uint8_t as_p; // Stack pointers

    struct mascot_behavior_reference behavior_pool[128];
    uint16_t behavior_pool_len;

    struct mascot_action_reference current_action;
    const struct mascot_behavior* current_behavior;
    const struct mascot_animation* current_animation;
    struct mascot_expression_value current_condition;
    uint16_t animation_index, action_index, frame_index;

    struct mascot_local_variable* X, *Y;
    struct mascot_local_variable* TargetX, *TargetY;
    struct mascot_local_variable* Gravity, *LookingRight;
    struct mascot_local_variable* AirDragX, *AirDragY;
    struct mascot_local_variable* VelocityX, *VelocityY;
    struct mascot_local_variable* BornX, *BornY;
    struct mascot_local_variable* InitialVelX, *InitialVelY;
    struct mascot_local_variable* FootX, *FootDX;
    struct mascot_local_variable* VelocityParam;
    struct mascot_local_variable* ModX, *ModY;
    struct mascot_local_variable* Gap;
    struct mascot_local_variable* BornInterval;
    struct mascot_local_variable* BornCount;

    uint32_t action_tick;

    uint32_t dragged_tick;

    uint16_t born_count;
    uint16_t born_tick;

    struct mascot_local_variable local_variables[128];

    // Tick syncronization
    pthread_mutex_t tick_lock;
    uint16_t refcounter;

    // Auxiliary data for the actions
    void* action_data;

};


struct mascot* mascot_new(
    const struct mascot_prototype* prototype, const char* starting_behavior,
    float velx, float vely, uint32_t posx, uint32_t posy, float_t gravity,
    float air_drag_x, float air_drag_y, bool looking_right, environment_t* env
);

// Reference counting
void mascot_unlink(struct mascot* mascot); // When refcounter is 0, mascot is destroyed
void mascot_link(struct mascot* mascot);

// Standard tick routine
enum mascot_tick_result mascot_tick(struct mascot* mascot, uint32_t tick, struct mascot_tick_return* tick_return);

// Behavior management
void mascot_set_behavior(struct mascot* mascot, const struct mascot_behavior* behavior);

// Dragging
bool mascot_drag_started(struct mascot* mascot, environment_pointer_t* pointer);
bool mascot_drag_ended(struct mascot* mascot, bool throw); // throw - if true, sets behavior to thrown

// Callback for position change
bool mascot_moved(struct mascot* mascot, int32_t x, int32_t y);

// Callback for environment change (For example when mascot is moved to another output, WIP)
bool mascot_environment_changed(struct mascot* mascot, environment_t* env);

// Affordance management, used for interaction between mascots
void mascot_attach_affordance_manager(struct mascot* mascot, struct mascot_affordance_manager* manager);
void mascot_detach_affordance_manager(struct mascot* mascot);
void mascot_announce_affordance(struct mascot* mascot, const char* affordance);
struct mascot* mascot_get_target_by_affordance(struct mascot* mascot, const char* affordance);

// Interact with target mascot
bool mascot_interact(struct mascot* mascot, struct mascot* target, const char* affordance, const char* my_behavior, const char* your_behavior);

// Attaches new image as mascot's buffer. Also sets velocity and etc.
void mascot_attach_pose(struct mascot* mascot, const struct mascot_pose* pose, uint32_t tick);
void mascot_reattach_pose(struct mascot* mascot); // Reattaches current pose, mainly used for cases where LookRight is changed

// Hotspot management
bool mascot_hotspot_click(struct mascot* mascot, int32_t x, int32_t y, enum mascot_hotspot_button button);
bool mascot_hotspot_hold(struct mascot* mascot, int32_t x, int32_t y, enum mascot_hotspot_button button, bool release);
struct mascot_hotspot* mascot_hotspot_by_pos(struct mascot* mascot, int32_t x, int32_t y);

// Variable wrappers
int32_t mascot_get_variable_i(struct mascot* mascot, uint16_t id);
float   mascot_get_variable_f(struct mascot* mascot, uint16_t id);
void    mascot_set_variable_i(struct mascot* mascot, uint16_t id, int32_t value);
void    mascot_set_variable_f(struct mascot* mascot, uint16_t id, float value);

// Behavior shortcuts
const struct mascot_behavior* mascot_fall_behavior(struct mascot* mascot);
const struct mascot_behavior* mascot_thrown_behavior(struct mascot* mascot);
const struct mascot_behavior* mascot_prototype_behavior_by_name(const struct mascot_prototype* prototype, const char* name);

// Helper functions
enum mascot_tick_result mascot_execute_variable(struct mascot *mascot, uint16_t variable_id);
enum mascot_tick_result mascot_assign_variable(struct mascot *mascot, uint16_t variable_id, struct mascot_local_variable* variable_data);
enum mascot_tick_result mascot_check_condition(struct mascot *mascot, const struct mascot_expression* condition);
enum mascot_tick_result mascot_recheck_condition(struct mascot *mascot, const struct mascot_expression* condition);
enum mascot_tick_result mascot_out_of_bounds_check(struct mascot* mascot);
enum mascot_tick_result mascot_ground_check(struct mascot* mascot, struct mascot_action_reference* actionref, void (*clean_func)(struct mascot*));
int32_t mascot_screen_y_to_mascot_y(struct mascot* mascot, int32_t screen_y);
bool mascot_is_on_workspace_border(struct mascot* mascot);

#define DIFF_HORIZONTAL_MOVE 1
#define DIFF_VERTICAL_MOVE 2
// If we are using unified mode (all outputs are treated as one), we need to adjust all positions of mascot
void mascot_apply_environment_position_diff(struct mascot* mascot, int32_t dx, int32_t dy, int32_t flags, environment_t* env);

// Set action, not recommended to use directly
enum action_set_result mascot_set_action(struct mascot* mascot, struct mascot_action_reference* actionref, bool push_stack, uint32_t tick);

#endif
