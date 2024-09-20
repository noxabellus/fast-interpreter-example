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

#define BB_DEBUG_TRACE 0

#ifndef __INTELLISENSE__ // intellisense can't handle the backing type attribute
    #define ENUM_T(T) enum : T
#else
    #define ENUM_T(T) enum
#endif

typedef uint64_t BB_Instruction;
typedef uint16_t BB_FunctionIndex;
typedef uint16_t BB_GlobalIndex;
typedef uint8_t BB_RegisterIndex;
typedef uint8_t BB_BlockIndex;
typedef uint32_t BB_InstructionPointer;
typedef uint16_t BB_InstructionPointerOffset;
typedef uint32_t BB_GlobalBaseOffset;
typedef uint16_t BB_CallFramePtr;
typedef uint16_t BB_BlockFramePtr;
typedef uint32_t BB_StackPtr;


#define BB_MAX_REGISTERS UINT8_MAX
#define BB_MAX_BLOCKS UINT8_MAX
#define BB_MAX_CALL_FRAMES 1024
#define BB_STACK_SIZE (1024 * 1024)


typedef ENUM_T(uint8_t) {
    BB_HALT,           // 8(c) = 8
    BB_UNREACHABLE,    // 8(c) = 8
    BB_READ_GLOBAL_64, // 8(c) + 16(G) + 8(I) = 32
    BB_IF_NZ,          // 8(c) + 8(B) + 8(B) + 8(I) = 32
    BB_I_ADD_64,       // 8(c) + 8(I) + 8(I) + 8(I) = 32
    BB_I_SUB_64,       // 8(c) + 8(I) + 8(I) + 8(I) = 32
    BB_S_LT_64,        // 8(c) + 8(I) + 8(I) + 8(I) = 32
    BB_CALL_V,         // 8(c) + 8(I) = 16
    BB_RET_V,          // 8(c) + 8(I) = 16
} BB_OpCode;

#define BB_I_ENCODE_0(op)          ((BB_Instruction) op)
#define BB_I_ENCODE_1(op, a)       (BB_I_ENCODE_0(op) | (((BB_Instruction) a) << 24))
#define BB_I_ENCODE_2(op, a, b)    (BB_I_ENCODE_1(op, a) | (((BB_Instruction) b) << 16))
#define BB_I_ENCODE_3(op, a, b, c) (BB_I_ENCODE_2(op, a, b) | (((BB_Instruction) c) <<  8))
#define BB_I_ENCODE_W0(op, w)      (BB_I_ENCODE_0(op) | (((BB_Instruction) w) << 24))
#define BB_I_ENCODE_W1(op, w, a)   (BB_I_ENCODE_W0(op, w) | (((BB_Instruction) a) <<  8))

#define BB_I_DECODE_OPCODE(op)     ((BB_OpCode) (op & 0xFF))
#define BB_I_DECODE_A(op)          ((uint8_t)   ((op >> 24) & 0xFF))
#define BB_I_DECODE_B(op)          ((uint8_t)   ((op >> 16) & 0xFF))
#define BB_I_DECODE_C(op)          ((uint8_t)   ((op >>  8) & 0xFF))
#define BB_I_DECODE_W0(op)         ((uint16_t)  ((op >> 24) & 0xFFFF))
#define BB_I_DECODE_W1(op)         ((uint8_t)   ((op >>  8) & 0xFF))

#define BB_ALIGNMENT_DELTA(base_address, alignment) ((alignment - (base_address % alignment)) % alignment)
#define BB_CALC_ARG_SIZE(num_args) ((num_args + BB_ALIGNMENT_DELTA(num_args, alignof(BB_Instruction))) / alignof(BB_Instruction))

typedef struct {
    BB_InstructionPointer const* blocks;
    BB_Instruction const* instructions;
} BB_Bytecode;

typedef struct {
    BB_RegisterIndex num_args;
    BB_RegisterIndex num_registers;
    BB_Bytecode bytecode;
} BB_Function;

typedef struct {
    BB_Function const* functions;
    BB_FunctionIndex num_functions;

    BB_GlobalBaseOffset const* globals;
    uint8_t* global_memory;
    BB_GlobalIndex num_globals;
} BB_Program;

typedef struct {
    BB_Instruction const* instruction_pointer;
    BB_RegisterIndex out_index;
} BB_BlockFrame;

typedef struct {
    BB_Function const* function;
    BB_BlockFrame* root_block;
    uint64_t* stack_base;
} BB_CallFrame;

typedef struct {
    BB_Program const* program;
    BB_CallFrame* call_stack;
    BB_CallFrame* call_stack_max;
    uint64_t* data_stack;
    uint64_t* data_stack_max;
    BB_BlockFrame* block_stack;
} BB_Fiber;

typedef ENUM_T(uint8_t) {
    BB_OKAY,
    BB_TRAP_UNREACHABLE,
    BB_TRAP_STACK_OVERFLOW,
} BB_Trap;

#if BB_DEBUG_TRACE
    #define BB_debug(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#else
    #define BB_debug(fmt, ...)
#endif

BB_Trap BB_eval(BB_Fiber *restrict fiber) {
    BB_debug("BB_eval");

    BB_CallFrame* current_call_frame;
    BB_Function const* current_function;
    BB_BlockFrame* current_block_frame;

    #define SET_CONTEXT() {                              \
        BB_debug("BB_SET_CONTEXT");                      \
        current_call_frame = fiber->call_stack;          \
        current_function = current_call_frame->function; \
        current_block_frame = fiber->block_stack;        \
    }                                                    \

    SET_CONTEXT();

    BB_Instruction last_instruction;

    #define DECODE_NEXT()                                                \
        last_instruction = *(current_block_frame->instruction_pointer++) \
    
    #define DECODE_A()  BB_I_DECODE_A(last_instruction)
    #define DECODE_B()  BB_I_DECODE_B(last_instruction)
    #define DECODE_C()  BB_I_DECODE_C(last_instruction)
    #define DECODE_W0() BB_I_DECODE_W0(last_instruction)
    #define DECODE_W1() BB_I_DECODE_W1(last_instruction)

    static void* DISPATCH_TABLE [] = {
        &&DO_HALT,
        &&DO_UNREACHABLE,
        &&DO_READ_GLOBAL_64,
        &&DO_IF_NZ,
        &&DO_I_ADD_64,
        &&DO_I_SUB_64,
        &&DO_S_LT_64,
        &&DO_CALL_V,
        &&DO_RET_V,
    };

    #define DISPATCH() {                                       \
        DECODE_NEXT();                                         \
        BB_OpCode next = BB_I_DECODE_OPCODE(last_instruction); \
        BB_debug("BB_DISPATCH %d", next);                      \
        goto *DISPATCH_TABLE[next];                            \
    }                                                          \
    
    DISPATCH();

    DO_HALT: {
        BB_debug("BB_HALT");
        return BB_OKAY;
    };

    DO_UNREACHABLE: {
        BB_debug("BB_UNREACHABLE");
        return BB_TRAP_UNREACHABLE;
    };

    DO_READ_GLOBAL_64: {
        BB_debug("BB_READ_GLOBAL_64");

        BB_GlobalIndex index = DECODE_W0();
        BB_RegisterIndex destination = DECODE_W1();

        *(current_call_frame->stack_base + destination) =
            *((uint64_t*) (fiber->program->global_memory + fiber->program->globals[index]));
        
        DISPATCH();
    };

    DO_IF_NZ: {
        BB_debug("BB_IF_NZ");

        BB_BlockIndex then_index = DECODE_A();
        BB_BlockIndex else_index = DECODE_B();
        BB_RegisterIndex condition = DECODE_C();

        BB_BlockIndex new_block_index;
        if (*((uint8_t*) (current_call_frame->stack_base + condition)) != 0) {
            new_block_index = then_index;
        } else {
            new_block_index = else_index;
        }

        BB_InstructionPointer new_block = current_function->bytecode.blocks[new_block_index];

        BB_BlockFrame new_block_frame = {current_function->bytecode.instructions + new_block, 0};
        *(++fiber->block_stack) = new_block_frame;

        SET_CONTEXT();
        DISPATCH();
    };

    DO_I_ADD_64: {
        BB_debug("BB_I_ADD_64");

        BB_RegisterIndex x = DECODE_A();
        BB_RegisterIndex y = DECODE_B();
        BB_RegisterIndex z = DECODE_C();

        *(current_call_frame->stack_base + z) =
            *(current_call_frame->stack_base + x) +
            *(current_call_frame->stack_base + y);

        DISPATCH();
    };

    DO_I_SUB_64: {
        BB_debug("BB_I_SUB_64");

        BB_RegisterIndex x = DECODE_A();
        BB_RegisterIndex y = DECODE_B();
        BB_RegisterIndex z = DECODE_C();

        *(current_call_frame->stack_base + z) =
            *(current_call_frame->stack_base + x) -
            *(current_call_frame->stack_base + y);
        
        DISPATCH();
    };

    DO_S_LT_64: {
        BB_debug("BB_S_LT_64");

        BB_RegisterIndex x = DECODE_A();
        BB_RegisterIndex y = DECODE_B();
        BB_RegisterIndex z = DECODE_C();

        *((uint8_t*) (current_call_frame->stack_base + z)) =
            *(current_call_frame->stack_base + x) <
            *(current_call_frame->stack_base + y);

        DISPATCH();
    };

    DO_CALL_V: {
        BB_debug("BB_CALL_V");

        BB_FunctionIndex functionIndex = DECODE_W0();
        BB_RegisterIndex out = DECODE_W1();

        BB_Function const* new_function = fiber->program->functions + functionIndex;

        BB_debug("\t%d %d %d", functionIndex, out, new_function->num_args);

        if ( fiber->call_stack >= fiber->call_stack_max
           | fiber->data_stack + new_function->num_registers >= fiber->data_stack_max
           ) {
            return BB_TRAP_STACK_OVERFLOW;
        }
        
        uint64_t* new_stack_base = fiber->data_stack;

        BB_RegisterIndex const* args = (BB_RegisterIndex const*) (current_block_frame->instruction_pointer);
        current_block_frame->instruction_pointer += BB_CALC_ARG_SIZE(new_function->num_args);

        for (BB_RegisterIndex i = 0; i < new_function->num_args; i++) {
            *(new_stack_base + i) =
                *(current_call_frame->stack_base + args[i]);
        }

        BB_BlockFrame new_block_frame = {new_function->bytecode.instructions + *new_function->bytecode.blocks, out};
        *(++fiber->block_stack) = new_block_frame;

        BB_CallFrame new_call_frame = {new_function, fiber->block_stack, new_stack_base};
        *(++fiber->call_stack) = new_call_frame;

        fiber->data_stack += new_function->num_registers;

        SET_CONTEXT();
        DISPATCH();
    };

    DO_RET_V: {
        BB_debug("BB_RET_V");

        BB_RegisterIndex y = DECODE_A();

        BB_BlockFrame* root_block = current_call_frame->root_block;
        BB_CallFrame* caller_frame = fiber->call_stack - 1;

        *(caller_frame->stack_base + root_block->out_index) =
            *(current_call_frame->stack_base + y);

        fiber->call_stack--;
        fiber->block_stack = current_call_frame->root_block - 1;
        fiber->data_stack = current_call_frame->stack_base;

        SET_CONTEXT();
        DISPATCH();
    };
}

BB_Trap BB_invoke(BB_Fiber *restrict fiber, BB_FunctionIndex functionIndex, uint64_t* ret_val, uint64_t* args) {
    BB_debug("BB_invoke");

    BB_Function const* function = fiber->program->functions + functionIndex;

    if ( fiber->call_stack + 2 >= fiber->call_stack_max
       | fiber->data_stack + function->num_registers + 1 >= fiber->data_stack_max
       ) {
        return BB_TRAP_STACK_OVERFLOW;
    }
    
    BB_InstructionPointer wrapper_blocks[1] = { 0 };
    BB_Instruction wrapper_instructions[] = { BB_I_ENCODE_0(BB_HALT) };
    BB_Bytecode wrapper_bytecode = {wrapper_blocks, wrapper_instructions};

    BB_Function wrapper = {0, 1, wrapper_bytecode};

    BB_BlockFrame wrapper_block_frame = {wrapper_instructions, 0};
    *(++fiber->block_stack) = wrapper_block_frame;

    BB_CallFrame wrapper_call_frame = {&wrapper, fiber->block_stack, fiber->data_stack};
    *(++fiber->call_stack) = wrapper_call_frame;

    fiber->data_stack += 1;

    BB_BlockFrame block_frame = {function->bytecode.instructions + *function->bytecode.blocks, 0};
    *(++fiber->block_stack) = block_frame;
    
    BB_CallFrame call_frame = {function, fiber->block_stack, fiber->data_stack};
    *(++fiber->call_stack) = call_frame;

    fiber->data_stack += function->num_registers;

    for (uint8_t i = 0; i < function->num_args; i++) {
        *(call_frame.stack_base + i) = args[i];
    }

    BB_Trap result = BB_eval(fiber);

    if (result == BB_OKAY) {
        *ret_val = *((uint64_t*) (wrapper_call_frame.stack_base));
        fiber->call_stack--;
        fiber->block_stack--;
        fiber->data_stack -= 1;
    }

    return result;
}

char const* BB_opcode_name(BB_OpCode op) {
    switch (op) {
        case BB_HALT: return "HALT";
        case BB_UNREACHABLE: return "UNREACHABLE";
        case BB_READ_GLOBAL_64: return "READ_GLOBAL_64";
        case BB_IF_NZ: return "IF_NZ";
        case BB_I_ADD_64: return "I_ADD_64";
        case BB_I_SUB_64: return "I_SUB_64";
        case BB_S_LT_64: return "S_LT_64";
        case BB_CALL_V: return "CALL_V";
        case BB_RET_V: return "RET_V";
        default: return "INVALID";
    }
}

typedef stbds_arr(uint8_t) BB_Encoder;

BB_InstructionPointer BB_encode_instr (BB_Encoder* encoder, BB_Instruction instr) {
    uint8_t* bytes = (uint8_t*) &instr;
    BB_InstructionPointer offset = stbds_arrlenu(*encoder) / sizeof(BB_Instruction);
    for (size_t i = 0; i < sizeof(BB_Instruction); i++) stbds_arrpush(*encoder, bytes[i]);
    return offset;
}

BB_InstructionPointer BB_encode_0 (BB_Encoder* encoder, BB_OpCode opcode) {
    BB_debug("BB_encode_0 %s", BB_opcode_name(opcode));
    BB_Instruction e = BB_I_ENCODE_0(opcode);
    BB_debug("\t%s", BB_opcode_name(BB_I_DECODE_OPCODE(e)));
    return BB_encode_instr(encoder, e);
}

BB_InstructionPointer BB_encode_1 (BB_Encoder* encoder, BB_OpCode opcode, uint8_t a) {
    BB_debug("BB_encode_1 %s %d", BB_opcode_name(opcode), a);
    BB_Instruction e = BB_I_ENCODE_1(opcode, a);
    BB_debug("\t%s %d", BB_opcode_name(BB_I_DECODE_OPCODE(e)), BB_I_DECODE_A(e));
    return BB_encode_instr(encoder, e);
}

BB_InstructionPointer BB_encode_2 (BB_Encoder* encoder, BB_OpCode opcode, uint8_t a, uint8_t b) {
    BB_debug("BB_encode_2 %s %d %d", BB_opcode_name(opcode), a, b);
    BB_Instruction e = BB_I_ENCODE_2(opcode, a, b);
    BB_debug("\t%s %d %d", BB_opcode_name(BB_I_DECODE_OPCODE(e)), BB_I_DECODE_A(e), BB_I_DECODE_B(e));
    return BB_encode_instr(encoder, e);
}

BB_InstructionPointer BB_encode_3 (BB_Encoder* encoder, BB_OpCode opcode, uint8_t a, uint8_t b, uint8_t c) {
    BB_debug("BB_encode_3 %s %d %d %d", BB_opcode_name(opcode), a, b, c);
    BB_Instruction e = BB_I_ENCODE_3(opcode, a, b, c);
    BB_debug("\t%s %d %d %d", BB_opcode_name(BB_I_DECODE_OPCODE(e)), BB_I_DECODE_A(e), BB_I_DECODE_B(e), BB_I_DECODE_C(e));
    return BB_encode_instr(encoder, e);
}

BB_InstructionPointer BB_encode_w0 (BB_Encoder* encoder, BB_OpCode opcode, uint16_t w) {
    BB_debug("BB_encode_w0 %s %d", BB_opcode_name(opcode), w);
    BB_Instruction e = BB_I_ENCODE_W0(opcode, w);
    BB_debug("\t%s %d", BB_opcode_name(BB_I_DECODE_OPCODE(e)), BB_I_DECODE_W0(e));
    return BB_encode_instr(encoder, e);
}

BB_InstructionPointer BB_encode_w1 (BB_Encoder* encoder, BB_OpCode opcode, uint16_t w, uint8_t a) {
    BB_debug("BB_encode_w1 %s %d %d", BB_opcode_name(opcode), w, a);
    BB_Instruction e = BB_I_ENCODE_W1(opcode, w, a);
    BB_debug("\t%s %d %d", BB_opcode_name(BB_I_DECODE_OPCODE(e)), BB_I_DECODE_W0(e), BB_I_DECODE_W1(e));
    return BB_encode_instr(encoder, e);
}

void BB_encode_registers (BB_Encoder* encoder, BB_RegisterIndex num_registers, BB_RegisterIndex* indices) {
    for (size_t i = 0; i < num_registers; i++) stbds_arrpush(*encoder, indices[i]);
    size_t padding = BB_ALIGNMENT_DELTA(num_registers, alignof(BB_Instruction));
    BB_debug("encoded %d registers:", num_registers);
    for (size_t i = 0; i < num_registers; i++) BB_debug("\tr%d", indices[i]);
    BB_debug("adding %lu padding", padding);
    for (size_t i = 0; i < padding; i++) stbds_arrpush(*encoder, 0);
}

void BB_disas(BB_Function const* functions, BB_InstructionPointer const* blocks, BB_Instruction const* instructions) {
    BB_BlockIndex to_disas [BB_MAX_BLOCKS] = {};
    BB_BlockIndex num_blocks = 0;
    #define DISAS_BLOCK(block) (to_disas[num_blocks++] = block)
    DISAS_BLOCK(0);

    while (num_blocks > 0) {
        BB_BlockIndex block_index = to_disas[--num_blocks];
        BB_InstructionPointer block = blocks[block_index];

        printf("[b%d : i%d @%ld]:\n", block_index, block, block * sizeof(BB_Instruction));

        BB_InstructionPointerOffset ip = 0;

        while (true) {
            BB_Instruction instr = instructions[block + ip];
            BB_OpCode opcode = BB_I_DECODE_OPCODE(instr);
            printf("\ti%d @%lx\t\t%x:%s", (block + ip), (block + ip) * sizeof(BB_Instruction), opcode, BB_opcode_name(opcode));

            ip++;

            bool block_done = false;

            switch (opcode) {
                case BB_HALT:
                case BB_UNREACHABLE: {
                    block_done = true;
                } break;

                case BB_READ_GLOBAL_64: {
                    BB_GlobalIndex index = BB_I_DECODE_W0(instr);
                    BB_RegisterIndex destination = BB_I_DECODE_W1(instr);
                    printf(" g%d r%d", index, destination);
                } break;

                case BB_IF_NZ: {
                    BB_BlockIndex then_index = BB_I_DECODE_A(instr);
                    BB_BlockIndex else_index = BB_I_DECODE_B(instr);
                    BB_RegisterIndex condition = BB_I_DECODE_C(instr);
                    printf(" b%d b%d r%d", then_index, else_index, condition);
                    DISAS_BLOCK(else_index);
                    DISAS_BLOCK(then_index);
                    block_done = true;
                } break;

                case BB_I_ADD_64: {
                    BB_RegisterIndex x = BB_I_DECODE_A(instr);
                    BB_RegisterIndex y = BB_I_DECODE_B(instr);
                    BB_RegisterIndex z = BB_I_DECODE_C(instr);
                    printf(" r%d r%d r%d", x, y, z);
                } break;

                case BB_I_SUB_64: {
                    BB_RegisterIndex x = BB_I_DECODE_A(instr);
                    BB_RegisterIndex y = BB_I_DECODE_B(instr);
                    BB_RegisterIndex z = BB_I_DECODE_C(instr);
                    printf(" r%d r%d r%d", x, y, z);
                } break;

                case BB_S_LT_64: {
                    BB_RegisterIndex x = BB_I_DECODE_A(instr);
                    BB_RegisterIndex y = BB_I_DECODE_B(instr);
                    BB_RegisterIndex z = BB_I_DECODE_C(instr);
                    printf(" r%d r%d r%d", x, y, z);
                } break;

                case BB_CALL_V: {
                    BB_FunctionIndex functionIndex = BB_I_DECODE_W0(instr);
                    BB_RegisterIndex out = BB_I_DECODE_W1(instr);
                    printf(" f%d r%d", functionIndex, out);
                    BB_Function const* function = functions + functionIndex;
                    BB_RegisterIndex const* args = (BB_RegisterIndex const*) (instructions + block + ip);
                    BB_InstructionPointerOffset offset = BB_CALC_ARG_SIZE(function->num_args);
                    ip += offset;
                    printf(" (%d~%d : ", function->num_args, offset);
                    for (uint8_t i = 0; i < function->num_args; i++) {
                        printf("r%d", args[i]);
                        if (i < function->num_args - 1) printf(", ");
                    }
                    printf(")");
                } break;

                case BB_RET_V: {
                    BB_RegisterIndex y = BB_I_DECODE_A(instr);
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


uint64_t native_fib(uint64_t n) {
    if (n < 2) {
        return n;
    }

    return native_fib(n - 1) + native_fib(n - 2);
}

int main (int argc, char** argv) {
    BB_Encoder instructions = NULL;

    BB_InstructionPointer entry_block =
        BB_encode_w1(&instructions, BB_READ_GLOBAL_64, 0, 1);
        BB_encode_w1(&instructions, BB_READ_GLOBAL_64, 1, 2);

        BB_encode_3(&instructions, BB_S_LT_64, 0, 2, 3);
        BB_encode_3(&instructions, BB_IF_NZ, 1, 2, 3);

    BB_InstructionPointer then_block =
        BB_encode_1(&instructions, BB_RET_V, 0);

    BB_InstructionPointer else_block =
        BB_encode_3(&instructions, BB_I_SUB_64, 0, 1, 4);
        BB_encode_w1(&instructions, BB_CALL_V, 0, 4);
        BB_encode_registers(&instructions, 1, (BB_RegisterIndex[]){4});

        BB_encode_3(&instructions, BB_I_SUB_64, 0, 2, 5);
        BB_encode_w1(&instructions, BB_CALL_V, 0, 5);
        BB_encode_registers(&instructions, 1, (BB_RegisterIndex[]){5});

        BB_encode_3(&instructions, BB_I_ADD_64, 4, 5, 5);
        BB_encode_1(&instructions, BB_RET_V, 5);

    BB_InstructionPointer blocks [] = {
        entry_block,
        then_block,
        else_block,
    };

    BB_Bytecode bytecode = {blocks, (BB_Instruction const*) instructions};

    BB_Function function = {1, 6, bytecode};

    // BB_disas(&function, blocks, (BB_Instruction const*) instructions);

    uint8_t global_memory[sizeof(int64_t) * 2];
    BB_GlobalBaseOffset globals[2] = {0, sizeof(int64_t)};

    memcpy(global_memory, &(int64_t){1}, sizeof(int64_t));
    memcpy(global_memory + sizeof(int64_t), &(int64_t){2}, sizeof(int64_t));

    BB_Program program = {
        .functions = &function,
        .num_functions = 1,
        .globals = globals,
        .global_memory = global_memory,
        .num_globals = 2,
    };

    uint64_t* data_stack = malloc(BB_STACK_SIZE);
    BB_CallFrame* call_stack = malloc(sizeof(BB_CallFrame) * BB_MAX_CALL_FRAMES);
    BB_BlockFrame* block_stack = malloc(sizeof(BB_BlockFrame) * BB_MAX_CALL_FRAMES * BB_MAX_BLOCKS);

    BB_Fiber fiber = {
        .program = &program,
        .call_stack = call_stack,
        .call_stack_max = call_stack + BB_MAX_CALL_FRAMES,
        .block_stack = block_stack,
        .data_stack = data_stack,
        .data_stack_max = data_stack + BB_STACK_SIZE,
    };

    uint64_t ret_val = 0xdeadbeef;
    uint64_t n = 32;
    uint64_t expected = native_fib(n);

    clock_t start = clock();
    BB_Trap result = BB_invoke(&fiber, 0, &ret_val, (uint64_t[]){n});
    clock_t end = clock();

    double elapsed = (((double) (end - start)) / ((double) CLOCKS_PER_SEC));

    if (result == BB_OKAY) {
        printf("Result: %"PRId64" (in %fs)\n", ret_val, elapsed);
        if (ret_val != expected) {
            return 1;
        }
    } else {
        printf("Trap: %d\n", result);
        return 2;
    }

    return 0;
}
