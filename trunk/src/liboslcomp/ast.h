/*****************************************************************************
 *
 *             Copyright (c) 2009 Sony Pictures Imageworks, Inc.
 *                            All rights reserved.
 *
 *  This material contains the confidential and proprietary information
 *  of Sony Pictures Imageworks, Inc. and may not be disclosed, copied or
 *  duplicated in any form, electronic or hardcopy, in whole or in part,
 *  without the express prior written consent of Sony Pictures Imageworks,
 *  Inc. This copyright notice does not imply publication.
 *
 *****************************************************************************/

#ifndef AST_H
#define AST_H

#include "OpenImageIO/ustring.h"
#include "OpenImageIO/thread.h"
#include "OpenImageIO/refcnt.h"
#include "oslcomp.h"


class oslFlexLexer;
extern int oslparse ();


namespace OSL {
namespace pvt {


// Forward declarations
class OSLCompilerImpl;
class Symbol;
class TypeSpec;



/// Base node for an abstract syntax tree for the OSL parser.
///
class ASTNode : public RefCnt {
public:
    typedef intrusive_ptr<ASTNode> ref;  ///< Ref-counted pointer to an ASTNode

    /// List of all the types of AST nodes.
    ///
    enum NodeType {
        unknown_node, shader_declaration_node,
        variable_declaration_node,
        variable_ref_node,
        conditional_statement_node, 
        loop_statement_node, loopmod_statement_node,
        binary_expression_node, unary_expression_node,
        assign_expression_node, ternary_expression_node, 
        typecast_expression_node,
        _last_node
    };

    ASTNode (NodeType nodetype, OSLCompilerImpl *compiler);

    ASTNode (NodeType nodetype, OSLCompilerImpl *compiler, int op,
             ASTNode *a);
    ASTNode (NodeType nodetype, OSLCompilerImpl *compiler, int op,
             ASTNode *a, ASTNode *b);
    ASTNode (NodeType nodetype, OSLCompilerImpl *compiler, int op,
             ASTNode *a, ASTNode *b, ASTNode *c);
    ASTNode (NodeType nodetype, OSLCompilerImpl *compiler, int op,
             ASTNode *a, ASTNode *b, ASTNode *c, ASTNode *d);

    virtual ~ASTNode () { }

    virtual void print (int indentlevel = 0) const;

    /// What type of node is this?
    ///
    NodeType nodetype () const { return m_nodetype; }

    /// Name of this type of node
    ///
    virtual const char *nodetypename () const = 0;

    /// Name of the op, if any, or NULL.
    ///
    virtual const char *opname () const { return NULL; }

    /// Name of the child node
    ///
    virtual const char *childname (size_t i) const = 0;

    /// Append a new node (specified by raw pointer) onto the end of the
    /// sequence that *this belongs to.  Return *this.
    ASTNode *append (ASTNode *newnode) {
        ASTNode *n = this;
        while (n->nextptr())
            n = n->nextptr();
        // Now n points to the last in the sequence
        n->m_next = newnode;
        return this;
    }

    /// Append a new node (specified by a reference-counted pointer)
    /// onto the end of the sequence that *this belongs to.  Return an
    /// reference-counted pointer to *this.
    ref append (ref &x) { append (x.get()); return this; }

    /// Concatenate ASTNode sequences A and B, returning a raw pointer to
    /// the concatenated sequence.  This is robust to either A or B or
    /// both being NULL.
    friend ASTNode *concat (ASTNode *A, ASTNode *B) {
        if (A)  // A is valid, B may or may not be
            return B ? A->append (B) : A;
        else    // A not valid, so just go with B
            return B;
    }

    /// What source file was this parse node created from?
    ///
    ustring sourcefile () const { return m_sourcefile; }

    /// What line of the source file was this parse node created from?
    ///
    int sourceline () const { return m_sourceline; }

protected:
    /// Return a reference-counted pointer to the next node in the sequence.
    ///
    ref next () const { return m_next; }

    /// Return the raw pointer to the next node in the sequence.  Use
    /// with caution!
    ASTNode *nextptr () const { return m_next.get(); }

    void indent (int indentlevel=0) const {
        while (indentlevel--)
            std::cout << "    ";
    }

    static void printlist (const ref &A, int indentlevel) {
        for (const ASTNode *n = A.get();  n;  n = n->nextptr())
            n->print (indentlevel);
    }

    /// Return the i-th child node, or NULL if there is no such node
    ///
    ASTNode *child (size_t i) const {
        return (i < m_children.size()) ? m_children[i].get() : NULL;
    }

    /// Add a new node to the list of children.
    ///
    void addchild (ASTNode *n) { m_children.push_back (n); }

    virtual void printchildren (int indentlevel = 0) const;


protected:
    NodeType m_nodetype;    ///< Type of node this is
    ref m_next;             ///< Next node in the list
    OSLCompilerImpl *m_compiler;  ///< Back-pointer to the compiler
    ustring m_sourcefile;   ///< Filename of source where the node came from
    int m_sourceline;       ///< Line number in source where the node came from
    std::vector<ref> m_children;  ///< Child nodes
    int m_op;                     ///< Operator selection

private:
};



class ASTshader_declaration : public ASTNode
{
public:
    ASTshader_declaration (OSLCompilerImpl *comp, int stype, ustring name,
                           ASTNode *form, ASTNode *stmts, ASTNode *meta) 
        : ASTNode (shader_declaration_node, comp, stype, meta, form, stmts),
          m_shadername(name)
    { }
    const char *nodetypename () const { return "shader_declaration"; }
    const char *childname (size_t i) const;
    void print (int indentlevel=0) const;

    ref metadata () const { return child (0); }
    ref formals () const { return child (1); }
    ref statements () const { return child (2); }
private:
    ustring m_shadername;
};



class ASTvariable_declaration : public ASTNode
{
public:
    ASTvariable_declaration (OSLCompilerImpl *comp, const TypeSpec &type,
                             ustring name, ASTNode *init);
    const char *nodetypename () const { return "variable_declaration"; }
    const char *childname (size_t i) const;
    void print (int indentlevel=0) const;

    ref init () const { return child (0); }
private:
    ustring m_name;
    Symbol *m_sym;
};



class ASTvariable_ref : public ASTNode
{
public:
    ASTvariable_ref (OSLCompilerImpl *comp, ustring name);
    const char *nodetypename () const { return "variable_ref"; }
    const char *childname (size_t i) const;
    void print (int indentlevel=0) const;
private:
    ustring m_name;
    Symbol *m_sym;
};



class ASTconditional_statement : public ASTNode
{
public:
    ASTconditional_statement (OSLCompilerImpl *comp, ASTNode *cond,
                              ASTNode *truestmt, ASTNode *falsestmt=NULL)
        : ASTNode (conditional_statement_node, comp, 0, 
                   cond, truestmt, falsestmt)
    { }

    const char *nodetypename () const { return "conditional_statement"; }
    const char *childname (size_t i) const;

    ref cond () const { return child (0); }
    ref truestmt () const { return child (1); }
    ref falsestmt () const { return child (2); }
};



class ASTloop_statement : public ASTNode
{
public:
    enum LoopType {
        LoopWhile, LoopDo, LoopFor
    };

    ASTloop_statement (OSLCompilerImpl *comp, LoopType looptype, ASTNode *init,
                       ASTNode *cond, ASTNode *iter, ASTNode *stmt)
        : ASTNode (loop_statement_node, comp, looptype, init, cond, iter, stmt)
    { }

    const char *nodetypename () const { return "loop_statement"; }
    const char *childname (size_t i) const;
    const char *opname () const;

    ref cond () const { return child (0); }
    ref init () const { return child (1); }
    ref iter () const { return child (2); }
    ref stmt () const { return child (3); }
};



class ASTassign_expression : public ASTNode
{
public:
    enum Assignment { Assign, MulAssign, DivAssign, AddAssign, SubAssign,
                      BitwiseAndAssign, BitwiseOrAssign, BitwiseXorAssign,
                      ShiftLeftAssign, ShiftRightAssign };

    ASTassign_expression (OSLCompilerImpl *comp, ASTNode *var, Assignment op,
                          ASTNode *expr)
        : ASTNode (assign_expression_node, comp, op, var, expr)
    { }

    const char *nodetypename () const { return "assign_expression"; }
    const char *childname (size_t i) const;
    const char *opname () const;

    ref var () const { return child (0); }
    ref expr () const { return child (1); }
};



class ASTbinary_expression : public ASTNode
{
public:
    enum Binop { Mul, Div, Add, Sub, Mod, 
                 Equal, NotEqual, Greater, Less, GreaterEqual, LessEqual, 
                 BitwiseAnd, BitwiseOr, BitwiseXor, LogicalAnd, LogicalOr,
                 ShiftLeft, ShiftRight };

    ASTbinary_expression (OSLCompilerImpl *comp, Binop op,
                          ASTNode *left, ASTNode *right)
        : ASTNode (binary_expression_node, comp, op, left, right)
    { }

    const char *nodetypename () const { return "binary_expression"; }
    const char *childname (size_t i) const;
    const char *opname () const;

    ref left () const { return child (0); }
    ref right () const { return child (1); }
};



#if 0
class ASTsubclass : public ASTNode
{
public:
    ASTsubclass (OSLCompilerImpl *comp) : ASTNode (unknown_node, comp) { }
    ~ASTsubclass () { }
    const char *nodetypename () const { return "<FIXME>"; }
    const char *childname (size_t i) const;
private:
};
#endif



}; // namespace pvt
}; // namespace OSL


#endif /* AST_H */
