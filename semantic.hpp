#ifndef SEMANTIC_HPP
#define SEMANTIC_HPP

#include "ast.hpp"

class SemanticAnalyzer
{
public:
	SemanticAnalyzer(ProgramNode* root);
	bool analyze();

private:
	ProgramNode* root_;
};

// Semantic analysis pass 1 - declarations
class SemanticPass1 : public AstVisitor
{
public:
	SemanticPass1();
	virtual void visit(LabelNode* node);
	virtual void visit(VariableNode* node);
	virtual void visit(FunctionDefNode* node);
	virtual void visit(ReadNode* node);
	virtual void visit(AssignNode* node);

	bool success() const { return success_; }

private:
	bool success_;
};

// Semantic analysis pass 2 - gotos / function calls
class SemanticPass2 : public AstVisitor
{
public:
	SemanticPass2();
	virtual void visit(GotoNode* node);
	virtual void visit(FunctionCallNode* node);

	bool success() const { return success_; }

private:
	bool success_;
};

// Pass 3
class TypeChecker : public AstVisitor
{
public:
	TypeChecker();

	// Internal nodes
	virtual void visit(ProgramNode* node);
	virtual void visit(NotNode* node);
	virtual void visit(ComparisonNode* node);
	virtual void visit(BinaryOperatorNode* node);
	virtual void visit(LogicalNode* node);
	virtual void visit(BlockNode* node);
	virtual void visit(IfNode* node);
	virtual void visit(IfElseNode* node);
	virtual void visit(PrintNode* node);
	virtual void visit(ReadNode* node);
	virtual void visit(WhileNode* node);
	virtual void visit(AssignNode* node);
	virtual void visit(FunctionDefNode* node);

	// Leaf nodes
	virtual void visit(LabelNode* node);
	virtual void visit(VariableNode* node);
	virtual void visit(IntNode* node);
	virtual void visit(GotoNode* node);
	virtual void visit(FunctionCallNode* node);

	bool success() const { return success_; }

private:
	bool typeCheck(AstNode* node, Type type);

	bool success_;
};

#endif
