/*
    global_symbols.h - wl_shimeji's expressions VM global symbols definitions

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

#ifndef GLOBAL_SYMS_H
#define GLOBAL_SYMS_H

#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>

#include "environment.h"
#include "mascot.h"
#include "expressions.h"
#include "physics.h"
#include "plugins.h"

bool mascot_noop(struct expression_vm_state* state);

// Math functions from js

bool math_e(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp++] = 2.718281828459045;
    return true;
}
#define GLOBAL_SYM_MATH_E { "math.e", math_e }

bool math_ln10(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp++] = 2.302585092994046;
    return true;
}
#define GLOBAL_SYM_MATH_LN10 { "math.ln10", math_ln10 }

bool math_ln2(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp++] = 0.6931471805599453;
    return true;
}
#define GLOBAL_SYM_MATH_LN2 { "math.ln2", math_ln2 }

bool math_log2e(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp++] = 1.4426950408889634;
    return true;
}
#define GLOBAL_SYM_MATH_LOG2E { "math.log2e", math_log2e }

bool math_log10e(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp++] = 0.4342944819032518;
    return true;
}
#define GLOBAL_SYM_MATH_LOG10E { "math.log10e", math_log10e }

bool math_pi(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp++] = 3.141592653589793;
    return true;
}
#define GLOBAL_SYM_MATH_PI { "math.pi", math_pi }

bool math_sqrt1_2(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp++] = 0.7071067811865476;
    return true;
}
#define GLOBAL_SYM_MATH_SQRT1_2 { "math.sqrt1_2", math_sqrt1_2 }

bool math_sqrt2(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp++] = 1.4142135623730951;
    return true;
}
#define GLOBAL_SYM_MATH_SQRT2 { "math.sqrt2", math_sqrt2 }

// Functions

bool math_abs(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = fabs(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_ABS { "math.abs", math_abs }

bool math_acos(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = acos(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_ACOS { "math.acos", math_acos }

bool math_acosh(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = acosh(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_ACOSH { "math.acosh", math_acosh }

bool math_asin(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = asin(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_ASIN { "math.asin", math_asin }

bool math_asinh(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = asinh(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_ASINH { "math.asinh", math_asinh }

bool math_atan(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = atan(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_ATAN { "math.atan", math_atan }

bool math_atanh(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = atanh(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_ATANH { "math.atanh", math_atanh }

bool math_cbrt(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = cbrt(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_CBRT { "math.cbrt", math_cbrt }

bool math_ceil(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = ceil(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_CEIL { "math.ceil", math_ceil }

bool math_clz32(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = __builtin_clz((int)state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_CLZ32 { "math.clz32", math_clz32 }

bool math_cos(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = cos(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_COS { "math.cos", math_cos }

bool math_cosh(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = cosh(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_COSH { "math.cosh", math_cosh }

bool math_exp(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = exp(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_EXP { "math.exp", math_exp }

bool math_expm1(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = expm1(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_EXPM1 { "math.expm1", math_expm1 }

bool math_floor(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = floor(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_FLOOR { "math.floor", math_floor }

bool math_fround(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    float f = state->stack[state->sp - 1];
    state->stack[state->sp - 1] = f;
    return true;
}
#define FUNC_MATH_FROUND { "math.fround", math_fround }

bool math_log(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = log(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_LOG { "math.log", math_log }

bool math_log1p(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = log1p(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_LOG1P { "math.log1p", math_log1p }

bool math_log2(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = log2(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_LOG2 { "math.log2", math_log2 }

bool math_log10(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = log10(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_LOG10 { "math.log10", math_log10 }

bool math_max(struct expression_vm_state* state)
{
    // Check underflow and overflow
    if (state->sp < 2) return false;
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 2] = fmax(state->stack[state->sp - 2], state->stack[state->sp - 1]);
    state->sp--;
    return true;
}
#define FUNC_MATH_MAX { "math.max", math_max }

bool math_min(struct expression_vm_state* state)
{
    // Check underflow and overflow
    if (state->sp < 2) return false;
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 2] = fmin(state->stack[state->sp - 2], state->stack[state->sp - 1]);
    state->sp--;
    return true;
}
#define FUNC_MATH_MIN { "math.min", math_min }

bool math_pow(struct expression_vm_state* state)
{
    // Check underflow and overflow
    if (state->sp < 2) return false;
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 2] = pow(state->stack[state->sp - 2], state->stack[state->sp - 1]);
    state->sp--;
    return true;
}
#define FUNC_MATH_POW { "math.pow", math_pow }

bool math_random(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp] = drand48();
    state->sp++;
    return true;
}
#define FUNC_MATH_RANDOM { "math.random", math_random }

bool math_round(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = round(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_ROUND { "math.round", math_round }

bool math_sign(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    float f = state->stack[state->sp - 1];
    state->stack[state->sp - 1] = f > 0 ? 1 : f < 0 ? -1 : 0;
    return true;
}
#define FUNC_MATH_SIGN { "math.sign", math_sign }

bool math_sin(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = sin(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_SIN { "math.sin", math_sin }

bool math_sinh(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = sinh(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_SINH { "math.sinh", math_sinh }

bool math_sqrt(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = sqrt(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_SQRT { "math.sqrt", math_sqrt }

bool math_tan(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = tan(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_TAN { "math.tan", math_tan }

bool math_tanh(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = tanh(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_TANH { "math.tanh", math_tanh }

bool math_trunc(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp - 1] = trunc(state->stack[state->sp - 1]);
    return true;
}
#define FUNC_MATH_TRUNC { "math.trunc", math_trunc }

// Our global syms

bool mascot_anchor(struct expression_vm_state* state)
{
    if (state->sp + 2 >= 255) return false;
    state->stack[state->sp] = state->ref_mascot->X->value.i;
    state->stack[state->sp + 1] = environment_screen_height(state->ref_mascot->environment) - state->ref_mascot->Y->value.i;
    state->sp += 2;
    return true;
}
#define GLOBAL_SYM_MASCOT_ANCHOR { "mascot.anchor", mascot_anchor }

bool mascot_anchor_x(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp++] = state->ref_mascot->X->value.i;
    return true;
}
#define GLOBAL_SYM_MASCOT_ANCHOR_X { "mascot.anchor.x", mascot_anchor_x }

bool mascot_anchor_y(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp++] = environment_screen_height(state->ref_mascot->environment) - state->ref_mascot->Y->value.i;
    return true;
}
#define GLOBAL_SYM_MASCOT_ANCHOR_Y { "mascot.anchor.y", mascot_anchor_y }

// Environment syms
bool mascot_environment_cursor_x(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp] = environment_cursor_x(state->ref_mascot, state->ref_mascot->environment);
    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_CURSOR_X { "mascot.environment.cursor.x", mascot_environment_cursor_x }

bool mascot_environment_cursor_y(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp] = (int32_t)environment_cursor_y(state->ref_mascot, state->ref_mascot->environment);
    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_CURSOR_Y { "mascot.environment.cursor.y", mascot_environment_cursor_y }

bool mascot_environment_cursor_dx(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp] = (int32_t)environment_cursor_dx(state->ref_mascot, state->ref_mascot->environment);
    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_CURSOR_DX { "mascot.environment.cursor.dx", mascot_environment_cursor_dx }

bool mascot_environment_cursor_dy(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp] = -(int32_t)environment_cursor_dy(state->ref_mascot, state->ref_mascot->environment);
    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_CURSOR_DY { "mascot.environment.cursor.dy", mascot_environment_cursor_dy }

bool mascot_environment_screen_width(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    int32_t alignment = BORDER_TYPE(check_collision_at(environment_local_geometry(state->ref_mascot->environment), state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i, 0));
    state->stack[state->sp] = environment_workarea_width_aligned(state->ref_mascot->environment, alignment);
    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_SCREEN_WIDTH { "mascot.environment.screen.width", mascot_environment_screen_width }

bool mascot_environment_screen_height(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;

    int32_t alignment = BORDER_TYPE(check_collision_at(environment_local_geometry(state->ref_mascot->environment), state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i, 0));
    state->stack[state->sp] = environment_workarea_height_aligned(state->ref_mascot->environment, alignment);
    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_SCREEN_HEIGHT { "mascot.environment.screen.height", mascot_environment_screen_height }

bool mascot_environment_work_area_width(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    int32_t alignment = BORDER_TYPE(check_collision_at(environment_local_geometry(state->ref_mascot->environment), state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i, 0));
    state->stack[state->sp] = environment_workarea_width_aligned(state->ref_mascot->environment, alignment);
    state->sp++;
    INFO("Work area width: %d", state->stack[state->sp - 1]);
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_WORK_AREA_WIDTH { "mascot.environment.workarea.width", mascot_environment_work_area_width }

bool mascot_environment_work_area_height(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    int32_t alignment = BORDER_TYPE(check_collision_at(environment_local_geometry(state->ref_mascot->environment), state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i, 0));
    state->stack[state->sp] = environment_workarea_height_aligned(state->ref_mascot->environment, alignment) - 128;
    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_WORK_AREA_HEIGHT { "mascot.environment.workarea.height", mascot_environment_work_area_height }

bool mascot_environment_work_area_left(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    int32_t alignment = BORDER_TYPE(check_collision_at(environment_local_geometry(state->ref_mascot->environment), state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i, 0));
    state->stack[state->sp] = environment_workarea_coordinate_aligned(state->ref_mascot->environment, BORDER_TYPE_LEFT, alignment);
    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_WORK_AREA_LEFT { "mascot.environment.workarea.left", mascot_environment_work_area_left }

bool mascot_environment_work_area_top(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    // state->stack[state->sp] = environment_workarea_height(state->ref_mascot->environment)-128;
    int32_t alignment = BORDER_TYPE(check_collision_at(environment_local_geometry(state->ref_mascot->environment), state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i, 0));
    state->stack[state->sp] = environment_workarea_coordinate_aligned(state->ref_mascot->environment, BORDER_TYPE_CEILING, alignment);
    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_WORK_AREA_TOP { "mascot.environment.workarea.top", mascot_environment_work_area_top }

bool mascot_environment_work_area_right(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    int32_t alignment = BORDER_TYPE(check_collision_at(environment_local_geometry(state->ref_mascot->environment), state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i, 0));
    state->stack[state->sp] = environment_workarea_coordinate_aligned(state->ref_mascot->environment, BORDER_TYPE_RIGHT, alignment);
    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_WORK_AREA_RIGHT { "mascot.environment.workarea.right", mascot_environment_work_area_right }

bool mascot_environment_work_area_bottom(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    int32_t alignment = BORDER_TYPE(check_collision_at(environment_local_geometry(state->ref_mascot->environment), state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i, 0));
    state->stack[state->sp] = environment_workarea_coordinate_aligned(state->ref_mascot->environment, BORDER_TYPE_FLOOR, alignment);
    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_WORK_AREA_BOTTOM { "mascot.environment.workarea.bottom", mascot_environment_work_area_bottom }

bool mascot_environment_floor_ison(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    enum environment_border_type border = environment_get_border_type(state->ref_mascot->environment, state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i);
    state->stack[state->sp-2] = border == environment_border_type_floor;
    state->sp--;
    return true;
}
#define FUNC_MASCOT_ENVIRONMENT_FLOOR_ISON { "mascot.environment.floor.ison", mascot_environment_floor_ison }

bool mascot_environment_ceiling_ison(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    enum environment_border_type border = environment_get_border_type(state->ref_mascot->environment, state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i);
    state->stack[state->sp-2] = border == environment_border_type_ceiling;
    state->sp--;
    return true;
}
#define FUNC_MASCOT_ENVIRONMENT_CEILING_ISON { "mascot.environment.ceiling.ison", mascot_environment_ceiling_ison }

bool mascot_environment_wall_ison(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    enum environment_border_type border = environment_get_border_type(state->ref_mascot->environment, state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i);
    state->stack[state->sp-2] = border == environment_border_type_wall;
    state->sp--;
    return true;
}
#define FUNC_MASCOT_ENVIRONMENT_WALL_ISON { "mascot.environment.wall.ison", mascot_environment_wall_ison }

bool mascot_environment_left_ison(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    enum environment_border_type border = environment_get_border_type(state->ref_mascot->environment, state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i);
    struct ie_object* ie = environment_get_ie(state->ref_mascot->environment);
    if (border == environment_border_type_wall) {
        int anchor_x = state->stack[state->sp-2];
        state->stack[state->sp-2] = anchor_x == (int32_t)environment_workarea_left(state->ref_mascot->environment) || anchor_x == ie->x;
    } else state->stack[state->sp-2] = 0;
    state->sp--;
    return true;
}
#define FUNC_MASCOT_ENVIRONMENT_LEFT_ISON { "mascot.environment.left.ison", mascot_environment_left_ison }

bool mascot_environment_right_ison(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    enum environment_border_type border = environment_get_border_type(state->ref_mascot->environment, state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i);
    struct ie_object* ie = environment_get_ie(state->ref_mascot->environment);
    if (border == environment_border_type_wall) {
        int anchor_x = state->stack[state->sp-2];
        state->stack[state->sp-2] = anchor_x == (int32_t)environment_workarea_right(state->ref_mascot->environment) || anchor_x == ie->x+ie->width;
    } else state->stack[state->sp-2] = 0;
    state->sp--;
    return true;
}
#define FUNC_MASCOT_ENVIRONMENT_RIGHT_ISON { "mascot.environment.right.ison", mascot_environment_right_ison }

bool mascot_environment_work_area_left_border_ison(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    enum environment_border_type border = environment_get_border_type(state->ref_mascot->environment, state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i);
    if (border == environment_border_type_wall && state->ref_mascot->X->value.i == (int32_t)environment_workarea_left(state->ref_mascot->environment)) {
        int anchor_x = state->stack[state->sp-2];
        state->stack[state->sp-2] = anchor_x == (int32_t)environment_workarea_left(state->ref_mascot->environment);
    } else {
        state->stack[state->sp-2] = 0;
    }
    state->sp--;
    return true;
}
#define FUNC_MASCOT_ENVIRONMENT_WORK_AREA_LEFT_BORDER_ISON { "mascot.environment.workarea.leftborder.ison", mascot_environment_work_area_left_border_ison }

bool mascot_environment_work_area_right_border_ison(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    enum environment_border_type border = environment_get_border_type(state->ref_mascot->environment, state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i);
    if (border == environment_border_type_wall && state->ref_mascot->X->value.i == (int32_t)environment_workarea_right(state->ref_mascot->environment)) {
        int anchor_x = state->stack[state->sp-2];
        state->stack[state->sp-2] = anchor_x == (int32_t)environment_workarea_right(state->ref_mascot->environment);
    } else {
        state->stack[state->sp-2] = 0;
    }
    state->sp--;
    return true;
}
#define FUNC_MASCOT_ENVIRONMENT_WORK_AREA_RIGHT_BORDER_ISON { "mascot.environment.workarea.rightborder.ison", mascot_environment_work_area_right_border_ison }

bool mascot_environment_work_area_top_border_ison(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    enum environment_border_type border = environment_get_border_type(state->ref_mascot->environment, state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i);
    if (border == environment_border_type_ceiling) {
        int anchor_y = state->stack[state->sp-1];
        state->stack[state->sp-2] = anchor_y == (int32_t)environment_workarea_top(state->ref_mascot->environment);
    } else {
        state->stack[state->sp-2] = 0;
    }
    state->sp--;
    return true;
}
#define FUNC_MASCOT_ENVIRONMENT_WORK_AREA_CEILING_BORDER_ISON { "mascot.environment.workarea.topborder.ison", mascot_environment_work_area_top_border_ison }

bool mascot_environment_work_area_bottom_border_ison(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    enum environment_border_type border = environment_get_border_type(state->ref_mascot->environment, state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i);
    if (border == environment_border_type_floor) {
        int anchor_y = state->stack[state->sp-1];
        state->stack[state->sp-2] = anchor_y == (int32_t)environment_workarea_bottom(state->ref_mascot->environment);
    } else {
        state->stack[state->sp-2] = 0;
    }
    state->sp--;
    return true;
}
#define FUNC_MASCOT_ENVIRONMENT_WORK_AREA_FLOOR_BORDER_ISON { "mascot.environment.workarea.bottomborder.ison", mascot_environment_work_area_bottom_border_ison }

bool mascot_environment_active_ie_right(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;

    struct ie_object* ie = environment_get_ie(state->ref_mascot->environment);
    if (ie) {
        if (ie->active) {
            state->stack[state->sp] = ie->x + ie->width;
        } else {
            state->stack[state->sp] = 0;
        }
    } else {
        state->stack[state->sp] = 0;
    }

    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_ACTIVE_IE_RIGHT { "mascot.environment.activeie.right", mascot_environment_active_ie_right }

bool mascot_environment_active_ie_left(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;

    struct ie_object* ie = environment_get_ie(state->ref_mascot->environment);
    if (ie) {
        if (ie->active) {
            state->stack[state->sp] = ie->x;
        } else {
            state->stack[state->sp] = 0;
        }
    } else {
        state->stack[state->sp] = 0;
    }

    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_ACTIVE_IE_LEFT { "mascot.environment.activeie.left", mascot_environment_active_ie_left }

bool mascot_environment_active_ie_top(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;

    struct ie_object* ie = environment_get_ie(state->ref_mascot->environment);
    if (ie) {
        if (ie->active) {
            state->stack[state->sp] = ie->y;
        } else {
            state->stack[state->sp] = 0;
        }
    } else {
        state->stack[state->sp] = 0;
    }

    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_ACTIVE_IE_TOP { "mascot.environment.activeie.top", mascot_environment_active_ie_top }

bool mascot_environment_active_ie_bottom(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;

    struct ie_object* ie = environment_get_ie(state->ref_mascot->environment);
    if (ie) {
        if (ie->active) {
            state->stack[state->sp] = (ie->y + ie->height);
        } else {
            state->stack[state->sp] = 0;
        }
    } else {
        state->stack[state->sp] = 0;
    }

    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_ACTIVE_IE_BOTTOM { "mascot.environment.activeie.bottom", mascot_environment_active_ie_bottom }

bool mascot_environment_active_ie_width(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;

    struct ie_object* ie = environment_get_ie(state->ref_mascot->environment);
    if (ie) {
        if (ie->active) {
            state->stack[state->sp] = ie->width;
        } else {
            state->stack[state->sp] = 0;
        }
    } else {
        state->stack[state->sp] = 0;
    }

    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_ACTIVE_IE_WIDTH { "mascot.environment.activeie.width", mascot_environment_active_ie_width }

bool mascot_environment_active_ie_height(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;

    struct ie_object* ie = environment_get_ie(state->ref_mascot->environment);
    if (ie) {
        if (ie->active) {
            state->stack[state->sp] = ie->height;
        } else {
            state->stack[state->sp] = 0;
        }
    } else {
        state->stack[state->sp] = 0;
    }

    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_ACTIVE_IE_HEIGHT { "mascot.environment.activeie.height", mascot_environment_active_ie_height }

bool mascot_environment_active_ie_visible(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;

    struct ie_object* ie = environment_get_ie(state->ref_mascot->environment);
    if (ie) {
        state->stack[state->sp] = ie->active;
    } else {
        state->stack[state->sp] = 0;
    }

    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_ENVIRONMENT_ACTIVE_IE_VISIBLE { "mascot.environment.activeie.visible", mascot_environment_active_ie_visible }

bool mascot_environment_active_ie_top_border_ison(struct expression_vm_state* state)
{
    if (state->sp - 2 <= 0) return false;

    struct ie_object* ie = environment_get_ie(state->ref_mascot->environment);
    enum environment_border_type border = environment_get_border_type(state->ref_mascot->environment, state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i);

    if (ie) {
        if (border == environment_border_type_floor) {
            int32_t anchor_y = state->stack[state->sp-1];
            state->stack[state->sp-2] = (ie->y == anchor_y) && ie->y + ie->width != 0;
        }
        else state->stack[state->sp-2] = 0;
    } else {
        state->stack[state->sp-2] = 0;
    }

    state->sp--;
    return true;
}
#define FUNC_MASCOT_ENVIRONMENT_ACTIVE_IE_TOP_BORDER_ISON { "mascot.environment.activeie.topborder.ison", mascot_environment_active_ie_top_border_ison }

bool mascot_environment_active_ie_bottom_border_ison(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;

    struct ie_object* ie = environment_get_ie(state->ref_mascot->environment);
    enum environment_border_type border = (int32_t)environment_get_border_type(state->ref_mascot->environment, state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i);

    if (ie) {
        if (border == environment_border_type_ceiling) {
            int anchor_y = state->stack[state->sp-1];
            state->stack[state->sp-2] = (ie->y + ie->height == anchor_y) && ie->y + ie->height != (int32_t)environment_screen_height(state->ref_mascot->environment);
        }
        else state->stack[state->sp-2] = 0;
    } else {
        state->stack[state->sp-2] = 0;
    }

    state->sp--;

    return true;
}
#define FUNC_MASCOT_ENVIRONMENT_ACTIVE_IE_BOTTOM_BORDER_ISON { "mascot.environment.activeie.bottomborder.ison", mascot_environment_active_ie_bottom_border_ison }

bool mascot_environment_active_ie_left_border_ison(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;

    struct ie_object* ie = environment_get_ie(state->ref_mascot->environment);
    enum environment_border_type border = environment_get_border_type(state->ref_mascot->environment, state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i);

    if (ie) {
        if (border == environment_border_type_wall) {
            int anchor_x = state->stack[state->sp-2];
            state->stack[state->sp-2] = (ie->x == anchor_x) && ie->x != 0;
        }
        else state->stack[state->sp-2] = 0;
    } else {
        state->stack[state->sp-2] = 0;
    }

    state->sp--;
    return true;
}
#define FUNC_MASCOT_ENVIRONMENT_ACTIVE_IE_LEFT_BORDER_ISON { "mascot.environment.activeie.leftborder.ison", mascot_environment_active_ie_left_border_ison }

bool mascot_environment_active_ie_right_border_ison(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;

    struct ie_object* ie = environment_get_ie(state->ref_mascot->environment);
    enum environment_border_type border = environment_get_border_type(state->ref_mascot->environment, state->ref_mascot->X->value.i, state->ref_mascot->Y->value.i);

    if (ie) {
        if (border == environment_border_type_wall) {
            int anchor_x = state->stack[state->sp-2];
            state->stack[state->sp-2] = (ie->x + ie->width == anchor_x) && ie->x + ie->width != (int32_t)environment_workarea_right(state->ref_mascot->environment);
        }
        else state->stack[state->sp-2] = 0;
    } else {
        state->stack[state->sp-2] = 0;
    }

    state->sp--;

    return true;
}
#define FUNC_MASCOT_ENVIRONMENT_ACTIVE_IE_RIGHT_BORDER_ISON { "mascot.environment.activeie.rightborder.ison", mascot_environment_active_ie_right_border_ison }

bool mascot_environment_active_ie_border_ison(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;

    mascot_environment_active_ie_top_border_ison(state);
    mascot_environment_active_ie_bottom_border_ison(state);
    mascot_environment_active_ie_left_border_ison(state);
    mascot_environment_active_ie_right_border_ison(state);

    state->stack[state->sp-4] = state->stack[state->sp-4] || state->stack[state->sp-3] || state->stack[state->sp-2] || state->stack[state->sp-1];

    state->sp -= 3;
    return true;
}
#define FUNC_MASCOT_ENVIRONMENT_ACTIVE_IE_BORDER_ISON { "mascot.environment.activeie.border.ison", mascot_environment_active_ie_border_ison }

bool mascot_count(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp] = state->ref_mascot->prototype->reference_count-1;
    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_COUNT { "mascot.count", mascot_count }

bool mascot_count_total(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp] = mascot_total_count;
    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_COUNT_TOTAL { "mascot.totalCount", mascot_count_total }

bool mascot_noop(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    state->stack[state->sp] = 0;
    state->sp++;
    return true;
}
#define GLOBAL_SYM_MASCOT_NOOP { "fallback", mascot_noop }

bool target_anchor(struct expression_vm_state* state)
{
    if (state->sp + 2 >= 255) return false;

    int32_t diff_x = 0, diff_y = 0;
    if (state->ref_mascot->environment != state->ref_mascot->target_mascot->environment) {
        environment_global_coordinates_delta(state->ref_mascot->environment, state->ref_mascot->target_mascot->environment, &diff_x, &diff_y);
    }

    if (state->ref_mascot->target_mascot) {
        state->stack[state->sp] = state->ref_mascot->target_mascot->X->value.i + diff_x;
        state->sp++;
        state->stack[state->sp] = environment_screen_height(state->ref_mascot->environment) - state->ref_mascot->target_mascot->Y->value.i + diff_y;
        state->sp++;
    } else {
        state->stack[state->sp] = 0.0;
        state->sp++;
        state->stack[state->sp] = 0.0;
        state->sp++;
    }
    return true;
}
#define GLOBAL_SYM_TARGET_ANCHOR { "target.anchor", target_anchor }

bool target_anchor_x(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;

    int32_t diff_x = 0, diff_y = 0;
    if (state->ref_mascot->environment != state->ref_mascot->target_mascot->environment) {
        environment_global_coordinates_delta(state->ref_mascot->environment, state->ref_mascot->target_mascot->environment, &diff_x, &diff_y);
    }

    if (state->ref_mascot->target_mascot) {
        state->stack[state->sp++] = state->ref_mascot->target_mascot->X->value.i + diff_x;
    } else {
        state->stack[state->sp++] = 0.0;
    }
    return true;
}
#define GLOBAL_SYM_TARGET_ANCHOR_X { "target.anchor.x", target_anchor_x }

bool target_anchor_y(struct expression_vm_state* state)
{
    if (state->sp + 1 >= 255) return false;
    int32_t diff_x = 0, diff_y = 0;
    if (state->ref_mascot->environment != state->ref_mascot->target_mascot->environment) {
        environment_global_coordinates_delta(state->ref_mascot->environment, state->ref_mascot->target_mascot->environment, &diff_x, &diff_y);
    }
    if (state->ref_mascot->target_mascot) {
        state->stack[state->sp++] = environment_screen_height(state->ref_mascot->environment) - state->ref_mascot->target_mascot->Y->value.i + diff_y;
    } else {
        state->stack[state->sp++] = 0.0;
    }
    return true;
}
#define GLOBAL_SYM_TARGET_ANCHOR_Y { "target.anchor.y", target_anchor_y }


#endif
