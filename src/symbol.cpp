#include "symbol.hpp"

VariableSymbol* Symbol::asVariable()
{
    return dynamic_cast<VariableSymbol*>(this);
}

FunctionSymbol* Symbol::asFunction()
{
    return dynamic_cast<FunctionSymbol*>(this);
}

TypeSymbol* Symbol::asType()
{
    return dynamic_cast<TypeSymbol*>(this);
}

TypeConstructorSymbol* Symbol::asTypeConstructor()
{
    return dynamic_cast<TypeConstructorSymbol*>(this);
}

MemberSymbol* Symbol::asMember()
{
    return dynamic_cast<MemberSymbol*>(this);
}

const VariableSymbol* Symbol::asVariable() const
{
    return dynamic_cast<const VariableSymbol*>(this);
}

const FunctionSymbol* Symbol::asFunction() const
{
    return dynamic_cast<const FunctionSymbol*>(this);
}

const TypeSymbol* Symbol::asType() const
{
    return dynamic_cast<const TypeSymbol*>(this);
}

const TypeConstructorSymbol* Symbol::asTypeConstructor() const
{
    return dynamic_cast<const TypeConstructorSymbol*>(this);
}

const MemberSymbol* Symbol::asMember() const
{
    return dynamic_cast<const MemberSymbol*>(this);
}

VariableSymbol::VariableSymbol(const std::string& name, AstNode* node, FunctionDefNode* enclosingFunction)
: Symbol(name, kVariable, node, enclosingFunction)
{
}

FunctionSymbol::FunctionSymbol(const std::string& name, AstNode* node, FunctionDefNode* definition)
: Symbol(name, kFunction, node, nullptr)
, definition(definition)
{
}

TypeSymbol::TypeSymbol(const std::string& name, AstNode* node, Type* type)
: Symbol(name, kType, node, nullptr)
{
    this->type = type;
}

TypeConstructorSymbol::TypeConstructorSymbol(const std::string& name, AstNode* node, TypeConstructor* typeConstructor)
: Symbol(name, kTypeConstructor, node, nullptr)
, typeConstructor(typeConstructor)
{
}

MemberSymbol::MemberSymbol(const std::string& name, AstNode* node)
: Symbol(name, kMember, node, nullptr)
{
}
