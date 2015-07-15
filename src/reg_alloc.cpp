#include "reg_alloc.hpp"
#include "machine_context.hpp"
#include <algorithm>
#include <deque>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stack>

std::ostream& operator<<(std::ostream& out, const RegSet& regs)
{
    out << "{";

    bool first = true;
    for (Reg* reg : regs)
    {
        if (first)
        {
            out << *reg;
            first = false;
        }
        else
        {
            out << ", " << *reg;
        }
    }
    out << "}";

    return out;
}

RegSet& operator+=(RegSet& lhs, const RegSet& rhs)
{
    std::set_union(
        lhs.begin(), lhs.end(),
        rhs.begin(), rhs.end(),
        std::inserter(lhs, lhs.end()));

    return lhs;
}

RegSet& operator-=(RegSet& lhs, const RegSet& rhs)
{
    RegSet tmp;

    std::set_difference(
        lhs.begin(), lhs.end(),
        rhs.begin(), rhs.end(),
        std::inserter(tmp, tmp.begin()));

    lhs.swap(tmp);
    return lhs;
}

void RegAlloc::run(MachineFunction* function)
{
    _function = function;
    _context = _function->context;

    colorGraph();
    replaceRegs();
    assignStackLocations();
    spillAroundCalls();

    allocateStack();
}

void RegAlloc::spillAroundCalls()
{
    // Recompute liveness information now that we've replace virtual registers
    // and done some other rewriting
    gatherDefinitions();
    gatherUses();
    computeLiveness();
    computeInterference();

    // Keep track of the furthest stack offset that we ever need, so that we
    // know how much space to allocate
    int64_t startOffset = _currentOffset;

    // Before each call instruction, spill every live register except rsp and
    // rbp, and restore them all afterwards
    for (MachineBB* block : _function->blocks)
    {
        // Compute live regs at the end of this block
        RegSet regs;
        for (MachineBB* succ : block->successors())
        {
            regs += _live.at(succ);
        }

        // Step through the instructions from back to front, updating live regs
        for (auto i = block->instructions.rbegin(); i != block->instructions.rend(); ++i)
        {
            MachineInst* inst = *i;

            // Data flow equation:
            // live[n] = (U_{s in succ[n]}  live[s]) - def[n] + ref[n]

            for (Reg* output : inst->outputs)
            {
                if (output->isRegister())
                {
                    if (regs.find(output) != regs.end())
                        regs.erase(output);
                }
            }

            for (Reg* input : inst->inputs)
            {
                if (input->isRegister())
                {
                    regs.insert(input);
                }
            }

            if (inst->opcode == Opcode::CALLi || inst->opcode == Opcode::CALLm)
            {
                int64_t offset = startOffset;

                std::vector<MachineInst*> saves;
                std::vector<MachineInst*> restores;
                for (Reg* liveReg : regs)
                {
                    // rbp and rsp are caller-save
                    if (liveReg == _context->rbp || liveReg == _context->rsp)
                        continue;

                    offset -= 8;
                    _currentOffset = std::min(_currentOffset, offset);

                    MachineInst* saveInst = new MachineInst(Opcode::MOVmd, {}, {_context->rsp, liveReg, new Immediate(offset)});
                    saves.push_back(saveInst);

                    MachineInst* restoreInst = new MachineInst(Opcode::MOVrm, {liveReg}, {_context->rsp, new Immediate(offset)});
                    restores.push_back(restoreInst);
                }

                // Insert restores "before" the current instruction, using a reverse iterator, so that
                // they end up after
                assert(saves.size() == restores.size());

                for (MachineInst* restoreInst : restores)
                {
                    block->instructions.insert(i.base(), restoreInst);
                    ++i;
                }

                // Insert save instructions "after" the current instruction
                auto next = i;
                ++next;
                for (MachineInst* saveInst : saves)
                {
                    block->instructions.insert(next.base(), saveInst);
                }
            }
        }
    }
}

Immediate* RegAlloc::getStackOffset(MachineOperand* operand)
{
    if (StackParameter* param = dynamic_cast<StackParameter*>(operand))
    {
        return new Immediate(16 + 8 * param->index);
    }
    else
    {
        StackLocation* stackLocation = dynamic_cast<StackLocation*>(operand);
        assert(stackLocation);

        auto itr = _stackOffsets.find(stackLocation);
        if (itr != _stackOffsets.end())
        {
            return itr->second;
        }
        else
        {
            _stackOffsets[stackLocation] = new Immediate(_currentOffset - 8);
            _currentOffset -= 8;

            return _stackOffsets[stackLocation];
        }
    }
}

void RegAlloc::assignStackLocations()
{
    _stackOffsets.clear();
    _currentOffset = 0;

    for (MachineBB* block : _function->blocks)
    {
        for (MachineInst* inst : block->instructions)
        {
            // Replace inputs
            for (size_t j = 0; j < inst->inputs.size(); ++j)
            {
                if (inst->inputs[j]->isStackLocation())
                {
                    if (inst->opcode == Opcode::MOVrm)
                    {
                        assert(inst->inputs.size() == 1);

                        MachineOperand* operand = inst->inputs[0];
                        inst->inputs[0] = _context->rbp;
                        inst->inputs.push_back(getStackOffset(operand));
                        break;
                    }
                    else if (inst->opcode == Opcode::MOVmd)
                    {
                        assert(inst->inputs.size() == 2);

                        MachineOperand* operand = inst->inputs[0];
                        inst->inputs[0] = _context->rbp;
                        inst->inputs.push_back(getStackOffset(operand));
                        break;
                    }
                    else
                    {
                        assert(false);
                    }
                }
            }

            // Replace outputs
            for (size_t j = 0; j < inst->outputs.size(); ++j)
            {
                if (inst->outputs[j]->isStackLocation())
                    std::cerr << *inst << std::endl;

                assert(!inst->outputs[j]->isStackLocation());
            }
        }
    }
}

void RegAlloc::allocateStack()
{
    // In the entry block, allocate room for the stack variables
    if (_currentOffset != 0)
    {
        // Round up to mantain 16-byte alignment
        if (_currentOffset % 16)
            _currentOffset -= 8;

        MachineBB* entryBlock = *(_function->blocks.begin());

        auto itr = entryBlock->instructions.begin();

        // HACK: First two instructions will always be push rbp; mov rbp, rsp
        ++itr;
        ++itr;

        MachineInst* allocInst = new MachineInst(Opcode::ADD, {_context->rsp}, {_context->rsp, new Immediate(_currentOffset)});
        entryBlock->instructions.insert(itr, allocInst);
    }
}

void RegAlloc::replaceRegs()
{
    for (MachineBB* block : _function->blocks)
    {
        for (MachineInst* inst : block->instructions)
        {
            // Replace inputs
            for (size_t j = 0; j < inst->inputs.size(); ++j)
            {
                if (inst->inputs[j]->isVreg())
                    inst->inputs[j] = _context->hregs[_coloring[inst->inputs[j]]];
            }

            // Replace outputs
            for (size_t j = 0; j < inst->outputs.size(); ++j)
            {
                if (inst->outputs[j]->isVreg())
                    inst->outputs[j] = _context->hregs[_coloring[inst->outputs[j]]];
            }
        }
    }
}

void RegAlloc::gatherDefinitions()
{
    _definitions.clear();

    for (MachineBB* block : _function->blocks)
    {
        RegSet result;

        for (MachineInst* inst : block->instructions)
        {
            for (Reg* output : inst->outputs)
            {
                if (output->isRegister())
                    result.insert(output);
            }
        }

        _definitions[block] = result;
    }
}

void RegAlloc::gatherUses()
{
    _uses.clear();

    for (MachineBB* block : _function->blocks)
    {
        RegSet result;
        RegSet defined;

        for (MachineInst* inst : block->instructions)
        {
            for (Reg* input : inst->inputs)
            {
                if (input->isRegister() && (defined.find(input) == defined.end()))
                    result.insert(input);
            }

            for (Reg* output : inst->outputs)
            {
                if (output->isRegister())
                    defined.insert(output);
            }
        }

        _uses[block] = result;
    }
}

void RegAlloc::computeLiveness()
{
    _live.clear();

    // Iterate until nothing changes
    while (true)
    {
        bool changed = false;

        for (MachineBB* block : _function->blocks)
        {
            RegSet regs;

            // Data flow equation:
            // live[n] = (U_{s in succ[n]}  live[s]) - def[n] + ref[n]
            for (MachineBB* succ : block->successors())
            {
                regs += _live[succ];
            }

            regs -= _definitions[block];
            regs += _uses[block];

            if (_live[block] != regs)
            {
                _live[block] = regs;
                changed = true;
            }
        }

        if (!changed)
            break;
    }

    dumpLiveness();
}

void RegAlloc::dumpLiveness() const
{
    std::cerr << "Liveness:" << std::endl;
    for (MachineBB* block : _function->blocks)
    {
        std::cerr << "label " << *block << ":" << std::endl;

        std::cerr << "\tref: " << _uses.at(block) << std::endl;
        std::cerr << "\tdef: " << _definitions.at(block) << std::endl;
        std::cerr << "\tlive: " << _live.at(block) << std::endl;
    }

    std::cerr << std::endl;
}

void RegAlloc::computeInterference()
{
    _igraph.clear();
    _precolored.clear();

    std::cerr << "Instruction-level liveness" << std::endl;
    for (MachineBB* block : _function->blocks)
    {
        std::cerr << "block " << *block << ":" << std::endl;

        // Compute live regs at the end of this block
        RegSet regs;
        for (MachineBB* succ : block->successors())
        {
            regs += _live.at(succ);
        }

        std::deque<RegSet> liveRegs;

        // Step through the instructions from back to front, updating live regs
        for (auto i = block->instructions.rbegin(); i != block->instructions.rend(); ++i)
        {
            MachineInst* inst = *i;

            // Data flow equation:
            // live[n] = (U_{s in succ[n]}  live[s]) - def[n] + ref[n]

            for (Reg* output : inst->outputs)
            {
                if (output->isRegister())
                {
                    if (regs.find(output) != regs.end())
                        regs.erase(output);
                }
            }

            for (Reg* input : inst->inputs)
            {
                if (input->isRegister())
                {
                    regs.insert(input);
                }
            }

            liveRegs.push_front(regs);
        }

        auto itr = block->instructions.begin();
        for (size_t i = 0; i < block->instructions.size(); ++i)
        {
            std::stringstream ss;
            ss << *(*itr);

            std::cerr << "\t" << std::setw(40) << std::left << ss.str() << "\t" << liveRegs[i] << std::endl;

            for (Reg* reg1 : liveRegs[i])
            {
                for (Reg* reg2 : liveRegs[i])
                {
                    if (reg1 == reg2) continue;

                    _igraph[reg1].insert(reg2);
                    _igraph[reg2].insert(reg1);
                }
            }

            ++itr;
        }
    }
    std::cerr << std::endl;

    // All hardware registers are pre-colored
    for (size_t i = 0; i < 16; ++i)
    {
        Reg* hreg = _context->hregs[i];
        if (_igraph.find(hreg) != _igraph.end())
        {
            _precolored[hreg] = i;
        }
    }

    // Add an inteference edge between every pair of precolored vertices
    // (Not necessary for correct coloring, but it makes the igraph look right)
    for (auto& i : _precolored)
    {
        for (auto& j : _precolored)
        {
            if (i.first != j.first)
            {
                _igraph[i.first].insert(j.first);
                _igraph[j.first].insert(i.first);
            }

        }
    }
}

// For generating the DOT file of the colored interference graph
static std::string palette[16] =
{
    "#000000",
    "#9D9D9D",
    "#FFFFFF",
    "#BE2633",
    "#E06F8B",
    "#493C2B",
    "#A46422",
    "#EB8931",
    "#F7E26B",
    "#2F484E",
    "#44891A",
    "#A3CE27",
    "#FF00FF",
    "#005784",
    "#31A2F2",
    "#B2DCEF",
};

static bool whiteText[16] =
{
    true,
    false,
    false,
    true,
    false,
    true,
    false,
    false,
    false,
    true,
    true,
    false,
    false,
    true,
    false,
    false,
};

static std::string colorNames[16] =
{
    "rax",
    "rbx",
    "rcx",
    "rdx",
    "rsi",
    "rdi",
    "rbp",
    "rsp",
    "r8",
    "r9",
    "r10",
    "r11",
    "r12",
    "r13",
    "r14",
    "r15",
};

void RegAlloc::removeFromGraph(IntGraph& graph, Reg* reg)
{
    for (Reg* other : graph[reg])
    {
        graph[other].erase(reg);
    }

    graph.erase(reg);
}

void RegAlloc::addVertexBack(IntGraph& graph, Reg* reg)
{
    for (Reg* other : _igraph.at(reg))
    {
        graph[reg].insert(other);
        graph[other].insert(reg);
    }
}

bool RegAlloc::findColorFor(const IntGraph& graph, Reg* reg)
{
    std::set<size_t> used;
    for (Reg* other : graph.at(reg))
    {
        auto i = _coloring.find(other);
        if (i != _coloring.end())
        {
            used.insert(i->second);
        }
    }

    // Handle pre-colored vertices
    auto itr = _precolored.find(reg);
    if (itr != _precolored.end())
    {
        assert(used.find(itr->second) == used.end());
        _coloring[reg] = itr->second;
        return true;
    }

    if (used.size() < AVAILABLE_COLORS)
    {
        for (size_t i = 0; i < AVAILABLE_COLORS; ++i)
        {
            if (used.find(i) == used.end())
            {
                _coloring[reg] = i;
                return true;
            }
        }

        assert(false);
    }
    else
    {
        return false;
    }
}

void RegAlloc::spillVariable(Reg* reg)
{
    VirtualRegister* vreg = dynamic_cast<VirtualRegister*>(reg);
    assert(vreg);

    std::stringstream ss;
    ss << "vreg" << vreg->id;
    StackLocation* spillLocation = new StackLocation(ss.str());

    _spilled[reg] = spillLocation;

    // Add code to spill and restore this register for each definition and use
    for (MachineBB* block : _function->blocks)
    {
        for (auto i = block->instructions.begin(); i != block->instructions.end(); ++i)
        {
            MachineInst* inst = *i;

            // Instruction uses the spilled register
            if (std::find(inst->inputs.begin(), inst->inputs.end(), reg) != inst->inputs.end())
            {
                // Load from the stack into a fresh register
                MachineOperand* newReg = _function->makeVreg();
                MachineInst* loadInst = new MachineInst(Opcode::MOVrm, {newReg}, {spillLocation});
                block->instructions.insert(i, loadInst);

                // Replace all uses of the spilled register with the new one
                for (size_t j = 0; j < inst->inputs.size(); ++j)
                {
                    if (inst->inputs[j] == reg)
                        inst->inputs[j] = newReg;
                }
            }

            // Instruction defines the spilled register
            if (std::find(inst->outputs.begin(), inst->outputs.end(), reg) != inst->outputs.end())
            {
                // Create a fresh register to store the result
                MachineOperand* newReg = _function->makeVreg();

                // Replace all uses of the spilled register with the new one
                for (size_t j = 0; j < inst->outputs.size(); ++j)
                {
                    if (inst->outputs[j] == reg)
                        inst->outputs[j] = newReg;
                }

                // Store back into the stack spill location when finished
                MachineInst* storeInst = new MachineInst(Opcode::MOVmd, {}, {spillLocation, newReg});

                auto next = i;
                ++next;
                block->instructions.insert(next, storeInst);
            }
        }
    }
}

void RegAlloc::colorGraph()
{
    _spilled.clear();

    do
    {
        gatherDefinitions();
        gatherUses();
        computeLiveness();
        computeInterference();

    } while (!tryColorGraph());
}

bool RegAlloc::tryColorGraph()
{
    _coloring.clear();

    IntGraph graph = _igraph;

    std::stack<Reg*> stack;

    // While there is a vertex with degree < k, remove it from the graph and add
    // it to the stack
    while (graph.size() > _precolored.size())
    {
        bool found = false;

        for (auto& item : graph)
        {
            Reg* reg = item.first;
            auto others = item.second;

            if (_precolored.find(reg) != _precolored.end())
                continue;

            if (others.size() < AVAILABLE_COLORS)
            {
                stack.push(reg);
                removeFromGraph(graph, reg);
                found = true;
                break;
            }
        }

        // If there are no such vertices, then we may have to spill something.
        // Put off this decision until the next stage
        if (!found)
        {
            for (auto& item : graph)
            {
                Reg* reg = item.first;

                if (_precolored.find(reg) == _precolored.end())
                {
                    stack.push(reg);
                    removeFromGraph(graph, reg);
                    found = true;
                    break;
                }
            }

            assert(found);
        }
    }

    // Handle pre-colored vertices last (hardware registers)
    for (auto& item : _precolored)
    {
        Reg* hreg = item.first;

        stack.push(hreg);
        removeFromGraph(graph, hreg);
    }

    assert(graph.empty());

    // Pop off the vertices in order, add them back to the graph, and assign
    // a color
    while (!stack.empty())
    {
        Reg* reg = stack.top();
        stack.pop();

        addVertexBack(graph, reg);
        bool success = findColorFor(graph, reg);

        // If we can't color this vertex, then we must spill it and try again
        if (!success)
        {
            spillVariable(reg);
            return false;
        }
    }

    return true;
}

void RegAlloc::dumpGraph() const
{
    std::string fname = std::string("dots/") + "interference-" + _function->name + ".dot";
    std::fstream f(fname.c_str(), std::ios::out);

    f << "graph {" << std::endl;
    f << "node[fontname=\"Inconsolata\"]" << ";" << std::endl;

    std::unordered_set<Reg*> finished;

    for (auto& item : _igraph)
    {
        Reg* reg = item.first;
        auto others = item.second;

        size_t color = _coloring.at(reg);

        f << "\"\\" << *reg << "\" [fillcolor=\"" << palette[color] << "\", style=filled";
        if (whiteText[color])
            f << ", fontcolor=white";
        f << "];" << std::endl;

        for (Reg* other : others)
        {
            if (finished.find(other) == finished.end())
                f << "\"\\" << *reg << "\" -- " << "\"\\" << *other << "\"" << ";" << std::endl;
        }

        finished.insert(reg);
    }

    for (auto& item : _spilled)
    {
        f << "\"\\" << *(item.first) << " (spilled)\";" << std::endl;
    }

    f << "}" << std::endl;
}