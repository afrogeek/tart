/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */

#include "tart/AST/ASTDecl.h"
#include "tart/Sema/TypeAnalyzer.h"
#include "tart/Sema/DefnAnalyzer.h"
#include "tart/Sema/BindingEnv.h"
#include "tart/CFG/CompositeType.h"
#include "tart/CFG/FunctionType.h"
#include "tart/CFG/FunctionDefn.h"
#include "tart/CFG/TypeDefn.h"
#include "tart/CFG/PrimitiveType.h"
#include "tart/CFG/NativeType.h"
#include "tart/CFG/UnionType.h"
#include "tart/CFG/Template.h"
#include "tart/CFG/Module.h"
#include "tart/Common/Diagnostics.h"
#include "tart/Common/PackageMgr.h"
#include "tart/Objects/Builtins.h"

namespace tart {

Type * TypeAnalyzer::typeFromAST(const ASTNode * ast) {
  if (ast == NULL) {
    return NULL;
  }

  const SourceLocation & loc = ast->getLocation();
  switch (ast->getNodeType()) {
    case ASTNode::Id: 
    case ASTNode::Member:
    case ASTNode::Specialize: {
      // Most of the work is done by lookupName. The rest is just validating
      // the result and making sure it's a type.
      ExprList typeExprs;
      lookupName(typeExprs, ast);
      
      if (typeExprs.empty()) {
        diag.fatal(loc) << "Undefined type '" << ast << "'";
        return &BadType::instance;
      }

      DefnList typeList;
      if (getTypesFromExprs(loc, typeExprs, typeList)) {
        if (typeList.size() > 1) {
          diag.fatal(loc) << "Multiple definitions for '" << ast << "'";
          return &BadType::instance;
        }
        
        TypeDefn * tdef = static_cast<TypeDefn *>(typeList.front());
        Type * type = tdef->getTypeValue();
        if (type->typeClass() == Type::NativePointer) {
          AnalyzerBase::analyzeTypeDefn(tdef, Task_PrepCallOrUse);
          //analyzeLater(tdef);
          //DFAIL("Implement");
          //NativePointerTypeAnalyzer(
          //  static_cast<NativePointerType *>(type)).analyze(Task_InferType);
        } else /*if (tdef->defnType() != Defn::TypeParameter)*/ {
          analyzeLater(tdef);
        }

        return type;
      }
      
      diag.error(loc) << "'" << ast << "' is not a type expression";
      for (ExprList::iterator it = typeExprs.begin(); it != typeExprs.end(); ++it) {
        diag.info(*it) << Format_Verbose << *it << " (" << (*it)->exprType() << ")";
      }
      
      return &BadType::instance;
    }

    case ASTNode::Array: {
      const ASTUnaryOp * arrayOp = static_cast<const ASTUnaryOp *>(ast);
      Type * elementType = typeFromAST(arrayOp->arg());
      DASSERT(elementType != NULL);

      CompositeType * arrayType = getArrayTypeForElement(elementType);
      if (arrayType->isSingular()) {
        analyzeLater(arrayType->typeDefn());
      }

      return arrayType;
    }

    case ASTNode::BuiltIn: {
      Defn * def = static_cast<const ASTBuiltIn *>(ast)->getValue();
      if (TypeDefn * tdef = dyn_cast<TypeDefn>(def)) {
        return tdef->getTypeValue();
      } else {
        diag.fatal(ast) << "'" << def->getName() << "' is not a type";
        return &BadType::instance;
      }
    }
    
    case ASTNode::LogicalOr: {
      const ASTOper * unionOp = static_cast<const ASTOper *>(ast);
      const ASTNodeList & args = unionOp->args();
      TypeList unionTypes;
      
      for (ASTNodeList::const_iterator it = args.begin(); it != args.end(); ++it) {
        Type * elementType = typeFromAST(*it);
        if (isErrorResult(elementType)) {
          return elementType;
        }
        
        unionTypes.push_back(elementType);
      }
      
      return UnionType::create(ast->getLocation(), unionTypes);
    }

    case ASTNode::AnonFn: {
      FunctionType * ftype = typeFromFunctionAST(static_cast<const ASTFunctionDecl *>(ast));
      if (isErrorResult(ftype)) {
        return ftype;
      }

      if (ftype->returnType() == NULL) {
        ftype->setReturnType(&VoidType::instance);
      }

      return ftype;
    }

    default:
      diag.fatal(ast) << "invalid node type " <<
          getNodeTypeName(ast->getNodeType());
      DFAIL("Unsupported node type");
  }
}

void TypeAnalyzer::undefinedType(const ASTNode * ast) {
  diag.fatal(ast) << "Undefined type '" << ast << "'";
  diag.writeLnIndent("Scopes searched:");
  dumpScopeHierarchy();
}

bool TypeAnalyzer::typeDefnListFromAST(const ASTNode * ast, DefnList & defns) {
  ExprList results;
  lookupName(results, ast, NULL);
  const SourceLocation & loc = ast->getLocation();
  for (ExprList::iterator it = results.begin(); it != results.end();
      ++it) {
    if (ConstantType * ctype = dyn_cast<ConstantType>(*it)) {
      if (TypeDefn * tdef = ctype->value()->typeDefn()) {
        defns.push_back(tdef);
      } else {
        diag.fatal(loc) << "'" << ctype << "' is not a named type.";
      }
    } else {
      diag.fatal(loc) << "'" << *it << "' is not a type.";
    }
  }

  return !defns.empty();
}

FunctionType * TypeAnalyzer::typeFromFunctionAST( const ASTFunctionDecl * ast) {
  Type * returnType = typeFromAST(ast->returnType());
  const ASTParamList & astParams = ast->params();
  ParameterList params;
  for (ASTParamList::const_iterator it = astParams.begin(); it != astParams.end(); ++it) {
    ASTParameter * aparam = *it;
    
    // Note that type might be NULL if not specified. We'll pick it up
    // later from the default value.
    Type * paramType = typeFromAST(aparam->getType());
    ParameterDefn * param = new ParameterDefn(NULL, aparam);
    param->setType(paramType);
    params.push_back(param);
  }
  
  return new FunctionType(returnType, params);
}

CompositeType * TypeAnalyzer::getArrayTypeForElement(Type * elementType) {
  // Look up the array class
  TemplateSignature * arrayTemplate = Builtins::typeArray->typeDefn()->templateSignature();

  // Do analysis on template if needed.
  if (arrayTemplate->getAST() != NULL) {
    DefnAnalyzer da(&Builtins::module, &Builtins::module);
    da.analyzeTemplateSignature(Builtins::typeArray->typeDefn());
  }

  DASSERT_OBJ(arrayTemplate->paramScope().getCount() == 1, elementType);

  BindingEnv arrayEnv(arrayTemplate);
  arrayEnv.bind(arrayTemplate->patternVar(0), elementType);
  //PatternBinding * elementTypeBinding = arrayEnv.getBinding(arrayTemplate->patternVar(0));
  //DASSERT(elementTypeBinding != NULL);
  //elementTypeBinding->setValue(elementType, true);
  return cast<CompositeType>(cast<TypeDefn>(
      arrayTemplate->instantiate(SourceLocation(), arrayEnv))->getTypeValue());
}

bool TypeAnalyzer::analyzeTypeExpr(Type * type) {
  TypeDefn * de = type->typeDefn();
  if (de != NULL) {
    analyzeTypeDefn(de, Task_PrepMemberLookup);
    analyzeLater(de);
  } else {
    switch (type->typeClass()) {
      case Type::Function:
        return true;

      case Type::Constraint:
      case Type::Tuple:
      case Type::Pattern:
      //case Type::Binding:
      default:
        DFAIL("Implement");
        break;
    }
  }
  
  return true;
}

} // namespace tart
