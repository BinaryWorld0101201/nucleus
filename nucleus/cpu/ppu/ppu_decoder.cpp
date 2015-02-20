/**
 * (c) 2015 Nucleus project. All rights reserved.
 * Released under GPL v2 license. Read LICENSE for more details.
 */

#include "ppu_decoder.h"
#include "nucleus/emulator.h"
#include "nucleus/cpu/ppu/ppu_instruction.h"
#include "nucleus/cpu/ppu/ppu_tables.h"
#include "nucleus/cpu/ppu/analyzer/ppu_analyzer.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"

#include <algorithm>
#include <queue>

namespace cpu {
namespace ppu {

/**
 * PPU Block methods
 */
bool Block::contains(u32 addr) const
{
    const u32 from = address;
    const u32 to = address + size;
    return from <= addr && addr < to;
}

bool Block::is_split() const
{
    const Instruction lastInstr = { nucleus.memory.read32(address + size - 4) };
    if (!lastInstr.is_branch() || lastInstr.is_call() || (lastInstr.opcode == 0x13 && lastInstr.op19 == 0x210) /*bcctr*/) {
        return true;
    }
    return false;
}

/**
 * PPU Function methods
 */
void Function::get_type()
{
    Analyzer status;
    Block block = blocks[address];

    // Analyze read/written registers
    for (u32 offset = 0; offset < block.size; offset += 4) {
        Instruction code = { nucleus.memory.read32(block.address + offset) };

        // Get instruction analyzer and call it
        auto method = get_entry(code).analyzer;
        (status.*method)(code);
        
        if (code.is_branch_conditional() || code.is_return()) {
            break;
        }
        if (code.is_branch_unconditional() && !code.is_call()) {
            block = blocks[block.branch_a];
            offset = 0;
        }
    }

    // Determine type of function arguments
    for (u32 reg = 0; reg < 13; reg++) {
        if ((status.gpr[reg + 3] & REG_READ_ORIG) && reg < 8) {
            type_in.push_back(FUNCTION_IN_INTEGER);
        }
        if ((status.fpr[reg + 1] & REG_READ_ORIG) && reg < 13) {
            type_in.push_back(FUNCTION_IN_FLOAT);
        }
        if ((status.vr[reg + 2] & REG_READ_ORIG) && reg < 12) {
            type_in.push_back(FUNCTION_IN_VECTOR);
        }
    }

    // Determine type of function return
    type_out = FUNCTION_OUT_VOID;
    if (status.gpr[3] & REG_WRITE) {
        type_out = FUNCTION_OUT_INTEGER;
    }
    if (status.vr[2] & REG_WRITE) {
        type_out = FUNCTION_OUT_VECTOR;
    }
    if (status.fpr[1] & REG_WRITE) {
        type_out = FUNCTION_OUT_FLOAT;
        if (status.fpr[2] & REG_WRITE) {
            type_out = FUNCTION_OUT_FLOAT_X2;
        }
        if (status.fpr[3] & REG_WRITE) {
            type_out = FUNCTION_OUT_FLOAT_X3;
        }
        if (status.fpr[4] & REG_WRITE) {
            type_out = FUNCTION_OUT_FLOAT_X4;
        }
    }
}

bool Function::analyze()
{
    blocks.clear();
    type_in.clear();

    std::queue<u32> labels({ address });

    Instruction code;  // Current instruction
    Block current;     // Current block
    current.initial = false;

    // Control Flow Graph generation
    while (!labels.empty()) {
        u32 addr = labels.front();
        code.instruction = nucleus.memory.read32(addr);

        // Initial Block properties
        current.address = addr;
        current.size = 4;
        current.branch_a = 0;
        current.branch_b = 0;

        u32 maxSize = 0xFFFFFFFF;
        bool continueLoop = false;
        for (auto& item : blocks) {
            Block& block_a = item.second;
            // Determine maximum possible size for the current block
            if ((block_a.address - current.address) < maxSize) {
                maxSize = block_a.address - current.address;
            }
            // Split block if label (Block B) is inside an existing block (Block A)
            if (block_a.contains(addr)) {
                // Configure Block B
                Block block_b{};
                block_b.address = addr;
                block_b.size = block_a.size - (addr - block_a.address);
                block_b.branch_a = block_a.branch_a;
                block_b.branch_b = block_a.branch_b;

                // Update Block A and push Block B
                block_a.size = addr - block_a.address;
                block_a.branch_a = addr;
                block_a.branch_b = 0;
                blocks[addr] = block_b;
                continueLoop = true;
                break;
            }
        }
        if (continueLoop) {
            labels.pop();
            continue;
        }

        // Wait for the end
        while ((!code.is_branch() || code.is_call()) && (current.size < maxSize)) {
            addr += 4;
            current.size += 4;
            code.instruction = nucleus.memory.read32(addr);
        }

        // Push new labels
        if (code.is_branch_conditional() && !code.is_call()) {
            const u32 target_a = code.get_target(addr);
            const u32 target_b = addr + 4;
            if (!parent->contains(target_a) || !parent->contains(target_b)) {
                return false;
            }
            labels.push(target_a);
            labels.push(target_b);
            current.branch_a = target_a;
            current.branch_b = target_b;
        }
        if (code.is_branch_unconditional() && !code.is_call()) {
            const u32 target = code.get_target(addr);
            if (!parent->contains(target)) {
                return false;
            }
            labels.push(target);
            current.branch_a = target;
        }

        blocks[labels.front()] = current;
        labels.pop();
    }
    
    // Determine function arguments/return types
    get_type();

    return true;
}

llvm::Function* Function::declare()
{
    // Return type
    llvm::Type* result = nullptr;
    switch (type_out) {
    case FUNCTION_OUT_INTEGER:
        result = llvm::Type::getInt64Ty(llvm::getGlobalContext());
        break;
    case FUNCTION_OUT_FLOAT:
        result = llvm::Type::getDoubleTy(llvm::getGlobalContext());
        break;
    case FUNCTION_OUT_FLOAT_X2:
        result = llvm::Type::getDoubleTy(llvm::getGlobalContext()); // TODO
        break;
    case FUNCTION_OUT_FLOAT_X3:
        result = llvm::Type::getDoubleTy(llvm::getGlobalContext()); // TODO
        break;
    case FUNCTION_OUT_FLOAT_X4: 
        result = llvm::Type::getDoubleTy(llvm::getGlobalContext()); // TODO
        break;
    case FUNCTION_OUT_VECTOR:
        result = llvm::Type::getIntNTy(llvm::getGlobalContext(), 128);
        break;
    case FUNCTION_OUT_VOID:
        result = llvm::Type::getVoidTy(llvm::getGlobalContext());
        break;
    }

    // Arguments type
    std::vector<llvm::Type*> params;
    for (auto& type : type_in) {
        switch (type) {
        case FUNCTION_OUT_INTEGER:
            params.push_back(llvm::Type::getInt64Ty(llvm::getGlobalContext()));
            break;
        case FUNCTION_OUT_FLOAT:
            params.push_back(llvm::Type::getDoubleTy(llvm::getGlobalContext()));
            break;
        case FUNCTION_OUT_VECTOR:
            params.push_back(llvm::Type::getIntNTy(llvm::getGlobalContext(), 128));
            break;
        }
    }
    
    llvm::FunctionType* ftype = llvm::FunctionType::get(result, params, false);

    // Declare function in module
    function = llvm::Function::Create(ftype, llvm::Function::ExternalLinkage, name, parent->module);
    return function;
}

llvm::Function* Function::recompile()
{
    Recompiler recompiler(parent, this);
    recompiler.returnType = type_out;

    std::queue<u32> labels({ address });

    // Create LLVM basic blocks
    blocks[address].bb = llvm::BasicBlock::Create(llvm::getGlobalContext(), "entry", function);
    for (auto& item : blocks) {
        Block& block = item.second;
        if (block.address != address) {
            std::string name = format("block_%X", block.address);
            block.bb = llvm::BasicBlock::Create(llvm::getGlobalContext(), name, function);
        }
    }

    // Recompile basic clocks
    while (!labels.empty()) {
        auto& block = blocks[labels.front()];
        if (block.recompiled) {
            labels.pop();
            continue;
        }

        // Recompile block instructions
        recompiler.setInsertPoint(block.bb);
        for (u32 offset = 0; offset < block.size; offset += 4) {
            recompiler.currentAddress = block.address + offset;
            const Instruction code = { nucleus.memory.read32(recompiler.currentAddress) };
            auto method = get_entry(code).recompiler;
            (recompiler.*method)(code);
        }

        // Block was splitted
        if (block.is_split()) {
            const u32 target = block.address + block.size;
            if (blocks.find(target) != blocks.end()) {
                recompiler.createBranch(blocks[target]);
            }
            // Required for .sceStub.text (single-block functions ending on bctr)
            else {
                recompiler.createReturn();
            }
        }

        block.recompiled = true;
        if (block.branch_a) {
            labels.push(block.branch_a);
        }
        if (block.branch_b) {
            labels.push(block.branch_b);
        }
        labels.pop();
    }

    // Validate the generated code, checking for consistency (TODO: Remove this once the recompiler is stable)
    llvm::verifyFunction(*function, &llvm::outs());
    return function;
}

/**
 * PPU Segment methods
 */
void Segment::analyze()
{
    // Lists of labels
    std::set<u32> labelBlocks;  // Detected immediately
    std::set<u32> labelCalls;   // Direct target of a {bl*, bcl*} instruction (call)
    std::set<u32> labelJumps;   // Direct or indirect target of a {b, ba, bc, bca} instruction (jump)

    // Basic Block Slicing
    u32 currentBlock = 0;
    for (u32 i = address; i < (address + size); i += 4) {
        const Instruction code = { nucleus.memory.read32(i) };

        // New block appeared
        if (code.is_valid() && currentBlock == 0) {
            currentBlock = i;
        }

        // Block is corrupt
        if (currentBlock != 0 && !code.is_valid()) {
            currentBlock = 0;
        }
        
        // Function call detected
        if (currentBlock != 0 && code.is_call()) {
            labelCalls.insert(code.get_target(i));
        }

        // Block finished
        if (currentBlock != 0 && code.is_branch() && !code.is_call()) {
            if (code.is_branch_conditional()) {
                labelJumps.insert(code.get_target(i));
                labelJumps.insert(i + 4);
            }
            if (code.is_branch_unconditional()) {
                labelJumps.insert(code.get_target(i));
            }
            labelBlocks.insert(currentBlock);
            currentBlock = 0;
        }
    }

    // Functions := ((Blocks \ Jumps) U Calls)
    std::set<u32> labelFunctions;
    std::set_difference(labelBlocks.begin(), labelBlocks.end(), labelJumps.begin(), labelJumps.end(), std::inserter(labelFunctions, labelFunctions.end()));
    std::set_union(labelFunctions.begin(), labelFunctions.end(), labelCalls.begin(), labelCalls.end(), std::inserter(labelFunctions, labelFunctions.end()));

    // List the functions and analyze them
    for (const auto& label : labelFunctions) {
        if (this->contains(label)) {
            Function function(label, this);
            if (function.analyze()) {
                functions[label] = function;
            }
        }
    }
}

void Segment::recompile()
{
    module = new llvm::Module(name, llvm::getGlobalContext());

    // Optimization passes
    fpm = new llvm::FunctionPassManager(module);
    fpm->add(llvm::createPromoteMemoryToRegisterPass());  // Promote allocas to registers
    fpm->add(llvm::createInstructionCombiningPass());     // Simple peephole and bit-twiddling optimizations
    fpm->add(llvm::createReassociatePass());              // Reassociate expressions
    fpm->add(llvm::createGVNPass());                      // Eliminate Common SubExpressions
    //fpm->add(llvm::createCFGSimplificationPass());      // TODO: Uncomment once undefined behaviours are fixed  // Simplify the Control Flow Graph (e.g.: deleting unreachable blocks)
    fpm->doInitialization();

    // Declare all functions
    for (auto& item : functions) {
        Function& function = item.second;
        function.declare();
    }

    // Recompile and optimize all functions
    for (auto& item : functions) {
        Function& function = item.second;
        llvm::Function* func = function.recompile();
        fpm->run(*func);
    }
    module->dump(); // REMOVE ME
}

bool Segment::contains(u32 addr) const
{
    const u32 from = address;
    const u32 to = address + size;
    return from <= addr && addr < to;
}

}  // namespace ppu
}  // namespace cpu
