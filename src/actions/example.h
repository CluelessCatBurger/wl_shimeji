/*
    example.h - wl_shimeji's example action implementation

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

#ifndef MASCOT_ACTION_EXAMPLE_H
#define MASCOT_ACTION_EXAMPLE_H

/*
    This is an example of how to implement an action types for the mascot.
*/

// Following include is required for struct mascot_action_next and contains useful functions
// like mascot_execute_variable or mascot_ground_check
#include "actionbase.h"

// After that, following functions can be implemented (if some function is not implemented by the action,
// it then should be replaced in mascot.c with same type of function from another action)
// Detailed explanation of each function is in example.c

enum mascot_tick_result example_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick);
enum mascot_tick_result example_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick);
struct mascot_action_next example_action_next(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick);
void example_action_clean(struct mascot *mascot);

#endif
