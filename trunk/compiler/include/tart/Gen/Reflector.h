/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */

#ifndef TART_GEN_REFLECTOR_H
#define TART_GEN_REFLECTOR_H

#include "tart/Common/SourceLocation.h"

#include "tart/CFG/CFG.h"

#include "tart/Gen/ReflectionMetadata.h"

#include "llvm/Support/IRBuilder.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SetVector.h"
#include "iostream"

namespace tart {

class Module;
class Type;
class Defn;
class ValueDefn;
class VariableDefn;
class FunctionDefn;
class NamespaceDefn;
class PropertyDefn;
class CompositeType;
class CodeGenerator;
class EnumType;
class PrimitiveType;
class FunctionType;
class IterableScope;
class TypeDefn;
class TupleType;
class ReflectionMetadata;

typedef std::vector<llvm::Constant *> ConstantList;
typedef llvm::StringMap<llvm::GlobalVariable *> GlobalVarMap;
typedef llvm::SetVector<Defn *> DefnSet;

/// -------------------------------------------------------------------
/// Represents all of the reflected symbols within a single scope.

struct ReflectedMembers {
  ConstantList fields;
  ConstantList properties;
  ConstantList constructors;
  ConstantList methods;
  ConstantList types;

  bool isEmpty() const {
    return fields.empty() &&
        properties.empty() &&
        constructors.empty() &&
        methods.empty() &&
        types.empty();
  }
};

/// -------------------------------------------------------------------
/// Represents a unique method signature.

#if 0
class UniqueMethodKey {
public:
  UniqueMethodKey(const char * name, const FunctionType * fnType);

  struct KeyInfo {
    static const UniqueMethodKey getEmptyKey();
    static const UniqueMethodKey getTombstoneKey();
    static unsigned getHashValue(const UniqueMethodKey & key);
    static bool isEqual(const UniqueMethodKey & lhs, const UniqueMethodKey & rhs);
    static bool isPod() { return true; }
  };

  const char * name() const { return name_; }
  const FunctionType * type() const { return ns_; }

private:
  const char * name_;
  const FunctionType * ns_;
};
#endif

/// -------------------------------------------------------------------
/// Class to handle generation of reflection data.
class Reflector {
public:
  typedef std::pair<const Type *, TagInfo> TypeArrayElement;
  typedef std::vector<TypeArrayElement> TypeArray;
  typedef llvm::DenseMap<const Type *, TagInfo, Type::CanonicalKeyInfo> TypeMap;

  // Keep these enums in sync with Member.tart
  enum Visibility {
    PUBLIC,
    PROTECTED,
    PRIVATE,
  };

  enum MemberKind {
    FIELD,
    PROPERTY,
    METHOD,
    CONSTRUCTOR,
  };

  enum Traits {
    FINAL     = (1 << 0),
    ABSTRACT  = (1 << 1),
    STATIC    = (1 << 2),
  };

  // Keep this enum in sync with Type.tart
  enum TypeKind {
    OPAQUE,
    PRIMITIVE,
    CLASS,
    STRUCT,
    INTERFACE,
    PROTOCOL,
    ENUM,
    FUNCTION,
    TUPLE,
    UNION,
    ADDRESS,
    NATIVE_ARRAY,
    //SingleValue
  };

  // Keep this enum in sync with PrimitiveType.tart
  enum SubtypeId {
    NONE = 0,
    VOID,
    NULLTYPE,
    BOOL,
    CHAR,
    BYTE,
    SHORT,
    INT,
    LONG,
    INTPTR,
    UBYTE,
    USHORT,
    UINT,
    ULONG,
    UINTPTR,
    FLOAT,
    DOUBLE,
  };

  Reflector(CodeGenerator & cg);
  ~Reflector();

  /** Whether reflection is enabled. */
  bool enabled() const { return enabled_; }
  void setEnabled(bool enabled) { enabled_ = enabled; }

  /** Given the name of a symbol, return a unique interned string for that name. Identical
      strings will be combined across module boundaries. */
  llvm::Constant * internSymbol(const llvm::StringRef &Key);

  /** Generate a pointer to a module's reflection info. */
  llvm::GlobalVariable * getModulePtr(Module * module);

  llvm::GlobalVariable * getNameTablePtr(Module * module);

  /** Generate a pointer to the package reflection info. */
  llvm::GlobalVariable * getPackagePtr(Module * module);

  /** Return the reflected symbol data for a given definition. */
  ReflectionMetadata * getReflectionMetadata(const Defn * def);

  /** Generate reflection information for a module. */
  void emitModule(Module * module);

  /** Generate the name table which contains tables of names used by the module
      and the definitions within it. */
  void emitNameTable(Module * module);

  /** Generate the table of invocation functions. */
  void emitCallAdapterFnTable(Module * module);

  /** Add a definition to the list of reflected members. */
  void addDefn(const Defn * def);

  /** Add all of the members of the given scope to the reflected scope. */
  void addMembers(const IterableScope * scope, ReflectionMetadata * rs);

  /** Add the member to the reflected scope. */
  void addMember(const Defn * def, ReflectionMetadata * rs);

  /** Generate reflection information for a definition in this module. */
  void buildRMD(const Defn * def);

  /** Write out reflection information for a definition in this module. */
  void emitReflectedDefn(ReflectionMetadata * rs, const Defn * def);

  /** Write out the reflection data for the contents of a definition. */
  void emitReflectedMembers(ReflectionMetadata * rs, const IterableScope * scope);

  /** Emitters for various sections. */
  void emitTypeParamsSection(ReflectionMetadata * rs, const Defn * def);
  void emitTemplateParamsSection(ReflectionMetadata * rs, const Defn * def);
  void emitBaseClassSection(ReflectionMetadata * rs, const CompositeType * type);
  void emitInterfacesSection(ReflectionMetadata * rs, const CompositeType * type);
  void emitAttributeSection(ReflectionMetadata * rs, const ExprList & attrs);
  void emitTemplateSection(ReflectionMetadata * rmd, const Defn * def);

  /** Emitters for various definition types. */
  void emitNamespaceDefn(ReflectionMetadata * rs, const NamespaceDefn * def,
      llvm::raw_ostream & out);
  void emitFieldDefn(ReflectionMetadata * rs, const VariableDefn * def, llvm::raw_ostream & out);
  void emitConstructorDefn(ReflectionMetadata * rs, const FunctionDefn * def,
      llvm::raw_ostream & out);
  void emitMethodDefn(ReflectionMetadata * rs, const FunctionDefn * def, llvm::raw_ostream & out);
  void emitPropertyDefn(ReflectionMetadata * rs, const PropertyDefn * def, llvm::raw_ostream & out);

  /** Generate an array containing reflection data supplied by the specified array. */
  llvm::Constant * emitArray(
      const std::string & baseName, const VariableDefn * var, const ConstantList & values);

private:
  typedef llvm::DenseMap<const Defn *, ReflectionMetadata *> ReflectedSymbolMap;

  Traits memberTraits(const Defn * member);

  Module * module();

  llvm::Constant * getRetainedAttr(const Expr * attrExpr);
  bool isExport(const Defn * de);

  CodeGenerator & cg_;
  bool enabled_;
  llvm::LLVMContext & context_;
  llvm::IRBuilder<true> builder_;    // LLVM builder
  llvm::Module * irModule_;
  llvm::GlobalVariable * nameTableVar_;
  llvm::Constant * invokeFnTableVar_;

  ReflectedSymbolMap rmdMap_;
  GlobalVarMap globals_;

  ModuleMetadata mmd_;
  TypeArray invokeRefs_;
};

}

#endif
