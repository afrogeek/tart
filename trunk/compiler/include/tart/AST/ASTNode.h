/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */
 
#ifndef TART_AST_ASTNODE_H
#define TART_AST_ASTNODE_H

#ifndef TART_COMMON_GC_H
#include "tart/Common/GC.h"
#endif

#ifndef TART_COMMON_SOURCELOCATION_H
#include "tart/Common/SourceLocation.h"
#endif

#ifndef TART_COMMON_FORMATTABLE_H
#include "tart/Common/Formattable.h"
#endif

#include <llvm/ADT/SmallVector.h>
#include <llvm/Constants.h>

namespace tart {

using llvm::dyn_cast;
using llvm::dyn_cast_or_null;
using llvm::cast;
using llvm::cast_or_null;
using llvm::isa;

class Defn;

/// -------------------------------------------------------------------
/// Forward declarations
class ASTNode;
class ASTDecl;
class ASTParameter;
class ASTFunctionDecl;

/// -------------------------------------------------------------------
/// Container types
typedef llvm::SmallVector<ASTNode *, 4> ASTNodeList;
typedef llvm::SmallVector<const ASTNode *, 4> ASTConstNodeList;
typedef llvm::SmallVector<ASTDecl *, 8> ASTDeclList;
typedef llvm::SmallVector<ASTParameter *, 8> ASTParamList;

/// ---------------------------------------------------------------
/// Base class of all AST nodes.
class ASTNode : public GC, public Formattable, public Locatable {
public:
  enum NodeType {
    #define NODE_TYPE(x) x,
    #include "ASTNodeType.def"
    NodeTypeCount,
    
    // First and last declaration node types
    DefFirst = Class,
    DefLast = Namespace,

    StmtFirst = Block,
    StmtLast = Intrinsic
  };

protected:
  const NodeType nodeType;
  SourceLocation loc;

public:
  ASTNode(NodeType nt, const SourceLocation & sl)
    : nodeType(nt)
    , loc(sl)
  {}

  virtual ~ASTNode() {}

  /** Return the type of this AST node. */
  NodeType getNodeType() const { return nodeType; }
  
  /** Where in the source file this expression comes from. */
  const SourceLocation & getLocation() const { return loc; }
  
  /** Produce a string representation of this node and its children. */
  const std::string toString(int formatOptions = Format_Default) const;
  
  /** Produce a textual representation of this node and its children. */
  virtual void format(FormatStream & out) const;

  // Overrides
  
  void trace() const { loc.trace(); }
  static inline bool classof(const NodeType *) { return true; }
};

/// -------------------------------------------------------------------
/// A reference to a name
class ASTIdent : public ASTNode {
private:
  const char * value;

public:
  // Constructor needs to be public because we create static versions of this.
  ASTIdent(const SourceLocation & loc, const char * v)
    : ASTNode(Id, loc)
    , value(v)
  {}
  
  ASTIdent * get(const SourceLocation & loc, const char * value) {
    return new ASTIdent(loc, value);
  }
  
  const char * getValue() const { return value; }

  void format(FormatStream & out) const;
  static inline bool classof(const ASTIdent *) { return true; }
  static inline bool classof(const ASTNode * ast) {
      return ast->getNodeType() == ASTNode::Id;
  }
};

/// -------------------------------------------------------------------
/// A reference to a member
class ASTMemberRef : public ASTNode {
private:
  ASTNode * qualifier;
  const char * memberName;

public:
  // Constructor needs to be public because we create static versions of this.
  ASTMemberRef(const SourceLocation & loc, ASTNode * qual, const char * name)
    : ASTNode(Member, loc)
    , qualifier(qual)
    , memberName(name)
  {}
  
  ASTMemberRef * get(const SourceLocation & loc, ASTNode * qual, const char * name) {
    return new ASTMemberRef(loc, qual, name);
  }
  
  /** The object that contains the member. */
  const ASTNode * getQualifier() const { return qualifier; }
  ASTNode * getQualifier() { return qualifier; }

  /** The name of the member. */
  const char * getMemberName() const { return memberName; }

  // Overrides
  
  void format(FormatStream & out) const;
  void trace() const;
  static inline bool classof(const ASTMemberRef *) { return true; }
  static inline bool classof(const ASTNode * ast) {
      return ast->getNodeType() == ASTNode::Member;
  }
};

/// -------------------------------------------------------------------
/// Base class for literals - ints, floats, etc.
template<class ValueType, ASTNode::NodeType type>
class ASTLiteral : public ASTNode {
private:
  ValueType value;

public:
  ASTLiteral(const SourceLocation & loc, const ValueType & val)
      : ASTNode(type, loc)
      , value(val)
  {}

  /** The value of this literal. */
  const ValueType & getValue() const { return value; }
  
  // Overrides
  
  void format(FormatStream & out) const;
  static inline bool classof(const ASTLiteral *) { return true; }
  static inline bool classof(const ASTNode * ast) {
    return ast->getNodeType() == type;
  }
};

/// -------------------------------------------------------------------
/// Various literal types
typedef ASTLiteral<llvm::APInt, ASTNode::LitInt> ASTIntegerLiteral;
typedef ASTLiteral<llvm::APFloat, ASTNode::LitFloat> ASTFloatLiteral;
typedef ASTLiteral<std::string, ASTNode::LitString> ASTStringLiteral;
typedef ASTLiteral<uint32_t, ASTNode::LitChar> ASTCharLiteral;
typedef ASTLiteral<bool, ASTNode::LitBool> ASTBoolLiteral;

/// -------------------------------------------------------------------
/// A node which has a single fixed argument
class ASTUnaryOp : public ASTNode {
private:
  ASTNode * arg_;

public:
  ASTUnaryOp(NodeType nt, const SourceLocation & loc, ASTNode * a)
    : ASTNode(nt, loc)
    , arg_(a)
  {}
  
  static ASTUnaryOp * get(NodeType nt, const SourceLocation & loc, ASTNode * arg = NULL) {
    return new ASTUnaryOp(nt, loc, arg);
  }
  
  static ASTUnaryOp * get(NodeType nt, ASTNode * arg) {
    return new ASTUnaryOp(nt, arg->getLocation(), arg);
  }
  
  /** The single argument. */
  const ASTNode * arg() const { return arg_; }
  ASTNode * arg() { return arg_; }

  // Overrides
  
  void format(FormatStream & out) const;
  void trace() const;
  static inline bool classof(const ASTUnaryOp *) { return true; }
  static inline bool classof(const ASTNode * ast) {
    switch (ast->getNodeType()) {
      case ASTNode::Array:
        return true;
        
      default:
        return false;
    }
  }
};

/// -------------------------------------------------------------------
/// A node that contains one or more child nodes.
class ASTOper : public ASTNode {
protected:
  // List of operands to this operator
  ASTNodeList args_;

public:
  ASTOper(NodeType type, const SourceLocation & loc)
      : ASTNode(type, loc) {}

  ASTOper(NodeType type, ASTNode * a0)
      : ASTNode(type, a0->getLocation()) {
    args_.push_back(a0);
  }

  ASTOper(NodeType type, const SourceLocation & loc, ASTNode * a0)
      : ASTNode(type, loc) {
    args_.push_back(a0);
  }

  ASTOper(NodeType type, ASTNode * a0, ASTNode * a1)
      : ASTNode(type, a0->getLocation() | a1->getLocation()) {
    args_.push_back(a0);
    args_.push_back(a1);
  }

  ASTOper(NodeType type, const ASTNodeList & alist)
      : ASTNode(type, SourceLocation()) {
    args_.append(alist.begin(), alist.end());
    for (ASTNodeList::const_iterator it = alist.begin(); it != alist.end(); ++it) {
      loc |= (*it)->getLocation();
    }
  }

  ASTOper(NodeType type, const SourceLocation & loc, const ASTNodeList & alist)
      : ASTNode(type, loc)
      , args_(alist) {
  }

  /** Return the list of operands for this operation. */
  const ASTNodeList & args() const {
    return args_;
  }
  
  ASTNodeList & args() {
    return args_;
  }

  /** Return the list of operands for this operation. */
  const ASTNode * arg(int i) const {
    return args_[i];
  }
  
  /** Append an operand to the list of operands. */
  void append(ASTNode * node) {
    args_.push_back(node);
    loc |= node->getLocation();
  }

  /** Return the number of arguments. */
  size_t count() const {
    return args_.size();
  }

  // Overrides
  
  void format(FormatStream & out) const;
  void trace() const;
  static inline bool classof(const ASTOper *) {
    return true;
  }
};

/// -------------------------------------------------------------------
/// A call expression
class ASTCall : public ASTOper {
private:
  const ASTNode * func;

public:
  ASTCall(const SourceLocation & loc, const ASTNode * f,
      const ASTNodeList & argList)
    : ASTOper(Call, loc, argList)
    , func(f)
  {
  }

  /** Function to be called. */
  const ASTNode * getFunc() const {
    return func;
  }

  // Overrides
  
  void format(FormatStream & out) const;
  void trace() const;
  static inline bool classof(const ASTCall *) { return true; }
  static inline bool classof(const ASTNode * ast) {
    return ast->getNodeType() == Call;
  }
};

/// -------------------------------------------------------------------
/// A template specialization
class ASTSpecialize : public ASTOper {
private:
  const ASTNode * templateExpr;

public:
  ASTSpecialize(const SourceLocation & loc, const ASTNode * f,
      const ASTNodeList & argList)
    : ASTOper(Specialize, loc, argList)
    , templateExpr(f)
  {
  }

  /** Function to be called. */
  const ASTNode * getTemplateExpr() const {
    return templateExpr;
  }

  // Overrides
  
  void format(FormatStream & out) const;
  void trace() const;
  static inline bool classof(const ASTSpecialize *) { return true; }
  static inline bool classof(const ASTNode * ast) {
    return ast->getNodeType() == Specialize;
  }
};

/// -------------------------------------------------------------------
/// A keyword argument
class ASTKeywordArg : public ASTNode {
private:
  const ASTNode * arg_;
  const char * keyword_;

public:
  ASTKeywordArg(const SourceLocation & loc, const ASTNode * a, const char * kw)
      : ASTNode(Keyword, loc)
      , arg_(a)
      , keyword_(kw) {}

  const ASTNode * arg() const {
    return arg_;
  }

  const char * getKeyword() const {
    return keyword_;
  }

  // Overrides
  
  void format(FormatStream & out) const;
  void trace() const;
  static inline bool classof(const ASTKeywordArg *) { return true; }
  static inline bool classof(const ASTNode * ast) {
    return ast->getNodeType() == ASTNode::Keyword;
  }
};

/// -------------------------------------------------------------------
/// An import expression
class ASTImport : public ASTNode {
  const ASTNode * path;
  const char * asName;
  bool unpack;

public:
  ASTImport(const SourceLocation & loc, const ASTNode * p, const char * as,
      bool unpk = false)
      : ASTNode(ASTNode::Import, loc)
      , path(p)
      , asName(as)
      , unpack(unpk)
  {}
  
  const ASTNode * getPath() const { return path; }
  const char * getAsName() const { return asName; }
  bool getUnpack() const { return unpack; }

  // Overrides
  
  void format(FormatStream & out) const;
  void trace() const;
  static inline bool classof(const ASTImport *) { return true; }
  static inline bool classof(const ASTNode * ast) {
    return ast->getNodeType() == ASTNode::Import;
  }
};

/// -------------------------------------------------------------------
/// A reference to a built-in definition
class ASTBuiltIn : public ASTNode {
private:
  Defn * value;

public:
  // Constructor needs to be public because we create static versions of this.
  ASTBuiltIn(Defn * val)
    : ASTNode(BuiltIn, SourceLocation())
    , value(val)
  {}
  
  Defn * getValue() const { return value; }

  void format(FormatStream & out) const;
  void trace() const;
  static inline bool classof(const ASTIdent *) { return true; }
  static inline bool classof(const ASTNode * ast) {
      return ast->getNodeType() == ASTNode::BuiltIn;
  }
};

/// -------------------------------------------------------------------
/// Utility functions

/** Return the string name of a node type. */
const char * getNodeTypeName(ASTNode::NodeType ec);

/** Format a list of nodes as comma-separated values. */
void formatNodeList(FormatStream & out, const ASTNodeList & nodes);

inline FormatStream & operator<<(FormatStream & out, const ASTNodeList & nodes) {
  formatNodeList(out, nodes);
  return out;
}

} // namespace tart

#endif
