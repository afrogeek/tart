/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */

#include "tart/Sema/ClassAnalyzer.h"
#include "tart/CFG/CompositeType.h"
#include "tart/CFG/FunctionType.h"
#include "tart/CFG/FunctionDefn.h"
#include "tart/CFG/PropertyDefn.h"
#include "tart/CFG/PrimitiveType.h"
#include "tart/CFG/TypeDefn.h"
#include "tart/CFG/Template.h"
#include "tart/CFG/Module.h"
#include "tart/CFG/Block.h"
#include "tart/Common/Diagnostics.h"
#include "tart/Common/InternedString.h"
#include "tart/Sema/TypeAnalyzer.h"
#include "tart/Sema/FunctionAnalyzer.h"
#include "tart/Sema/EvalPass.h"
#include "tart/Objects/Builtins.h"

namespace tart {

static const Defn::Traits CONSTRUCTOR_TRAITS = Defn::Traits::of(Defn::Ctor);

static const CompositeType::PassSet PASS_SET_RESOLVE_TYPE = CompositeType::PassSet::of(
  CompositeType::ScopeCreationPass,
  CompositeType::BaseTypesPass
);

static const CompositeType::PassSet PASS_SET_LOOKUP = CompositeType::PassSet::of(
  CompositeType::ScopeCreationPass,
  CompositeType::BaseTypesPass,
  CompositeType::AttributePass
);

static const CompositeType::PassSet PASS_SET_CONSTRUCTION = CompositeType::PassSet::of(
  CompositeType::ScopeCreationPass,
  CompositeType::BaseTypesPass,
  CompositeType::AttributePass,
  CompositeType::NamingConflictPass,
  CompositeType::ConstructorPass
);

static const CompositeType::PassSet PASS_SET_CONVERSION = CompositeType::PassSet::of(
  CompositeType::ScopeCreationPass,
  CompositeType::BaseTypesPass,
  CompositeType::AttributePass,
  CompositeType::NamingConflictPass,
  CompositeType::ConverterPass
);

static const CompositeType::PassSet PASS_SET_EVALUATION = CompositeType::PassSet::of(
  CompositeType::ScopeCreationPass,
  CompositeType::BaseTypesPass,
  CompositeType::AttributePass,
  CompositeType::NamingConflictPass,
  CompositeType::ConverterPass,
  CompositeType::MemberTypePass,
  CompositeType::FieldPass,
  CompositeType::MethodPass,
  CompositeType::OverloadingPass
);

static const CompositeType::PassSet PASS_SET_TYPEGEN = CompositeType::PassSet::of(
  CompositeType::ScopeCreationPass,
  CompositeType::BaseTypesPass,
  CompositeType::NamingConflictPass,
  CompositeType::AttributePass,
  CompositeType::FieldPass,
  CompositeType::FieldTypePass
);

static const CompositeType::PassSet PASS_SET_CODEGEN = CompositeType::PassSet::of(
  CompositeType::ScopeCreationPass,
  CompositeType::BaseTypesPass,
  CompositeType::AttributePass,
  CompositeType::NamingConflictPass,
  CompositeType::ConverterPass,
  CompositeType::ConstructorPass,
  CompositeType::MemberTypePass,
  CompositeType::FieldPass,
  CompositeType::MethodPass,
  CompositeType::OverloadingPass,
  CompositeType::CompletionPass
);

ClassAnalyzer::ClassAnalyzer(TypeDefn * de)
  : DefnAnalyzer(de->module(), de->definingScope(), de)
  , target(de)
{
  DASSERT(de != NULL);
  DASSERT(isa<CompositeType>(target->typeValue()));
}

CompositeType * ClassAnalyzer::targetType() const {
  return static_cast<CompositeType *>(target->typeValue());
}

bool ClassAnalyzer::analyze(AnalysisTask task) {
  TaskInProgress tip(target, task);

  switch (task) {
    case Task_PrepTypeComparison:
      return runPasses(PASS_SET_RESOLVE_TYPE);

    case Task_PrepMemberLookup:
      return runPasses(PASS_SET_LOOKUP);

    case Task_PrepConstruction:
      return runPasses(PASS_SET_CONSTRUCTION);

    case Task_PrepConversion:
      return runPasses(PASS_SET_CONVERSION);

    case Task_PrepEvaluation:
      return runPasses(PASS_SET_EVALUATION);

    case Task_PrepTypeGeneration:
      return runPasses(PASS_SET_TYPEGEN);

    case Task_PrepCodeGeneration:
      return runPasses(PASS_SET_CODEGEN);

    default:
      return true;
  }
}

bool ClassAnalyzer::runPasses(CompositeType::PassSet passesToRun) {
  // Work out what passes need to be run.
  CompositeType * type = targetType();
  passesToRun.removeAll(type->passes().finished());
  if (passesToRun.empty()) {
    return true;
  }

  // Skip analysis of templates - for now.
  if (target->isTemplate()) {
    // Get the template scope and set it as the active scope.
    analyzeTemplateSignature(target);

    if (passesToRun.contains(CompositeType::BaseTypesPass) && !analyzeBaseClasses()) {
      return false;
    }

    if (passesToRun.contains(CompositeType::ScopeCreationPass) &&
        type->passes().begin(CompositeType::ScopeCreationPass)) {
      if (!createMembersFromAST(target)) {
        return false;
      }

      type->passes().finish(CompositeType::ScopeCreationPass);
    }

    return true;
  }

  if (target->isTemplateMember()) {
    return true;
  }

  if (passesToRun.contains(CompositeType::ScopeCreationPass) &&
      type->passes().begin(CompositeType::ScopeCreationPass)) {
    if (!createMembersFromAST(target)) {
      return false;
    }

    type->passes().finish(CompositeType::ScopeCreationPass);
  }

  if (passesToRun.contains(CompositeType::AttributePass) &&
      type->passes().begin(CompositeType::AttributePass)) {
    if (!resolveAttributes(target)) {
      return false;
    }

    type->passes().finish(CompositeType::AttributePass);
  }

  if (passesToRun.contains(CompositeType::NamingConflictPass) && !checkNameConflicts()) {
    return false;
  }

  if (passesToRun.contains(CompositeType::BaseTypesPass) && !analyzeBaseClasses()) {
    return false;
  }

  if (passesToRun.contains(CompositeType::MemberTypePass) && !analyzeMemberTypes()) {
    return false;
  }

  if (passesToRun.contains(CompositeType::FieldPass) && !analyzeFields()) {
    return false;
  }

  if (passesToRun.contains(CompositeType::ConverterPass) && !analyzeConverters()) {
    return false;
  }

  if (passesToRun.contains(CompositeType::ConstructorPass) && !analyzeConstructors()) {
    return false;
  }

  if (passesToRun.contains(CompositeType::MethodPass) && !analyzeMethods()) {
    return false;
  }

  if (passesToRun.contains(CompositeType::OverloadingPass) && !analyzeOverloading()) {
    return false;
  }

  if (passesToRun.contains(CompositeType::FieldTypePass) && !analyzeFieldTypes()) {
    return false;
  }

  if (passesToRun.contains(CompositeType::CompletionPass) && !analyzeCompletely()) {
    return false;
  }

  return true;
}

bool ClassAnalyzer::checkNameConflicts() {
  CompositeType * type = targetType();
  bool success = true;
  if (type->passes().begin(CompositeType::NamingConflictPass)) {
    Defn::DefnType dtype = target->defnType();
    const SymbolTable & symbols = type->members();
    for (SymbolTable::const_iterator entry = symbols.begin(); entry != symbols.end(); ++entry) {
      const SymbolTable::Entry & defns = entry->second;
      Defn::DefnType dtype = defns.front()->defnType();

      // First insure that all entries are the same type
      for (SymbolTable::Entry::const_iterator it = defns.begin(); it != defns.end(); ++it) {
        Defn * de = *it;
        if (de->defnType() != dtype) {
          diag.error(de) << "Definition of '" << de->name() << "' as '" << de <<
              "' conflicts with earlier definition:";
          diag.info(defns.front()) << defns.front();
          success = false;
          break;
        }
      }
    }

    type->passes().finish(CompositeType::NamingConflictPass);
  }

  return success;
}

bool ClassAnalyzer::analyzeBaseClasses() {
  CompositeType * type = targetType();
  if (type->passes().isRunning(CompositeType::BaseTypesPass)) {
    diag.error(target) << "Circular inheritance not allowed";
    return false;
  }

  if (!type->passes().begin(CompositeType::BaseTypesPass)) {
    return true;
  }

  bool result = analyzeBaseClassesImpl();
  type->passes().finish(CompositeType::BaseTypesPass);
  return result;
}

bool ClassAnalyzer::analyzeBaseClassesImpl() {

  // If there is no AST, then it means that this class was created
  // internally by the compiler, in which case the compiler is responsible
  // for setting up the base class list correctly.
  const ASTTypeDecl * ast = cast_or_null<const ASTTypeDecl>(target->ast());
  if (ast == NULL) {
    return true;
  }

  CompositeType * type = targetType();
  bool isFromTemplate =
      target->isTemplate() || target->isTemplateMember() || target->isPartialInstantiation();
  DASSERT_OBJ(isFromTemplate || type->isSingular(), type);
  DASSERT_OBJ(type->super() == NULL, type);

  // Check for valid finality
  if (target->isFinal()) {
    if (type->typeClass() == Type::Interface) {
      diag.error(target) << "Interface type cannot be final";
    } else if (type->typeClass() == Type::Protocol) {
      diag.error(target) << "Protocol type cannot be final";
    }
  }

  // Resolve base class references to real types.
  Type::TypeClass dtype = type->typeClass();
  const ASTNodeList & astBases = ast->bases();
  CompositeType * primaryBase = NULL;
  TypeAnalyzer ta(moduleForDefn(target), target->definingScope());
  if (target->isTemplate()) {
    ta.setActiveScope(&target->templateSignature()->paramScope());
  }

  for (ASTNodeList::const_iterator it = astBases.begin(); it != astBases.end(); ++it) {
    Type * baseType = ta.typeFromAST(*it);
    if (isErrorResult(baseType)) {
      return false;
    }

    TypeDefn * baseDefn = baseType->typeDefn();
    if (baseDefn == NULL || !isa<CompositeType>(baseType)) {
      diag.error(*it) << "Cannot inherit from " << *it << " type";
      return false;
    }

    if (!baseType->isSingular() && !isFromTemplate) {
      diag.error(*it) << "Base type '" << baseDefn << "' is a template, not a type";
      return false;
    }

    if (baseDefn->isFinal()) {
      diag.error(*it) << "Base type '" << baseDefn << "' is final";
    }

    // Recursively analyze the bases of the base
    if (!ClassAnalyzer(baseDefn).analyze(Task_PrepMemberLookup)) {
      return false;
    }

    Type::TypeClass baseKind = baseType->typeClass();
    bool isPrimary = false;
    switch (dtype) {
      case Type::Class:
        if (baseKind == Type::Class) {
          if (primaryBase == NULL) {
            isPrimary = true;
          } else {
            diag.error(target) << "classes can only have a single concrete supertype";
          }
        } else if (baseKind != Type::Interface) {
          diag.error(target) << (Defn *)target <<
              "a class can only inherit from class or interface";
        }
        break;

      case Type::Struct:
        if (baseKind != Type::Struct && baseKind != Type::Protocol) {
          diag.error(target) <<
            "struct can only derive from a struct or static interface type";
        } else if (primaryBase == NULL) {
          isPrimary = true;
        } else {
          diag.error(target) << "structs can only have a single concrete supertype";
        }
        break;

      case Type::Interface:
        if (baseKind != Type::Interface && baseKind != Type::Protocol) {
          diag.error(*it) << "interface can only inherit from interface or protocol";
        } else if (primaryBase == NULL) {
          isPrimary = true;
        }

        break;

      default:
        DFAIL("IllegalState");
        break;
    }

    // Add an external reference to this base (does nothing if it's defined
    // by this module.)
    CompositeType * baseClass = cast<CompositeType>(baseType);
    if (baseClass->isSingular()) {
      baseClass->addBaseXRefs(module);
    }

    if (isPrimary) {
      primaryBase = baseClass;
    } else {
      type->bases().push_back(baseClass);
    }
  }

  // If no base was specified, use Object.
  if (dtype == Type::Class && primaryBase == NULL && type != Builtins::typeObject) {
    primaryBase = static_cast<CompositeType *>(Builtins::typeObject);
    module->addSymbol(primaryBase->typeDefn());
  }

  type->setSuper(primaryBase);

  // define the super type
  if (primaryBase != NULL) {
    // Move the primary base to be first in the list.
    type->bases().insert(type->bases().begin(), primaryBase);
    propagateSubtypeAttributes(primaryBase->typeDefn(), target);
  }

  if (dtype == Type::Interface) {
    module->addSymbol(Builtins::funcTypecastError);
  }

  return true;
}

bool ClassAnalyzer::analyzeConverters() {
  CompositeType * type = targetType();
  if (type->passes().begin(CompositeType::ConverterPass)) {
    Type::TypeClass tcls = type->typeClass();
    if (tcls == Type::Class || tcls == Type::Struct) {
      // Note: "coerce" methods are *not* inherited.
      DefnList methods;
      if (type->lookupMember(istrings.idCoerce, methods, false)) {
        for (DefnList::iterator it = methods.begin(); it != methods.end(); ++it) {
          if (FunctionDefn * fn = dyn_cast<FunctionDefn>(*it)) {
            diag.recovered();

            if (FunctionAnalyzer(fn).analyze(Task_PrepTypeComparison) &&
                fn->returnType().isNonVoidType() &&
                fn->storageClass() == Storage_Static &&
                fn->params().size() == 1) {

              // Mark the constructor as singular if in fact it is.
              if (!fn->hasUnboundTypeParams() && type->isSingular()) {
                fn->addTrait(Defn::Singular);
              }

              type->coercers_.push_back(fn);
            }
          }
        }
      }
    }

    type->passes().finish(CompositeType::ConverterPass);
  }

  return true;
}

bool ClassAnalyzer::analyzeMemberTypes() {
  CompositeType * type = targetType();
  if (type->passes().begin(CompositeType::MemberTypePass)) {
    for (Defn * member = type->firstMember(); member != NULL; member = member->nextInScope()) {
      if (TypeDefn * memberType = dyn_cast<TypeDefn>(member)) {
        // TODO: Copy attributes that are inherited.
        memberType->copyTrait(target, Defn::Nonreflective);
      }
    }

    type->passes().finish(CompositeType::MemberTypePass);
  }

  return true;
}

bool ClassAnalyzer::analyzeFields() {
  CompositeType * type = targetType();
  if (type->passes().begin(CompositeType::FieldPass)) {
    CompositeType * super = type->super();
    // Also analyze base class fields.
    int instanceFieldCount = 0;
    int instanceFieldCountRecursive = 0;
    if (super != NULL) {
      // The extra check is to prevent infinite recursion when analyzing class Object.
      if (!super->passes().isFinished(CompositeType::FieldPass)) {
        ClassAnalyzer(super->typeDefn()).analyze(Task_PrepTypeComparison);
      }

      // Reserve one slot for the superclass.
      type->instanceFields_.push_back(NULL);
      instanceFieldCount = 1;
      instanceFieldCountRecursive = super->instanceFieldCountRecursive();
    }

    Defn::DefnType dtype = target->defnType();
    for (Defn * member = type->firstMember(); member != NULL; member = member->nextInScope()) {
      switch (member->defnType()) {
        case Defn::Var:
        case Defn::Let: {
          VariableDefn * field = static_cast<VariableDefn *>(member);
          field->copyTrait(target, Defn::Final);

          analyzeValueDefn(field, Task_PrepTypeComparison);
          DASSERT(field->type().isDefined());

          bool isStorageRequired = true;
          if (field->defnType() == Defn::Let) {
            if (field->initValue() != NULL && field->initValue()->isConstant()) {
              // TODO: There may be other cases not handled here.
              isStorageRequired = false;
            }
          }

          if (isStorageRequired) {
            if (type->typeClass() == Type::Interface) {
              diag.error(field) << "Data member not allowed in interface: " << field;
            }

            if (field->storageClass() == Storage_Instance) {
              field->setMemberIndex(instanceFieldCount++);
              field->setMemberIndexRecursive(instanceFieldCountRecursive++);
              type->instanceFields_.push_back(field);

              // Special case for non-reflective classes, we need to also export the types
              // of members.
              //if (target->hasTrait(Defn::Nonreflective)) {
              //  module->addSymbol(field);
              //}
            } else if (field->storageClass() == Storage_Static) {
              module->addSymbol(field);
              type->staticFields_.push_back(field);
            }
          }

          break;
        }

        case Defn::Namespace: {
          //DFAIL("Implement");
          break;
        }
      }
    }

    DASSERT(type->instanceFields_.size() == instanceFieldCount);
    type->passes().finish(CompositeType::FieldPass);
  }

  return true;
}

bool ClassAnalyzer::analyzeConstructors() {
  CompositeType * type = targetType();
  if (type->passes().begin(CompositeType::ConstructorPass)) {
    // Analyze the constructors first, because we may need them
    // during the rest of the analysis.
    Type::TypeClass tcls = type->typeClass();
    if (tcls == Type::Class || tcls == Type::Struct) {
      // Analyze superclass constructors
      if (type->super() != NULL &&
          !type->super()->passes().isFinished(CompositeType::ConstructorPass) &&
          !type->super()->passes().isRunning(CompositeType::ConstructorPass)) {
        ClassAnalyzer ca(type->super()->typeDefn());
        if (!ca.analyze(Task_PrepConstruction)) {
          return false;
        }
      }

      DefnList ctors;
      bool hasConstructors = false;
      if (type->lookupMember(istrings.idConstruct, ctors, false)) {
        for (DefnList::iterator it = ctors.begin(); it != ctors.end(); ++it) {
          if (FunctionDefn * ctor = dyn_cast<FunctionDefn>(*it)) {
            diag.recovered();

            hasConstructors = true;
            ctor->addTrait(Defn::Ctor);

            if (!FunctionAnalyzer(ctor).analyze(Task_PrepTypeComparison)) {
              continue;
            }

            if (!ctor->returnType().isDefined()) {
              ctor->functionType()->setReturnType(&VoidType::instance);
            }

            if (ctor->returnType().isNonVoidType()) {
              diag.fatal(ctor) << "Constructor cannot declare a return type.";
              break;
            }

            if (ctor->storageClass() != Storage_Instance) {
              diag.fatal(ctor) << "Constructor must be instance method.";
              break;
            }

            if (!ctor->hasUnboundTypeParams() && type->isSingular()) {
              // Mark the constructor as singular if in fact it is.
              ctor->addTrait(Defn::Singular);
            }

            analyzeConstructBase(ctor);
          } else {
            diag.fatal(*it) << "Member named 'construct' must be a method.";
            break;
          }
        }
      }

      // Look for creator functions.
      ctors.clear();
      if (type->lookupMember(istrings.idCreate, ctors, false)) {
        for (DefnList::iterator it = ctors.begin(); it != ctors.end(); ++it) {
          if (FunctionDefn * ctor = dyn_cast<FunctionDefn>(*it)) {
            diag.recovered();
            if (ctor->storageClass() == Storage_Static) {
              hasConstructors = true;
            }

            if (!FunctionAnalyzer(ctor).analyze(Task_PrepTypeComparison)) {
              continue;
            }

            // TODO: check return type.
          }
        }
      }

      if (!hasConstructors) {
        createDefaultConstructor();
      }
    }

    type->passes().finish(CompositeType::ConstructorPass);
  }

  return true;
}

void ClassAnalyzer::analyzeConstructBase(FunctionDefn * ctor) {
  CompositeType * type = targetType();
  CompositeType * superType = cast_or_null<CompositeType>(type->super());
  if (superType != NULL) {
    BlockList & blocks = ctor->blocks();
    for (BlockList::iterator blk = blocks.begin(); blk != blocks.end(); ++blk) {
      ExprList & exprs = (*blk)->exprs();
      for (ExprList::iterator e = exprs.begin(); e != exprs.end(); ++e) {
        //if (e->exprType() ==
      }
    }
  }
}

bool ClassAnalyzer::analyzeMethods() {
  CompositeType * type = targetType();
  if (type->passes().begin(CompositeType::MethodPass)) {
    Defn::DefnType dtype = target->defnType();

    // Analyze all methods
    for (Defn * member = type->firstMember(); member != NULL; member = member->nextInScope()) {
      if (METHOD_DEFS.contains(member->defnType()) || member->defnType() == Defn::Property) {
        if (member->isTemplate()) {
         analyzeTemplateSignature(member);
         if (member->hasUnboundTypeParams()) {
           continue;
         }
        }

        if (member->isFinal()) {
          if (type->typeClass() == Type::Interface || type->typeClass() == Type::Protocol) {
            diag.error(target) << "Interface or protocol method cannot be final";
          }
        } else if (member->visibility() != Public) {
          if (type->typeClass() == Type::Interface || type->typeClass() == Type::Protocol) {
            diag.error(target) << "Interface or protocol method cannot be non-public";
          }
        }

        if (ValueDefn * val = dyn_cast<ValueDefn>(member)) {
          analyzeValueDefn(val, Task_PrepTypeComparison);
        }
      }
    }

    const SymbolTable & symbols = type->members();
    for (SymbolTable::const_iterator entry = symbols.begin(); entry != symbols.end(); ++entry) {
      const SymbolTable::Entry & defns = entry->second;
      Defn::DefnType dtype = defns.front()->defnType();

      if (METHOD_DEFS.contains(dtype) || dtype == Defn::Property) {
        for (SymbolTable::Entry::const_iterator it = defns.begin(); it != defns.end(); ++it) {
          ValueDefn * val = cast<ValueDefn>(*it);
          if (val->hasUnboundTypeParams()) {
            continue;
          }

          // Compare with all previous defns
          for (SymbolTable::Entry::const_iterator m = defns.begin(); m != it; ++m) {
            ValueDefn * prevVal = cast<ValueDefn>(*m);
            if (prevVal->hasUnboundTypeParams()) {
              continue;
            }

            if (dtype == Defn::Property) {
              PropertyDefn * p1 = cast<PropertyDefn>(val);
              PropertyDefn * p2 = cast<PropertyDefn>(prevVal);
              if (p1->type().isEqual(p2->type())) {
                diag.error(p2) << "Definition of property << '" << p2 <<
                    "' conflicts with earlier definition:";
                diag.info(p1) << p1;
              }
            } else if (dtype == Defn::Indexer) {
              IndexerDefn * i1 = cast<IndexerDefn>(val);
              IndexerDefn * i2 = cast<IndexerDefn>(prevVal);
            } else {
              FunctionDefn * f1 = cast<FunctionDefn>(val);
              FunctionDefn * f2 = cast<FunctionDefn>(prevVal);
              if (f1->hasSameSignature(f2)) {
                diag.error(f2) << "Member type signature conflict";
                diag.info(f1) << "From here";
              }
            }
          }
        }
      }
    }

    type->passes().finish(CompositeType::MethodPass);
  }

  return true;
}

bool ClassAnalyzer::analyzeOverloading() {
  CompositeType * type = targetType();
  if (type->passes().begin(CompositeType::OverloadingPass)) {
    // Do overload analysis on all bases
    ClassList & bases = type->bases();
    for (ClassList::iterator it = bases.begin(); it != bases.end(); ++it) {
      analyzeTypeDefn((*it)->typeDefn(), Task_PrepEvaluation);
    }

    copyBaseClassMethods();
    createInterfaceTables();
    overrideMembers();
    addNewMethods();
    checkForRequiredMethods();

    type->passes().finish(CompositeType::OverloadingPass);
  }

  return true;
}

void ClassAnalyzer::copyBaseClassMethods() {
  // If it's not a normal class, it can still have a supertype.
  CompositeType * type = targetType();
  Type::TypeClass tcls = type->typeClass();
  CompositeType * superClass = type->super();
  if (superClass == NULL &&
      (tcls == Type::Interface || tcls == Type::Struct) &&
      !type->bases().empty()) {
    superClass = type->bases().front();
  }

  // Copy superclass methods to instance method table
  if (superClass != NULL) {
    DASSERT_OBJ(superClass->isSingular(), target);
    type->instanceMethods_.append(
        superClass->instanceMethods_.begin(),
        superClass->instanceMethods_.end());
  }
}

void ClassAnalyzer::createInterfaceTables() {
  typedef CompositeType::InterfaceList InterfaceList;

  // Get the set of all ancestor types.
  ClassSet ancestors;
  CompositeType * type = targetType();
  type->ancestorClasses(ancestors);

  // Remove from the set all types which are the first parent of some other type
  // that is already in the set, since they can use the same dispatch table.
  ClassSet interfaceTypes(ancestors);
  ancestors.insert(type);
  for (ClassSet::iterator it = ancestors.begin(); it != ancestors.end(); ++it) {
    CompositeType * base = *it;

    // The first parent of each parent can always be removed, since the itable
    // of any class is always a superset of the itable of its first parent.
    if (!base->bases().empty()) {
      CompositeType * baseBase = base->bases().front();
      interfaceTypes.remove(baseBase);
    }
  }

  // Create the tables for each interface that remains.
  for (ClassSet::iterator it = interfaceTypes.begin(); it != interfaceTypes.end(); ++it) {
    CompositeType * itype = *it;
    DASSERT(itype->typeClass() == Type::Interface);

    // Do the search before we push the new itable entry.
    const CompositeType::InterfaceTable * parentImpl = type->findBaseImplementationOf(itype);

    // Add an itable entry.
    type->interfaces_.push_back(CompositeType::InterfaceTable(itype));
    CompositeType::InterfaceTable & itable = type->interfaces_.back();

    if (parentImpl != NULL) {
      DASSERT(itype->instanceMethods_.size() == parentImpl->methods.size());
      itable.methods.append(parentImpl->methods.begin(), parentImpl->methods.end());
    } else {
      itable.methods.append(itype->instanceMethods_.begin(), itype->instanceMethods_.end());
    }
  }
}

void ClassAnalyzer::overrideMembers() {
  typedef CompositeType::InterfaceList InterfaceList;

  // In this case, we iterate through the symbol table so that we can
  // get all of the overloads at once.
  CompositeType * type = targetType();
  SymbolTable & clMembers = type->members();
  for (SymbolTable::iterator s = clMembers.begin(); s != clMembers.end(); ++s) {
    SymbolTable::Entry & entry = s->second;
    MethodList methods;
    MethodList getters;
    MethodList setters;
    FunctionDefn * uniqueGetter = NULL;
    PropertyDefn * prop = NULL;

    // Look for properties and methods. Methods can have more than one implementation
    // for the same name.
    // Find all same-named methods.
    for (SymbolTable::Entry::iterator it = entry.begin(); it != entry.end(); ++it) {
      if (FunctionDefn * func = dyn_cast<FunctionDefn>(*it)) {
        if (func->isSingular()) {
          module->addSymbol(func);
          if (func->storageClass() == Storage_Instance && !func->isCtor()) {
            methods.push_back(func);
          }
        }
      } else if ((*it)->defnType() == Defn::Property || (*it)->defnType() == Defn::Indexer) {
        prop = cast<PropertyDefn>(*it);
        if (prop->storageClass() == Storage_Instance && prop->isSingular()) {
          DASSERT_OBJ(prop->passes().isFinished(PropertyDefn::PropertyTypePass), prop);
          if (prop->getter() != NULL) {
            analyzeValueDefn(prop->getter(), Task_PrepTypeGeneration);
            getters.push_back(prop->getter());
          }

          if (prop->setter() != NULL) {
            analyzeValueDefn(prop->setter(), Task_PrepTypeGeneration);
            setters.push_back(prop->setter());
          }
        }
      }
    }

    InterfaceList & ifaceList = type->interfaces_;

    if (!methods.empty()) {
      // Insure that there's no duplicate method signatures.
      ensureUniqueSignatures(methods);

      // Update the table of instance methods and the interface tables
      overrideMethods(type->instanceMethods_, methods, true);
      for (InterfaceList::iterator it = ifaceList.begin(); it != ifaceList.end(); ++it) {
        overrideMethods(it->methods, methods, false);
      }
    }

    if (!getters.empty()) {
      ensureUniqueSignatures(getters);
      overridePropertyAccessors(type->instanceMethods_, prop, getters, true);
      for (InterfaceList::iterator it = ifaceList.begin(); it != ifaceList.end(); ++it) {
        overridePropertyAccessors(it->methods, prop, getters, false);
      }
    }

    if (!setters.empty()) {
      ensureUniqueSignatures(setters);
      overridePropertyAccessors(type->instanceMethods_, prop, setters, true);
      for (InterfaceList::iterator it = ifaceList.begin(); it != ifaceList.end(); ++it) {
        overridePropertyAccessors(it->methods, prop, setters, false);
      }
    }
  }
}

void ClassAnalyzer::ensureUniqueSignatures(MethodList & methods) {
  for (size_t i = 0; i < methods.size(); ++i) {
    for (size_t j = i + 1; j < methods.size(); ++j) {
      if (methods[i]->hasSameSignature(methods[j])) {
        diag.error(methods[j]) << "Member type signature conflict";
        diag.info(methods[i]) << "From here";
      }
    }
  }
}

void ClassAnalyzer::addNewMethods() {
  // Append all methods that aren't overrides of a superclass. Note that we
  // don't need to include 'final' methods since they are never called via
  // vtable lookup.
  CompositeType * type = targetType();
  for (Defn * de = type->firstMember(); de != NULL; de = de->nextInScope()) {
    if (de->storageClass() == Storage_Instance && de->isSingular()) {
      Defn::DefnType dt = de->defnType();
      if (dt == Defn::Function) {
        FunctionDefn * fn = static_cast<FunctionDefn *>(de);
        if (fn->isUndefined() && fn->overriddenMethods().empty()) {
          if (!fn->isCtor() || !fn->params().empty()) {
            diag.error(fn) << "Method '" << fn->name() <<
                "' defined with 'undef' but does not override a base class method.";
          }
        } else if (fn->isOverride()) {
          // TODO: Implement
        }

        if (!fn->isCtor() && !fn->isFinal() && fn->dispatchIndex() < 0) {
          fn->setDispatchIndex(type->instanceMethods_.size());
          type->instanceMethods_.push_back(fn);
        }
      } else if (dt == Defn::Property || dt == Defn::Indexer) {
        PropertyDefn * prop = static_cast<PropertyDefn *>(de);
        FunctionDefn * getter = prop->getter();
        if (getter != NULL && !getter->isFinal() && getter->dispatchIndex() < 0) {
          getter->setDispatchIndex(type->instanceMethods_.size());
          type->instanceMethods_.push_back(getter);
        }

        FunctionDefn * setter = prop->setter();
        if (setter != NULL && !setter->isFinal() && setter->dispatchIndex() < 0) {
          setter->setDispatchIndex(type->instanceMethods_.size());
          type->instanceMethods_.push_back(setter);
        }
      }
    }
  }
}

void ClassAnalyzer::checkForRequiredMethods() {
  typedef CompositeType::InterfaceList InterfaceList;

  if (target->isAbstract()) {
    return;
  }

  CompositeType * type = targetType();
  Type::TypeClass tcls = type->typeClass();
  MethodList & methods = type->instanceMethods_;
  if (!methods.empty()) {

    // Check for abstract or interface methods which weren't overridden.
    MethodList abstractMethods;
    for (MethodList::iterator it = methods.begin(); it != methods.end(); ++it) {
      FunctionDefn * func = *it;
      if (!func->hasBody() && !func->isExtern() && !func->isIntrinsic() && !func->isUndefined()) {
        abstractMethods.push_back(func);
      }
    }

    if (!abstractMethods.empty()) {
      if (tcls == Type::Struct || (tcls == Type::Class && !target->isAbstract())) {
        diag.recovered();
        diag.error(target) << "Concrete type '" << target <<
            "'lacks definition for the following methods:";
        for (MethodList::iterator it = abstractMethods.begin(); it != abstractMethods.end(); ++it) {
          diag.info(*it) << Format_Type << *it;
        }
      }

      return;
    }
  }

  InterfaceList & itab = type->interfaces_;
  for (InterfaceList::iterator it = itab.begin(); it != itab.end(); ++it) {
    MethodList unimpMethods;
    for (MethodList::iterator di = it->methods.begin(); di != it->methods.end(); ++di) {
      FunctionDefn * func = *di;
      if (!func->hasBody() && !func->isExtern() && !func->isIntrinsic() && !func->isUndefined()) {
        unimpMethods.push_back(func);
      }
    }

    if (!unimpMethods.empty()) {
      diag.recovered();
      diag.error(target) << "Concrete class '" << target <<
          "' implements interface '" << it->interfaceType <<
          "' but lacks implementations for:";
      for (MethodList::iterator it = unimpMethods.begin(); it != unimpMethods.end(); ++it) {
        diag.info(*it) << Format_Verbose << *it;
      }

      return;
    }
  }
}

void ClassAnalyzer::overrideMethods(MethodList & table, const MethodList & overrides,
    bool canHide) {
  // 'table' is the set of methods inherited from the superclass or interface.
  // 'overrides' is all of the methods defined in *this* class that share the same name.
  // 'canHide' is true if 'overrides' are from a class, false if from an interface.
  const char * name = overrides.front()->name();
  size_t tableSize = table.size();
  for (size_t i = 0; i < tableSize; ++i) {
    // For every inherited method whose name matches the name of the overrides.
    // See if there is a new method that goes in that same slot
    FunctionDefn * m = table[i];
    if (m->name() == name) {
      FunctionDefn * newMethod = findOverride(m, overrides);
      if (newMethod != NULL) {
        table[i] = newMethod;
        if (canHide && newMethod->dispatchIndex() < 0) {
          newMethod->setDispatchIndex(i);
        }

        if (m->hasBody() && !newMethod->isOverride()) {
          diag.error(newMethod) << "Method '" << newMethod->name() <<
              "' which overrides method in base class '" << m->parentDefn()->qualifiedName() <<
              "' should be declared with 'override'";
        }
        newMethod->overriddenMethods().insert(m);
      } else if (canHide) {
        diag.recovered();
        diag.warn(m) << "Definition of '" << m << "' is hidden";
        for (MethodList::const_iterator it = overrides.begin(); it != overrides.end(); ++it) {
          diag.info(*it) << "by '" << *it << "'";
        }
      }
    }
  }
}

void ClassAnalyzer::overridePropertyAccessors(MethodList & table, PropertyDefn * prop,
    const MethodList & accessors, bool canHide) {
  const char * name = accessors.front()->name();
  size_t tableSize = table.size();
  for (size_t i = 0; i < tableSize; ++i) {
    FunctionDefn * m = table[i];
    if (PropertyDefn * p = dyn_cast_or_null<PropertyDefn>(m->parentDefn())) {
      if (m->name() == name && p->name() == prop->name()) {
        FunctionDefn * newAccessor = findOverride(m, accessors);
        if (newAccessor != NULL) {
          table[i] = newAccessor;
          if (canHide && newAccessor->dispatchIndex() < 0) {
            newAccessor->setDispatchIndex(i);
          }
          newAccessor->overriddenMethods().insert(m);
        } else {
          diag.recovered();
          diag.warn(m) << "Invalid override of property accessor '" << m
              << "' by accessor of incompatible type:";
          for (MethodList::const_iterator it = accessors.begin(); it != accessors.end(); ++it) {
            diag.info(*it) << "by '" << *it << "'";
          }
        }
      }
    }
  }
}

FunctionDefn * ClassAnalyzer::findOverride(const FunctionDefn * f, const MethodList & overrides) {
  for (MethodList::const_iterator it = overrides.begin(); it != overrides.end(); ++it) {
    if ((*it)->canOverride(f)) {
      return *it;
    }
  }

  return NULL;
}

bool ClassAnalyzer::createDefaultConstructor() {
  // Determine if the superclass has a default constructor. If it doesn't,
  // then we cannot make a default constructor.
  CompositeType * type = targetType();
  CompositeType * super = type->super();
  FunctionDefn * superCtor = NULL;
  if (super != NULL && super->defaultConstructor() == NULL) {
    diag.fatal(target) << "Cannot create a default constructor for '" <<
        target << "' because super type '" << super <<
        "' has no default constructor";
    return false;
  }

  // List of parameters to the default constructor
  ParameterList requiredParams;
  ParameterList optionalParams;
  ParameterDefn * selfParam = new ParameterDefn(module, istrings.idSelf);
  selfParam->setType(type);
  selfParam->setInternalType(type);
  selfParam->addTrait(Defn::Singular);
  selfParam->setFlag(ParameterDefn::Reference, true);
  LValueExpr * selfExpr = new LValueExpr(target->location(), NULL, selfParam);

  //if (classType->getKind() == Type::Struct) {
  // The 'self' param of struct methods is passed by reference instead of by
  // value as normal.
  //  selfParam->setParameterFlag(ParameterDef::Reference, true);
  //}
  //requiredParams.push_back(selfParam);

  Block * constructorBody = new Block("entry");
  constructorBody->exitReturn(target->location(), NULL);
  for (Defn * de = type->firstMember(); de != NULL; de = de->nextInScope()) {
    if (de->storageClass() == Storage_Instance) {
      if (de->defnType() == Defn::Let) {
        VariableDefn * let = static_cast<VariableDefn *>(de);
        //analyze(let);

        // TODO: Write tests for this case (instance lets)
        if (let->initValue() != NULL) {
          // We need a better way to designate which lets require runtime init.
          DFAIL("Implement me!");
        }
      } else if (de->defnType() == Defn::Var) {
        VariableDefn * memberVar = static_cast<VariableDefn *>(de);
        analyzeValueDefn(memberVar, Task_PrepConstruction);
        Expr * defaultValue = memberVar->initValue();
        TypeRef memberType = memberVar->type().type();
        if (defaultValue == NULL) {
          // TODO: If this is 'final' it must be initialized here or in
          // the constructor.
          defaultValue = memberType.type()->nullInitValue();
          // TODO: Must be a constant...?
          if (defaultValue && !defaultValue->isConstant()) {
            defaultValue = NULL;
          }
        }

        Expr * initVal;
        if (memberType.typeClass() == Type::NArray) {
          // TODO: If this array is non-zero size, we have a problem I think.
          // Native arrays must be initialized in the constructor.
          continue;
        } else if (memberVar->visibility() == Public) {
          ParameterDefn * param = new ParameterDefn(module, memberVar->name());
          param->setLocation(target->location());
          param->setType(memberType);
          param->setInternalType(memberType);
          param->addTrait(Defn::Singular);
          param->passes().finish(VariableDefn::VariableTypePass);
          param->setInitValue(defaultValue);

          if (defaultValue != NULL) {
            optionalParams.push_back(param);
          } else {
            requiredParams.push_back(param);
          }

          initVal = new LValueExpr(target->location(), NULL, param);
        } else {
          if (defaultValue != NULL) {
            // TODO: This doesn't work because native pointer initializations
            // are the wrong type.
            initVal = defaultValue;
            continue;
          } else if (type == Builtins::typeObject) {
            continue;
          } else {
            // TODO: Write tests for this case (private instance variables
            // being initialized to default values.)
            diag.fatal(de) << "Unimplemented default initialization: " << de;
            DFAIL("Implement");
            continue;
          }
        }

        LValueExpr * memberExpr = new LValueExpr(target->location(), selfExpr, memberVar);
        Expr * initExpr = new AssignmentExpr(target->location(), memberExpr, initVal);
        constructorBody->append(initExpr);
        //diag.info(de) << "Uninitialized field " << de->qualifiedName() << " with default value " << initExpr;
        //DFAIL("Implement");
      }
    }
  }

  // Optional params go after required params.
  ParameterList params(requiredParams);
  params.append(optionalParams.begin(), optionalParams.end());

  FunctionType * funcType = new FunctionType(&VoidType::instance, params);
  funcType->setSelfParam(selfParam);
  FunctionDefn * constructorDef = new FunctionDefn(Defn::Function, module, istrings.idConstruct);
  constructorDef->setFunctionType(funcType);
  constructorDef->setLocation(target->location());
  constructorDef->setStorageClass(Storage_Instance);
  constructorDef->setVisibility(Public);
  constructorDef->addTrait(Defn::Ctor);
  constructorDef->addTrait(Defn::Ctor);
  constructorDef->copyTrait(target, Defn::Synthetic);
  constructorDef->blocks().push_back(constructorBody);
  constructorDef->passes().finished().addAll(
      FunctionDefn::PassSet::of(
          FunctionDefn::AttributePass,
          FunctionDefn::ControlFlowPass,
          FunctionDefn::ParameterTypePass,
          FunctionDefn::ReturnTypePass));

  //constructorDef->setBody(constructorBody);
  if (target->isSingular()) {
    constructorDef->addTrait(Defn::Singular);

    // If it's synthetic, then don't add the constructor unless someone
    // actually calls it.
    if (!target->isSynthetic()) {
      module->addSymbol(constructorDef);
    }
  }

  DASSERT_OBJ(constructorDef->isSingular(), constructorDef);
  if (!funcType->isSingular()) {
    diag.fatal(target) << "Default constructor type " << funcType << " is not singular";
    funcType->whyNotSingular();
  }

  type->addMember(constructorDef);
  constructorDef->createQualifiedName(target);
  return true;
}

bool ClassAnalyzer::analyzeFieldTypes() {
  CompositeType * type = targetType();
  if (type->passes().begin(CompositeType::FieldTypePass, true)) {
    if (type->super() != NULL) {
      analyzeType(type->super(), Task_PrepTypeGeneration);
    }

    for (DefnList::iterator it = type->instanceFields_.begin(); it != type->instanceFields_.end();
        ++it) {
      VariableDefn * var = dyn_cast_or_null<VariableDefn>(*it);
      if (var != NULL) {
        analyzeType(var->type(), Task_PrepTypeGeneration);
      }
    }

    type->passes().finish(CompositeType::FieldTypePass);
  }

  return true;
}

bool ClassAnalyzer::analyzeCompletely() {
  // In this case, it's OK if it's already running. All we care about is that it eventually
  // completes, not that it completes right now.
  CompositeType * type = targetType();
  if (type->passes().begin(CompositeType::CompletionPass, true)) {
    CompositeType * super = type->super();
    if (super != NULL) {
      analyzeType(super, Task_PrepCodeGeneration);
    }

    for (Defn * member = type->firstMember(); member != NULL; member = member->nextInScope()) {
      analyzeDefn(member, Task_PrepCodeGeneration);
    }

    /*for (DefnList::iterator it = type->staticFields_.begin(); it != type->staticFields_.end();
        ++it) {
      VariableDefn * var = cast<VariableDefn>(*it);
      if (var->initValue() != NULL) {
        Expr * initVal = var->initValue();
        Expr * constInitVal = EvalPass::eval(initVal, true);
        if (constInitVal != NULL) {
          var->setInitValue(constInitVal);
        } else {
          DFAIL("Implement");
        }
      }
    }*/

    type->passes().finish(CompositeType::CompletionPass);
  }

  return true;
}

} // namespace tart
