/*
    expressions.c - wl_shimeji's virtual machine for config conditions and expressions

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

#include "expressions.h"
#include "mascot.h"
#include <stdbool.h>
#include <time.h>

#define OP_ERR 0x00
#define OP_RET 0x01

#define OP_LOADL  0x10 // Push mascot variable to stack
#define OP_LOADE  0x11 // Execute global variable getter
#define OP_STORE0 0x12 // Store float value to stack (0th byte)
#define OP_STORE1 0x13 // Store float value to stack (1st byte)
#define OP_STORE2 0x14 // Store float value to stack (2nd byte)
#define OP_STORE3 0x15 // Store float value to stack (3rd byte)

#define OP_ADD 0x20 // Add two from stack
#define OP_SUB 0x21 // Subtract two from stack
#define OP_MUL 0x22 // Multiply two from stack
#define OP_DIV 0x23 // Divide two from stack
#define OP_MOD 0x24 // Modulo two from stack
#define OP_POW 0x25 // Power two from stack

#define OP_AND 0x30 // Bitwise AND two from stack
#define OP_OR 0x31 // Bitwise OR two from stack
#define OP_XOR 0x32 // Bitwise XOR two from stack
#define OP_NOT 0x33 // Bitwise NOT from stack
#define OP_SHL 0x34 // Bitwise shift left from stack
#define OP_SHR 0x35 // Bitwise shift right from stack

#define OP_LT 0x40 // Less than
#define OP_LE 0x41 // Greater than
#define OP_GT 0x42 // Less or equal
#define OP_GE 0x43 // Greater or equal
#define OP_EQ 0x44 // Equal
#define OP_NE 0x45 // Not equal

#define OP_LAND 0x50 // Logical AND
#define OP_LOR 0x51 // Logical OR
#define OP_LNOT 0x52 // Logical NOT

#define OP_BQZ 0x60 // Branch if zero
#define OP_BNZ 0x61 // Branch if not zero
#define OP_JMP 0x62 // Jump by offset

#define OP_CALL 0x70 // Call function

#define OP_PUSH 0x80 // Increment stack pointer

typedef bool (*global_getter)(struct expression_vm_state*);

struct expression_prototype* expression_prototype_new()
{
    struct expression_prototype* prototype = calloc(1, sizeof(struct expression_prototype));
    return prototype;
}

void expression_prototype_free(struct expression_prototype* prototype)
{
    free(prototype);
}

void expression_prototype_load_mascot_vars(struct expression_prototype* prototype, uint8_t* mascot_vars, uint8_t mascot_vars_size)
{
    if (mascot_vars_size > 127) return;
    if (mascot_vars_size == 0) return;
    if (!mascot_vars) return;
    if (!prototype) return;
    memcpy(prototype->mascot_vars, mascot_vars, mascot_vars_size);
    prototype->mascot_vars_size = mascot_vars_size;
}

void expression_prototype_load_global_getters(struct expression_prototype* prototype, void** function_ptrs, uint8_t function_ptrs_size)
{
    if (function_ptrs_size > 127) return;
    if (function_ptrs_size == 0) return;
    if (!function_ptrs) return;
    if (!prototype) return;
    memcpy(prototype->global_getters, function_ptrs, function_ptrs_size*sizeof(void*));
    prototype->global_getters_size = function_ptrs_size;
}

void expression_prototype_load_function_ptrs(struct expression_prototype* prototype, void** function_ptrs, uint8_t function_ptrs_size)
{
    if (function_ptrs_size > 127) return;
    if (function_ptrs_size == 0) return;
    if (!function_ptrs) return;
    if (!prototype) return;
    memcpy(prototype->function_ptrs, function_ptrs, function_ptrs_size*sizeof(void*));
    prototype->function_ptrs_size = function_ptrs_size;
}

bool expression_prototype_load_bytecode(struct expression_prototype* prototype, uint8_t* bytecode, uint16_t bytecode_size)
{
    if (!prototype) return false;
    if (bytecode_size > 1024) return false;
    if (bytecode_size == 0) return false;
    if (!bytecode) return false;

    // Bytecode is encoded as hex string, so we need to convert it to binary
    for (int i = 0; i < bytecode_size; i += 2)
    {
        char hex[3] = {bytecode[i], bytecode[i + 1], 0};
        prototype->bytecode[i / 2] = strtol(hex, NULL, 16);
    }

    prototype->bytecode_size = bytecode_size / 2;

    return true;
}

#define FAIL(message) \
{ \
    state.error_message = message ; \
    goto vmfail; \
}\


enum expression_execution_result expression_vm_execute(struct expression_prototype* prototype, struct mascot* mascot, float* execution_result)
{
    if (!prototype) return EXPRESSION_EXECUTION_ERROR;
    if (!mascot) return EXPRESSION_EXECUTION_ERROR;

    DEBUG("EXECUTING EXPRESSIONS VM: bytecode size = %d, vars_size = %d, globals_size = %d", prototype->bytecode_size, prototype->mascot_vars_size, prototype->global_getters_size);
    DEBUG(",   functions_size = %d, id = %d", prototype->function_ptrs_size, prototype->id);

    // Prepare the VM
    struct expression_vm_state state = {};
    state.stack[0] = 0.0;
    state.sp = 1;
    state.ip = 0;
    state.ref_mascot = mascot;
#ifdef DEBUG
    struct timespec start, end;
    uint16_t opcode_count = 0;
    clock_gettime(CLOCK_MONOTONIC, &start);
#endif
    uint8_t opcode = OP_ERR;
    float freg0 = 0.0, freg1 = 0.0;
    uint8_t immoperand = 0;
    uint8_t offsetr = 0;
    uint8_t indexr = 0;
    global_getter ptrr = NULL;
    // Execute the VM
    while (state.ip < prototype->bytecode_size)
    {
#ifdef DEBUG
        ++opcode_count;
#endif
        opcode = prototype->bytecode[state.ip++];
        immoperand = prototype->bytecode[state.ip++];
        freg0 = 0.0;
        freg1 = 0.0;
        offsetr = 0;
        indexr = 0;
        ptrr = NULL;
        switch (opcode) {
            case OP_ERR:
                *execution_result = 0.0;
                FAIL("Program aborted execution using OP_ERR or unbound jump is occured");
            case OP_RET:
                if (state.sp < 1) FAIL("Stack underflow");
                *execution_result = state.stack[state.sp - 1];
#ifdef DEBUG
                clock_gettime(CLOCK_MONOTONIC, &end);
                long seconds = end.tv_sec - start.tv_sec;
                long ns = end.tv_nsec - start.tv_nsec;
                if (start.tv_nsec > end.tv_nsec) { // clock underflow
                    --seconds;
                    ns += 1000000000;
                }
                DEBUG("OP_RET: Executed %u instructions in %ld.%09ld seconds, execution result: %f", opcode_count, seconds, ns, *execution_result);
#endif
                return EXPRESSION_EXECUTION_OK;

            case OP_LOADL:
                if (state.sp >= 255) FAIL("Stack overflow");
                indexr = immoperand;
                if (indexr > prototype->mascot_vars_size) FAIL("Trying to access out of bounds variable");
                indexr = prototype->mascot_vars[indexr];
                if (indexr > mascot->prototype->local_variables_count) FAIL("Variable points towards non existing local variable");
                struct mascot_local_variable* var = &mascot->local_variables[indexr];

                if (var->kind == mascot_local_variable_int) {
                    state.stack[state.sp] = (float)var->value.i;
                } else if (var->kind == mascot_local_variable_float) {
                    state.stack[state.sp] = var->value.f;
                } else {
                    FAIL("Unknown variable type");
                }
                state.sp++;
                break;
            case OP_LOADE:
                if (state.sp >= 255) FAIL("Stack overflow");
                indexr = immoperand;
                if (indexr > prototype->global_getters_size) FAIL("Trying to access out of bounds global variable");
                ptrr = prototype->global_getters[indexr];
                if (!ptrr) FAIL("Global variable not found");
                if (!ptrr(&state)) goto vmfail;
                break;

            case OP_ADD:
                if (state.sp < 3) FAIL("Stack underflow");
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = freg0 + freg1;
                state.sp--;
                break;
            case OP_SUB:
                if (state.sp < 3) FAIL("Stack underflow");
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = freg0 - freg1;
                state.sp--;

                break;
            case OP_MUL:
                if (state.sp < 3) FAIL("Stack underflow");
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = freg0 * freg1;
                state.sp--;
                break;
            case OP_DIV:
                if (state.sp < 3) FAIL("Stack underflow");
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = freg0 / freg1;
                state.sp--;
                break;
            case OP_MOD:
                if (state.sp < 3) FAIL("Stack underflow");
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = fmodf(freg0, freg1);
                state.sp--;
                break;
            case OP_POW:
                if (state.sp < 3) FAIL("Stack underflow");
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = powf(freg0, freg1);
                state.sp--;
                break;

            case OP_AND:
                if (state.sp < 3) FAIL("Stack underflow");
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = ((int)freg0) & ((int)freg1);
                state.sp--;
                break;
            case OP_OR:
                if (state.sp < 3) FAIL("Stack underflow");
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = ((int)freg0) | ((int)freg1);
                state.sp--;
                break;
            case OP_XOR:
                if (state.sp < 3) FAIL("Stack underflow");
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = ((int)freg0) ^ ((int)freg1);
                state.sp--;
                break;
            case OP_NOT:
                if (state.sp < 2) FAIL("Stack underflow");
                freg0 = state.stack[state.sp - 1];
                state.stack[state.sp - 1] = !((int)freg0);
                break;
            case OP_SHL:
                if (state.sp < 3)
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = ((int)freg0) << ((int)freg1);
                state.sp--;
                break;
            case OP_SHR:
                if (state.sp < 3) FAIL("Stack underflow");
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = ((int)freg0) >> ((int)freg1);
                state.sp--;
                break;

            case OP_LT:
                if (state.sp < 3) FAIL("Stack underflow");
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = freg0 < freg1;
                state.sp--;
                break;
            case OP_LE:
                if (state.sp < 3) FAIL("Stack underflow")
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = freg0 <= freg1;
                state.sp--;
                break;
            case OP_GT:
                if (state.sp < 3) FAIL("Stack underflow")
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = freg0 > freg1;
                state.sp--;
                break;
            case OP_GE:
                if (state.sp < 3) FAIL("Stack underflow")
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = freg0 >= freg1;
                state.sp--;
                break;
            case OP_EQ:
                if (state.sp < 3) FAIL("Stack underflow")
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = freg0 == freg1;
                state.sp--;
                break;
            case OP_NE:
                if (state.sp < 3) FAIL("Stack underflow")
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = freg0 != freg1;
                state.sp--;
                break;

            case OP_LAND:
                if (state.sp < 3) FAIL("Stack underflow")
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = ((int)freg0) && ((int)freg1);
                state.sp--;
                break;
            case OP_LOR:
                if (state.sp < 3) FAIL("Stack underflow")
                freg0 = state.stack[state.sp - 2];
                freg1 = state.stack[state.sp - 1];
                state.stack[state.sp - 2] = ((int)freg0) || ((int)freg1);
                break;
            case OP_LNOT:
                if (state.sp < 2) FAIL("Stack underflow")
                freg0 = state.stack[state.sp - 1];
                state.stack[state.sp - 1] = !((int)freg0);
                break;

            case OP_BQZ:
                if (state.sp < 2) FAIL("Stack underflow")
                freg0 = state.stack[state.sp - 1];
                if (!(int)freg0) {
                    offsetr = immoperand;
                    if (state.ip + offsetr > prototype->bytecode_size) FAIL("Jump beyond bytecode size");
                    state.ip += offsetr;
                    break;
                }
                break;
            case OP_BNZ:
                if (state.sp < 2) FAIL("Stack underflow")
                freg0 = state.stack[state.sp - 1];
                if ((int)freg0) {
                    offsetr = immoperand;
                    if (state.ip + offsetr > prototype->bytecode_size) FAIL("Jump beyond bytecode size");
                    state.ip += offsetr;
                    break;
                }
                break;
            case OP_JMP:
                offsetr = immoperand;
                if (state.ip + offsetr > prototype->bytecode_size) FAIL("Jump beyond bytecode size");
                state.ip += offsetr;
                break;

            case OP_CALL:
                if (state.sp < 1) FAIL("Stack underflow")
                indexr = immoperand;
                if (indexr > prototype->function_ptrs_size) FAIL("Function index out of bounds");
                if (prototype->function_ptrs[indexr] == NULL) FAIL("Function is not found");
                ptrr = prototype->function_ptrs[indexr];
                if (!ptrr(&state)) goto vmfail;
                break;

            case OP_STORE0:
                if (state.sp >= 255) FAIL("Stack overflow");
                *(uint8_t*)(&state.stack[state.sp]) = immoperand;
                break;
            case OP_STORE1:
                if (state.sp >= 255) FAIL("Stack overflow")
                *((uint8_t*)(&state.stack[state.sp])+1) = immoperand;
                break;
            case OP_STORE2:
                if (state.sp >= 255) FAIL("Stack overflow")
                *((uint8_t*)(&state.stack[state.sp])+2) = immoperand;
                break;
            case OP_STORE3:
                if (state.sp >= 255) FAIL("Stack overflow")
                *((uint8_t*)(&state.stack[state.sp])+3) = immoperand;
                break;

            case OP_PUSH:
                if (state.sp >= 255) FAIL("Stack overflow")
                state.sp++;
                break;

        }
    }

    uint32_t mascot_id = 0;
    const char* mascot_name = NULL;
vmfail:

    if (mascot) {
        mascot_id = mascot->id;
        mascot_name = mascot->prototype->name;
    }

    TRACE("<Mascot:%s:%u> VM Execution failed for program with id %i", mascot_name, mascot_id, prototype->id);
    TRACE("-> instruction pointer: %u", state.ip);
    TRACE("-> stack pointer: %u", state.sp);
    TRACE("-> stack:");
    for (int i = 0; i < state.sp; i++) {
        TRACE("    -> %u: %f", i, state.stack[i]);
    }
    TRACE("-> bytecode length: %u", prototype->bytecode_size);
    TRACE("-> opcode: %hhx", opcode);
    TRACE("-> immoperand: %hhx", immoperand);
    TRACE("-> offset register: %u", offsetr);
    TRACE("-> index register: %u", indexr);
    TRACE("-> operand0: %f", freg0);
    TRACE("-> operand1: %f", freg1);
    TRACE("-> function pointer register: %p", ptrr);
    if (state.error_message) {
        TRACE("-> stop reason: %s", state.error_message);
    }
    TRACE("------------[TRACE ENDS HERE]------------");

    return EXPRESSION_EXECUTION_ERROR;
}
