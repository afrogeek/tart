/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */

#include "tart/Type/Type.h"
#include "tart/Type/TypeAlias.h"
#include "tart/Type/PrimitiveType.h"
#include "tart/Type/CompositeType.h"
#include "tart/Defn/FunctionDefn.h"
#include "tart/Type/NativeType.h"
#include "tart/Type/UnionType.h"
#include "tart/Type/TupleType.h"
#include "tart/Type/TypeLiteral.h"
#include "tart/Defn/Template.h"
#include "tart/Sema/BindingEnv.h"
#include "tart/Common/Diagnostics.h"

namespace tart {

#ifdef TYPE_CLASS
#undef TYPE_CLASS
#endif

#define TYPE_CLASS(x) #x,

const char * TypeClassNames[] = {
#include "tart/Type/TypeClass.def"
};

#undef TYPE_CLASS

const char * compatibilityError(ConversionRank rank) {
  switch (rank) {
    case Incompatible:
      return "Type mismatch";
      break;

    case Truncation:
      return "Truncation of value";
      break;

    case IntegerToBool:
      return "Implicit conversion of integer to bool type";
      break;

    case PrecisionLoss:
      return "Possible loss of precision";
      break;

    case SignedUnsigned:
      return "Signed/unsigned mismatch";
      break;

    default:
      DFAIL("IllegalState");
  }
}

void compatibilityWarning(const SourceLocation & loc,
  ConversionRank rank, const Type * from, const Type * to) {
  DASSERT(!isErrorResult(from));
  if (isConversionWarning(rank)) {
    diag.error(loc) << Format_QualifiedName << compatibilityError(rank) <<
        " converting from '" << Format_Type << from << "' to '" << to << "'";
  }
}

void compatibilityWarning(const SourceLocation & loc,
  ConversionRank rank, const Expr * from, const Type * to) {
  DASSERT(!isErrorResult(from));
  if (isConversionWarning(rank)) {
    diag.error(loc) << Format_QualifiedName << compatibilityError(rank) <<
        " converting " << from << " from '" << Format_Type << from->type() << "' to '" << to << "'";
  }
}

FormatStream & operator<<(FormatStream & out, ConversionRank rank) {
  switch (rank) {
    case Incompatible: {
      out << "Incompatible";
      break;
    }

    case Truncation: {
      out << "Truncation";
      break;
    }

    case IntegerToBool: {
      out << "IntegerToBool";
      break;
    }

    case SignedUnsigned: {
      out << "SignedUnsigned";
      break;
    }

    case PrecisionLoss: {
      out << "PrecisionLoss";
      break;
    }

    case NonPreferred: {
      out << "NonPreferred";
      break;
    }

    case ExactConversion: {
      out << "ExactConversion";
      break;
    }

    case IdenticalTypes: {
      out << "IdenticalTypes";
      break;
    }
  }

  return out;
}

// Given a type, append the linkage name of that type to the output buffer.
void typeLinkageName(std::string & out, const Type * ty) {
  ty = dealias(ty);
  if (TypeDefn * td = ty->typeDefn()) {
    out.append(td->linkageName());
  } else if (const FunctionType * ftype = dyn_cast<FunctionType>(ty)) {
    out.append("fn");
    if (ftype->selfParam() != NULL) {
      out.append(":");
      typeLinkageName(out, ftype->selfParam()->type());
    }

    if (!ftype->params().empty()) {
      out.append("(");

      const ParameterList & params = ftype->params();
      for (ParameterList::const_iterator it = params.begin(); it != params.end(); ++it) {
        if (it != params.begin()) {
          out.append(",");
        }

        typeLinkageName(out, (*it)->type());
        if ((*it)->isVariadic()) {
          out.append("...");
        }
      }
      out.append(")");
    }

    if (!ftype->isVoidType()) {
      out.append("->");
      typeLinkageName(out, ftype->returnType());
    }
  } else if (const TupleType * ttype = dyn_cast<TupleType>(ty)) {
    out.append("(");
    for (TupleType::const_iterator it = ttype->begin(); it != ttype->end(); ++it) {
      if (it != ttype->begin()) {
        out.append(",");
      }

      typeLinkageName(out, *it);
    }
    out.append(")");
  } else if (const UnionType * utype = dyn_cast<UnionType>(ty)) {
    for (TupleType::const_iterator it = utype->members().begin(); it != utype->members().end();
        ++it) {
      if (it != utype->members().begin()) {
        out.append("|");
      }

      typeLinkageName(out, *it);
    }
  } else if (const AddressType * mat = dyn_cast<AddressType>(ty)) {
    typeLinkageName(out, mat->typeParam(0));
    out.append("^");
  } else if (const TypeLiteralType * npt = dyn_cast<TypeLiteralType>(ty)) {
    out.append("tart.reflect.Type");
  } else if (const TypeVariable * tvar = dyn_cast<TypeVariable>(ty)) {
    //out.append("%");
    out.append(tvar->name());
  } else {
    diag.error() << "Type: " << ty;
    TFAIL << "Can't compute linkage name of type: " << ty;
  }
}

void typeLinkageName(llvm::raw_ostream & out, const Type * ty) {
  ty = dealias(ty);
  if (TypeDefn * td = ty->typeDefn()) {
    out << td->linkageName();
  } else if (const FunctionType * ftype = dyn_cast<FunctionType>(ty)) {
    out << "fn";
    if (ftype->selfParam() != NULL) {
      out << ':';
      typeLinkageName(out, ftype->selfParam()->type());
    }

    if (!ftype->params().empty()) {
      out << '(';

      const ParameterList & params = ftype->params();
      for (ParameterList::const_iterator it = params.begin(); it != params.end(); ++it) {
        if (it != params.begin()) {
          out << ',';
        }

        typeLinkageName(out, (*it)->type());
        if ((*it)->isVariadic()) {
          out << "...";
        }
      }
      out << ')';
    }

    if (!ftype->isVoidType()) {
      out << "->";
      typeLinkageName(out, ftype->returnType());
    }
  } else if (const TupleType * ttype = dyn_cast<TupleType>(ty)) {
    out << '(';
    for (TupleType::const_iterator it = ttype->begin(); it != ttype->end(); ++it) {
      if (it != ttype->begin()) {
        out << ',';
      }

      typeLinkageName(out, *it);
    }
    out << ')';
  } else if (const UnionType * utype = dyn_cast<UnionType>(ty)) {
    for (TupleType::const_iterator it = utype->members().begin(); it != utype->members().end();
        ++it) {
      if (it != utype->members().begin()) {
        out << '|';
      }

      typeLinkageName(out, *it);
    }
  } else if (const AddressType * mat = dyn_cast<AddressType>(ty)) {
    typeLinkageName(out, mat->typeParam(0));
    out << '^';
  } else if (const TypeLiteralType * npt = dyn_cast<TypeLiteralType>(ty)) {
    out << "tart.reflect.Type";
  } else if (const TypeVariable * tvar = dyn_cast<TypeVariable>(ty)) {
    out << tvar->name();
  } else if (const FlexibleArrayType * fa = dyn_cast<FlexibleArrayType>(ty)) {
    out << "FlexibleArray[";
    typeLinkageName(out, fa->typeParam(0));
    out << ']';
  } else if (const NativeArrayType * na = dyn_cast<NativeArrayType>(ty)) {
    out << "NativeArray[";
    typeLinkageName(out, na->typeParam(0));
    out << ',';
    out << na->size();
    out << ']';
  } else {
    diag.error() << "Type: " << ty;
    TFAIL << "Can't compute linkage name of type: " << ty;
  }
}

// -------------------------------------------------------------------
// Represents a type conversion operation.
Conversion::Conversion(const Type * from)
  : fromType(from)
  , fromValue(NULL)
  , resultValue(NULL)
  , options(0)
{}

Conversion::Conversion(Expr * from)
  : fromType(from->type())
  , fromValue(from)
  , resultValue(NULL)
  , options(0)
{}

Conversion::Conversion(Expr * from, Expr ** to, int opts)
  : fromType(from->type())
  , fromValue(from)
  , resultValue(to)
  , options(opts)
{}

const Type * Conversion::getFromType() const {
  const Type * ty = dealias(fromType);
  return ty;
}

// -------------------------------------------------------------------
// Type
void Type::trace() const {}

const char * Type::typeClassName(TypeClass tc) {
  uint32_t index = (uint32_t)tc;
  if (index < KindCount) {
    return TypeClassNames[index];
  }
  return "<Invalid Type>";
}

void Type::getTypeParams(ConstTypeList & out) const {
  for (int i = 0, end = numTypeParams(); i < end; ++i) {
    out.push_back(typeParam(i));
  }
}

bool Type::isEqual(const Type * other) const {
  return this == dealias(other);
}

/** Return true if this type supports the specified protocol. */
bool Type::supports(const Type * protocol) const {
  return protocol->typeClass() == Protocol &&
      static_cast<const CompositeType *>(protocol)->isSupportedBy(this);
}

bool Type::isVoidType() const {
  return cls == Primitive && static_cast<const PrimitiveType *>(this)->typeId() == TypeId_Void;
}

bool Type::isNullType() const {
  return cls == Primitive && static_cast<const PrimitiveType *>(this)->typeId() == TypeId_Null;
}

bool Type::isIntType() const {
  return cls == Primitive &&
      isIntegerTypeId(static_cast<const PrimitiveType *>(this)->typeId());
}

bool Type::isUnsignedType() const {
  return cls == Primitive &&
      isUnsignedIntegerTypeId(static_cast<const PrimitiveType *>(this)->typeId());
}

bool Type::isFPType() const {
  return cls == Primitive &&
      isFloatingTypeId(static_cast<const PrimitiveType *>(this)->typeId());
}

bool Type::isUnsizedIntType() const {
  return cls == Primitive &&
      static_cast<const PrimitiveType *>(this)->typeId() == TypeId_UnsizedInt;
}

bool Type::isErrorType() const {
  return cls == Primitive &&
      static_cast<const PrimitiveType *>(this)->typeId() == TypeId_Bad;
}

bool Type::isBooleanType() const {
  return cls == Primitive &&
      static_cast<const PrimitiveType *>(this)->typeId() == TypeId_Bool;
}

bool Type::isBoxableType() const {
  switch (cls) {
    // Types that need to be boxed.
    case Type::Primitive: {
      return !isVoidType();
    }

    case Type::Struct:
    case Type::Enum:
    case Type::Tuple:
    case Type::Union: {
      return true;
    }

    case Type::Alias: {
      return static_cast<const TypeAlias *>(this)->value()->isBoxableType();
    }

    default:
      return false;
  }
}

const Type * Type::typeParam(int index) const {
  diag.debug() << "Type " << this << " does not have type parameters.";
  DFAIL("No type params");
}

ConversionRank Type::convert(const Conversion & cn) const {
  ConversionRank rank;

  // Ask the constraint if we can convert to this type. Most types don't
  // know about constraints, but constraints know about most types.
  rank = cn.fromType->convertTo(this, cn);
  if (rank != Incompatible) {
    return rank;
  }

  rank = convertImpl(cn);

  if (rank == Incompatible && (cn.options & Conversion::Coerce) && !cn.resultValue) {
    if (const CompositeType * ctype = dyn_cast<CompositeType>(this)) {
      if (!ctype->passes().isFinished(CompositeType::CoercerPass)) {
        diag.warn() << "Converter pass for " << ctype << " was not done.";
      }

      if (!ctype->coercers().empty()) {
        const MethodList & coercers = ctype->coercers();
        ConversionRank bestRank = Incompatible;
        for (MethodList::const_iterator it = coercers.begin(); it != coercers.end(); ++it) {
          const FunctionType * fnType = (*it)->functionType();
          rank = std::min(
              fnType->param(0)->type()->canConvert(cn.fromType),
              canConvert(fnType->returnType()));
          bestRank = std::max(bestRank, rank);
        }

        rank = bestRank;
      }
    }
  }

  return rank;
}

ConversionRank Type::canConvert(const Expr * fromExpr, int options) const {
  return convert(Conversion(const_cast<Expr *>(fromExpr)).setOption(options));
}

ConversionRank Type::canConvert(const Type * fromType, int options) const {
  return convert(Conversion(fromType).setOption(options));
}

Expr * Type::implicitCast(const SourceLocation & loc, Expr * from, int options) const {
  Expr * result = NULL;
  ConversionRank tc = convert(Conversion(from, &result, options));
  compatibilityWarning(loc, tc, from, this);
  DASSERT(tc == Incompatible || result != NULL);
  return result;
}

Expr * Type::explicitCast(const SourceLocation & loc, Expr * from, int options) const {
  Expr * result = NULL;
  ConversionRank tc = convert(Conversion(from, &result, options));
  if (tc == Incompatible) {
    compatibilityWarning(loc, tc, from, this);
  }
  return result;
}
bool Type::equivalent(const Type * type1, const Type * type2) {
  while (const TypeBinding * pval = dyn_cast<TypeBinding>(type1)) {
    type1 = pval->value();
    if (type1 == NULL) {
      return false;
    }
  }

  while (const TypeBinding * pval = dyn_cast<TypeBinding>(type2)) {
    type2 = pval->value();
    if (type2 == NULL) {
      return false;
    }
  }

  if (type1 == type2) {
    return true;
  }

  // Compare the ASTs to see if they derive from the same original symbol.
  if (type1->typeDefn() != NULL &&
      type2->typeDefn() != NULL &&
      type1->typeDefn()->ast() != NULL &&
      type1->typeDefn()->ast() == type2->typeDefn()->ast()) {

    // Now test the type parameters to see if they are also equivalent.
    const TypeDefn * d1 = type1->typeDefn();
    const TypeDefn * d2 = type2->typeDefn();
    const ConstTypeList * type1Params = NULL;
    const ConstTypeList * type2Params = NULL;

    if (d1->isTemplate()) {
      type1Params = &d1->templateSignature()->typeParams()->members();
    } else if (d1->isTemplateInstance()) {
      type1Params = &d1->templateInstance()->typeArgs()->members();
    }

    if (d2->isTemplate()) {
      type2Params = &d2->templateSignature()->typeParams()->members();
    } else if (d2->isTemplateInstance()) {
      type2Params = &d2->templateInstance()->typeArgs()->members();
    }

    if (type1Params == type2Params) {
      return true;
    }

    if (type1Params == NULL ||
        type2Params == NULL ||
        type1Params->size() != type2Params->size()) {
      return false;
    }

    size_t numParams = type1Params->size();
    for (size_t i = 0; i < numParams; ++i) {
      if (!equivalent((*type1Params)[i], (*type2Params)[i])) {
        return false;
      }
    }

    return true;
  }

  return false;
}

// -------------------------------------------------------------------
// DeclaredType

DeclaredType::DeclaredType(TypeClass cls, TypeDefn * de, Scope * parentScope, TypeShape shape)
  : TypeImpl(cls, shape)
  , IterableScope(parentScope)
  , defn_(de)
{
  DASSERT(de != NULL);
  setScopeName(de->name());
}

  /** Return the number of type parameters of this type. */
size_t DeclaredType::numTypeParams() const {
  TemplateSignature * tsig = defn_->templateSignature();
  if (tsig != NULL) {
    return tsig->patternVarCount();
  }

  TemplateInstance * tinst = defn_->templateInstance();
  if (tinst != NULL) {
    return tinst->typeArgs()->size();
  }

  return 0;
}

const Type * DeclaredType::typeParam(int index) const {
  TemplateSignature * tsig = defn_->templateSignature();
  if (tsig != NULL) {
    return tsig->typeParam(index);
  }

  TemplateInstance * tinst = defn_->templateInstance();
  if (tinst != NULL) {
    return tinst->typeArg(index);
  }

  DFAIL("Illegal State");
}

void DeclaredType::trace() const {
  safeMark(defn_);
  IterableScope::trace();
}

void DeclaredType::format(FormatStream & out) const {
  defn_->format(out);
}

// -------------------------------------------------------------------
// Utility functions

const Type * findCommonType(const Type * t0, const Type * t1) {
  DASSERT(t0 != NULL);
  DASSERT(t1 != NULL);

  if (t0->isEqual(t1)) {
    return t0;
  }

  ConversionRank tc0 = t0->canConvert(t1);
  ConversionRank tc1 = t1->canConvert(t0);
  if (tc1 > tc0) {
    return t1;
  } else if (tc0 > tc1) {
    return t0;
  } else {
    return NULL;
  }
}

Type * dealiasImpl(Type * t) {
  while (t != NULL && t->typeClass() == Type::Alias) {
    if (TypeAlias * alias = dyn_cast<TypeAlias>(t)) {
      t = const_cast<Type *>(alias->value());
      DASSERT_OBJ(t != NULL, alias);
    } else {
      break;
    }
  }

  while (const TypeBinding * pval = dyn_cast_or_null<TypeBinding>(t)) {
    Type * v = pval->value();
    if (v != NULL) {
      t = v;
    } else {
      break;
    }
  }

  return t;
}

const Type * dealias(const Type * t) {
  return dealiasImpl(const_cast<Type *>(t));
}

Type * dealias(Type * t) {
  return dealiasImpl(t);
}

void estimateTypeSize(const llvm::Type * type, size_t & numPointers, size_t & numBits) {

  switch (type->getTypeID()) {
    case llvm::Type::VoidTyID:
    case llvm::Type::FloatTyID:
    case llvm::Type::DoubleTyID:
    case llvm::Type::X86_FP80TyID:
    case llvm::Type::FP128TyID:
    case llvm::Type::PPC_FP128TyID:
    case llvm::Type::IntegerTyID:
      numBits += type->getPrimitiveSizeInBits();
      break;

    case llvm::Type::PointerTyID:
      numPointers += 1;
      break;

    case llvm::Type::StructTyID:
      for (llvm::Type::subtype_iterator it = type->subtype_begin(); it != type->subtype_end();
          ++it) {
        estimateTypeSize(*it, numPointers, numBits);
      }

      break;

    case llvm::Type::ArrayTyID: {
      const llvm::ArrayType * atype = cast<llvm::ArrayType>(type);
      size_t elemPointers = 0;
      size_t elemBits = 0;
      estimateTypeSize(atype->getElementType(), numPointers, numBits);
      numPointers += elemPointers * atype->getNumElements();
      numBits += elemBits * atype->getNumElements();
      break;
    }

    case llvm::Type::VectorTyID: {
      const llvm::VectorType * atype = cast<llvm::VectorType>(type);
      size_t elemPointers = 0;
      size_t elemBits = 0;
      estimateTypeSize(atype->getElementType(), numPointers, numBits);
      numPointers += elemPointers * atype->getNumElements();
      numBits += elemBits * atype->getNumElements();
      break;
    }

    case llvm::Type::OpaqueTyID:
    case llvm::Type::LabelTyID:
    case llvm::Type::MetadataTyID:
    case llvm::Type::FunctionTyID:
    default:
      break;
  }
}

bool isLargeIRType(const llvm::Type * type) {
  if (type->isAbstract() || !type->isFirstClassType()) {
    return true;
  }

  size_t numPointers = 0;
  size_t numBits = 0;
  estimateTypeSize(type, numPointers, numBits);
  return (numPointers > 2 || numBits > 64 || (numPointers > 0 && numBits > 32));
}

} // namespace tart