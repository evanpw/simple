#include "semantic/unify_trait.hpp"
#include "semantic/subtype.hpp"
#include "semantic/type_functions.hpp"
#include <sstream>

std::pair<bool, std::string> tryUnify(const Trait* lhs, const Trait* rhs)
{
    assert(lhs->prototype() == rhs->prototype());
    assert(lhs->parameters().size() == rhs->parameters().size());

    for (size_t i = 0; i < lhs->parameters().size(); ++i)
    {
        auto result = tryUnify(lhs->parameters()[0], rhs->parameters()[0]);
        if (!result.first)
        {
            return result;
        }
    }

    return {true, ""};
}

std::pair<bool, std::string> tryUnify(const Type* type, const Trait* trait)
{
    if (type->isVariable())
    {
        TypeVariable* var = type->get<TypeVariable>();
        auto& constraints = var->constraints();

        bool found = false;
        for (const Trait* constraint : constraints)
        {
            if (constraint->prototype() == trait->prototype())
            {
                found = true;

                auto result = tryUnify(constraint, trait);
                if (!result.first)
                {
                    return result;
                }

                break;
            }
        }

        if (!found)
        {
            // Quantified type variables can't acquire new constraints
            if (var->quantified())
            {
                std::stringstream ss;
                ss << "Type variable " << type->str() << " does not satisfy constraint " << trait->str();
                return {false, ss.str()};
            }
            else
            {
                var->addConstraint(trait);
            }
        }
    }
    else /* non-variable type */
    {
        bool found = false;

        for (const Trait::Instance& instance : trait->instances())
        {
            if (isSubtype(type, instance.type))
            {
                found = true;

                assert(instance.traitParams.size() == trait->parameters().size());

                for (size_t i = 0; i < instance.traitParams.size(); ++i)
                {
                    auto result = tryUnify(trait->parameters()[i], instance.traitParams[i]);
                    if (!result.first)
                    {
                        return result;
                    }
                }

                break;
            }
        }

        if (!found)
        {
            std::stringstream ss;
            ss << "Type " << type->str() << " is not an instance of trait " << trait->str();
            return {false, ss.str()};
        }
    }

    return {true, ""};
}
