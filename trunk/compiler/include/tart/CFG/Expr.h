/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */

#ifndef TART_CFG_EXPR_H
#define TART_CFG_EXPR_H

#ifndef TART_COMMON_GC_H
#include "tart/Common/GC.h"
#endif

#ifndef TART_COMMON_SOURCELOCATION_H
#include "tart/Common/SourceLocation.h"
#endif

#ifndef TART_COMMON_FORMATTABLE_H
#include "tart/Common/Formattable.h"
#endif

#ifndef TART_CFG_CFG_H
#include "tart/CFG/CFG.h"
#endif

#include <llvm/Instructions.h>

namespace tart {

class Scope;  
class ErrorExpr;
class VariableDefn;
class CompositeType;
  
/// -------------------------------------------------------------------
/// A Control Flow Graph value or expression
class Expr : public GC, public Formattable, public Locatable {
public:
  enum ExprType {
    #define EXPR_TYPE(x) x,
    #include "ExprType.def"
    #undef EXPR_TYPE
    TypeCount,
  };
  
private:
  const ExprType    exprType_;
  SourceLocation    loc_;
  Type            * type_;
  
  static const ExprList emptyList;
  
public:
  Expr(ExprType k, const SourceLocation & l, Type * type)
    : exprType_(k)
    , loc_(l)
    , type_(type)
  {}

  virtual ~Expr() {}

  /** The type of expression node. */
  ExprType exprType() const { return exprType_; }
  
  /** The type of this expression. */
  const Type * type() const { return type_; }
  Type * type() { return type_; }
  const Type * getType() const { return type_; }
  Type * getType() { return type_; }
  void setType(Type * type) { type_ = type; }
  
  /** Return true if this expression is a constant. */
  virtual bool isConstant() const { return false; }
  
  /** Return true if this expression has no side effects. */
  virtual bool isSideEffectFree() const = 0;

  /** Return true if this expression has been fully resolved. */
  virtual bool isSingular() const = 0;

  /** Where in the source file this expression comes from. */
  const SourceLocation & getLocation() const { return loc_; }

  /** Cast operator so we can use an expression node as a location. */
  operator const SourceLocation & () const { return loc_; }

  /** Produce a textual representation of this value. */
  virtual void format(FormatStream & out) const;

  /** Trace through all references in this expression. */
  void trace() const;
  
  /** LLVM dynamic casting primitive. */
  static inline bool classof(const Expr *) { return true; }

  /** A placeholder node used to signal an error in the computation. */
  static ErrorExpr ErrorVal;
};

/// -------------------------------------------------------------------
/// Return result indicating a fatal compilation error. Used when no
/// valid result can be returned.
class ErrorExpr : public Expr {
public:
  /** Constructor. */
  ErrorExpr();

  // Overrides

  bool isSideEffectFree() const { return true; }
  bool isSingular() const { return true; }
};

/// -------------------------------------------------------------------
/// An operation with a single argument
class UnaryExpr : public Expr {
private:
  Expr * arg_;

public:
  /** Constructor. */
  UnaryExpr(ExprType k, const SourceLocation & loc, Type * type, Expr * a)
    : Expr(k, loc, type)
    , arg_(a)
  {}

  /** The argument expression. */
  Expr * arg() const { return arg_; }
  void setArg(Expr * ex) { arg_ = ex; }

  // Overrides

  bool isSideEffectFree() const;
  bool isConstant() const;
  bool isSingular() const;
  void format(FormatStream & out) const;
  void trace() const;
};

/// -------------------------------------------------------------------
/// An operation with two arguments
class BinaryExpr : public Expr {
private:
  Expr * first_;
  Expr * second_;

public:
  /** Constructor. */
  BinaryExpr(ExprType k, const SourceLocation & loc, Type * type)
    : Expr(k, loc, type)
    , first_(NULL)
    , second_(NULL)
  {}

  /** Constructor. */
  BinaryExpr(ExprType k, const SourceLocation & loc, Type * type,
      Expr * f, Expr * s)
    : Expr(k, loc, type)
    , first_(f)
    , second_(s)
  {}

  /** The first argument. */
  Expr * first() const { return first_; }
  void setFirst(Expr * ex) { first_ = ex; }

  /** The second argument. */
  Expr * second() const { return second_; }
  void setSecond(Expr * ex) { second_ = ex; }

  // Overrides

  bool isSideEffectFree() const;
  bool isConstant() const;
  bool isSingular() const;
  void format(FormatStream & out) const;
  void trace() const;
};

/// -------------------------------------------------------------------
/// An operation with a variable number of arguments
class ArglistExpr : public Expr {
protected:
  ExprList args_;

  ArglistExpr(ExprType k, const SourceLocation & loc, Type * type)
    : Expr(k, loc, type)
  {}

public:
  
  /** The argument list. */
  ExprList & args() { return args_; }
  const Expr * arg(size_t index) const { return args_[index]; }
  Expr * arg(size_t index) { return args_[index]; }
  size_t argCount() const { return args_.size(); }
  void appendArg(Expr * en) { args_.push_back(en); }

  // Overrides

  bool isSingular() const;
  void trace() const;
};

/// -------------------------------------------------------------------
/// A reference to a variable or field.
class LValueExpr : public Expr {
private:
  Expr * base_;
  ValueDefn * value_;

public:
  /** Constructor. */
  LValueExpr(const SourceLocation & loc, Expr * baseVal, ValueDefn * val);

  /** Return the reference to the base (the 'self' param) */
  Expr * base() const { return base_; }
  void setBase(Expr * b) { base_ = b; }

  /** Return the reference to the definition */
  const ValueDefn * value() const { return value_; }
  ValueDefn * value() { return value_; }
  
  // If the input expression is an LValue which is bound to a compile-time constant,
  // return the constant, otherwise return the input expression.
  static Expr * constValue(Expr * lv);

  // Overrides

  void format(FormatStream & out) const;
  void trace() const;
  bool isSideEffectFree() const { return true; }
  bool isSingular() const;

  static inline bool classof(const LValueExpr *) { return true; }
  static inline bool classof(const Expr * ex) {
    return ex->exprType() == LValue;
  }
};

/// -------------------------------------------------------------------
/// A reference to a scope name
class ScopeNameExpr : public Expr {
private:
  Defn * value_;

public:
  ScopeNameExpr(const SourceLocation & loc, Defn * value)
    : Expr(ScopeName, loc, NULL)
    , value_(value)
  {}
  
  // Overrides
  
  void format(FormatStream & out) const;
  void trace() const;
  bool isSideEffectFree() const { return true; }
  bool isSingular() const;
  const Defn * getValue() const { return value_; }
  Defn * getValue() { return value_; }

  static inline bool classof(const ScopeNameExpr *) { return true; }
  static inline bool classof(const Expr * ex) {
    return ex->exprType() == ScopeName;
  }
};

/// -------------------------------------------------------------------
/// An assignment expression
class AssignmentExpr : public Expr {
private:
  Expr * fromExpr;
  Expr * toExpr;

public:
  AssignmentExpr(const SourceLocation & loc, Expr * to, Expr * from)
    : Expr(Assign, loc, to->getType())
    , fromExpr(from)
    , toExpr(to)
  {}
  
  AssignmentExpr(ExprType k, const SourceLocation & loc, Expr * to, Expr * from)
    : Expr(k, loc, to->getType())
    , fromExpr(from)
    , toExpr(to)
  {}
  
  Expr * getFromExpr() const { return fromExpr; }
  void setFromExpr(Expr * ex) { fromExpr = ex; }

  Expr * getToExpr() const { return toExpr; }
  void setToExpr(Expr * ex) { toExpr = ex; }

  // Overrides

  bool isSideEffectFree() const { return false; }
  bool isSingular() const {
    return fromExpr->isSingular() && toExpr->isSingular();
  }
  void format(FormatStream & out) const;

  static inline bool classof(const AssignmentExpr *) { return true; }
  static inline bool classof(const Expr * ex) {
    return ex->exprType() == Assign || ex->exprType() == PostAssign;
  }
};

/// -------------------------------------------------------------------
/// An initialization of a local variable
class InitVarExpr : public Expr {
private:
  VariableDefn * var;
  Expr * initExpr;

public:
  InitVarExpr(const SourceLocation & loc, VariableDefn * var, Expr * expr);
  
  Expr * getInitExpr() const { return initExpr; }
  void setInitExpr(Expr * e) { initExpr = e; }
  VariableDefn * getVar() const { return var; }

  // Overrides

  bool isSideEffectFree() const { return false; }
  bool isSingular() const;
  void format(FormatStream & out) const;

  static inline bool classof(const InitVarExpr *) { return true; }
  static inline bool classof(const Expr * ex) {
    return ex->exprType() == InitVar;
  }
};

/// -------------------------------------------------------------------
/// A general function call
class CallExpr : public ArglistExpr {
private:
  Expr * function_;
  Candidates candidates_;
  Type * expectedReturnType;

public:
  CallExpr(ExprType k, const SourceLocation & loc, Expr * f)
    : ArglistExpr(k, loc, NULL)
    , function_(f)
    , expectedReturnType(NULL)
  {}

  /** The function expression being called. */
  Expr * function() { return function_; }
  void setFunction(Expr * ex) { function_ = ex; }

  /** The list of overload candidates. */
  Candidates & candidates() { return candidates_; }

  /** The function expression being called. */
  Type * getExpectedReturnType() { return expectedReturnType; }
  void setExpectedReturnType(Type * t) { expectedReturnType = t; }

  /** If all of the overload candidates have the same type for the Nth
      parameter slot, then return that type, otherwise return NULL. */
  Type * getSingularParamType(int arg);

  /** If all of the overload candidates have the same return type, then
      return that type, otherwise return NULL. */
  Type * getSingularResultType();

  /** Return either the single non-culled candidate, or NULL. */
  CallCandidate * getSingularCandidate();
  
  /** Return true if there is at least one non-culled candidate. */
  bool hasAnyCandidates() const;

  // Overridden methods

  void format(FormatStream & out) const;
  bool isSideEffectFree() const { return false; }
  bool isSingular() const;
  void trace() const;

  static inline bool classof(const CallExpr *) { return true; }
  static inline bool classof(const Expr * ex) {
    return ex->exprType() == Call || ex->exprType() == Construct;
  }
};

/// -------------------------------------------------------------------
/// A call to a global or member function.
class FnCallExpr : public ArglistExpr {
private:
  FunctionDefn * function_;
  Expr * selfArg;

public:
  FnCallExpr(ExprType k, const SourceLocation & loc, FunctionDefn * function,
      Expr * self)
    : ArglistExpr(k, loc, NULL)
    , function_(function)
    , selfArg(self)
  {}

  /** The function expression being called. */
  FunctionDefn * function() { return function_; }
  const FunctionDefn * function() const { return function_; }
  void setFunction(FunctionDefn * function) { function_ = function; }

  /** The 'self' argument. */
  Expr * getSelfArg() const { return selfArg; }
  void setSelfArg(Expr * self) { selfArg = self; }

  // Overridden methods

  void format(FormatStream & out) const;
  bool isSideEffectFree() const { return false; }
  bool isSingular() const;
  void trace() const;

  static inline bool classof(const CallExpr *) { return true; }
  static inline bool classof(const Expr * ex) {
    return ex->exprType() == FnCall || ex->exprType() == CtorCall;
  }
};

/// -------------------------------------------------------------------
/// A 'new object' expression
class NewExpr : public Expr {
public:
  NewExpr(const SourceLocation & loc, Type * type)
    : Expr(New, loc, type)
  {}

  // Overridden methods
  void format(FormatStream & out) const;
  bool isSideEffectFree() const { return true; }
  bool isSingular() const;
  static inline bool classof(const NewExpr *) { return true; }
  static inline bool classof(const Expr * ex) {
    return ex->exprType() == New;
  }
};

/// -------------------------------------------------------------------
/// An explicit template instantiation, which may be complete or
/// partial.

// TODO: Delete this class?
class InstantiateExpr : public Expr {
private:
  Expr * base_;
  ValueDefn * value_;
  ExprList args_;

public:
  /** Constructor. */
  InstantiateExpr(const SourceLocation & loc, Expr * base, ValueDefn * val,
      const ExprList args);

  /** Return the reference to the base (the 'self' param) */
  Expr * base() const { return base_; }
  void setBase(Expr * b) { base_ = b; }

  /** Return the reference to the definition */
  const ValueDefn * value() const { return value_; }
  ValueDefn * value() { return value_; }

  /** Return the arguments to the template */
  const ExprList & args() const { return args_; }
  ExprList & args() { return args_; }

  // Overrides

  void format(FormatStream & out) const;
  void trace() const;

  static inline bool classof(const InstantiateExpr *) { return true; }
  static inline bool classof(const Expr * ex) {
    return ex->exprType() == Instantiate;
  }
};

/// -------------------------------------------------------------------
/// A typecast operator
class CastExpr : public UnaryExpr {
public:
  /** Constructor. */
  CastExpr(ExprType k, const SourceLocation & loc, Type * type, Expr * a)
    : UnaryExpr(k, loc, type, a)
    , typeIndex_(0)
  {
  }

  // Type discriminator index used in union types
  int typeIndex() const { return typeIndex_; }
  void setTypeIndex(int index) { typeIndex_ = index; }
  
  void format(FormatStream & out) const;
  static inline bool classof(const CastExpr *) { return true; }
  static inline bool classof(const Expr * ex) {
    return ex->exprType() >= ImplicitCast &&
        ex->exprType() <= ZeroExtend;
  }
  
private:
  int typeIndex_;
};

/// -------------------------------------------------------------------
/// A low-level binary machine opcode
class BinaryOpcodeExpr : public BinaryExpr {
private:
  llvm::Instruction::BinaryOps opCode;

public:
  /** Constructor. */
  BinaryOpcodeExpr(
      llvm::Instruction::BinaryOps op,
      const SourceLocation & loc, Type * type)
    : BinaryExpr(BinaryOpcode, loc, type)
    , opCode(op)
  {}

  /** Constructor. */
  BinaryOpcodeExpr(
      llvm::Instruction::BinaryOps op,
      const SourceLocation & loc, Type * type,
      Expr * a0, Expr * a1)
    : BinaryExpr(BinaryOpcode, loc, type, a0, a1)
    , opCode(op)
  {}

  /** The LLVM opcode for this binary expression. */
  llvm::Instruction::BinaryOps getOpCode() const { return opCode; }

  // Overrides

  bool isSingular() const;
  bool isSideEffectFree() const;
  void format(FormatStream & out) const;
};

/// -------------------------------------------------------------------
/// A comparison operator
class CompareExpr : public BinaryExpr {
public:
  typedef llvm::CmpInst::Predicate Predicate;

private:
  Predicate predicate;

public:
  /** Constructor. */
  CompareExpr(const SourceLocation & loc, Predicate pred);

  /** Constructor. */
  CompareExpr(const SourceLocation & loc, Predicate pred, Expr * f, Expr * s);

  Predicate getPredicate() const { return predicate; }

  // Overrides

  void format(FormatStream & out) const;
};

/// -------------------------------------------------------------------
/// An IsInstanceOf test
class InstanceOfExpr : public Expr {
private:
  Expr * value_;
  Type * toType;

public:
  /** Constructor. */
  InstanceOfExpr(const SourceLocation & loc, Expr * value, Type * ty);
    
  /* The instance value we are testing. */
  const Expr * getValue() const { return value_; }
  Expr * getValue() { return value_; }
  void setValue(Expr * value) { value_ = value; }

  /* The type we are testing against. */
  const Type * getToType() const { return toType; }
  Type * getToType() { return toType; }
  void setToType(Type * ty) { toType = ty; }

  // Overrides

  bool isSideEffectFree() const { return true; }
  bool isSingular() const;
  void format(FormatStream & out) const;
  void trace() const;
};

/// -------------------------------------------------------------------
/// An expression that directly represents an IR value.
class IRValueExpr : public Expr {
private:
  llvm::Value * value_;

public:
  /** Constructor. */
  IRValueExpr(const SourceLocation & loc, Type * type, llvm::Value * value = NULL)
    : Expr(IRValue, loc, type)
    , value_(value)
  {}

  /** The argument expression. */
  llvm::Value * value() const { return value_; }
  void setValue(llvm::Value * value) { value_ = value; }

  // Overrides

  bool isSideEffectFree() const { return true; }
  bool isConstant() const { return false; }
  bool isSingular() const { return true; }
  void format(FormatStream & out) const;

  static inline bool classof(const IRValueExpr *) { return true; }
  static inline bool classof(const Expr * ex) {
    return ex->exprType() == IRValue;
  }
};

/// -------------------------------------------------------------------
/// A statement that executes a local jump and return within a function.
/// This is used for cleanup handlers.
class LocalCallExpr : public Expr {
private:
  Block * target_;
  int returnState_;

public:
  /** Constructor. */
  LocalCallExpr(Block * target)
    : Expr(LocalCall, SourceLocation(), NULL)
    , target_(target)
    , returnState_(-1)
  {}

  /** The target of the call. */
  Block * target() const { return target_; }
  void setTarget(Block * target) { target_ = target; }

  /** Used in generating the call - sets a state variable before the branch. */
  int returnState() const { return returnState_; }
  void setReturnState(int state) { returnState_ = state; }

  // Overrides

  bool isSideEffectFree() const { return true; }
  bool isConstant() const { return false; }
  bool isSingular() const { return true; }
  void format(FormatStream & out) const;

  static inline bool classof(const LocalCallExpr *) { return true; }
  static inline bool classof(const Expr * ex) {
    return ex->exprType() == LocalCall;
  }
};

/// -------------------------------------------------------------------
/// Utility functions

/** Return the text name of a node class. */
const char * exprTypeName(Expr::ExprType type);

/** Format a list of expressions as comma-separated values. */
void formatExprList(FormatStream & out, const ExprList & exprs);

/** Format a list of expression types as comma-separated values. */
void formatExprTypeList(FormatStream & out, const ExprList & exprs);

/** Format a list of types as comma-separated values. */
void formatTypeList(FormatStream & out, const TypeList & types);

/** Return true if the expression is an error result. */
inline bool isErrorResult(const Expr * ex) {
  return ex == NULL || ex->exprType() == Expr::Invalid;
}

bool isErrorResult(const Type * ty);

} // namespace tart

#endif
