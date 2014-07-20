#include "platform.hpp"
#include "tac_codegen.hpp"
#include "library.h"

#include <iostream>

std::shared_ptr<Address> TACCodeGen::getNameAddress(const Symbol* symbol)
{
    auto i = _names.find(symbol);
    if (i != _names.end())
    {
        return i->second;
    }
    else
    {
        _names[symbol] = std::make_shared<NameAddress>(symbol);
        return _names[symbol];
    }
}

void TACCodeGen::visit(ProgramNode* node)
{
    _currentFunction = &_tacProgram.mainFunction;

    for (auto& child : node->children)
    {
        child->accept(this);
    }

    // The previous loop will have filled in _functions with a list of all
    // other functions. Now generate code for those
    for (FunctionDefNode* funcDefNode : _functions)
    {
        _tacProgram.otherFunctions.emplace_back(funcDefNode->symbol->name);
        _currentFunction = &_tacProgram.otherFunctions.back();

        AstVisitor::visit(funcDefNode);

        // Collect all local variables
        for (auto& local : funcDefNode->scope->symbols.symbols)
        {
            const Symbol* symbol = local.second.get();

            if (symbol->kind == kVariable && !symbol->asVariable()->isParam)
            {
                _currentFunction->locals.push_back(getNameAddress(symbol));
            }
        }

        // Collect all function parameters
        for (Symbol* param : funcDefNode->parameterSymbols)
        {
            assert(param->asVariable()->isParam);
            _currentFunction->params.push_back(getNameAddress(param));
        }
    }

    for (DataDeclaration* dataDeclaration : _dataDeclarations)
    {
        ValueConstructor* constructor = dataDeclaration->valueConstructor;

        _tacProgram.otherFunctions.emplace_back(constructor->name());
        _currentFunction = &_tacProgram.otherFunctions.back();

        createConstructor(dataDeclaration->valueConstructor);

        _tacProgram.otherFunctions.emplace_back("_destroy" + mangle(constructor->name()));
        _currentFunction = &_tacProgram.otherFunctions.back();

        createDestructor(dataDeclaration->valueConstructor);
    }

    for (StructDefNode* structDeclaration : _structDeclarations)
    {
        ValueConstructor* constructor = structDeclaration->valueConstructor;

        _tacProgram.otherFunctions.emplace_back(constructor->name());
        _currentFunction = &_tacProgram.otherFunctions.back();

        createConstructor(structDeclaration->valueConstructor);

        _tacProgram.otherFunctions.emplace_back("_destroy" + mangle(constructor->name()));
        _currentFunction = &_tacProgram.otherFunctions.back();

        createDestructor(structDeclaration->valueConstructor);
    }

    // Gather up all references to external functions and all global variables
    for (auto& i : node->scope->symbols.symbols)
    {
        const std::string& name = i.first;
        const Symbol* symbol = i.second.get();

        if (symbol->kind == kFunction && symbol->asFunction()->isExternal)
        {
            if (!symbol->asFunction()->isForeign)
            {
                _tacProgram.externs.push_back(name);
            }
            else
            {
                _tacProgram.externs.push_back(FOREIGN_NAME(name));
            }
        }
        else if (symbol->kind == kVariable)
        {
            _tacProgram.globals.push_back(getNameAddress(symbol));
        }
    }
}

void TACCodeGen::visit(ComparisonNode* node)
{
    std::shared_ptr<Address> lhs = visitAndGet(*node->lhs);
    std::shared_ptr<Address> rhs = visitAndGet(*node->rhs);

    std::shared_ptr<Label> trueBranch(new Label);
    std::shared_ptr<Label> endLabel(new Label);

    switch(node->op)
    {
        case ComparisonNode::kGreater:
            emit(new TACConditionalJump(lhs, ">", rhs, trueBranch));
            break;

        case ComparisonNode::kLess:
            emit(new TACConditionalJump(lhs, "<", rhs, trueBranch));
            break;

        case ComparisonNode::kEqual:
            emit(new TACConditionalJump(lhs, "==", rhs, trueBranch));
            break;

        case ComparisonNode::kGreaterOrEqual:
            emit(new TACConditionalJump(lhs, ">=", rhs, trueBranch));
            break;

        case ComparisonNode::kLessOrEqual:
            emit(new TACConditionalJump(lhs, "<=", rhs, trueBranch));
            break;

        case ComparisonNode::kNotEqual:
            emit(new TACConditionalJump(lhs, "!=", rhs, trueBranch));
            break;

        default: assert(false);
    }

    node->address = makeTemp();

    emit(new TACAssign(node->address, ConstAddress::False));
    emit(new TACJump(endLabel));
    emit(new TACLabel(trueBranch));
    emit(new TACAssign(node->address, ConstAddress::True));
    emit(new TACLabel(endLabel));
}

void TACCodeGen::visit(LogicalNode* node)
{
    node->address = makeTemp();
    std::shared_ptr<Address> result = node->address;

    std::shared_ptr<Label> endLabel(new Label);

    if (node->op == LogicalNode::kAnd)
    {
        emit(new TACAssign(result, ConstAddress::False));
        std::shared_ptr<Address> lhs = visitAndGet(*node->lhs);
        emit(new TACJumpIfNot(lhs, endLabel));
        std::shared_ptr<Address> rhs = visitAndGet(*node->rhs);
        emit(new TACJumpIfNot(rhs, endLabel));
        emit(new TACAssign(result, ConstAddress::True));
        emit(new TACLabel(endLabel));

        return;
    }
    else if (node->op == LogicalNode::kOr)
    {
        emit(new TACAssign(result, ConstAddress::True));
        std::shared_ptr<Address> lhs = visitAndGet(*node->lhs);
        emit(new TACJumpIf(lhs, endLabel));
        std::shared_ptr<Address> rhs = visitAndGet(*node->rhs);
        emit(new TACJumpIf(rhs, endLabel));
        emit(new TACAssign(result, ConstAddress::False));
        emit(new TACLabel(endLabel));

        return;
    }

    assert(false);
}

void TACCodeGen::visit(NullaryNode* node)
{
    assert(node->symbol->kind == kVariable || node->symbol->kind == kFunction);

    if (node->symbol->kind == kVariable)
    {
        node->address = getNameAddress(node->symbol);
    }
    else
    {
        node->address = makeTemp();
        std::shared_ptr<Address> dest = node->address;

        if (node->type->tag() != ttFunction)
        {
            emit(new TACCall(node->symbol->asFunction()->isForeign, dest, mangle(node->symbol->name)));
        }
        else
        {
            // If the function is not completely applied, then this nullary node
            // evaluates to a function type -- create a closure
            size_t size = sizeof(SplObject) + 8;
            emit(new TACCall(true, dest, FOREIGN_NAME("malloc"), {std::make_shared<ConstAddress>(size)}));

            // SplObject header fields
            emit(new TACLeftIndexedAssignment(dest, offsetof(SplObject, refCount), ConstAddress::Zero));

            emit(new TACLeftIndexedAssignment(
                dest,
                offsetof(SplObject, destructor),
                std::make_shared<NameAddress>(
                    FOREIGN_NAME("__destroyClosure"),
                    NameTag::Function)));

            // Address of the function as an unboxed member
            emit(new TACLeftIndexedAssignment(dest, sizeof(SplObject), getNameAddress(node->symbol)));
        }
    }
}

void TACCodeGen::visit(IntNode* node)
{
    node->address.reset(new ConstAddress(2 * node->value + 1));
}

void TACCodeGen::visit(BoolNode* node)
{
    if (node->value)
    {
        node->address.reset(new ConstAddress(3));
    }
    else
    {
        node->address.reset(new ConstAddress(1));
    }
}

void TACCodeGen::visit(BlockNode* node)
{
    for (auto& child : node->children)
    {
        child->accept(this);
    }
}

void TACCodeGen::visit(IfNode* node)
{
    std::shared_ptr<Address> condition = visitAndGet(*node->condition);

    std::shared_ptr<Label> endLabel(new Label);

    emit(new TACJumpIfNot(condition, endLabel));

    node->body->accept(this);

    emit(new TACLabel(endLabel));
}

void TACCodeGen::visit(IfElseNode* node)
{
    std::shared_ptr<Address> condition = visitAndGet(*node->condition);

    std::shared_ptr<Label> elseLabel(new Label);
    std::shared_ptr<Label> endLabel(new Label);

    emit(new TACJumpIfNot(condition, elseLabel));

    node->body->accept(this);

    emit(new TACJump(endLabel));

    emit(new TACLabel(elseLabel));

    node->else_body->accept(this);

    emit(new TACLabel(endLabel));
}

void TACCodeGen::visit(WhileNode* node)
{
    std::shared_ptr<Label> beginLabel(new Label);
    std::shared_ptr<Label> endLabel(new Label);

    emit(new TACLabel(beginLabel));

    std::shared_ptr<Address> condition = visitAndGet(*node->condition);

    // Push a new inner loop on the (implicit) stack
    std::shared_ptr<Label> prevLoopEnd = _currentLoopEnd;
    _currentLoopEnd = endLabel;

    emit(new TACJumpIfNot(condition, endLabel));

    node->body->accept(this);

    _currentLoopEnd = prevLoopEnd;

    emit(new TACJump(beginLabel));

    emit(new TACLabel(endLabel));
}

void TACCodeGen::visit(BreakNode* node)
{
    emit(new TACJump(_currentLoopEnd));
}

void TACCodeGen::visit(AssignNode* node)
{
    std::shared_ptr<Address> value = visitAndGet(*node->value);

    node->address = getNameAddress(node->symbol);
    std::shared_ptr<Address> dest = node->address;

    // We lose a reference to the original contents, and gain a reference to the
    // new rhs
    if (node->symbol->type->isBoxed())
    {
        // Make sure to incref before decref in case dest currently has the
        // only reference to value
        emit(new TACCall(true, FOREIGN_NAME("_incref"), {value}));
        emit(new TACCall(true, FOREIGN_NAME("_decref"), {dest}));
        emit(new TACAssign(dest, value));
    }
    else
    {
        emit(new TACAssign(dest, value));
    }
}

void TACCodeGen::visit(LetNode* node)
{
    std::shared_ptr<Address> value = visitAndGet(*node->value);

    node->address = getNameAddress(node->symbol);
    std::shared_ptr<Address> dest = node->address;

    // We lose a reference to the original contents, and gain a reference to the
    // new rhs
    if (node->symbol->type->isBoxed())
    {
        // Make sure to incref before decref in case dest currently has the
        // only reference to value
        emit(new TACCall(true, FOREIGN_NAME("_incref"), {value}));
        emit(new TACCall(true, FOREIGN_NAME("_decref"), {dest}));
        emit(new TACAssign(dest, value));
    }
    else
    {
        emit(new TACAssign(dest, value));
    }
}

void TACCodeGen::visit(MatchNode* node)
{
    std::shared_ptr<Address> body = visitAndGet(*node->body);

    // Decrement references to the existing variables
    for (size_t i = 0; i < node->symbols.size(); ++i)
    {
        const Symbol* member = node->symbols.at(i);

        if (member->typeScheme->isBoxed())
        {
            emit(new TACCall(true, FOREIGN_NAME("_decref"), {getNameAddress(member)}));
        }
    }

    FunctionType* functionType = node->constructorSymbol->typeScheme->type()->get<FunctionType>();
    auto& constructor = functionType->output()->valueConstructors().front();

    // Copy over each of the members of the constructor pattern
    for (size_t i = 0; i < node->symbols.size(); ++i)
    {
        const Symbol* member = node->symbols.at(i);
        size_t location = constructor->members().at(i).location;

        emit(new TACRightIndexedAssignment(getNameAddress(member), body, sizeof(SplObject) + 8 * location));
    }

    // Increment references to the new variables
    for (size_t i = 0; i < node->symbols.size(); ++i)
    {
        const Symbol* member = node->symbols.at(i);

        if (member->typeScheme->isBoxed())
        {
            emit(new TACCall(true, FOREIGN_NAME("_incref"), {getNameAddress(member)}));
        }
    }
}

void TACCodeGen::visit(FunctionCallNode* node)
{
    std::vector<std::shared_ptr<Address>> arguments;
    for (auto& i : node->arguments)
    {
        i->accept(this);
        arguments.push_back(i->address);
    }

    node->address = makeTemp();
    std::shared_ptr<Address> result = node->address;

    if (node->symbol->kind == kFunction && node->symbol->asFunction()->isBuiltin)
    {
        if (node->target == "not")
        {
            assert(arguments.size() == 1);

            std::shared_ptr<Label> endLabel(new Label);
            std::shared_ptr<Label> trueBranch(new Label);

            emit(new TACJumpIf(arguments[0], trueBranch));
            emit(new TACAssign(result, ConstAddress::True));
            emit(new TACJump(endLabel));
            emit(new TACLabel(trueBranch));
            emit(new TACAssign(result, ConstAddress::False));
            emit(new TACLabel(endLabel));
        }
        else if (node->target == "head")
        {
            assert(arguments.size() == 1);

            std::shared_ptr<Label> good(new Label);

            emit(new TACConditionalJump(arguments[0], "!=", ConstAddress::Zero, good));
            emit(new TACCall(true, FOREIGN_NAME("_die"), {ConstAddress::Zero}));
            emit(new TACLabel(good));
            emit(new TACRightIndexedAssignment(result, arguments[0], offsetof(List, value)));
        }
        else if (node->target == "tail")
        {
            assert(arguments.size() == 1);

            std::shared_ptr<Label> good(new Label);

            emit(new TACConditionalJump(arguments[0], "!=", ConstAddress::Zero, good));
            emit(new TACCall(true, FOREIGN_NAME("_die"), {ConstAddress::Zero}));
            emit(new TACLabel(good));
            emit(new TACRightIndexedAssignment(result, arguments[0], offsetof(List, next)));
        }
        else if (node->target == "Nil")
        {
            assert(arguments.size() == 0);

            emit(new TACAssign(result, ConstAddress::Zero));
        }
        else if (node->target == "null")
        {
            assert(arguments.size() == 1);

            std::shared_ptr<Label> endLabel(new Label);
            std::shared_ptr<Label> trueBranch(new Label);

            emit(new TACConditionalJump(arguments[0], "==", ConstAddress::Zero, trueBranch));
            emit(new TACAssign(result, ConstAddress::False));
            emit(new TACJump(endLabel));
            emit(new TACLabel(trueBranch));
            emit(new TACAssign(result, ConstAddress::True));
            emit(new TACLabel(endLabel));
        }
        else if (node->target == "+")
        {
            assert(arguments.size() == 2);
            emit(new TACBinaryOperation(result, arguments[0], "+", arguments[1]));
        }
        else if (node->target == "-")
        {
            assert(arguments.size() == 2);
            emit(new TACBinaryOperation(result, arguments[0], "-", arguments[1]));
        }
        else if (node->target == "*")
        {
            assert(arguments.size() == 2);
            emit(new TACBinaryOperation(result, arguments[0], "*", arguments[1]));
        }
        else if (node->target == "/")
        {
            assert(arguments.size() == 2);
            emit(new TACBinaryOperation(result, arguments[0], "/", arguments[1]));
        }
        else if (node->target == "%")
        {
            assert(arguments.size() == 2);
            emit(new TACBinaryOperation(result, arguments[0], "%", arguments[1]));
        }
        else
        {
            assert(false);
        }
    }
    else if (node->symbol->kind == kFunction)
    {
        emit(new TACCall(node->symbol->asFunction()->isForeign, result, mangle(node->symbol->name), arguments));
    }
    else /* node->symbol->kind == kVariable */
    {
        // The variable represents a closure, so extract the actual function
        // address
        std::shared_ptr<Address> functionAddress = makeTemp();
        emit(new TACRightIndexedAssignment(functionAddress, getNameAddress(node->symbol), sizeof(SplObject)));
        emit(new TACIndirectCall(result, functionAddress, arguments));
    }
}

void TACCodeGen::visit(ReturnNode* node)
{
    std::shared_ptr<Address> result = visitAndGet(*node->expression);
    emit(new TACReturn(result));
}

void TACCodeGen::visit(VariableNode* node)
{
    assert(node->symbol->kind == kVariable);
    node->address = getNameAddress(node->symbol);
}

void TACCodeGen::visit(MemberAccessNode* node)
{
    std::shared_ptr<Address> varAddress(getNameAddress(node->varSymbol));

    node->address = makeTemp();
    std::shared_ptr<Address> result = node->address;

    emit(new TACRightIndexedAssignment(result, varAddress, sizeof(SplObject) + 8 * node->memberLocation));
}

void TACCodeGen::visit(StructDefNode* node)
{
    _structDeclarations.push_back(node);
}

void TACCodeGen::visit(MemberDefNode* node)
{
}

void TACCodeGen::visit(TypeAliasNode* node)
{
}

void TACCodeGen::visit(FunctionDefNode* node)
{
    // Do the code generation for this function later, after we've generated
    // code for the main function
    _functions.push_back(node);
}

void TACCodeGen::visit(DataDeclaration* node)
{
    _dataDeclarations.push_back(node);
}

void TACCodeGen::createConstructor(ValueConstructor* constructor)
{
    const std::vector<ValueConstructor::MemberDesc> members = constructor->members();

    // For now, every member takes up exactly 8 bytes (either directly or as a pointer).
    size_t size = sizeof(SplObject) + 8 * members.size();

    // Allocate room for the object
    std::shared_ptr<Address> result = makeTemp();
    emit(new TACCall(
        true,
        result,
        FOREIGN_NAME("malloc"),
        {std::make_shared<ConstAddress>(size)}));

    //// Fill in the members with the constructor arguments

    // SplObject header fields
    emit(new TACLeftIndexedAssignment(result, offsetof(SplObject, refCount), ConstAddress::Zero));

    std::string destructor = "_destroy" + mangle(constructor->name());
    emit(new TACLeftIndexedAssignment(
        result,
        offsetof(SplObject, destructor),
        std::make_shared<NameAddress>(destructor, NameTag::Function)));

    std::shared_ptr<Address> t = makeTemp();
    for (size_t i = 0; i < members.size(); ++i)
    {
        auto& member = members[i];
        size_t location = member.location;

        std::shared_ptr<Address> param(new NameAddress(member.name, NameTag::Param));
        _currentFunction->params.push_back(param);

        emit(new TACLeftIndexedAssignment(t, sizeof(SplObject) + 8 * location, param));

        // Assigning into this structure gives a new reference to each member
        if (member.type->isBoxed())
        {
            emit(new TACCall(true, FOREIGN_NAME("_incref"), {param}));
        }
    }

    emit(new TACReturn(result));
}

void TACCodeGen::createDestructor(ValueConstructor* constructor)
{
    const std::vector<ValueConstructor::MemberDesc> members = constructor->members();

    std::string destructorName = "_destroy" + mangle(constructor->name());

    std::shared_ptr<Address> param(new NameAddress("object", NameTag::Param));
    _currentFunction->params.push_back(param);

    for (size_t i = 0; i < members.size(); ++i)
    {
        auto& member = members[i];
        size_t location = member.location;

        if (member.type->isBoxed())
        {
            std::shared_ptr<Address> temp = makeTemp();

            emit(new TACRightIndexedAssignment(temp, param, sizeof(SplObject) + 8 * location));
            emit(new TACCall(true, FOREIGN_NAME("_decref"), {temp}));
        }
    }

    emit(new TACCall(true, FOREIGN_NAME("free"), {param}));
}