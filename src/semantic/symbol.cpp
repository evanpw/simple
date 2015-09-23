#include "semantic/symbol.hpp"
#include "ast/ast.hpp"

VariableSymbol::VariableSymbol(const std::string& name, AstNode* node, FunctionDefNode* enclosingFunction, bool global)
: Symbol(name, kVariable, node, enclosingFunction, global)
{
}

FunctionSymbol::FunctionSymbol(const std::string& name, AstNode* node, FunctionDefNode* definition)
: Symbol(name, kFunction, node, nullptr, true)
, definition(definition)
{
}

ConstructorSymbol::ConstructorSymbol(const std::string& name, AstNode* node, ValueConstructor* constructor)
: FunctionSymbol(name, node, nullptr)
, constructor(constructor)
{
    isConstructor = true;
    isForeign = true;
}

TypeSymbol::TypeSymbol(const std::string& name, AstNode* node, Type* type)
: Symbol(name, kType, node, nullptr, true)
{
    this->type = type;
}

TypeConstructorSymbol::TypeConstructorSymbol(const std::string& name, AstNode* node, TypeConstructor* typeConstructor)
: Symbol(name, kTypeConstructor, node, nullptr, true)
, typeConstructor(typeConstructor)
{
}

MemberSymbol::MemberSymbol(const std::string& name, Kind kind, AstNode* node, Type* parentType, size_t index)
: Symbol(name, kind, node, nullptr, true)
, parentType(parentType)
, index(index)
{
}

MethodSymbol::MethodSymbol(const std::string& name, FunctionDefNode* node, Type* parentType, size_t index)
: MemberSymbol(name, kMethod, node, parentType, index)
, definition(node)
{
}

MemberVarSymbol::MemberVarSymbol(const std::string& name, AstNode* node, Type* parentType, size_t index, size_t location)
: MemberSymbol(name, kMemberVar, node, parentType, index)
, location(location)
{
}