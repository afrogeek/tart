/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */
 
#ifndef TART_CFG_PRIMITIVETYPE_H
#define TART_CFG_PRIMITIVETYPE_H

#ifndef TART_CFG_TYPE_H
#include "tart/CFG/Type.h"
#endif

#ifndef TART_CFG_DEFN_H
#include "tart/CFG/Defn.h"
#endif

namespace tart {

class ASTBuiltIn;

// -------------------------------------------------------------------
// Base class for primitive types
class PrimitiveType : public DeclaredType {
protected:
  ConversionRank convertToInteger(const Conversion & cn) const;
  ConversionRank convertConstantToInteger(const Conversion & cn) const;
  ConversionRank fromUnsizedIntToInt(const ConstantInteger * cint, Expr ** out) const;

  ConversionRank convertToFloat(const Conversion & cn) const;
  ConversionRank convertConstantToFloat(const Conversion & cn) const;
  ConversionRank fromUnsizedIntToFloat(const ConstantInteger * cint, Expr ** out) const;

  ConversionRank convertToBool(const Conversion & cn) const;
  ConversionRank convertConstantToBool(const Conversion & cn) const;
  
  PrimitiveType * nextType_;
  
public:

  /** Construct a primitive type */
  PrimitiveType(TypeDefn * de);

  /** Deferred initialization function, unique to each type. */
  virtual void init() = 0;

  /** Return the type id. */
  virtual TypeId getTypeId() const = 0;

  /** Return the number of bits of this primitive type. */
  virtual uint32_t numBits() const = 0;

  /** Define a constant member of this type. */
  void defineConstant(const char * name, ConstantExpr * value);

  // Overrides
  const llvm::Type * createIRType() const;
  virtual bool isSingular() const { return true; }
  
  static inline bool classof(const PrimitiveType *) { return true; }
  static inline bool classof(const Type * t) {
    return t->typeClass() == Type::Primitive;
  }
  
  PrimitiveType * nextType() const { return nextType_; }
  
  // Return an integer type that fits the given number of bits.
  static PrimitiveType * fitIntegerType(size_t nBits, bool isUnsigned);

  // Static list of all primitive types.
  static PrimitiveType * primitiveTypeList;

  // If 'in' is an enum type, return its base type, otherwise just return 'in'.
  static const Type * derefEnumType(const Type * in);
};

// -------------------------------------------------------------------
// Implementation class for primitive types
template<TypeId typeId>
class PrimitiveTypeImpl : public PrimitiveType {
public:
  
  /** Construct a primitive type */
  PrimitiveTypeImpl() : PrimitiveType(&typedefn) {}

  /** Deferred initialization function, unique to each type. */
  void init();

  Expr * nullInitValue() const;
  bool isReferenceType() const { return typeId == TypeId_Null; }
  
  // Overrides 

  TypeId getTypeId() const { return typeId; }
  uint32_t numBits() const;
  ConversionRank convertImpl(const Conversion & conversion) const;
  static inline bool classof(const PrimitiveTypeImpl<typeId> *) { return true; }
  static inline bool classof(const Type * t) {
    return t->typeClass() == Type::Primitive &&
        static_cast<const PrimitiveType *>(t)->getTypeId() == typeId;
  }

  bool isSubtype(const Type * other) const;
  bool includes(const Type * other) const;

  /** Singleton instance. */
  static PrimitiveTypeImpl instance;
  static TypeDefn typedefn;
  static ASTBuiltIn biDef;
  static TypeIdSet MORE_GENERAL;
  static TypeIdSet INCLUDES;
};

template<TypeId typeId> bool PrimitiveTypeImpl<typeId>::isSubtype(const Type * other) const {
  if (other == this) {
    return true;
  }

  if (other->typeClass() == Type::Primitive) {
    const PrimitiveType * ptype = static_cast<const PrimitiveType *>(other);
    return MORE_GENERAL.contains(ptype->getTypeId());
  }

  return false;
}
  
template<TypeId typeId> bool PrimitiveTypeImpl<typeId>::includes(const Type * other) const {
  other = derefEnumType(other);
  if (other == this) {
    return true;
  }

  if (other->typeClass() == Type::Primitive) {
    const PrimitiveType * ptype = static_cast<const PrimitiveType *>(other);
    return INCLUDES.contains(ptype->getTypeId());
  }

  return false;
}


template<TypeId typeId>
ASTBuiltIn PrimitiveTypeImpl<typeId>::biDef(&typedefn);

template<TypeId typeId>
PrimitiveTypeImpl<typeId> PrimitiveTypeImpl<typeId>::instance;

// -------------------------------------------------------------------
// Specific primitive type implementations

typedef PrimitiveTypeImpl<TypeId_Void>    VoidType;
typedef PrimitiveTypeImpl<TypeId_Bool>    BoolType;
typedef PrimitiveTypeImpl<TypeId_Char>    CharType;
typedef PrimitiveTypeImpl<TypeId_SInt8>   ByteType;
typedef PrimitiveTypeImpl<TypeId_SInt16>  ShortType;
typedef PrimitiveTypeImpl<TypeId_SInt32>  IntType;
typedef PrimitiveTypeImpl<TypeId_SInt64>  LongType;
typedef PrimitiveTypeImpl<TypeId_UInt8>   UByteType;
typedef PrimitiveTypeImpl<TypeId_UInt16>  UShortType;
typedef PrimitiveTypeImpl<TypeId_UInt32>  UIntType;
typedef PrimitiveTypeImpl<TypeId_UInt64>  ULongType;
typedef PrimitiveTypeImpl<TypeId_Float>   FloatType;
typedef PrimitiveTypeImpl<TypeId_Double>  DoubleType;
typedef PrimitiveTypeImpl<TypeId_Null>    NullType;
typedef PrimitiveTypeImpl<TypeId_UnsizedInt> UnsizedIntType;
typedef PrimitiveTypeImpl<TypeId_Bad>     BadType;

}

#endif
