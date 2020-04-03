#include "mipssim.h"

#define BREAK_POINT 200000 // exit after so many cycles -- useful for debugging

// Global variables
char mem_init_path[1000];
char reg_init_path[1000];

uint32_t cache_size = 0;
struct architectural_state arch_state;

#define I_TYPE 2;
#define J_TYPE 3;
static inline uint8_t get_instruction_type(int opcode)
{
    switch (opcode) {
        /// opcodes are defined in mipssim.h

        case SPECIAL:
            return R_TYPE;
        case EOP:
            return EOP_TYPE;
        case ADD:
            return R_TYPE;
        case ADDI:
            return I_TYPE;
        case LW:
            return I_TYPE;
        case SW:
            return I_TYPE;
        case BEQ:
            return I_TYPE;
        case J:
            return J_TYPE;
        case SLT:
            return R_TYPE;

        default:
            assert(false);
    }
    assert(false);
}

void FSM()
{
    struct ctrl_signals *control = &arch_state.control;
    struct instr_meta *IR_meta = &arch_state.IR_meta;

    //reset control signals
    memset(control, 0, (sizeof(struct ctrl_signals)));

    int opcode = IR_meta->opcode;
    int state = arch_state.state;
    switch (state) {
        case INSTR_FETCH:
            control->MemRead = 1;
            control->ALUSrcA = 0;
            control->IorD = 0;
            control->IRWrite = 1;
            control->ALUSrcB = 1;
            control->ALUOp = 0;
            control->PCWrite = 1;
            control->PCSource = 0;
            state = DECODE;
            break;
        case DECODE:
            control->ALUSrcA = 0;
            control->ALUSrcB = 3;
            control->ALUOp = 0;
            if (IR_meta->type == R_TYPE) state = EXEC;
            else if (opcode == LW || opcode == SW) state = MEM_ADDR_COMP;
            else if (opcode == ADDI) state = I_TYPE_EXEC;
            else if (opcode == BEQ) state = BRANCH_COMPL;
            else if (opcode == J) state = JUMP_COMPL;
            else if (opcode == EOP) state = EXIT_STATE;
            else assert(false);
            break;
        case MEM_ADDR_COMP:
            control->ALUSrcA = 1;
            control->ALUSrcB = 2;
            control->ALUOp = 0;
            if (IR_meta->opcode == LW) state = MEM_ACCESS_LD;
            else if (IR_meta->opcode == SW) state = MEM_ACCESS_ST;
            else assert(false);
            break;
        case MEM_ACCESS_LD:
            control->MemRead = 1;
            control->IorD = 1;
            state = WB_STEP;
            break;
        case WB_STEP:
            control->RegDst = 0;
            control->RegWrite = 1;
            control->MemtoReg = 1;
            state = INSTR_FETCH;
            break;
        case MEM_ACCESS_ST:
            control->MemWrite = 1;
            control->IorD = 1;
            state = INSTR_FETCH;
            break;
        case EXEC:
            control->ALUSrcA = 1;
            control->ALUSrcB = 0;
            control->ALUOp = 2;
            state = R_TYPE_COMPL;
            break;
        case R_TYPE_COMPL:
            control->RegDst = 1;
            control->RegWrite = 1;
            control->MemtoReg = 0;
            state = INSTR_FETCH;
            break;
        case BRANCH_COMPL:
            control->ALUSrcA = 1;
            control->ALUSrcB = 0;
            control->ALUOp = 1;
            control->PCWriteCond = 1;
            control->PCSource = 1;
            state = INSTR_FETCH;
            break;
        case JUMP_COMPL:
            control->PCWrite = 1;
            control->PCSource = 2;
            state = INSTR_FETCH;
            break;
        case EXIT_STATE:
            break;
        case I_TYPE_EXEC:
            control->ALUOp = 0;         // Despite everything, the ALU just adds 2 things(?)
            control->ALUSrcA = 1;       // We want rs in A, not the PC
            control->ALUSrcB = 2;       // We want to get the unshifted immediate into B
            state = I_TYPE_COMPL;
            break;
        case I_TYPE_COMPL:
            control->RegDst = 0;
            control->RegWrite = 1;
            control->MemtoReg = 0;
            state = INSTR_FETCH;
            break;
        default: assert(false);
    }
    arch_state.state = state;
}


void instruction_fetch()
{
    if (arch_state.control.MemRead) {
        if (arch_state.control.IRWrite) {
            int address = (arch_state.control.IorD == 0) ? arch_state.curr_pipe_regs.pc : arch_state.curr_pipe_regs.ALUOut; // IorD = 0: address = PC. else: address = ALUOUT
            arch_state.next_pipe_regs.IR = memory_read(address);
        }
    }
}

void decode_and_read_RF()
{
    int read_register_1 = arch_state.IR_meta.reg_21_25;
    int read_register_2 = arch_state.IR_meta.reg_16_20;
    check_is_valid_reg_id(read_register_1);
    check_is_valid_reg_id(read_register_2);
    arch_state.next_pipe_regs.A = arch_state.registers[read_register_1];
    arch_state.next_pipe_regs.B = arch_state.registers[read_register_2];
}

void execute()
{
    struct ctrl_signals *control = &arch_state.control;
    struct instr_meta *IR_meta = &arch_state.IR_meta;
    struct pipe_regs *curr_pipe_regs = &arch_state.curr_pipe_regs;
    struct pipe_regs *next_pipe_regs = &arch_state.next_pipe_regs;

    int alu_opA = control->ALUSrcA == 1 ? curr_pipe_regs->A : curr_pipe_regs->pc;
    int alu_opB;
    int immediate = IR_meta->immediate;
    int shifted_immediate = (immediate) << 2;
    switch (control->ALUSrcB) {
        case 0:
            alu_opB = curr_pipe_regs->B;
            break;
        case 1:
            alu_opB = WORD_SIZE;
            break;
        case 2:
            alu_opB = immediate;
            break;
        case 3:
            alu_opB = shifted_immediate;
            break;
        default:
            assert(false);
    }

    switch (control->ALUOp) {
        case 0:
            next_pipe_regs->ALUOut = alu_opA + alu_opB;
            break;
        case 1:
            next_pipe_regs->ALUOut = alu_opA - alu_opB;
            // next_pipe_regs->ALUOut = alu_opA == alu_opB ? 0 : 1;             // This would do the job for beq. Is it actually safer? Am i otherwise
                                                                                // missing a corner case?
            break;
        case 2:
            if (IR_meta->function == ADD)
                next_pipe_regs->ALUOut = alu_opA + alu_opB;
            else if (IR_meta->function == SLT) {
                // This is fairly high-level. I trust that the ALU knows how to check
                // if one variable is less than another, without explicitly being told
                // by me to perform a subtraction.
                next_pipe_regs->ALUOut = alu_opA < alu_opB ? 1 : 0; // v fancy
            }
            else
                assert(false);
            break;
        default:
            assert(false);
    }

    // PC calculation
    switch (control->PCSource) {
        case 0:
            next_pipe_regs->pc = next_pipe_regs->ALUOut;
            break;
        case 1: // This is used for branches. The current ALUOut is still the branch target from stage 2.
            next_pipe_regs->pc = curr_pipe_regs->ALUOut;
            break;
        case 2:
            //next_pipe_regs->pc = ((( (uint32_t) curr_pipe_regs->pc) >> 28) << 28) + (IR_meta->jmp_offset << 2);
            next_pipe_regs->pc = (get_piece_of_a_word(curr_pipe_regs->pc, 28, 4) << 28) + (IR_meta->jmp_offset << 2);
            break;
        default:
            assert(false);
    }
}


void memory_access() {
  ///@students: appropriate calls to functions defined in memory_hierarchy.c must be added

    struct ctrl_signals *control = &arch_state.control;
    //struct instr_meta *IR_meta = &arch_state.IR_meta; // WHY WOULD WE NEED THIS?
    struct pipe_regs *curr_pipe_regs = &arch_state.curr_pipe_regs;
    struct pipe_regs *next_pipe_regs = &arch_state.next_pipe_regs;

    if (control->MemRead == 1) {
            next_pipe_regs->MDR = control->IorD == 1 ? memory_read(curr_pipe_regs->ALUOut) : memory_read(curr_pipe_regs->PC); // IorD==1: mdr = memread(ALUOUT). else: mdr = memread(PC)
    }
    if (control->MemWrite == 1) {
        if (control->IorD == 1) {
            memory_write(curr_pipe_regs->ALUOut , curr_pipe_regs->B);
        } else {
            memory_write(curr_pipe_regs->PC , curr_pipe_regs->B);
        }
    }

}

void write_back()
{
    if (arch_state.control.RegWrite) {
        int write_reg_id = arch_state.control.RegDst == 1 ? arch_state.IR_meta.reg_11_15 : arch_state.IR_meta.reg_16_20;       // if 1 -> rd : if 0 -> rt
        check_is_valid_reg_id(write_reg_id);
        int write_data = arch_state.control.MemtoReg == 1 ? arch_state.curr_pipe_regs.MDR : arch_state.curr_pipe_regs.ALUOut;
        if (write_reg_id > 0) {
            arch_state.registers[write_reg_id] = write_data;
            //printf("Reg $%u = %d \n", write_reg_id, write_data);
        } else printf("Attempting to write reg_0. That is likely a mistake \n");
    }
}


void set_up_IR_meta(int IR, struct instr_meta *IR_meta)
{
    IR_meta->opcode = get_piece_of_a_word(IR, OPCODE_OFFSET, OPCODE_SIZE);
    IR_meta->immediate = get_sign_extended_imm_id(IR, IMMEDIATE_OFFSET);
    IR_meta->function = get_piece_of_a_word(IR, 0, 6);
    IR_meta->jmp_offset = get_piece_of_a_word(IR, 0, 26);
    IR_meta->reg_11_15 = (uint8_t) get_piece_of_a_word(IR, 11, REGISTER_ID_SIZE);
    IR_meta->reg_16_20 = (uint8_t) get_piece_of_a_word(IR, 16, REGISTER_ID_SIZE);
    IR_meta->reg_21_25 = (uint8_t) get_piece_of_a_word(IR, 21, REGISTER_ID_SIZE);
    IR_meta->type = get_instruction_type(IR_meta->opcode);

    switch (IR_meta->opcode) {
        case SPECIAL:
            if (IR_meta->function == ADD)
                printf("Executing ADD(%d), $%u = $%u + $%u (function: %u) \n",
                       IR_meta->opcode,  IR_meta->reg_11_15, IR_meta->reg_21_25,  IR_meta->reg_16_20, IR_meta->function);
            else if (IR_meta->function == SLT) {
                printf("Executing SLT(%d), $%u = $%u < $%u (function: %u) \n",
                        IR_meta->opcode,  IR_meta->reg_11_15, IR_meta->reg_21_25,  IR_meta->reg_16_20, IR_meta->function);
            }
            else assert(false);
            break;
        case ADDI:
            printf("Executing ADDI(%d), $%u = $%u + %u \n",
                    IR_meta->opcode,  IR_meta->reg_16_20, IR_meta->reg_21_25,  IR_meta->immediate);
            break;
        case BEQ:
            printf("Executing BEQ(%d), if ($%u == $%u) pc += %u*4 \n",
                IR_meta->opcode,  IR_meta->reg_21_25, IR_meta->reg_16_20,  IR_meta->immediate);
            break;
        case J:
            printf("Executing J(%d), pc = (%u<<2) \n",
                IR_meta->opcode, IR_meta->jmp_offset);
            break;
        case SW:
            printf("Executing SW(%d), memory entry (%u+$%u) = $%u  \n",
                IR_meta->opcode,  IR_meta->immediate, IR_meta->reg_21_25,  IR_meta->reg_16_20);
            break;
        case LW:
            printf("Executing LW(%d), $%u = memory entry (%u+$%u)  \n",
                IR_meta->opcode,  IR_meta->reg_16_20, IR_meta->immediate,  IR_meta->reg_21_25);
            break;
        case EOP:
            printf("Executing EOP(%d) \n", IR_meta->opcode);
// THIS PART EXISTS SOLELY FOR TESTING PURPOSES------------------------------------------------------------------------------
            printf("Register entries at the end: \n");
            print_uint32_bin_array(arch_state.registers, 5);
            printf(" ~~~ Memory at the end: \n");
            print_uint32_bin_array(arch_state.memory, 32);
// THIS PART EXISTS SOLELY FOR TESTING PURPOSES -----------------------------------------------------------------------------
            break;
        default: assert(false);
    }
}

void assign_pipeline_registers_for_the_next_cycle()
{

    struct ctrl_signals *control = &arch_state.control;
    struct instr_meta *IR_meta = &arch_state.IR_meta;
    struct pipe_regs *curr_pipe_regs = &arch_state.curr_pipe_regs;
    struct pipe_regs *next_pipe_regs = &arch_state.next_pipe_regs;

    if (control->IRWrite) {
        curr_pipe_regs->IR = next_pipe_regs->IR;
        printf("PC %d: ", curr_pipe_regs->pc / 4);
        set_up_IR_meta(curr_pipe_regs->IR, IR_meta);
    }
    curr_pipe_regs->ALUOut = next_pipe_regs->ALUOut;
    curr_pipe_regs->A = next_pipe_regs->A;
    curr_pipe_regs->B = next_pipe_regs->B;
    curr_pipe_regs->MDR = next_pipe_regs->MDR;              // THIS WAS NOT IN HERE ORIGINALLY AND I HAVE NO iDEA WHY
    if (control->PCWrite) {
        check_address_is_word_aligned(next_pipe_regs->pc);
        curr_pipe_regs->pc = next_pipe_regs->pc;
    }
    if (control->PCWriteCond) {
        // FAIRLY HIGH LEVEL! We would actually take the Zero output bit of the ALU
        if (next_pipe_regs->ALUOut == 0) {
            check_address_is_word_aligned(next_pipe_regs->pc);
            curr_pipe_regs->pc = next_pipe_regs->pc;
        }
    }
}


int main(int argc, const char* argv[])
{
    /*--------------------------------------
    /------- Global Variable Init ----------
    /--------------------------------------*/
    parse_arguments(argc, argv);
    arch_state_init(&arch_state);
    
    while (true) {

        FSM();

        instruction_fetch();

        decode_and_read_RF();

        execute();

        memory_access();

        write_back();

        assign_pipeline_registers_for_the_next_cycle();

        marking_after_clock_cycle();
        arch_state.clock_cycle++;
        // Check exit statements
        if (arch_state.state == EXIT_STATE) { // I.E. EOP instruction!
            printf("Exiting because the exit state was reached \n");
            break;
        }
        if (arch_state.clock_cycle == BREAK_POINT) {
            printf("Exiting because the break point (%u) was reached \n", BREAK_POINT);
            break;
        }
    }
    marking_at_the_end();
}
