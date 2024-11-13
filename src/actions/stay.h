/*
    stay.h - wl_shimeji's stay action implementation

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

#ifndef MASCOT_ACTION_STAY_H
#define MASCOT_ACTION_STAY_H

#include "actionbase.h"

enum mascot_tick_result stay_action_init(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick);
enum mascot_tick_result stay_action_tick(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick);
struct mascot_action_next stay_action_next(struct mascot *mascot, struct mascot_action_reference *actionref, uint32_t tick);

#endif
