#include "parser/parser.hpp"
#include "exceptions.hpp"
#include "parser/tokens.hpp"

#include <iostream>
#include <boost/lexical_cast.hpp>

//// Lexing machinery //////////////////////////////////////////////////////////

// Two tokens of look-ahead
Token nextTokens[2];

// Calls the lexer
Token yylex();

void advance()
{
    nextTokens[0] = nextTokens[1];

    if (nextTokens[0].type != tEOF)
    {
        nextTokens[1] = yylex();
    }
}

void initialize()
{
    advance();
    advance();
}

//// Parsing machinery /////////////////////////////////////////////////////////

YYLTYPE getLocation()
{
    return nextTokens[0].location;
}

bool accept(TokenType t)
{
    if (nextTokens[0].type == t)
    {
        advance();
        return true;
    }

    return false;
}

bool accept(char c) { return accept((TokenType)c); }

Token expect(TokenType t)
{
    if (nextTokens[0].type == t)
    {
        Token token = nextTokens[0];
        advance();
        return token;
    }

    YYLTYPE location = getLocation();

    std::stringstream ss;

    ss << location.filename << ":" << location.first_line << ":" << location.first_column
       << ": expected " << tokenToString(t) << ", but got "
       << tokenToString(nextTokens[0].type);

    throw LexerError(ss.str());
}

Token expect(char c) { return expect((TokenType)c); }

TokenType peekType()
{
    return nextTokens[0].type;
}

TokenType peek2ndType()
{
    return nextTokens[1].type;
}

void Parser::parse()
{
    initialize();
    program();
}

////////////////////////////////////////////////////////////////////////////////
//// Grammar ///////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

//// Statements ////////////////////////////////////////////////////////////////

ProgramNode* Parser::program()
{
    ProgramNode* node = new ProgramNode(_context, getLocation());

    while (!accept(tEOF))
    {
        node->children.push_back(statement());
    }

    _context->setRoot(node);

    return node;
}

StatementNode* Parser::statement()
{
    switch(peekType())
    {
    case tIF:
        return if_statement();

    case tASSERT:
        return assert_statement();

    case tDATA:
        return data_declaration();

    case tTYPE:
        return type_alias_declaration();

    case tDEF:
        return function_definition();

    case tFOR:
        return for_statement();

    case tFOREIGN:
        return foreign_declaration();

    case tFOREVER:
        return forever_statement();

    case tLET:
        return let_statement();

    case tMATCH:
        return match_statement();

    case tRETURN:
        return return_statement();

    case tSTRUCT:
        return struct_declaration();

    case tWHILE:
        return while_statement();

    case tVAR:
        return variable_declaration();

    case tBREAK:
        return break_statement();

    case tIMPL:
        return implementation_block();

    case tLIDENT:
        if (peek2ndType() == tCOLON_EQUAL)
        {
            return variable_declaration();
        }

        // Else fallthrough

    default:
        return assign_or_expr();
    }
}

// Like an if statement, but doesn't match IF first
StatementNode* Parser::if_helper(const YYLTYPE& location)
{
    ExpressionNode* condition = expression();
    StatementNode* ifBody = suite();

    YYLTYPE intermediateLocation = getLocation();
    if (accept(tELIF))
    {
        StatementNode* elseBody = if_helper(intermediateLocation);
        return new IfElseNode(_context, location, condition, ifBody, elseBody);
    }
    else if (accept(tELSE))
    {
        StatementNode* elseBody = suite();
        return new IfElseNode(_context, location, condition, ifBody, elseBody);
    }
    else
    {
        return new IfNode(_context, location, condition, ifBody);
    }
}

StatementNode* Parser::if_statement()
{
    YYLTYPE location = getLocation();

    expect(tIF);
    return if_helper(location);
}

/// assert_statement
///     : ASSERT expression EOL
StatementNode* Parser::assert_statement()
{
    YYLTYPE location = getLocation();

    expect(tASSERT);
    ExpressionNode* condition = expression();
    expect(tEOL);

    return new AssertNode(_context, location, condition);
}

/// data_declaration
///     : DATA UIDENT type_params '=' constructor_spec { '|' constructor_spec } EOL
StatementNode* Parser::data_declaration()
{
    YYLTYPE location = getLocation();

    expect(tDATA);
    Token name = expect(tUIDENT);

    std::vector<std::string> typeParameters = type_params();

    expect('=');

    std::vector<ConstructorSpec*> specs;
    specs.push_back(constructor_spec());
    while (accept('|'))
    {
        specs.push_back(constructor_spec());
    }

    expect(tEOL);

    return new DataDeclaration(_context, location, name.value.str, typeParameters, specs);
}

StatementNode* Parser::type_alias_declaration()
{
    YYLTYPE location = getLocation();

    expect(tTYPE);
    Token name = expect(tUIDENT);
    expect('=');
    TypeName* typeName = type();
    expect(tEOL);

    return new TypeAliasNode(_context, location, name.value.str, typeName);
}

StatementNode* Parser::function_definition()
{
    YYLTYPE location = getLocation();

    expect(tDEF);
    std::string name = ident();
    std::vector<std::string> typeParams = type_params();
    std::pair<std::vector<std::string>, TypeName*> paramsAndTypes = params_and_types();

    StatementNode* body = suite();
    return new FunctionDefNode(_context, location, name, body, typeParams, paramsAndTypes.first, paramsAndTypes.second);
}

StatementNode* Parser::foreign_declaration()
{
    YYLTYPE location = getLocation();

    expect(tFOREIGN);
    std::string name = ident();
    std::vector<std::string> typeParams = type_params();
    std::pair<std::vector<std::string>, TypeName*> paramsAndTypes = params_and_types();
    expect(tEOL);

    return new ForeignDeclNode(_context, location, name, typeParams, paramsAndTypes.first, paramsAndTypes.second);
}

StatementNode* Parser::for_statement()
{
    YYLTYPE location = getLocation();

    expect(tFOR);
    Token loopVar = expect(tLIDENT);

    if (accept(tIN))
    {
        ExpressionNode* listExpression = expression();
        StatementNode* body = suite();

        return new ForeachNode(_context, location, loopVar.value.str, listExpression, body);
    }
    else
    {
        expect('=');
        ExpressionNode* fromExpression = expression();
        expect(tTO);
        ExpressionNode* toExpression = expression();
        StatementNode* body = suite();

        return new ForNode(_context, location, loopVar.value.str, fromExpression, toExpression, body);
    }
}

StatementNode* Parser::forever_statement()
{
    YYLTYPE location = getLocation();

    expect(tFOREVER);
    StatementNode* body = suite();

    return new ForeverNode(_context, location, body);
}

StatementNode* Parser::let_statement()
{
    YYLTYPE location = getLocation();

    expect(tLET);
    Token constructor = expect(tUIDENT);
    std::vector<std::string> params = parameters();
    expect(tCOLON_EQUAL);
    ExpressionNode* body = expression();
    expect(tEOL);

    return new LetNode(_context, location, constructor.value.str, params, body);
}

/// match_statement: MATCH expression EOL match_body
/// match_body: INDENT match_arm { match_arm } DEDENT
StatementNode* Parser::match_statement()
{
    YYLTYPE location = getLocation();

    expect(tMATCH);
    ExpressionNode* expr = expression();
    expect(tEOL);
    expect(tINDENT);

    std::vector<MatchArm*> arms;
    while (!accept(tDEDENT))
    {
        arms.push_back(match_arm());
    }

    return new MatchNode(_context, location, expr, std::move(arms));
}

/// match_arm
///     : UIDENT parameters ( '=>' statement | EOL INDENT statement_list DEDENT)
MatchArm* Parser::match_arm()
{
    YYLTYPE location = getLocation();
    Token constructor = expect(tUIDENT);
    std::vector<std::string> params = parameters();

    if (accept(tEOL))
    {
        expect(tINDENT);

        BlockNode* block = new BlockNode(_context, getLocation());
        while (peekType() != tDEDENT)
        {
            block->children.push_back(statement());
        }

        expect(tDEDENT);

        return new MatchArm(_context, location, constructor.value.str, params, block);
    }
    else
    {
        expect(tDARROW);
        StatementNode* body = statement();
        return new MatchArm(_context, location, constructor.value.str, params, body);
    }
}

StatementNode* Parser::return_statement()
{
    YYLTYPE location = getLocation();

    expect(tRETURN);
    ExpressionNode* value = expression();
    expect(tEOL);

    return new ReturnNode(_context, location, value);
}

/// struct_declaration
///     : STRUCT UIDENT type_params '=' members
StatementNode* Parser::struct_declaration()
{
    YYLTYPE location = getLocation();

    expect(tSTRUCT);
    Token name = expect(tUIDENT);

    std::vector<std::string> typeParams = type_params();

    std::vector<MemberDefNode*> memberList = members();

    return new StructDefNode(_context, location, name.value.str, std::move(memberList), std::move(typeParams));
}

StatementNode* Parser::while_statement()
{
    YYLTYPE location = getLocation();

    expect(tWHILE);
    ExpressionNode* condition = expression();
    StatementNode* body = suite();

    return new WhileNode(_context, location, condition, body);
}

StatementNode* Parser::assign_or_expr()
{
    YYLTYPE location = getLocation();

    ExpressionNode* lhs = expression();

    if (accept('='))
    {
        ExpressionNode* rhs = expression();

        expect(tEOL);

        return new AssignNode(_context, location, lhs, rhs);
    }
    else if (peekType() == tPLUS_EQUAL ||
             peekType() == tMINUS_EQUAL ||
             peekType() == tTIMES_EQUAL ||
             peekType() == tDIV_EQUAL)
    {
        std::vector<ExpressionNode*> argList;
        argList.push_back(lhs);

        std::string functionName;
        switch (peekType())
        {
        case tPLUS_EQUAL:
            expect(tPLUS_EQUAL);
            functionName = '+';
            break;

        case tMINUS_EQUAL:
            expect(tMINUS_EQUAL);
            functionName = '-';
            break;

        case tTIMES_EQUAL:
            expect(tTIMES_EQUAL);
            functionName = '*';
            break;

        case tDIV_EQUAL:
            expect(tDIV_EQUAL);
            functionName = '/';
            break;

        default:
            assert(false);
        }

        ExpressionNode* rhs = expression();
        argList.push_back(rhs);

        expect(tEOL);

        return new AssignNode(_context, location, lhs, new FunctionCallNode(_context, location, functionName, std::move(argList)));
    }
    else
    {
        expect(tEOL);
        return lhs;
    }
}

StatementNode* Parser::variable_declaration()
{
    YYLTYPE location = getLocation();

    if (peekType() == tLIDENT)
    {
        Token varName = expect(tLIDENT);
        TypeName* varType = accept(':') ? type() : nullptr;
        expect(tCOLON_EQUAL);
        ExpressionNode* value = expression();
        expect(tEOL);

        return new VariableDefNode(_context, location, varName.value.str, varType, value);
    }
    else
    {
        expect(tVAR);
        Token varName = expect(tLIDENT);
        TypeName* varType = accept(':') ? type() : nullptr;
        expect('=');
        ExpressionNode* value = expression();
        expect(tEOL);

        return new VariableDefNode(_context, location, varName.value.str, varType, value);
    }
}

StatementNode* Parser::break_statement()
{
    YYLTYPE location = getLocation();

    expect(tBREAK);
    expect(tEOL);

    return new BreakNode(_context, location);
}

/// implementation_block
///     : IMPL type_params type EOL INDENT method_definition { method_definition } DEDENT
StatementNode* Parser::implementation_block()
{
    YYLTYPE location = getLocation();

    expect(tIMPL);

    std::vector<std::string> typeParams = type_params();
    TypeName* typeName = type();

    expect(tEOL);
    expect(tINDENT);

    std::vector<MethodDefNode*> methods;
    while (peekType() == tDEF)
    {
        MethodDefNode* method = dynamic_cast<MethodDefNode*>(method_definition());
        assert(method);

        methods.push_back(method);
    }

    expect(tDEDENT);

    return new ImplNode(_context, location, std::move(typeParams), typeName, std::move(methods));
}

StatementNode* Parser::method_definition()
{
    YYLTYPE location = getLocation();

    expect(tDEF);
    std::string name = ident();
    std::vector<std::string> typeParams = type_params();
    std::pair<std::vector<std::string>, TypeName*> paramsAndTypes = params_and_types();

    StatementNode* body = suite();
    return new MethodDefNode(_context, location, name, body, typeParams, paramsAndTypes.first, paramsAndTypes.second);
}


//// Miscellaneous /////////////////////////////////////////////////////////////

StatementNode* Parser::suite()
{
    if (accept(tEOL))
    {
        expect(tINDENT);

        BlockNode* block = new BlockNode(_context, getLocation());
        while (peekType() != tDEDENT)
        {
            block->children.push_back(statement());
        }

        expect(tDEDENT);

        return block;
    }
    else
    {
        expect(':');
        return statement();
    }
}

std::vector<std::string> Parser::parameters()
{
    std::vector<std::string> result;

    if (accept('('))
    {
        Token param = expect(tLIDENT);
        result.push_back(param.value.str);

        while (accept(','))
        {
            Token param = expect(tLIDENT);
            result.push_back(param.value.str);
        }

        expect(')');
    }

    return result;
}

std::string Parser::ident()
{
    YYLTYPE location = getLocation();

    Token name;
    if (peekType() == tLIDENT)
    {
        name = expect(tLIDENT);
    }
    else if (peekType() == tUIDENT)
    {
        name = expect(tUIDENT);
    }
    else
    {
        std::stringstream ss;

        ss << location.filename << ":" << location.first_line << ":" << location.first_column
           << ": expected identifier, but got "
           << tokenToString(nextTokens[0].type);

        throw LexerError(ss.str());
    }

    return name.value.str;
}

//// Types /////////////////////////////////////////////////////////////////////

/// type
///     : '|' [ arrow_type { ',' arrow_type } ] '|' RARROW constructed_type
///     | arrow_type
TypeName* Parser::type()
{
    YYLTYPE location = getLocation();

    if (!accept('|')) return arrow_type();

    TypeName* typeName = new TypeName(_context, location, "Function");

    if (peekType() != '|')
    {
        typeName->parameters.push_back(arrow_type());

        while (accept(','))
        {
            typeName->parameters.push_back(arrow_type());
        }
    }

    expect('|');
    expect(tRARROW);

    // Return type
    typeName->parameters.push_back(constructed_type());

    return typeName;
}

/// arrow_type
///     : constructed_type [ RARROW constructed_type ]
TypeName* Parser::arrow_type()
{
    YYLTYPE location = getLocation();

    TypeName* firstType = constructed_type();
    if (accept(tRARROW))
    {
        TypeName* functionType = new TypeName(_context, location, "Function");
        functionType->parameters.push_back(firstType);
        functionType->parameters.push_back(constructed_type());

        return functionType;
    }
    else
    {
        return firstType;
    }
}

/// constructed_type
///     : UIDENT [ '<' type { ',' type } '>' ]
///     | simple_type
TypeName* Parser::constructed_type()
{
    YYLTYPE location = getLocation();

    if (peekType() == tUIDENT)
    {
        Token name = expect(tUIDENT);
        TypeName* typeName = new TypeName(_context, location, name.value.str);

        if (accept('<'))
        {
            typeName->parameters.push_back(type());
            while (accept(','))
            {
                typeName->parameters.push_back(type());
            }

            expect('>');
        }

        return typeName;
    }
    else
    {
        return simple_type();
    }
}

/// simple_type
///     : LIDENT
///     | UIDENT
///     | '[' type ']'
///     | '(' type ')'
TypeName* Parser::simple_type()
{
    YYLTYPE location = getLocation();

    if (peekType() == tUIDENT)
    {
        Token typeName = expect(tUIDENT);
        return new TypeName(_context, location, typeName.value.str);
    }
    else if (peekType() == tLIDENT)
    {
        Token typeName = expect(tLIDENT);
        return new TypeName(_context, location, typeName.value.str);
    }
    else if (peekType() == '(')
    {
        expect('(');
        TypeName* internalType = type();
        expect(')');

        return internalType;
    }
    else
    {
        expect('[');
        TypeName* internalType = type();
        expect(']');

        TypeName* typeName = new TypeName(_context, location, "List");
        typeName->parameters.push_back(internalType);

        return typeName;
    }
}

/// constructor_spec
///     : UIDENT [ '(' type { ',' type } ')' ]
ConstructorSpec* Parser::constructor_spec()
{
    YYLTYPE location = getLocation();
    Token name = expect(tUIDENT);

    ConstructorSpec* constructorSpec = new ConstructorSpec(_context, location, name.value.str);
    if (accept('('))
    {
        constructorSpec->members.push_back(type());
        while (accept(','))
        {
            constructorSpec->members.push_back(type());
        }

        expect(')');
    }

    return constructorSpec;
}

std::pair<std::string, TypeName*> Parser::param_and_type()
{
    Token param = expect(tLIDENT);
    expect(':');
    TypeName* typeName = type();

    return {param.value.str, typeName};
}

/// type_declaration
///     : '(' [ LIDENT ':' type { ',' LIDENT ':' type } ] ')' RARROW constructed_type
///
/// param_and_type
///     : LIDENT ':' type
std::pair<std::vector<std::string>, TypeName*> Parser::params_and_types()
{
    YYLTYPE location = getLocation();

    expect('(');

    std::vector<std::string> params;
    TypeName* typeName = new TypeName(_context, location, "Function");

    if (peekType() == tLIDENT)
    {
        std::pair<std::string, TypeName*> param_type = param_and_type();
        params.push_back(param_type.first);
        typeName->parameters.push_back(param_type.second);

        while (accept(','))
        {
            param_type = param_and_type();
            params.push_back(param_type.first);
            typeName->parameters.push_back(param_type.second);
        }
    }

    expect(')');
    expect(tRARROW);

    // Return type
    typeName->parameters.push_back(constructed_type());

    return {params, typeName};
}

std::vector<std::string> Parser::type_params()
{
    if (peekType() != '<')
        return {};

    std::vector<std::string> result;

    expect('<');

    Token typeVar = expect(tUIDENT);
    result.push_back(typeVar.value.str);
    while (accept(','))
    {
        Token typeVar = expect(tUIDENT);
        result.push_back(typeVar.value.str);
    }

    expect('>');

    return result;
}

//// Structures ///////////////////////////////////////////////////////////////

std::vector<MemberDefNode*> Parser::members()
{
    std::vector<MemberDefNode*> memberList;

    expect(tEOL);
    expect(tINDENT);

    while (peekType() != tDEDENT)
    {
        memberList.push_back(member_definition());
    }

    expect(tDEDENT);

    return memberList;
}

MemberDefNode* Parser::member_definition()
{
    YYLTYPE location = getLocation();

    Token name = expect(tLIDENT);
    expect(':');
    TypeName* typeName = type();
    expect(tEOL);

    return new MemberDefNode(_context, location, name.value.str, typeName);
}

//// Expressions ///////////////////////////////////////////////////////////////

ExpressionNode* Parser::expression()
{
    ExpressionNode* lhs = and_expression();

    if (accept(tOR))
    {
        return new LogicalNode(_context, getLocation(), lhs, LogicalNode::kOr, expression());
    }
    else
    {
        return lhs;
    }
}

ExpressionNode* Parser::and_expression()
{
    ExpressionNode* lhs = equality_expression();

    if (accept(tAND))
    {
        return new LogicalNode(_context, getLocation(), lhs, LogicalNode::kAnd, and_expression());
    }
    else
    {
        return lhs;
    }
}

ExpressionNode* Parser::equality_expression()
{
    ExpressionNode* lhs = relational_expression();

    if (accept(tEQUALS))
    {
        return new ComparisonNode(_context, getLocation(), lhs, ComparisonNode::kEqual, relational_expression());
    }
    else if (accept(tNE))
    {
        return new ComparisonNode(_context, getLocation(), lhs, ComparisonNode::kNotEqual, relational_expression());
    }
    else
    {
        return lhs;
    }
}

ExpressionNode* Parser::relational_expression()
{
    ExpressionNode* lhs = cons_expression();

    if (accept('>'))
    {
        return new ComparisonNode(_context, getLocation(), lhs, ComparisonNode::kGreater, cons_expression());
    }
    else if (accept('<'))
    {
        return new ComparisonNode(_context, getLocation(), lhs, ComparisonNode::kLess, cons_expression());
    }
    else if (accept(tGE))
    {
        return new ComparisonNode(_context, getLocation(), lhs, ComparisonNode::kGreaterOrEqual, cons_expression());
    }
    else if (accept(tLE))
    {
        return new ComparisonNode(_context, getLocation(), lhs, ComparisonNode::kLessOrEqual, cons_expression());
    }
    else
    {
        return lhs;
    }
}

ExpressionNode* Parser::cons_expression()
{
    ExpressionNode* lhs = additive_expression();

    if (accept(tDCOLON))
    {
        return new FunctionCallNode(_context, getLocation(), "Cons", {lhs, cons_expression()});
    }
    else
    {
        return lhs;
    }
}

ExpressionNode* Parser::additive_expression()
{
    ExpressionNode* result = multiplicative_expression();

    while (peekType() == '+' || peekType() == '-')
    {
        if (accept('+'))
        {
            result = new FunctionCallNode(_context, getLocation(), "+", {result, multiplicative_expression()});
        }
        else
        {
            expect('-');
            result = new FunctionCallNode(_context, getLocation(), "-", {result, multiplicative_expression()});
        }
    }

    return result;
}

ExpressionNode* Parser::multiplicative_expression()
{
    ExpressionNode* result = concat_expression();

    while (peekType() == '*' || peekType() == '/' || peekType() == tMOD)
    {
        if (accept('*'))
        {
            result = new FunctionCallNode(_context, getLocation(), "*", {result, concat_expression()});
        }
        else if (accept('/'))
        {
            result = new FunctionCallNode(_context, getLocation(), "/", {result, concat_expression()});
        }
        else
        {
            expect(tMOD);
            result = new FunctionCallNode(_context, getLocation(), "%", {result, concat_expression()});
        }
    }

    return result;
}

/// concat_expression
///     : negation_expression [ '++' concat_expression ]
ExpressionNode* Parser::concat_expression()
{
    ExpressionNode* lhs = negation_expression();

    if (accept(tCONCAT))
    {
        return new MethodCallNode(_context, getLocation(), lhs, "concat", {concat_expression()});
    }
    else
    {
        return lhs;
    }
}

/// negation_expression
///     : index_expression
///     | '-' index_expression
///     | NOT index_expression
ExpressionNode* Parser::negation_expression()
{
    if (accept('-'))
    {
        return new FunctionCallNode(_context, getLocation(), "-", {new IntNode(_context, getLocation(), 0), index_expression()});
    }
    else if (accept(tNOT))
    {
        return new FunctionCallNode(_context, getLocation(), "not", {index_expression()});
    }
    else
    {
        return index_expression();
    }
}

/// index_expression
///     : method_or_member_expression
///     | method_or_member_expression '[' expression ']'
ExpressionNode* Parser::index_expression()
{
    YYLTYPE location = getLocation();

    ExpressionNode* lhs = method_or_member_expression();
    if (accept('['))
    {
        ExpressionNode* index = expression();
        expect(']');

        return new MethodCallNode(_context, location, lhs, "at", {index});
    }
    else
    {
        return lhs;
    }
}

/// method_or_member_expression
///     : func_call_expression
///     | func_call_expression '.' LIDENT
///     | func_call_expression '.' LIDENT '(' [ expression ] { ',' expression } ] ')'
ExpressionNode* Parser::method_or_member_expression()
{
    YYLTYPE location = getLocation();

    ExpressionNode* expr = func_call_expression();
    while (accept('.'))
    {
        Token name = expect(tLIDENT);

        if (accept('('))
        {
            std::vector<ExpressionNode*> argList;
            if (!accept(')'))
            {
                argList.push_back(expression());

                while (accept(','))
                {
                    argList.push_back(expression());
                }

                expect(')');
            }

            expr = new MethodCallNode(_context, location, expr, name.value.str, std::move(argList));
        }
        else
        {
            expr = new MemberAccessNode(_context, location, expr, name.value.str);
        }
    }

    return expr;
}

/// func_call_expression
///     : ident '$' expression
///     | ident '(' [ expression ] { ',' expression } ] ')'
///     | unary_expression
ExpressionNode* Parser::func_call_expression()
{
    if ((peekType() == tLIDENT || peekType() == tUIDENT) && (peek2ndType() == '(' || peek2ndType() == '$'))
    {
        YYLTYPE location = getLocation();

        std::string functionName = ident();

        std::vector<ExpressionNode*> argList;
        if (accept('$'))
        {
            argList.push_back(expression());
        }
        else
        {
            expect('(');

            if (!accept(')'))
            {
                argList.push_back(expression());

                while (accept(','))
                {
                    argList.push_back(expression());
                }

                expect(')');
            }
        }

        return new FunctionCallNode(_context, location, functionName, std::move(argList));
    }
    else
    {
        return unary_expression();
    }
}

ExpressionNode* Parser::unary_expression()
{
    YYLTYPE location = getLocation();

    switch (peekType())
    {
    case '(':
    {
        expect('(');
        ExpressionNode* interior = expression();
        expect(')');

        return interior;
    }

    case tTRUE:
        expect(tTRUE);
        return new BoolNode(_context, location, true);

    case tFALSE:
        expect(tFALSE);
        return new BoolNode(_context, location, false);

    case '[':
        expect('[');
        if (accept(']'))
        {
            return new FunctionCallNode(_context, location, "Nil", {});
        }
        else
        {
            std::vector<ExpressionNode*> argList;
            argList.push_back(expression());

            while (accept(','))
            {
                argList.push_back(expression());
            }

            expect(']');

            return createList(_context, location, argList);
        }

    case tINT_LIT:
    {
        Token token = expect(tINT_LIT);
        return new IntNode(_context, location, token.value.number);
    }

    case tSTRING_LIT:
    {
        Token token = expect(tSTRING_LIT);
        return new StringLiteralNode(_context, location, token.value.str);
    }

    case tLIDENT:
    case tUIDENT:
        return new NullaryNode(_context, location, ident());

    default:
    {
        std::stringstream ss;

        ss << location.filename << ":" << location.first_line << ":" << location.first_column
           << ": token " << tokenToString(peekType()) << " cannot start a unary expression.";

        throw LexerError(ss.str());
    }

    }
}

