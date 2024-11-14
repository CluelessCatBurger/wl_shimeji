/*
    actionbase.h - wl_shimeji's useful action base functions and related definitions

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

#ifndef ACTIONBASE_H
#define ACTIONBASE_H
#include "../mascot.h"
#include <stdint.h>

struct mascot_action_next {
    enum mascot_tick_result status;
    struct mascot_action_reference next_action;
    const struct mascot_animation* next_animation;
    const struct mascot_pose* next_pose;
};

typedef enum mascot_tick_result   (*mascot_action_init)(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick);
typedef struct mascot_action_next (*mascot_action_next)(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick);
typedef enum mascot_tick_result   (*mascot_action_tick)(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick);
typedef void (*mascot_action_clean)(struct mascot *mascot);

enum mascot_tick_result mascot_execute_variable(struct mascot *mascot, uint16_t variable_id);
enum mascot_tick_result mascot_assign_variable(struct mascot *mascot, uint16_t variable_id, struct mascot_local_variable* variable_data);
enum mascot_tick_result mascot_check_condition(struct mascot *mascot, const struct mascot_expression* condition);
enum mascot_tick_result mascot_recheck_condition(struct mascot *mascot, const struct mascot_expression* condition);
enum mascot_tick_result mascot_out_of_bounds_check(struct mascot* mascot);
enum mascot_tick_result mascot_ground_check(struct mascot* mascot, struct mascot_action_reference* actionref, mascot_action_clean clean_func);
int32_t mascot_screen_y_to_mascot_y(struct mascot* mascot, int32_t screen_y);

#endif
