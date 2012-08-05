#include <list>
#include "ast.hpp"
#include "ast_visitor.hpp"

void AstVisitor::visit(ProgramNode* node)
{
	for (std::list<AstNode*>::const_iterator i = node->children().begin(); i != node->children().end(); ++i)
	{
		(*i)->accept(this);	
	}
}

void AstVisitor::visit(NotNode* node)
{
	node->child()->accept(this);
}

void AstVisitor::visit(ComparisonNode* node)
{
	node->lhs()->accept(this);
	node->rhs()->accept(this);
}

void AstVisitor::visit(BinaryOperatorNode* node)
{
	node->lhs()->accept(this);
	node->rhs()->accept(this);
}

void AstVisitor::visit(BlockNode* node)
{
	for (std::list<StatementNode*>::const_iterator i = node->children().begin(); i != node->children().end(); ++i)
	{
		(*i)->accept(this);	
	}
}

void AstVisitor::visit(IfNode* node)
{
	node->condition()->accept(this);
	node->body()->accept(this);
}

void AstVisitor::visit(IfElseNode* node)
{
	node->condition()->accept(this);
	node->body()->accept(this);
	node->else_body()->accept(this);
}

void AstVisitor::visit(PrintNode* node)
{
	node->expression()->accept(this);
}

void AstVisitor::visit(ReadNode* node)
{
	node->target()->accept(this);
}

void AstVisitor::visit(AssignNode* node)
{
	node->target()->accept(this);
	node->value()->accept(this);
}