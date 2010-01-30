/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */

#include "tart/CFG/UnionType.h"
#include "tart/CFG/Module.h"
#include "tart/CFG/PrimitiveType.h"
#include "tart/CFG/CompositeType.h"
#include "tart/CFG/TupleType.h"
#include "tart/CFG/TypeOrdering.h"
#include "tart/Common/Diagnostics.h"
#include "tart/Objects/Builtins.h"

#if HAVE_STDARG_H
#include <stdarg.h>
#endif

namespace tart {

// -------------------------------------------------------------------
// UnionType

UnionType * UnionType::get(const SourceLocation & loc, const ConstTypeList & members) {
  return new UnionType(loc, members);
}

UnionType * UnionType::get(const SourceLocation & loc, ...) {
  ConstTypeList members;

  va_list args;
  va_start(args, loc);
  for (;;) {
    const Type * arg = va_arg(args, const Type *);
    if (arg == NULL) {
      break;
    }

    members.push_back(arg);
  }
  va_end(args);

  return new UnionType(loc, members);
}

UnionType::UnionType(const SourceLocation & loc, const ConstTypeList & members)
  : TypeImpl(Union, Shape_Unset)
  , loc_(loc)
  , numValueTypes_(0)
  , numReferenceTypes_(0)
  , hasVoidType_(false)
  , hasNullType_(false)
{
  // Make sure that the set of types is disjoint, meaning that there are no types
  // in the set which are subtypes of one another.
  TypeList combined;
  for (ConstTypeList::const_iterator it = members.begin(); it != members.end(); ++it) {
    const Type * type = dealias(*it);

    bool addNew = true;
    for (TypeList::iterator m = combined.begin(); m != combined.end();) {
      if ((*m)->isEqual(type) || type->isSubtype(*m)) {
        addNew = false;
      } else if ((*m)->isSubtype(type)) { // TODO: Is isSubtype the right test for this?
        m = combined.erase(m);
        continue;
      }

      ++m;
    }

    if (addNew) {
      combined.push_back(const_cast<Type *>(type));
    }
  }

  std::sort(combined.begin(), combined.end(), LexicalTypeOrdering());

  for (TypeList::const_iterator it = combined.begin(); it != combined.end(); ++it) {
    const Type * memberType = dealias(*it);
    if (memberType == &VoidType::instance) {
      hasVoidType_ = true;
    } else if (memberType == &NullType::instance) {
      hasNullType_ = true;
    } else if (memberType->isReferenceType()) {
      numReferenceTypes_ += 1;
    } else {
      numValueTypes_ += 1;
    }
  }

  members_ = TupleType::get(combined);
}

size_t UnionType::numTypeParams() const {
  return members_->size();
}

const Type * UnionType::typeParam(int index) const {
  return (*members_)[index];
}

bool UnionType::hasRefTypesOnly() const {
  return numValueTypes_ == 0 && !hasVoidType_;
}

bool UnionType::isSingleOptionalType() const {
  if (numValueTypes_ == 0) {
    return (hasNullType_ && !hasVoidType_ && numReferenceTypes_ == 1);
  } else if (numReferenceTypes_ == 0) {
    return hasVoidType_ && !hasNullType_ && numValueTypes_ == 1;
  }

  return false;
}

const Type * UnionType::getFirstNonVoidType() const {
  for (TupleType::const_iterator it = members_->begin(); it != members_->end(); ++it) {
    const Type * memberType = *it;
    if (!memberType->isVoidType() && !memberType->isNullType()) {
      return memberType;
    }
  }

  return NULL;
}

const llvm::Type * UnionType::createIRType() const {
  // Since LLVM does not support unions as first-class types, what we'll do is to find the
  // "largest" type and use that as the base representation. (Plus the discriminator field,
  // of course). However, we don't know what the size of a pointer is yet, so we'll calculate
  // the type size for both 32 and 64 bit machines. The only problem we might have is if the
  // largest type is different on 32 bit vs. 64 bits, in which case we'll just report failure
  // for now.

  size_t largestSize32 = 0;
  size_t largestSize64 = 0;
  const Type * largestType32 = 0;   // Largest type on 32-bit platforms
  const Type * largestType64 = 0;   // Largest type on 64-bit platforms.
  shape_ = Shape_Small_RValue;
  shape_ = Shape_Large_Value;

  // Create an array representing all of the IR types that correspond to the Tart types.
  for (ConstTypeList::const_iterator it = members().begin(); it != members().end(); ++it) {
    const Type * type = dealias(*it);

    const llvm::Type * irType = type->irEmbeddedType();
    irTypes_.push_back(irType);

    if (type->typeShape() == Shape_Large_Value) {
      shape_ = Shape_Large_Value;
    }

    size_t size32 = estimateTypeSize(irType, 32);
    size_t size64 = estimateTypeSize(irType, 64);

    if (size32 > largestSize32 || (size32 == largestSize32 && size64 > largestSize64)) {
      largestSize32 = size32;
      largestType32 = type;
    }

    if (size64 > largestSize64 || (size64 == largestSize64 && size32 > largestSize32)) {
      largestSize64 = size64;
      largestType64 = type;
    }
  }

  if (largestType32 != largestType64) {
    diag.error(loc_) << "Internal error: conflict generating union type:";
    diag.info(loc_) << "  Largest type on 32-bit system is " << largestType32;
    diag.info(loc_) << "  Largest type on 64-bit system is " << largestType64;
  }

  if (numValueTypes_ > 0 || hasVoidType_) {
    const llvm::Type * discriminatorType = getDiscriminatorType();
    const llvm::Type * largestType = largestType32->irType();
    if (largestType32->isReferenceType()) {
      largestType = llvm::PointerType::get(largestType, 0);
    }
    std::vector<const llvm::Type *> unionMembers;
    unionMembers.push_back(discriminatorType);
    unionMembers.push_back(largestType);
    return llvm::StructType::get(llvm::getGlobalContext(), unionMembers);
  } else if (hasNullType_ && numReferenceTypes_ == 1) {
    // If it's Null or some reference type, then use the reference type.
    shape_ = Shape_Primitive;
    return getFirstNonVoidType()->irEmbeddedType();
  } else {
    shape_ = Shape_Primitive;
    return Builtins::typeObject->irParameterType();
  }
}

const llvm::Type * UnionType::irParameterType() const {
  const llvm::Type * type = irType();
  if (shape_ == Shape_Large_Value) {
    type = llvm::PointerType::get(type, 0);
  }

  return type;
}

const llvm::Type * UnionType::getDiscriminatorType() const {
  size_t numStates = numValueTypes_;
  if (numReferenceTypes_ > 0 || hasVoidType_ || hasNullType_) {
    numStates += 1;
  }

  if (numStates == 2) {
    return llvm::Type::getInt1Ty(llvm::getGlobalContext());
  } else if (numStates < 256) {
    return llvm::Type::getInt8Ty(llvm::getGlobalContext());
  } else if (numStates < 0x10000) {
    return llvm::Type::getInt16Ty(llvm::getGlobalContext());
  } else {
    return llvm::Type::getInt32Ty(llvm::getGlobalContext());
  }
}

size_t UnionType::estimateTypeSize(const llvm::Type * type, size_t ptrSize) {
  switch (type->getTypeID()) {
    case llvm::Type::VoidTyID:
    case llvm::Type::FloatTyID:
    case llvm::Type::DoubleTyID:
    case llvm::Type::X86_FP80TyID:
    case llvm::Type::FP128TyID:
    case llvm::Type::PPC_FP128TyID:
    case llvm::Type::LabelTyID:
    case llvm::Type::IntegerTyID:
    case llvm::Type::FunctionTyID:
      return type->getPrimitiveSizeInBits();

    case llvm::Type::PointerTyID:
      return ptrSize;

    case llvm::Type::StructTyID: {
      size_t total = 0;
      unsigned numFields = type->getNumContainedTypes();
      for (unsigned i = 0; i < numFields; ++i) {
        total += estimateTypeSize(type->getContainedType(i), ptrSize);
        // TODO: Add padding?
      }

      return total;
    }

    case llvm::Type::ArrayTyID:
    case llvm::Type::VectorTyID:
      DFAIL("Implement");

    default:
      DFAIL("IllegalState");
  }
}

ConversionRank UnionType::convertImpl(const Conversion & cn) const {
  if (isEqual(cn.fromType)) {
    if (cn.resultValue != NULL) {
      *cn.resultValue = cn.fromValue;
    }

    return IdenticalTypes;
  }

  ConversionRank bestRank = Incompatible;
  const Type * bestType = NULL;

  // Create a temporary cn with no result value.
  Conversion ccTemp(cn);
  ccTemp.resultValue = NULL;
  for (TupleType::const_iterator it = members_->begin(); it != members_->end(); ++it) {
    ConversionRank rank = (*it)->convert(ccTemp);
    if (rank > bestRank) {
      bestRank = rank;
      bestType = (*it);
    }
  }

  // Since we're converting to a union type, it's not identical.
  // TODO: Don't know if we really need this.
  //if (bestRank == IdenticalTypes) {
  //  bestRank = ExactConversion;
  //}

  if (bestType != NULL && cn.resultValue != NULL) {
    // Do the conversion to the best type first.
    bestRank = bestType->convertImpl(cn);

    // And now add a cast to the union type.
    if (*cn.resultValue != NULL) {
      int typeIndex = getTypeIndex(bestType);
      if (typeIndex < 0) {
        //diag.error(cn.fromValue) << "Cannot convert type '" << cn.fromType << "' to '" <<
        //    this << "'";
        return Incompatible;
      }

      CastExpr * result = new CastExpr(
          Expr::UnionCtorCast, cn.fromValue->location(), this, *cn.resultValue);
      result->setTypeIndex(typeIndex);
      *cn.resultValue = result;
    }
  }

  return bestRank;
}

ConversionRank UnionType::convertTo(const Type * toType, const Conversion & cn) const {
  if (isSingleOptionalType() && toType->isReferenceType()) {
    // Find the single optional type.
    const Type * memberType = getFirstNonVoidType();
    DASSERT(memberType != NULL);
    ConversionRank rank = toType->canConvert(memberType, cn.options);
    if (rank != Incompatible && cn.resultValue != NULL) {
      *cn.resultValue = new CastExpr(Expr::CheckedUnionMemberCast, SourceLocation(),
          toType, cn.fromValue);
    }

    return rank == IdenticalTypes ? ExactConversion : rank;
  }

  return Incompatible;
}

bool UnionType::isEqual(const Type * other) const {
  if (other == this) {
    return true;
  }

  // A union type is the same if it contains the same types (dealiased).
  if (const UnionType * u = dyn_cast<UnionType>(other)) {
    // Make sure that all types in u are in this.
    for (TupleType::const_iterator it = u->typeArgs()->begin(); it != u->typeArgs()->end(); ++it) {
      if (getTypeIndex(*it) < 0) {
        return false;
      }
    }

    // Make sure that all types in this are in u.
    for (TupleType::const_iterator it = typeArgs()->begin(); it != typeArgs()->end(); ++it) {
      if (u->getTypeIndex(*it) < 0) {
        return false;
      }
    }

    return true;
  }

  return false;
}

bool UnionType::isSingular() const {
  for (TupleType::const_iterator it = members_->begin(); it != members_->end(); ++it) {
    if (!(*it)->isSingular()) {
      return false;
    }
  }

  return true;
}

bool UnionType::isSubtype(const Type * other) const {
  // TODO: Is this meaningful with unions?
  return isEqual(other);
  //DFAIL("Implement");
}

bool UnionType::includes(const Type * other) const {
  for (TupleType::const_iterator it = members_->begin(); it != members_->end(); ++it) {
    if ((*it)->includes(other)) {
      return true;
    }
  }

  return false;
}

Expr * UnionType::nullInitValue() const {
  return NULL;
}

TypeShape UnionType::typeShape() const {
  if (shape_ == Shape_Unset) {
    irType();
  }

  return shape_;
}

int UnionType::getTypeIndex(const Type * type) const {
  type = dealias(type);

  // If it only has reference types, then use subclass tests instead of
  // a discriminator field.
  if (numValueTypes_ == 0 && !hasVoidType_) {
    return 0;
  }

  // Otherwise, calculate the type index.
  int index = 0;
  for (TupleType::const_iterator it = members_->begin(); it != members_->end(); ++it) {
    if (type->isEqual(*it)) {
      return index;
    }

    ++index;
  }

  return -1;
}

Expr * UnionType::createDynamicCast(Expr * from, const Type * toType) const {
  const Type * fromType = dealias(from->type());
  if (toType->isEqual(fromType)) {
    return from;
  }

  // Determine all of the possible member types that could represent an object of
  // type 'toType'.
  for (TupleType::const_iterator it = members_->begin(); it != members_->end(); ++it) {
    const Type * memberType = *it;
    if (toType->canConvert(memberType)) {
      // TODO: Add additional cast if toType is not exactly the same type as the member.
      return new CastExpr(Expr::CheckedUnionMemberCast, from->location(), toType, from);
    }
  }

  diag.warn(from->location()) << "Union member cast from type '" << fromType << "' to '" <<
      toType << "' can never succeed.";
  return ConstantInteger::getConstantBool(from->location(), false);
}

void UnionType::format(FormatStream & out) const {
  for (TupleType::const_iterator it = members_->begin(); it != members_->end(); ++it) {
    if (it != members_->begin()) {
      out << " or ";
    }

    out << *it;
  }
}

void UnionType::trace() const {
  members_->trace();
}

} // namespace tart
