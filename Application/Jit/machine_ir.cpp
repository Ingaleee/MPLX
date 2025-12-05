#include "machine_ir.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace mplx::jit {
    MachineIR lower_machine_ir(const VerifiedModule &module) {
        const auto &bc = module.bytecode();
        const auto &verified = module.verification();
        MachineIR result;
        result.instructions.resize(verified.instructions.size());
        std::set<uint32_t> leaders;
        for (const auto &function : bc.functions)
            leaders.insert(function.entry);
        for (const auto &instruction : verified.instructions) {
            if (instruction.op == OP_JMP || instruction.op == OP_JMP_IF_FALSE ||
                instruction.op == OP_JMP_IF_TRUE)
                leaders.insert(instruction.operand);
            if (instruction.op == OP_JMP_IF_FALSE || instruction.op == OP_JMP_IF_TRUE ||
                instruction.op == OP_CALL)
                leaders.insert(instruction.next);
        }
        std::vector<uint32_t> functions;
        for (uint32_t function = 0; function < bc.functions.size(); ++function)
            functions.push_back(function);
        std::sort(functions.begin(), functions.end(),
                  [&](auto a, auto b) { return bc.functions[a].entry < bc.functions[b].entry; });
        std::map<uint32_t, uint32_t> block_at;
        std::map<uint32_t, uint32_t> active_slots;
        uint32_t previous_next = UINT32_MAX, block = no_machine_value;
        size_t region = 0;
        for (uint32_t index = 0; index < verified.instructions.size(); ++index) {
            const auto &instruction = verified.instructions[index];
            auto &machine = result.instructions[index];
            machine.pc = instruction.offset;
            if (instruction.op == OP_HALT || verified.stack_heights[index] < 0)
                continue;
            while (region + 1 < functions.size() &&
                   instruction.offset >= bc.functions[functions[region + 1]].entry)
                ++region;
            const auto function = functions[region];
            if (block == no_machine_value || leaders.contains(instruction.offset) ||
                previous_next != instruction.offset || result.blocks[block].function != function) {
                block = static_cast<uint32_t>(result.blocks.size());
                result.blocks.push_back({function, index, index, {}});
                block_at.emplace(instruction.offset, block);
                active_slots.clear();
            }
            machine.function = function;
            machine.block = block;
            result.blocks[block].last = index;
            previous_next = instruction.next;
            const auto height = static_cast<uint32_t>(verified.stack_heights[index]);
            const auto locals = bc.functions[function].locals;
            auto read = [&](uint32_t slot) {
                auto found = active_slots.find(slot);
                if (found == active_slots.end()) {
                    const auto value = static_cast<uint32_t>(result.values.size());
                    result.values.push_back({slot, block, no_machine_value, {}});
                    found = active_slots.emplace(slot, value).first;
                }
                machine.reads.push_back(found->second);
                result.values[found->second].uses.push_back(instruction.offset);
            };
            auto write = [&](uint32_t slot) {
                const auto value = static_cast<uint32_t>(result.values.size());
                result.values.push_back({slot, block, instruction.offset, {}});
                active_slots[slot] = value;
                machine.writes.push_back(value);
            };
            if (instruction.op == OP_PUSH_CONST)
                write(locals + height);
            else if ((instruction.op >= OP_LD0 && instruction.op <= OP_LD3) ||
                     instruction.op == OP_LOAD_LOCAL || instruction.op == OP_LOAD_LOCAL8) {
                read(instruction.op >= OP_LD0 && instruction.op <= OP_LD3 ? instruction.op - OP_LD0
                                                                          : instruction.operand);
                write(locals + height);
            } else if ((instruction.op >= OP_ST0 && instruction.op <= OP_ST3) ||
                       instruction.op == OP_STORE_LOCAL || instruction.op == OP_STORE_LOCAL8) {
                read(locals + height - 1);
                write(instruction.op >= OP_ST0 && instruction.op <= OP_ST3 ? instruction.op - OP_ST0
                                                                           : instruction.operand);
            } else if (instruction.op == OP_RET || instruction.op == OP_JMP_IF_FALSE ||
                       instruction.op == OP_JMP_IF_TRUE) {
                read(locals + height - 1);
            } else if (instruction.op == OP_NEG || instruction.op == OP_NOT ||
                       instruction.op == OP_CHECK_I32) {
                read(locals + height - 1);
                write(locals + height - 1);
            } else if (instruction.op == OP_CALL) {
                const auto arguments = bc.functions[instruction.operand].arity;
                for (uint32_t argument = 0; argument < arguments; ++argument)
                    read(locals + height - arguments + argument);
            } else if (instruction.op != OP_POP && instruction.op != OP_JMP) {
                read(locals + height - 2);
                read(locals + height - 1);
                write(locals + height - 2);
            }
            if (instruction.op == OP_JMP || instruction.op == OP_RET)
                previous_next = UINT32_MAX;
        }
        for (auto &machine_block : result.blocks) {
            const auto &last = verified.instructions[machine_block.last];
            auto edge = [&](uint32_t pc) {
                const auto found = block_at.find(pc);
                if (found == block_at.end())
                    throw std::runtime_error("machine IR edge has no target block");
                if (std::find(machine_block.successors.begin(), machine_block.successors.end(),
                              found->second) == machine_block.successors.end())
                    machine_block.successors.push_back(found->second);
            };
            if (last.op == OP_JMP || last.op == OP_JMP_IF_FALSE || last.op == OP_JMP_IF_TRUE)
                edge(last.operand);
            if (last.op != OP_JMP && last.op != OP_RET)
                edge(last.next);
        }
        return result;
    }
    std::string dump_machine_ir_json(const MachineIR &module) {
        auto ids = [](const std::vector<uint32_t> &values) {
            std::string text = "[";
            for (size_t index = 0; index < values.size(); ++index) {
                if (index)
                    text += ',';
                text += std::to_string(values[index]);
            }
            return text + ']';
        };
        std::string result = "{\"schema_version\":1,\"allocation_scope\":\"basic-block\",\"blocks\":[";
        for (size_t index = 0; index < module.blocks.size(); ++index) {
            if (index)
                result += ',';
            const auto &block = module.blocks[index];
            result += "{\"id\":" + std::to_string(index) + ",\"function\":" + std::to_string(block.function) +
                      ",\"first_instruction\":" + std::to_string(block.first) +
                      ",\"last_instruction\":" + std::to_string(block.last) +
                      ",\"successors\":" + ids(block.successors) + "}";
        }
        result += "],\"instructions\":[";
        bool comma = false;
        for (const auto &instruction : module.instructions) {
            if (instruction.block == no_machine_value)
                continue;
            if (comma)
                result += ',';
            comma = true;
            result += "{\"pc\":" + std::to_string(instruction.pc) +
                      ",\"block\":" + std::to_string(instruction.block) +
                      ",\"reads\":" + ids(instruction.reads) + ",\"writes\":" + ids(instruction.writes) + "}";
        }
        result += "],\"values\":[";
        for (size_t index = 0; index < module.values.size(); ++index) {
            if (index)
                result += ',';
            const auto &value = module.values[index];
            result += "{\"id\":" + std::to_string(index) + ",\"slot\":" + std::to_string(value.slot) +
                      ",\"block\":" + std::to_string(value.block) + ",\"definition\":" +
                      (value.definition == no_machine_value ? std::string("null")
                                                            : std::to_string(value.definition)) +
                      ",\"uses\":" + ids(value.uses) + "}";
        }
        return result + "]}";
    }
} // namespace mplx::jit
