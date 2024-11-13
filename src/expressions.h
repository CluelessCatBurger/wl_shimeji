/*
    expressions.h - wl_shimeji's virtual machine for config conditions and expressions

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

#ifndef EXPRESSION_VM_H
#define EXPRESSION_VM_H

#include "master_header.h"
#include "mascot.h"

struct expression_prototype {
    uint16_t id;
    uint8_t bytecode[512];
    uint16_t bytecode_size;
    uint8_t mascot_vars[128];
    uint8_t mascot_vars_size;
    void* global_getters[128];
    uint8_t global_getters_size;
    void* function_ptrs[128];
    uint8_t function_ptrs_size;
};

struct expression_vm_state {
    float stack[255];
    uint8_t sp;
    uint16_t ip;
    struct mascot* ref_mascot;
    const char* error_message;
};

enum expression_execution_result {
    EXPRESSION_EXECUTION_OK,
    EXPRESSION_EXECUTION_ERROR
};

struct expression_prototype* expression_prototype_new();
void expression_prototype_free(struct expression_prototype* prototype);
bool expression_prototype_load_bytecode(struct expression_prototype* prototype, uint8_t* bytecode, uint16_t bytecode_size);
void expression_prototype_load_mascot_vars(struct expression_prototype* prototype, uint8_t* mascot_vars, uint8_t mascot_vars_size);
void expression_prototype_load_global_getters(struct expression_prototype* prototype, void** global_getters, uint8_t global_getters_size);
void expression_prototype_load_function_ptrs(struct expression_prototype* prototype, void** function_ptrs, uint8_t function_ptrs_size);

enum expression_execution_result expression_vm_execute(struct expression_prototype* prototype, struct mascot* mascot, float* result);

#endif
