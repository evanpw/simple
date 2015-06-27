#include "address.hpp"
#include "mangler.hpp"
#include <sstream>

NameAddress& Address::asName()
{
    return dynamic_cast<NameAddress&>(*this);
}

ConstAddress& Address::asConst()
{
    return dynamic_cast<ConstAddress&>(*this);
}

TempAddress& Address::asTemp()
{
    return dynamic_cast<TempAddress&>(*this);
}

std::shared_ptr<Address> Address::Null;

std::string NameAddress::str() const
{
    return name;
}

NameAddress::NameAddress(const Symbol* symbol)
: Address(AddressTag::Name)
{
    name = mangle(symbol->name);

    if (symbol->kind == kVariable)
    {
        if (symbol->asVariable()->isStatic)
        {
            nameTag = NameTag::Static;
        }
        else if (symbol->asVariable()->isParam)
        {
            nameTag = NameTag::Param;
        }
        else if (symbol->enclosingFunction == nullptr)
        {
            nameTag = NameTag::Global;
        }
        else
        {
            nameTag = NameTag::Local;
        }
    }
    else if (symbol->kind == kFunction)
    {
        nameTag = NameTag::Function;
    }
    else
    {
        assert(false);
    }
}

NameAddress::NameAddress(const std::string& name, NameTag nameTag)
: Address(AddressTag::Name)
, name("_" + name)
, nameTag(nameTag)
{
}

std::string ConstAddress::str() const
{
    std::stringstream ss;
    ss << value;
    return ss.str();
}

std::string TempAddress::str() const
{
    std::stringstream ss;
    ss << "%" << number;
    return ss.str();
}

std::shared_ptr<ConstAddress> ConstBoolAddress::True(new ConstBoolAddress(3));
std::shared_ptr<ConstAddress> ConstBoolAddress::False(new ConstBoolAddress(1));

std::shared_ptr<ConstAddress> ConstAddress::UnboxedZero(new ConstAddress(0));
std::shared_ptr<ConstAddress> ConstAddress::UnboxedOne(new ConstAddress(1));

std::string ConstBoolAddress::str() const
{
    if (value == 1)
    {
        return "true";
    }
    else if (value == 3)
    {
        return "false";
    }

    assert(false);
}
