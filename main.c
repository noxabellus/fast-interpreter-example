#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <stdalign.h>

#include "stb_ds.h"
#include "stb_ds.c"

#define DEBUG_TRACE 0

#define MAX_REGISTERS UINT8_MAX
#define MAX_BLOCKS UINT8_MAX
#define MAX_CALL_FRAMES 4096
#define STACK_SIZE (1024 * 1024)

#define BITCAST(A, B, v) ((union { A a; B b;}){.a = v}).b

#define I_ENCODE_0(op)            ((Instruction) (op))
#define I_ENCODE_1(op, a)         (I_ENCODE_0(op) | (((Instruction) (a)) << 24))
#define I_ENCODE_2(op, a, b)      (I_ENCODE_1(op, a) | (((Instruction) (b)) << 16))
#define I_ENCODE_3(op, a, b, c)   (I_ENCODE_2(op, a, b) | (((Instruction) (c)) <<  8))
#define I_ENCODE_W0(op, w)        (I_ENCODE_0(op) | (((Instruction) (w)) << 24))
#define I_ENCODE_W1(op, w, a)     (I_ENCODE_W0(op, w) | (((Instruction) (a)) <<  8))
#define I_ENCODE_IM32(T, op, imm) ((BITCAST(T, Instruction, imm) << 32) | (op))

#define I_DECODE_OPCODE(op)       ((OpCode) ((op) & 0xFF))
#define I_DECODE_A(op)            ((uint8_t)   (((op) >> 24) & 0xFF))
#define I_DECODE_B(op)            ((uint8_t)   (((op) >> 16) & 0xFF))
#define I_DECODE_C(op)            ((uint8_t)   (((op) >>  8) & 0xFF))
#define I_DECODE_W0(op)           ((uint16_t)  (((op) >> 24) & 0xFFFF))
#define I_DECODE_W1(op)           ((uint8_t)   (((op) >>  8) & 0xFF))
#define I_DECODE_IM32(T, op)      BITCAST(Instruction, T, ((op) >> 32) & 0xFFFFFFFF)

#define ALIGNMENT_DELTA(base_address, alignment) (((alignment) - ((base_address) % (alignment))) % (alignment))
#define CALC_ARG_SIZE(num_args) (((num_args) + ALIGNMENT_DELTA((num_args), alignof(Instruction))) / alignof(Instruction))

#ifndef __INTELLISENSE__ // intellisense can't handle the backing type attribute
    #define ENUM_T(T) enum : T
#else
    #define ENUM_T(T) enum
#endif

#if DEBUG_TRACE
    #define debug(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#else
    #define debug(fmt, ...)
#endif

typedef uint64_t Instruction;
typedef uint16_t FunctionIndex;
typedef uint16_t GlobalIndex;
typedef uint8_t RegisterIndex;
typedef uint8_t BlockIndex;
typedef uint32_t InstructionPointer;
typedef uint16_t InstructionPointerOffset;
typedef uint32_t GlobalBaseOffset;
typedef uint16_t CallFramePtr;
typedef uint16_t BlockFramePtr;
typedef uint32_t StackPtr;

typedef ENUM_T(uint8_t) {
    HALT,
    UNREACHABLE,
    READ_GLOBAL_32,
    READ_GLOBAL_64,
    COPY_IM_64,
    IF_NZ,
    WHEN_NZ,
    BLOCK,
    BR,
    BR_NZ,
    RE,
    RE_NZ,
    F_ADD_32,
    F_ADD_IM_32,
    F_SUB_32,
    F_SUB_IM_A_32,
    F_SUB_IM_B_32,
    F_ADD_64,
    F_ADD_IM_64,
    F_SUB_64,
    F_SUB_IM_A_64,
    F_SUB_IM_B_64,
    I_ADD_64,
    I_SUB_64,
    F_EQ_32,
    F_EQ_IM_32,
    F_LT_32,
    F_LT_IM_A_32,
    F_LT_IM_B_32,
    F_EQ_64,
    F_EQ_IM_64,
    F_LT_64,
    F_LT_IM_A_64,
    F_LT_IM_B_64,
    S_EQ_64,
    S_EQ_IM_64,
    S_LT_64,
    CALL_V,
    TAIL_CALL_V,
    RET_V,
} OpCode;

typedef ENUM_T(uint8_t) {
    OKAY,
    TRAP_UNREACHABLE,
    TRAP_CALL_OVERFLOW,
    TRAP_STACK_OVERFLOW,
} Trap;

typedef struct {
    InstructionPointer const* blocks;
    Instruction const* instructions;
} Bytecode;

typedef struct {
    RegisterIndex num_args;
    RegisterIndex num_registers;
    Bytecode bytecode;
} Function;

typedef struct {
    Function const* functions;
    uint8_t* const* globals;
} Program;

typedef struct {
    Instruction const* start_pointer;
    Instruction const* instruction_pointer;
    RegisterIndex out_index;
} BlockFrame;

typedef struct {
    Function const* function;
    BlockFrame* root_block;
    uint64_t* stack_base;
} CallFrame;

typedef struct {
    Program const* program;
    CallFrame* call_stack;
    CallFrame* call_stack_max;
    uint64_t* data_stack;
    uint64_t* data_stack_max;
    BlockFrame* block_stack;
} Fiber;

Trap eval(Fiber *restrict fiber) {
    debug("eval");

    CallFrame* current_call_frame;
    Function const* current_function;
    BlockFrame* current_block_frame;

    uint64_t register_scratch_space [MAX_REGISTERS];

    #define SET_CONTEXT() {                              \
        debug("SET_CONTEXT");                      \
        current_call_frame = fiber->call_stack;          \
        current_function = current_call_frame->function; \
        current_block_frame = fiber->block_stack;        \
    }                                                    \

    SET_CONTEXT();

    Instruction last_instruction;

    #define DECODE_NEXT()                                                \
        last_instruction = *(current_block_frame->instruction_pointer++) \
    
    #define DECODE_A()  I_DECODE_A(last_instruction)
    #define DECODE_B()  I_DECODE_B(last_instruction)
    #define DECODE_C()  I_DECODE_C(last_instruction)
    #define DECODE_W0() I_DECODE_W0(last_instruction)
    #define DECODE_W1() I_DECODE_W1(last_instruction)
    #define DECODE_IM64(T) BITCAST(Instruction, T, *(current_block_frame->instruction_pointer++))

    static void* DISPATCH_TABLE [] = {
        &&DO_HALT,
        &&DO_UNREACHABLE,
        &&DO_READ_GLOBAL_32,
        &&DO_READ_GLOBAL_64,
        &&DO_COPY_IM_64,
        &&DO_IF_NZ,
        &&DO_WHEN_NZ,
        &&DO_BLOCK,
        &&DO_BR,
        &&DO_BR_NZ,
        &&DO_RE,
        &&DO_RE_NZ,
        &&DO_F_ADD_32,
        &&DO_F_ADD_IM_32,
        &&DO_F_SUB_32,
        &&DO_F_SUB_IM_A_32,
        &&DO_F_SUB_IM_B_32,
        &&DO_F_ADD_64,
        &&DO_F_ADD_IM_64,
        &&DO_F_SUB_64,
        &&DO_F_SUB_IM_A_64,
        &&DO_F_SUB_IM_B_64,
        &&DO_I_ADD_64,
        &&DO_I_SUB_64,
        &&DO_F_EQ_32,
        &&DO_F_EQ_IM_32,
        &&DO_F_LT_32,
        &&DO_F_LT_IM_A_32,
        &&DO_F_LT_IM_B_32,
        &&DO_F_EQ_64,
        &&DO_F_EQ_IM_64,
        &&DO_F_LT_64,
        &&DO_F_LT_IM_A_64,
        &&DO_F_LT_IM_B_64,
        &&DO_S_EQ_64,
        &&DO_S_EQ_IM_64,
        &&DO_S_LT_64,
        &&DO_CALL_V,
        &&DO_TAIL_CALL_V,
        &&DO_RET_V,
    };

    #define DISPATCH() {                                 \
        DECODE_NEXT();                                   \
        OpCode next = I_DECODE_OPCODE(last_instruction); \
        debug("DISPATCH %d", next);                      \
        goto *DISPATCH_TABLE[next];                      \
    }                                                    \
    
    DISPATCH();

    DO_HALT: {
        debug("HALT");
        return OKAY;
    };

    DO_UNREACHABLE: {
        debug("UNREACHABLE");
        return TRAP_UNREACHABLE;
    };

    DO_READ_GLOBAL_32: {
        debug("READ_GLOBAL_32");

        GlobalIndex index = DECODE_W0();
        RegisterIndex destination = DECODE_W1();

        *(current_call_frame->stack_base + destination) =
            *((uint32_t*) fiber->program->globals[index]);
        
        DISPATCH();
    };

    DO_READ_GLOBAL_64: {
        debug("READ_GLOBAL_64");

        GlobalIndex index = DECODE_W0();
        RegisterIndex destination = DECODE_W1();

        *(current_call_frame->stack_base + destination) =
            *((uint64_t*) fiber->program->globals[index]);
        
        DISPATCH();
    };

    DO_COPY_IM_64: {
        debug("COPY_IM_64");

        uint64_t imm = DECODE_IM64(uint64_t);
        RegisterIndex destination = DECODE_A();

        *(current_call_frame->stack_base + destination) = imm;

        DISPATCH();
    };

    DO_IF_NZ: {
        debug("IF_NZ");

        BlockIndex then_index = DECODE_A();
        BlockIndex else_index = DECODE_B();
        RegisterIndex condition = DECODE_C();

        BlockIndex new_block_index;
        if (*((uint8_t*) (current_call_frame->stack_base + condition)) != 0) {
            new_block_index = then_index;
        } else {
            new_block_index = else_index;
        }

        InstructionPointer new_block = current_function->bytecode.blocks[new_block_index];
        Instruction const* start = current_function->bytecode.instructions + new_block;

        BlockFrame new_block_frame = {start, start, 0};
        *(++fiber->block_stack) = new_block_frame;

        SET_CONTEXT();
        DISPATCH();
    };

    DO_WHEN_NZ: {
        debug("WHEN_NZ");

        BlockIndex new_block_index = DECODE_A();
        RegisterIndex condition = DECODE_B();

        if (*((uint8_t*) (current_call_frame->stack_base + condition)) != 0) {
            InstructionPointer new_block = current_function->bytecode.blocks[new_block_index];

            Instruction const* start = current_function->bytecode.instructions + new_block;

            BlockFrame new_block_frame = {start, start, 0};
            *(++fiber->block_stack) = new_block_frame;

            SET_CONTEXT();
        }

        DISPATCH();
    };

    DO_BLOCK: {
        debug("BLOCK");

        BlockIndex new_block_index = DECODE_A();
        InstructionPointer new_block = current_function->bytecode.blocks[new_block_index];
        Instruction const* start = current_function->bytecode.instructions + new_block;

        BlockFrame new_block_frame = {start, start, 0};
        *(++fiber->block_stack) = new_block_frame;

        SET_CONTEXT();
        DISPATCH();
    };

    DO_BR: {
        debug("BR");

        BlockIndex relative_block_index = DECODE_A();

        fiber->block_stack -= relative_block_index + 1;

        SET_CONTEXT();
        DISPATCH();
    };

    DO_BR_NZ: {
        debug("BR_NZ");

        BlockIndex relative_block_index = DECODE_A();
        RegisterIndex condition = DECODE_B();

        if (*((uint8_t*) (current_call_frame->stack_base + condition)) != 0) {
            fiber->block_stack -= relative_block_index + 1;

            SET_CONTEXT();
        }

        DISPATCH();
    };

    DO_RE: {
        debug("RE");

        BlockIndex relative_block_index = DECODE_A();

        BlockFrame* frame = fiber->block_stack - relative_block_index;
        frame->instruction_pointer = frame->start_pointer;

        SET_CONTEXT();
        DISPATCH();
    };

    DO_RE_NZ: {
        debug("RE_NZ");

        BlockIndex relative_block_index = DECODE_A();
        RegisterIndex condition = DECODE_B();

        if (*((uint8_t*) (current_call_frame->stack_base + condition)) != 0) {
            BlockFrame* frame = fiber->block_stack - relative_block_index;
            frame->instruction_pointer = frame->start_pointer;

            SET_CONTEXT();
        }

        DISPATCH();
    };

    DO_F_ADD_32: {
        debug("F_ADD_32");

        RegisterIndex x = DECODE_A();
        RegisterIndex y = DECODE_B();
        RegisterIndex z = DECODE_C();

        *((float*) (current_call_frame->stack_base + z)) =
            *((float*) (current_call_frame->stack_base + x)) +
            *((float*) (current_call_frame->stack_base + y));

        DISPATCH();
    };

    DO_F_ADD_IM_32: {
        debug("F_ADD_IM_32");

        float x = I_DECODE_IM32(float, last_instruction);
        RegisterIndex y = DECODE_A();
        RegisterIndex z = DECODE_B();

        *((float*) (current_call_frame->stack_base + z)) =
            x + *((float*) (current_call_frame->stack_base + y));

        DISPATCH();
    };

    DO_F_SUB_32: {
        debug("F_SUB_32");

        RegisterIndex x = DECODE_A();
        RegisterIndex y = DECODE_B();
        RegisterIndex z = DECODE_C();

        *((float*) (current_call_frame->stack_base + z)) =
            *((float*) (current_call_frame->stack_base + x)) -
            *((float*) (current_call_frame->stack_base + y));
        
        DISPATCH();
    };

    DO_F_SUB_IM_A_32: {
        debug("F_SUB_IM_A_32");

        float x = I_DECODE_IM32(float, last_instruction);
        RegisterIndex y = DECODE_A();
        RegisterIndex z = DECODE_B();

        *((float*) (current_call_frame->stack_base + z)) =
            x - *((float*) (current_call_frame->stack_base + y));
        
        DISPATCH();
    };

    DO_F_SUB_IM_B_32: {
        debug("F_SUB_IM_B_32");

        RegisterIndex x = DECODE_A();
        float y = I_DECODE_IM32(float, last_instruction);
        RegisterIndex z = DECODE_B();

        *((float*) (current_call_frame->stack_base + z)) =
            *((float*) (current_call_frame->stack_base + x)) - y;
        
        DISPATCH();
    };

    DO_F_ADD_64: {
        debug("F_ADD_64");

        RegisterIndex x = DECODE_A();
        RegisterIndex y = DECODE_B();
        RegisterIndex z = DECODE_C();
        
        *((double*) (current_call_frame->stack_base + z)) =
            *((double*) (current_call_frame->stack_base + x)) +
            *((double*) (current_call_frame->stack_base + y));
            
        DISPATCH();
    };

    DO_F_ADD_IM_64: {
        debug("F_ADD_IM_64");

        double x = DECODE_IM64(double);
        RegisterIndex y = DECODE_A();
        RegisterIndex z = DECODE_B();

        *((double*) (current_call_frame->stack_base + z)) =
            x + *((double*) (current_call_frame->stack_base + y));
        
        DISPATCH();
    };

    DO_F_SUB_64: {
        debug("F_SUB_64");

        RegisterIndex x = DECODE_A();
        RegisterIndex y = DECODE_B();
        RegisterIndex z = DECODE_C();

        *((double*) (current_call_frame->stack_base + z)) =
            *((double*) (current_call_frame->stack_base + x)) -
            *((double*) (current_call_frame->stack_base + y));
        
        DISPATCH();
    };

    DO_F_SUB_IM_A_64: {
        debug("F_SUB_IM_A_64");

        double x = DECODE_IM64(double);
        RegisterIndex y = DECODE_A();
        RegisterIndex z = DECODE_B();

        *((double*) (current_call_frame->stack_base + z)) =
            x - *((double*) (current_call_frame->stack_base + y));
        
        DISPATCH();
    };

    DO_F_SUB_IM_B_64: {
        debug("F_SUB_IM_B_64");

        RegisterIndex x = DECODE_A();
        double y = DECODE_IM64(double);
        RegisterIndex z = DECODE_B();

        *((double*) (current_call_frame->stack_base + z)) =
            *((double*) (current_call_frame->stack_base + x)) - y;

        DISPATCH();
    };

    DO_I_ADD_64: {
        debug("I_ADD_64");

        RegisterIndex x = DECODE_A();
        RegisterIndex y = DECODE_B();
        RegisterIndex z = DECODE_C();

        *(current_call_frame->stack_base + z) =
            *(current_call_frame->stack_base + x) +
            *(current_call_frame->stack_base + y);

        DISPATCH();
    };

    DO_I_SUB_64: {
        debug("I_SUB_64");

        RegisterIndex x = DECODE_A();
        RegisterIndex y = DECODE_B();
        RegisterIndex z = DECODE_C();

        *(current_call_frame->stack_base + z) =
            *(current_call_frame->stack_base + x) -
            *(current_call_frame->stack_base + y);
        
        DISPATCH();
    };

    DO_F_EQ_32: {
        debug("F_EQ_32");

        RegisterIndex x = DECODE_A();
        RegisterIndex y = DECODE_B();
        RegisterIndex z = DECODE_C();

        *((uint8_t*) (current_call_frame->stack_base + z)) =
            *((float*) (current_call_frame->stack_base + x)) ==
            *((float*) (current_call_frame->stack_base + y));

        DISPATCH();
    };

    DO_F_EQ_IM_32: {
        debug("F_EQ_IM_32");

        float x = I_DECODE_IM32(float, last_instruction);
        RegisterIndex y = DECODE_A();
        RegisterIndex z = DECODE_B();

        *((uint8_t*) (current_call_frame->stack_base + z)) =
            x == *((float*) (current_call_frame->stack_base + y));

        DISPATCH();
    };

    DO_F_LT_32: {
        debug("F_LT_32");

        RegisterIndex x = DECODE_A();
        RegisterIndex y = DECODE_B();
        RegisterIndex z = DECODE_C();

        *((uint8_t*) (current_call_frame->stack_base + z)) =
            *((float*) (current_call_frame->stack_base + x)) <
            *((float*) (current_call_frame->stack_base + y));

        DISPATCH();
    };

    DO_F_LT_IM_A_32: {
        debug("F_LT_IM_A_32");

        float x = I_DECODE_IM32(float, last_instruction);
        RegisterIndex y = DECODE_A();
        RegisterIndex z = DECODE_B();

        *((uint8_t*) (current_call_frame->stack_base + z)) =
            x < *((float*) (current_call_frame->stack_base + y));

        DISPATCH();
    };

    DO_F_LT_IM_B_32: {
        debug("F_LT_IM_B_32");

        RegisterIndex x = DECODE_A();
        float y = I_DECODE_IM32(float, last_instruction);
        RegisterIndex z = DECODE_B();

        *((uint8_t*) (current_call_frame->stack_base + z)) =
            *((float*) (current_call_frame->stack_base + x)) < y;

        DISPATCH();
    };

    DO_F_EQ_64: {
        debug("F_EQ_64");

        RegisterIndex x = DECODE_A();
        RegisterIndex y = DECODE_B();
        RegisterIndex z = DECODE_C();

        *((uint8_t*) (current_call_frame->stack_base + z)) =
            *((double*) (current_call_frame->stack_base + x)) ==
            *((double*) (current_call_frame->stack_base + y));

        DISPATCH();
    };

    DO_F_EQ_IM_64: {
        debug("F_EQ_IM_64");

        double x = DECODE_IM64(double);
        RegisterIndex y = DECODE_A();
        RegisterIndex z = DECODE_B();

        *((uint8_t*) (current_call_frame->stack_base + z)) =
            x == *((double*) (current_call_frame->stack_base + y));

        DISPATCH();
    };

    DO_F_LT_64: {
        debug("F_LT_64");

        RegisterIndex x = DECODE_A();
        RegisterIndex y = DECODE_B();
        RegisterIndex z = DECODE_C();

        *((uint8_t*) (current_call_frame->stack_base + z)) =
            *((double*) (current_call_frame->stack_base + x)) <
            *((double*) (current_call_frame->stack_base + y));

        DISPATCH();
    };

    DO_F_LT_IM_A_64: {
        debug("F_LT_IM_A_64");

        double x = DECODE_IM64(double);
        RegisterIndex y = DECODE_A();
        RegisterIndex z = DECODE_B();

        *((uint8_t*) (current_call_frame->stack_base + z)) =
            x < *((double*) (current_call_frame->stack_base + y));

        DISPATCH();
    };

    DO_F_LT_IM_B_64: {
        debug("F_LT_IM_B_64");

        RegisterIndex x = DECODE_A();
        double y = DECODE_IM64(double);
        RegisterIndex z = DECODE_B();

        *((uint8_t*) (current_call_frame->stack_base + z)) =
            *((double*) (current_call_frame->stack_base + x)) < y;

        DISPATCH();
    };

    DO_S_EQ_64: {
        debug("S_EQ_64");

        RegisterIndex x = DECODE_A();
        RegisterIndex y = DECODE_B();
        RegisterIndex z = DECODE_C();

        *((uint8_t*) (current_call_frame->stack_base + z)) =
            *(current_call_frame->stack_base + x) ==
            *(current_call_frame->stack_base + y);

        DISPATCH();
    };

    DO_S_EQ_IM_64: {
        debug("S_EQ_IM_64");

        uint64_t x = DECODE_IM64(uint64_t);
        RegisterIndex y = DECODE_A();
        RegisterIndex z = DECODE_B();

        *((uint8_t*) (current_call_frame->stack_base + z)) =
            x == *(current_call_frame->stack_base + y);

        DISPATCH();
    };

    DO_S_LT_64: {
        debug("S_LT_64");

        RegisterIndex x = DECODE_A();
        RegisterIndex y = DECODE_B();
        RegisterIndex z = DECODE_C();

        *((uint8_t*) (current_call_frame->stack_base + z)) =
            *(current_call_frame->stack_base + x) <
            *(current_call_frame->stack_base + y);

        DISPATCH();
    };

    DO_CALL_V: {
        debug("CALL_V");

        FunctionIndex functionIndex = DECODE_W0();
        RegisterIndex out = DECODE_W1();

        Function const* new_function = fiber->program->functions + functionIndex;

        debug("\t%d %d %d", functionIndex, out, new_function->num_args);

        if ( fiber->call_stack + 1 >= fiber->call_stack_max
           | fiber->data_stack + new_function->num_registers >= fiber->data_stack_max
           ) {
            if (fiber->call_stack + 1 >= fiber->call_stack_max) return TRAP_CALL_OVERFLOW;
            else return TRAP_STACK_OVERFLOW;
        }
        
        uint64_t* new_stack_base = fiber->data_stack;

        RegisterIndex const* args = (RegisterIndex const*) (current_block_frame->instruction_pointer);
        current_block_frame->instruction_pointer += CALC_ARG_SIZE(new_function->num_args);

        for (RegisterIndex i = 0; i < new_function->num_args; i++) {
            *(new_stack_base + i) =
                *(current_call_frame->stack_base + args[i]);
        }

        Instruction const* start = new_function->bytecode.instructions + *new_function->bytecode.blocks;

        BlockFrame new_block_frame = {start, start, out};
        *(++fiber->block_stack) = new_block_frame;

        CallFrame new_call_frame = {new_function, fiber->block_stack, new_stack_base};
        *(++fiber->call_stack) = new_call_frame;

        fiber->data_stack += new_function->num_registers;

        SET_CONTEXT();
        DISPATCH();
    };

    DO_TAIL_CALL_V: {
        debug("TAIL_CALL_V");

        FunctionIndex functionIndex = DECODE_W0();

        Function const* new_function = fiber->program->functions + functionIndex;

        debug("\t%d %d %d", functionIndex, current_call_frame->root_block->out_index, new_function->num_args);

        int16_t register_delta = ((int16_t) current_function->num_registers) - ((int16_t) new_function->num_registers);

        if ( register_delta < 0
           & fiber->data_stack + new_function->num_registers - current_function->num_registers >= fiber->data_stack_max
           ) {
            return TRAP_STACK_OVERFLOW;
        }

        RegisterIndex const* args = (RegisterIndex const*) (current_block_frame->instruction_pointer);
        current_block_frame->instruction_pointer += CALC_ARG_SIZE(new_function->num_args);

        for (RegisterIndex i = 0; i < new_function->num_args; i++) {
            register_scratch_space[i] =
                *(current_call_frame->stack_base + args[i]);
        }

        uint64_t* new_stack_base = current_call_frame->stack_base;

        for (RegisterIndex i = 0; i < new_function->num_registers; i++) {
            *(new_stack_base + i) = register_scratch_space[i];
        }

        Instruction const* start = new_function->bytecode.instructions + *new_function->bytecode.blocks;

        fiber->block_stack = current_call_frame->root_block;
        fiber->block_stack->start_pointer = start;
        fiber->block_stack->instruction_pointer = start;

        current_call_frame->function = new_function;
        fiber->data_stack -= register_delta;

        SET_CONTEXT();
        DISPATCH();
    };

    DO_RET_V: {
        debug("RET_V");

        RegisterIndex y = DECODE_A();

        BlockFrame* root_block = current_call_frame->root_block;
        CallFrame* caller_frame = fiber->call_stack - 1;

        *(caller_frame->stack_base + root_block->out_index) =
            *(current_call_frame->stack_base + y);

        fiber->call_stack--;
        fiber->block_stack = current_call_frame->root_block - 1;
        fiber->data_stack = current_call_frame->stack_base;

        SET_CONTEXT();
        DISPATCH();
    };
}

Trap invoke(Fiber *restrict fiber, FunctionIndex functionIndex, uint64_t* ret_val, uint64_t* args) {
    debug("invoke");

    Function const* function = fiber->program->functions + functionIndex;

    if ( fiber->call_stack + 2 >= fiber->call_stack_max
       | fiber->data_stack + function->num_registers + 1 >= fiber->data_stack_max
       ) {
        return TRAP_STACK_OVERFLOW;
    }
    
    InstructionPointer wrapper_blocks[1] = { 0 };
    Instruction wrapper_instructions[] = { I_ENCODE_0(HALT) };
    Bytecode wrapper_bytecode = {wrapper_blocks, wrapper_instructions};

    Function wrapper = {0, 1, wrapper_bytecode};

    BlockFrame wrapper_block_frame = {wrapper_instructions, wrapper_instructions, 0};
    *(++fiber->block_stack) = wrapper_block_frame;

    CallFrame wrapper_call_frame = {&wrapper, fiber->block_stack, fiber->data_stack};
    *(++fiber->call_stack) = wrapper_call_frame;

    fiber->data_stack += 1;

    Instruction const* start = function->bytecode.instructions + *function->bytecode.blocks;

    BlockFrame block_frame = {start, start, 0};
    *(++fiber->block_stack) = block_frame;
    
    CallFrame call_frame = {function, fiber->block_stack, fiber->data_stack};
    *(++fiber->call_stack) = call_frame;

    fiber->data_stack += function->num_registers;

    for (uint8_t i = 0; i < function->num_args; i++) {
        *(call_frame.stack_base + i) = args[i];
    }

    Trap result = eval(fiber);

    if (result == OKAY) {
        *ret_val = *((uint64_t*) (wrapper_call_frame.stack_base));
        fiber->call_stack--;
        fiber->block_stack--;
        fiber->data_stack -= 1;
    }

    return result;
}

char const* trap_name(Trap trap) {
    switch (trap) {
        case OKAY: return "OKAY";
        case TRAP_UNREACHABLE: return "UNREACHABLE";
        case TRAP_CALL_OVERFLOW: return "CALL_OVERFLOW";
        case TRAP_STACK_OVERFLOW: return "STACK_OVERFLOW";
        default: return "INVALID";
    }
}

char const* opcode_name(OpCode op) {
    switch (op) {
        case HALT: return "HALT";
        case UNREACHABLE: return "UNREACHABLE";
        case READ_GLOBAL_32: return "READ_GLOBAL_32";
        case READ_GLOBAL_64: return "READ_GLOBAL_64";
        case COPY_IM_64: return "COPY_IM_64";
        case IF_NZ: return "IF_NZ";
        case WHEN_NZ: return "WHEN_NZ";
        case BLOCK: return "BLOCK";
        case BR: return "BR";
        case BR_NZ: return "BR_NZ";
        case RE: return "RE";
        case RE_NZ: return "RE_NZ";
        case F_ADD_32: return "F_ADD_32";
        case F_ADD_IM_32: return "F_ADD_IM_32";
        case F_SUB_32: return "F_SUB_32";
        case F_SUB_IM_A_32: return "F_SUB_IM_A_32";
        case F_SUB_IM_B_32: return "F_SUB_IM_B_32";
        case F_ADD_64: return "F_ADD_64";
        case F_ADD_IM_64: return "F_ADD_IM_64";
        case F_SUB_64: return "F_SUB_64";
        case F_SUB_IM_A_64: return "F_SUB_IM_A_64";
        case F_SUB_IM_B_64: return "F_SUB_IM_B_64";
        case I_ADD_64: return "I_ADD_64";
        case I_SUB_64: return "I_SUB_64";
        case F_EQ_32: return "F_EQ_32";
        case F_EQ_IM_32: return "F_EQ_IM_32";
        case F_LT_32: return "F_LT_32";
        case F_LT_IM_A_32: return "F_LT_IM_A_32";
        case F_LT_IM_B_32: return "F_LT_IM_B_32";
        case F_EQ_64: return "F_EQ_64";
        case F_EQ_IM_64: return "F_EQ_IM_64";
        case F_LT_64: return "F_LT_64";
        case F_LT_IM_A_64: return "F_LT_IM_A_64";
        case F_LT_IM_B_64: return "F_LT_IM_B_64";
        case S_EQ_64: return "S_EQ_64";
        case S_EQ_IM_64: return "S_EQ_IM_64";
        case S_LT_64: return "S_LT_64";
        case CALL_V: return "CALL_V";
        case TAIL_CALL_V: return "TAIL_CALL_V";
        case RET_V: return "RET_V";
        default: return "INVALID";
    }
}

typedef stbds_arr(uint8_t) Encoder;

InstructionPointer encode_instr (Encoder* encoder, Instruction instr) {
    uint8_t* bytes = (uint8_t*) &instr;
    InstructionPointer offset = stbds_arrlenu(*encoder) / sizeof(Instruction);
    for (size_t i = 0; i < sizeof(Instruction); i++) stbds_arrpush(*encoder, bytes[i]);
    return offset;
}

InstructionPointer encode_0 (Encoder* encoder, OpCode opcode) {
    debug("encode_0 %s", opcode_name(opcode));
    Instruction e = I_ENCODE_0(opcode);
    debug("\t%s", opcode_name(I_DECODE_OPCODE(e)));
    return encode_instr(encoder, e);
}

InstructionPointer encode_1 (Encoder* encoder, OpCode opcode, uint8_t a) {
    debug("encode_1 %s %d", opcode_name(opcode), a);
    Instruction e = I_ENCODE_1(opcode, a);
    debug("\t%s %d", opcode_name(I_DECODE_OPCODE(e)), I_DECODE_A(e));
    return encode_instr(encoder, e);
}

InstructionPointer encode_2 (Encoder* encoder, OpCode opcode, uint8_t a, uint8_t b) {
    debug("encode_2 %s %d %d", opcode_name(opcode), a, b);
    Instruction e = I_ENCODE_2(opcode, a, b);
    debug("\t%s %d %d", opcode_name(I_DECODE_OPCODE(e)), I_DECODE_A(e), I_DECODE_B(e));
    return encode_instr(encoder, e);
}

InstructionPointer encode_3 (Encoder* encoder, OpCode opcode, uint8_t a, uint8_t b, uint8_t c) {
    debug("encode_3 %s %d %d %d", opcode_name(opcode), a, b, c);
    Instruction e = I_ENCODE_3(opcode, a, b, c);
    debug("\t%s %d %d %d", opcode_name(I_DECODE_OPCODE(e)), I_DECODE_A(e), I_DECODE_B(e), I_DECODE_C(e));
    return encode_instr(encoder, e);
}

InstructionPointer encode_w0 (Encoder* encoder, OpCode opcode, uint16_t w) {
    debug("encode_w0 %s %d", opcode_name(opcode), w);
    Instruction e = I_ENCODE_W0(opcode, w);
    debug("\t%s %d", opcode_name(I_DECODE_OPCODE(e)), I_DECODE_W0(e));
    return encode_instr(encoder, e);
}

InstructionPointer encode_w1 (Encoder* encoder, OpCode opcode, uint16_t w, uint8_t a) {
    debug("encode_w1 %s %d %d", opcode_name(opcode), w, a);
    Instruction e = I_ENCODE_W1(opcode, w, a);
    debug("\t%s %d %d", opcode_name(I_DECODE_OPCODE(e)), I_DECODE_W0(e), I_DECODE_W1(e));
    return encode_instr(encoder, e);
}

InstructionPointer encode_0_im (Encoder* encoder, OpCode opcode, uint32_t im) {
    debug("encode_0_im %s %u", opcode_name(opcode), im);
    Instruction e = I_ENCODE_IM32(uint32_t, I_ENCODE_0(opcode), im);
    debug("\t%s %u", opcode_name(I_DECODE_OPCODE(e)), I_DECODE_IM32(uint32_t, e));
    return encode_instr(encoder, e);
}

InstructionPointer encode_1_im (Encoder* encoder, OpCode opcode, uint32_t im, uint8_t a) {
    debug("encode_1_im %s %d %u", opcode_name(opcode), a, im);
    Instruction e = I_ENCODE_IM32(uint32_t, I_ENCODE_1(opcode, a), im);
    debug("\t%s %d %u", opcode_name(I_DECODE_OPCODE(e)), I_DECODE_A(e), I_DECODE_IM32(uint32_t, e));
    return encode_instr(encoder, e);
}

InstructionPointer encode_2_im (Encoder* encoder, OpCode opcode, uint32_t im, uint8_t a, uint8_t b) {
    debug("encode_2_im %s %d %d %u", opcode_name(opcode), a, b, im);
    Instruction e = I_ENCODE_IM32(uint32_t, I_ENCODE_2(opcode, a, b), im);
    debug("\t%s %d %d %u", opcode_name(I_DECODE_OPCODE(e)), I_DECODE_A(e), I_DECODE_B(e), I_DECODE_IM32(uint32_t, e));
    return encode_instr(encoder, e);
}

InstructionPointer encode_3_im (Encoder* encoder, OpCode opcode, uint32_t im, uint8_t a, uint8_t b, uint8_t c) {
    debug("encode_3_im %s %d %d %d %u", opcode_name(opcode), a, b, c, im);
    Instruction e = I_ENCODE_IM32(uint32_t, I_ENCODE_3(opcode, a, b, c), im);
    debug("\t%s %d %d %d %u", opcode_name(I_DECODE_OPCODE(e)), I_DECODE_A(e), I_DECODE_B(e), I_DECODE_C(e), I_DECODE_IM32(uint32_t, e));
    return encode_instr(encoder, e);
}

InstructionPointer encode_w0_im (Encoder* encoder, OpCode opcode, uint32_t im, uint16_t w) {
    debug("encode_w0_im %s %d %u", opcode_name(opcode), w, im);
    Instruction e = I_ENCODE_IM32(uint32_t, I_ENCODE_W0(opcode, w), im);
    debug("\t%s %d %u", opcode_name(I_DECODE_OPCODE(e)), I_DECODE_W0(e), I_DECODE_IM32(uint32_t, e));
    return encode_instr(encoder, e);
}

InstructionPointer encode_w1_im (Encoder* encoder, OpCode opcode, uint32_t im, uint16_t w, uint8_t a) {
    debug("encode_w1_im %s %d %d %u", opcode_name(opcode), w, a, im);
    Instruction e = I_ENCODE_IM32(uint32_t, I_ENCODE_W1(opcode, w, a), im);
    debug("\t%s %d %d %u", opcode_name(I_DECODE_OPCODE(e)), I_DECODE_W0(e), I_DECODE_W1(e), I_DECODE_IM32(uint32_t, e));
    return encode_instr(encoder, e);
}

InstructionPointer encode_im64 (Encoder* encoder, uint64_t im) {
    debug("encode_im64 %lu", im);
    Instruction e = BITCAST(uint64_t, Instruction, im);
    return encode_instr(encoder, e);
}

void encode_registers (Encoder* encoder, RegisterIndex num_registers, RegisterIndex* indices) {
    for (size_t i = 0; i < num_registers; i++) stbds_arrpush(*encoder, indices[i]);
    size_t padding = ALIGNMENT_DELTA(num_registers, alignof(Instruction));
    debug("encoded %d registers:", num_registers);
    for (size_t i = 0; i < num_registers; i++) debug("\tr%d", indices[i]);
    debug("adding %lu padding", padding);
    for (size_t i = 0; i < padding; i++) stbds_arrpush(*encoder, 0);
}

void disas(Function const* functions, InstructionPointer const* blocks, Instruction const* instructions) {
    BlockIndex to_disas [MAX_BLOCKS] = {};
    BlockIndex num_blocks = 0;
    #define DISAS_BLOCK(block) (to_disas[num_blocks++] = block)
    DISAS_BLOCK(0);

    while (num_blocks > 0) {
        BlockIndex block_index = to_disas[--num_blocks];
        InstructionPointer block = blocks[block_index];

        printf("[b%d : i%d @%ld]:\n", block_index, block, block * sizeof(Instruction));

        InstructionPointerOffset ip = 0;

        while (true) {
            Instruction instr = instructions[block + ip];
            OpCode opcode = I_DECODE_OPCODE(instr);
            printf("\ti%d @%lx\t\t%x:%s", (block + ip), (block + ip) * sizeof(Instruction), opcode, opcode_name(opcode));

            ip++;

            bool block_done = false;

            switch (opcode) {
                case HALT:
                case UNREACHABLE: {
                    block_done = true;
                } break;

                case READ_GLOBAL_32: {
                    GlobalIndex index = I_DECODE_W0(instr);
                    RegisterIndex destination = I_DECODE_W1(instr);
                    printf(" g%d r%d", index, destination);
                } break;

                case READ_GLOBAL_64: {
                    GlobalIndex index = I_DECODE_W0(instr);
                    RegisterIndex destination = I_DECODE_W1(instr);
                    printf(" g%d r%d", index, destination);
                } break;

                case COPY_IM_64: {
                    uint64_t imm = BITCAST(Instruction, uint64_t, instructions[block + ip++]);
                    RegisterIndex destination = I_DECODE_A(instr);
                    printf(" %lu r%d", imm, destination);
                } break;

                case IF_NZ: {
                    BlockIndex then_index = I_DECODE_A(instr);
                    BlockIndex else_index = I_DECODE_B(instr);
                    RegisterIndex condition = I_DECODE_C(instr);
                    printf(" b%d b%d r%d", then_index, else_index, condition);
                    DISAS_BLOCK(else_index);
                    DISAS_BLOCK(then_index);
                    block_done = true;
                } break;

                case WHEN_NZ: {
                    BlockIndex new_block_index = I_DECODE_A(instr);
                    RegisterIndex condition = I_DECODE_B(instr);
                    printf(" b%d r%d", new_block_index, condition);
                    DISAS_BLOCK(new_block_index);
                } break;

                case BLOCK: {
                    BlockIndex new_block_index = I_DECODE_A(instr);
                    printf(" b%d", new_block_index);
                    DISAS_BLOCK(new_block_index);
                } break;

                case BR: {
                    BlockIndex relative_block_index = I_DECODE_A(instr);
                    printf(" b%d", relative_block_index);
                    block_done = true;
                } break;

                case BR_NZ: {
                    BlockIndex relative_block_index = I_DECODE_A(instr);
                    RegisterIndex condition = I_DECODE_B(instr);
                    printf(" b%d r%d", relative_block_index, condition);
                } break;

                case RE: {
                    BlockIndex relative_block_index = I_DECODE_A(instr);
                    printf(" b%d", relative_block_index);
                    block_done = true;
                } break;

                case RE_NZ: {
                    BlockIndex relative_block_index = I_DECODE_A(instr);
                    RegisterIndex condition = I_DECODE_B(instr);
                    printf(" b%d r%d", relative_block_index, condition);
                } break;

                case F_ADD_32: {
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex y = I_DECODE_B(instr);
                    RegisterIndex z = I_DECODE_C(instr);
                    printf(" r%d r%d r%d", x, y, z);
                } break;

                case F_ADD_IM_32: {
                    float x = I_DECODE_IM32(float, instr);
                    RegisterIndex y = I_DECODE_A(instr);
                    RegisterIndex z = I_DECODE_B(instr);
                    printf(" %f r%d r%d", x, y, z);
                } break;

                case F_SUB_32: {
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex y = I_DECODE_B(instr);
                    RegisterIndex z = I_DECODE_C(instr);
                    printf(" r%d r%d r%d", x, y, z);
                } break;

                case F_SUB_IM_A_32: {
                    float x = I_DECODE_IM32(float, instr);
                    RegisterIndex y = I_DECODE_A(instr);
                    RegisterIndex z = I_DECODE_B(instr);
                    printf(" %f r%d r%d", x, y, z);
                } break;

                case F_SUB_IM_B_32: {
                    float y = I_DECODE_IM32(float, instr);
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex z = I_DECODE_B(instr);
                    printf(" %f r%d r%d", y, x, z);
                } break;

                case F_ADD_64: {
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex y = I_DECODE_B(instr);
                    RegisterIndex z = I_DECODE_C(instr);
                    printf(" r%d r%d r%d", x, y, z);
                } break;

                case F_ADD_IM_64: {
                    double x = BITCAST(Instruction, double, instructions[block + ip++]);
                    RegisterIndex y = I_DECODE_A(instr);
                    RegisterIndex z = I_DECODE_B(instr);
                    printf(" %f r%d r%d", x, y, z);
                } break;

                case F_SUB_64: {
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex y = I_DECODE_B(instr);
                    RegisterIndex z = I_DECODE_C(instr);
                    printf(" r%d r%d r%d", x, y, z);
                } break;

                case F_SUB_IM_A_64: {
                    RegisterIndex y = I_DECODE_A(instr);
                    RegisterIndex z = I_DECODE_B(instr);
                    double x = BITCAST(Instruction, double, instructions[block + ip++]);
                    printf(" %f r%d r%d", x, y, z);
                } break;

                case F_SUB_IM_B_64: {
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex z = I_DECODE_B(instr);
                    double y = BITCAST(Instruction, double, instructions[block + ip++]);
                    printf(" %f r%d r%d", y, x, z);
                } break;

                case I_ADD_64: {
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex y = I_DECODE_B(instr);
                    RegisterIndex z = I_DECODE_C(instr);
                    printf(" r%d r%d r%d", x, y, z);
                } break;

                case I_SUB_64: {
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex y = I_DECODE_B(instr);
                    RegisterIndex z = I_DECODE_C(instr);
                    printf(" r%d r%d r%d", x, y, z);
                } break;

                case F_EQ_32: {
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex y = I_DECODE_B(instr);
                    RegisterIndex z = I_DECODE_C(instr);
                    printf(" r%d r%d r%d", x, y, z);
                } break;

                case F_EQ_IM_32: {
                    float x = I_DECODE_IM32(float, instr);
                    RegisterIndex y = I_DECODE_A(instr);
                    RegisterIndex z = I_DECODE_B(instr);
                    printf(" %f r%d r%d", x, y, z);
                } break;

                case F_LT_32: {
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex y = I_DECODE_B(instr);
                    RegisterIndex z = I_DECODE_C(instr);
                    printf(" r%d r%d r%d", x, y, z);
                } break;

                case F_LT_IM_A_32: {
                    float x = I_DECODE_IM32(float, instr);
                    RegisterIndex y = I_DECODE_A(instr);
                    RegisterIndex z = I_DECODE_B(instr);
                    printf(" %f r%d r%d", x, y, z);
                } break;

                case F_LT_IM_B_32: {
                    float y = I_DECODE_IM32(float, instr);
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex z = I_DECODE_B(instr);
                    printf(" %f r%d r%d", y, x, z);
                } break;

                case F_EQ_64: {
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex y = I_DECODE_B(instr);
                    RegisterIndex z = I_DECODE_C(instr);
                    printf(" r%d r%d r%d", x, y, z);
                } break;

                case F_EQ_IM_64: {
                    RegisterIndex y = I_DECODE_A(instr);
                    RegisterIndex z = I_DECODE_B(instr);
                    double x = BITCAST(Instruction, double, instructions[block + ip++]);
                    printf(" %f r%d r%d", x, y, z);
                } break;

                case F_LT_64: {
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex y = I_DECODE_B(instr);
                    RegisterIndex z = I_DECODE_C(instr);
                    printf(" r%d r%d r%d", x, y, z);
                } break;

                case F_LT_IM_A_64: {
                    RegisterIndex y = I_DECODE_A(instr);
                    RegisterIndex z = I_DECODE_B(instr);
                    double x = BITCAST(Instruction, double, instructions[block + ip++]);
                    printf(" %f r%d r%d", x, y, z);
                } break;

                case F_LT_IM_B_64: {
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex z = I_DECODE_B(instr);
                    double y = BITCAST(Instruction, double, instructions[block + ip++]);
                    printf(" %f r%d r%d", y, x, z);
                } break;

                case S_EQ_64: {
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex y = I_DECODE_B(instr);
                    RegisterIndex z = I_DECODE_C(instr);
                    printf(" r%d r%d r%d", x, y, z);
                } break;

                case S_EQ_IM_64: {
                    uint64_t x = BITCAST(Instruction, uint64_t, instructions[block + ip++]);
                    RegisterIndex y = I_DECODE_A(instr);
                    RegisterIndex z = I_DECODE_B(instr);
                    printf(" %lu r%d r%d", x, y, z);
                } break;

                case S_LT_64: {
                    RegisterIndex x = I_DECODE_A(instr);
                    RegisterIndex y = I_DECODE_B(instr);
                    RegisterIndex z = I_DECODE_C(instr);
                    printf(" r%d r%d r%d", x, y, z);
                } break;

                case CALL_V: {
                    FunctionIndex functionIndex = I_DECODE_W0(instr);
                    RegisterIndex out = I_DECODE_W1(instr);
                    printf(" f%d r%d", functionIndex, out);
                    Function const* function = functions + functionIndex;
                    RegisterIndex const* args = (RegisterIndex const*) (instructions + block + ip);
                    InstructionPointerOffset offset = CALC_ARG_SIZE(function->num_args);
                    ip += offset;
                    printf(" (%d~%d : ", function->num_args, offset);
                    for (uint8_t i = 0; i < function->num_args; i++) {
                        printf("r%d", args[i]);
                        if (i < function->num_args - 1) printf(", ");
                    }
                    printf(")");
                } break;

                case TAIL_CALL_V: {
                    FunctionIndex functionIndex = I_DECODE_W0(instr);
                    printf(" f%d", functionIndex);
                    Function const* function = functions + functionIndex;
                    RegisterIndex const* args = (RegisterIndex const*) (instructions + block + ip);
                    InstructionPointerOffset offset = CALC_ARG_SIZE(function->num_args);
                    ip += offset;
                    printf(" (%d~%d : ", function->num_args, offset);
                    for (uint8_t i = 0; i < function->num_args; i++) {
                        printf("r%d", args[i]);
                        if (i < function->num_args - 1) printf(", ");
                    }
                    printf(")");
                    block_done = true;
                } break;

                case RET_V: {
                    RegisterIndex y = I_DECODE_A(instr);
                    printf(" r%d", y);
                    block_done = true;
                } break;

                default:
                    printf(" ???");
                    block_done = true;
                    break;
            }

            printf("\n");

            if (block_done) break;
        }
    }
}


double ackermann(double m, double n) {
    if (m == 0.0) return n + 1.0;
    if (n == 0.0) return ackermann(m - 1.0, 1.0);
    return ackermann(m - 1.0, ackermann(m, n - 1.0));
}

static double loop_count = 10.0;

double loop_ackermann(double m, double n) {
    double i = 0.0;
    double a = 0.0;
    
    while (i != loop_count) {
        a = a + ackermann(m, n);
        i += 1;
    }

    return a;
}

int main (int argc, char** argv) {
    stbds_arr(Function) functions = NULL;

    FunctionIndex ack = (FunctionIndex) stbds_arrlenu(functions);
    {
        stbds_arr(InstructionPointer) blocks = NULL;
        Encoder instructions = NULL;

        uint64_t zero = BITCAST(double, uint64_t, 0.0);
        uint64_t one = BITCAST(double, uint64_t, 1.0);
        uint64_t two = BITCAST(double, uint64_t, 2.0);

        RegisterIndex m = 0;
        RegisterIndex n = 1;

        RegisterIndex cond = 2;
        RegisterIndex m_minus_1 = 2;
        RegisterIndex n_minus_1 = 3;

        InstructionPointer entry_block =
            // m == 0
            encode_2(&instructions, F_EQ_IM_64, m, cond);
            encode_im64(&instructions, zero);
            encode_2(&instructions, WHEN_NZ, 1, cond);
            // n == 0
            encode_2(&instructions, F_EQ_IM_64, n, cond);
            encode_im64(&instructions, zero);
            encode_2(&instructions, WHEN_NZ, 2, cond);

        // fallthrough case
            // m - 1
            encode_2(&instructions, F_SUB_IM_B_64, m, m_minus_1);
            encode_im64(&instructions, one);
            // n - 1
            encode_2(&instructions, F_SUB_IM_B_64, n, n_minus_1);
            encode_im64(&instructions, one);

            encode_w1(&instructions, CALL_V, ack, n_minus_1);
            encode_registers(&instructions, 2, (RegisterIndex[]){m, n_minus_1});

            encode_w0(&instructions, TAIL_CALL_V, ack);
            encode_registers(&instructions, 2, (RegisterIndex[]){m_minus_1, n_minus_1});

        stbds_arrpush(blocks, entry_block);

        InstructionPointer m_eql_0 =
            encode_2(&instructions, F_ADD_IM_64, n, n);
            encode_im64(&instructions, one);
            encode_1(&instructions, RET_V, n);

        stbds_arrpush(blocks, m_eql_0);

        InstructionPointer n_eql_0 =
            encode_2(&instructions, F_SUB_IM_B_64, m, m);
            encode_im64(&instructions, one);
            encode_1(&instructions, COPY_IM_64, n);
            encode_im64(&instructions, one);
            encode_w0(&instructions, TAIL_CALL_V, ack);
            encode_registers(&instructions, 2, (RegisterIndex[]){m, n});

        stbds_arrpush(blocks, n_eql_0);

        Bytecode bytecode = {blocks, (Instruction const*) instructions};

        Function function = {2, 4, bytecode};
        stbds_arrpush(functions, function);

        #if DEBUG_TRACE
            disas(&function, blocks, (Instruction const*) instructions);
        #endif
    }

    FunctionIndex loop_ack = (FunctionIndex) stbds_arrlenu(functions);
    {
        stbds_arr(InstructionPointer) blocks = NULL;
        Encoder instructions = NULL;

        uint64_t zero = BITCAST(double, uint64_t, 0.0);
        uint64_t one = BITCAST(double, uint64_t, 1.0);
        uint64_t lc = BITCAST(double, uint64_t, loop_count);

        RegisterIndex m = 0;
        RegisterIndex n = 1;
        RegisterIndex i = 2;
        RegisterIndex a = 3;
        RegisterIndex b = 4;
        RegisterIndex cond = 4;

        InstructionPointer entry_block =
            encode_1(&instructions, COPY_IM_64, i);
            encode_im64(&instructions, zero);
            encode_1(&instructions, COPY_IM_64, a);
            encode_im64(&instructions, zero);

            encode_1(&instructions, BLOCK, 1);

            encode_1(&instructions, RET_V, a);

        stbds_arrpush(blocks, entry_block);
        
        InstructionPointer loop_block =
            encode_2(&instructions, F_EQ_IM_64, i, cond);
            encode_im64(&instructions, lc);
            encode_2(&instructions, BR_NZ, 0, cond);

            encode_w1(&instructions, CALL_V, ack, b);
            encode_registers(&instructions, 2, (RegisterIndex[]){m, n});
            encode_3(&instructions, F_ADD_64, a, b, a);

            encode_2(&instructions, F_ADD_IM_64, i, i);
            encode_im64(&instructions, one);

            encode_1(&instructions, RE, 0);
        
        stbds_arrpush(blocks, loop_block);

        Bytecode bytecode = {blocks, (Instruction const*) instructions};

        Function function = {2, 5, bytecode};
        stbds_arrpush(functions, function);

        #if DEBUG_TRACE
            disas(&function, blocks, (Instruction const*) instructions);
        #endif
    }

    Program program = {
        .functions = functions,
        .globals = NULL,
    };

    uint64_t* data_stack = malloc(STACK_SIZE);
    CallFrame* call_stack = malloc(sizeof(CallFrame) * MAX_CALL_FRAMES);
    BlockFrame* block_stack = malloc(sizeof(BlockFrame) * MAX_CALL_FRAMES * MAX_BLOCKS);

    Fiber fiber = {
        .program = &program,
        .call_stack = call_stack,
        .call_stack_max = call_stack + MAX_CALL_FRAMES,
        .block_stack = block_stack,
        .data_stack = data_stack,
        .data_stack_max = data_stack + STACK_SIZE,
    };

    uint64_t ret_val = 0xdeadbeef;
    double m = 3.0;
    double n = 8.0;
    uint64_t args [2] = {BITCAST(double, uint64_t, m), BITCAST(double, uint64_t, n)};
    double expected = loop_ackermann(m, n);

    clock_t start = clock();
    Trap result = invoke(&fiber, loop_ack, &ret_val, args);
    clock_t end = clock();

    double elapsed = (((double) (end - start)) / ((double) CLOCKS_PER_SEC));

    if (result == OKAY) {
        double res = BITCAST(uint64_t, double, ret_val);
        printf("Result: %f (in %fs) [expected %f]\n", res, elapsed, expected);
        if (res != expected) {
            return 1;
        }
    } else {
        printf("Trap: %s\n", trap_name(result));
        return 2;
    }

    return 0;
}
