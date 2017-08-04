/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtQml module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qv4codegen_p.h"
#include "qv4util_p.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QStringList>
#include <QtCore/QSet>
#include <QtCore/QBuffer>
#include <QtCore/QBitArray>
#include <QtCore/QLinkedList>
#include <QtCore/QStack>
#include <private/qqmljsast_p.h>
#include <private/qv4string_p.h>
#include <private/qv4value_p.h>
#include <private/qv4compilercontext_p.h>
#include <private/qv4compilercontrolflow_p.h>
#include <private/qv4bytecodegenerator_p.h>
#include <private/qv4compilationunit_moth_p.h>
#include <private/qv4compilerscanfunctions_p.h>

#include <cmath>
#include <iostream>

#ifdef CONST
#undef CONST
#endif

QT_USE_NAMESPACE
using namespace QV4;
using namespace QV4::Compiler;
using namespace QQmlJS::AST;

static inline QV4::Runtime::RuntimeMethods aluOpFunction(QSOperator::Op op)
{
    switch (op) {
    case QSOperator::Invalid:
        return QV4::Runtime::InvalidRuntimeMethod;
    case QSOperator::Add:
        return QV4::Runtime::InvalidRuntimeMethod;
    case QSOperator::Sub:
        return QV4::Runtime::sub;
    case QSOperator::Mul:
        return QV4::Runtime::mul;
    case QSOperator::Div:
        return QV4::Runtime::div;
    case QSOperator::Mod:
        return QV4::Runtime::mod;
    case QSOperator::LShift:
        return QV4::Runtime::shl;
    case QSOperator::RShift:
        return QV4::Runtime::shr;
    case QSOperator::URShift:
        return QV4::Runtime::ushr;
    case QSOperator::Gt:
        return QV4::Runtime::greaterThan;
    case QSOperator::Lt:
        return QV4::Runtime::lessThan;
    case QSOperator::Ge:
        return QV4::Runtime::greaterEqual;
    case QSOperator::Le:
        return QV4::Runtime::lessEqual;
    case QSOperator::Equal:
        return QV4::Runtime::equal;
    case QSOperator::NotEqual:
        return QV4::Runtime::notEqual;
    case QSOperator::StrictEqual:
        return QV4::Runtime::strictEqual;
    case QSOperator::StrictNotEqual:
        return QV4::Runtime::strictNotEqual;
    case QSOperator::BitAnd:
    case QSOperator::BitOr:
    case QSOperator::BitXor:
        Q_UNREACHABLE();
        // fall through
    default:
        Q_ASSERT(!"Unknown AluOp");
        return QV4::Runtime::InvalidRuntimeMethod;
    }
};

Codegen::Codegen(QV4::Compiler::JSUnitGenerator *jsUnitGenerator, bool strict)
    : _module(0)
    , _returnAddress(0)
    , _context(0)
    , _labelledStatement(0)
    , jsUnitGenerator(jsUnitGenerator)
    , _strictMode(strict)
    , _fileNameIsUrl(false)
    , hasError(false)
{
    jsUnitGenerator->codeGeneratorName = QStringLiteral("moth");
}

void Codegen::generateFromProgram(const QString &fileName,
                                  const QString &sourceCode,
                                  Program *node,
                                  Module *module,
                                  CompilationMode mode)
{
    Q_ASSERT(node);

    _module = module;
    _context = 0;

    _module->fileName = fileName;

    ScanFunctions scan(this, sourceCode, mode);
    scan(node);

    defineFunction(QStringLiteral("%entry"), node, 0, node->elements);
}

void Codegen::enterContext(Node *node)
{
    _context = _module->contextMap.value(node);
    Q_ASSERT(_context);
}

int Codegen::leaveContext()
{
    Q_ASSERT(_context);
    Q_ASSERT(!_context->controlFlow);
    int functionIndex = _context->functionIndex;
    _context = _context->parent;
    return functionIndex;
}

Codegen::Reference Codegen::unop(UnaryOperation op, const Reference &expr)
{
    if (hasError)
        return _expr.result();

#ifndef V4_BOOTSTRAP
    if (expr.isConst()) {
        auto v = Value::fromReturnedValue(expr.constant);
        if (v.isNumber()) {
            switch (op) {
            case Not:
                return Reference::fromConst(this, Encode(!v.toBoolean()));
            case UMinus:
                return Reference::fromConst(this, Runtime::method_uMinus(v));
            case UPlus:
                return expr;
            case Compl:
                return Reference::fromConst(this, Encode((int)~v.toInt32()));
            default:
                break;
            }
        }
    }
#endif // V4_BOOTSTRAP

    switch (op) {
    case UMinus: {
        expr.loadInAccumulator();
        Instruction::UMinus uminus;
        bytecodeGenerator->addInstruction(uminus);
        return Reference::fromAccumulator(this);
    }
    case UPlus: {
        expr.loadInAccumulator();
        Instruction::UPlus uplus;
        bytecodeGenerator->addInstruction(uplus);
        return Reference::fromAccumulator(this);
    }
    case Not: {
        expr.loadInAccumulator();
        Instruction::UNot unot;
        bytecodeGenerator->addInstruction(unot);
        return Reference::fromAccumulator(this);
    }
    case Compl: {
        expr.loadInAccumulator();
        Instruction::UCompl ucompl;
        bytecodeGenerator->addInstruction(ucompl);
        return Reference::fromAccumulator(this);
    }
    case PostIncrement:
        if (!_expr.accept(nx) || requiresReturnValue) {
            Reference e = expr.asLValue();
            e.loadInAccumulator();
            Instruction::UPlus uplus;
            bytecodeGenerator->addInstruction(uplus);
            Reference originalValue = Reference::fromStackSlot(this).storeRetainAccumulator();
            Instruction::Increment inc;
            bytecodeGenerator->addInstruction(inc);
            e.storeConsumeAccumulator();
            return originalValue;
        } else {
            // intentionally fall-through: the result is never used, so it's equivalent to
            // "expr += 1", which is what a pre-increment does as well.
        }
    case PreIncrement: {
        Reference e = expr.asLValue();
        e.loadInAccumulator();
        Instruction::Increment inc;
        bytecodeGenerator->addInstruction(inc);
        if (_expr.accept(nx))
            return e.storeConsumeAccumulator();
        else
            return e.storeRetainAccumulator();
    }
    case PostDecrement:
        if (!_expr.accept(nx) || requiresReturnValue) {
            Reference e = expr.asLValue();
            e.loadInAccumulator();
            Instruction::UPlus uplus;
            bytecodeGenerator->addInstruction(uplus);
            Reference originalValue = Reference::fromStackSlot(this).storeRetainAccumulator();
            Instruction::Decrement dec;
            bytecodeGenerator->addInstruction(dec);
            e.storeConsumeAccumulator();
            return originalValue;
        } else {
            // intentionally fall-through: the result is never used, so it's equivalent to
            // "expr -= 1", which is what a pre-decrement does as well.
        }
    case PreDecrement: {
        Reference e = expr.asLValue();
        e.loadInAccumulator();
        Instruction::Decrement dec;
        bytecodeGenerator->addInstruction(dec);
        if (_expr.accept(nx))
            return e.storeConsumeAccumulator();
        else
            return e.storeRetainAccumulator();
    }
    }

    Q_UNREACHABLE();
}

void Codegen::accept(Node *node)
{
    if (hasError)
        return;

    if (node)
        node->accept(this);
}

void Codegen::statement(Statement *ast)
{
    RegisterScope scope(this);

    bytecodeGenerator->setLocation(ast->firstSourceLocation());
    accept(ast);
}

void Codegen::statement(ExpressionNode *ast)
{
    RegisterScope scope(this);

    if (! ast) {
        return;
    } else {
        Result r(nx);
        qSwap(_expr, r);
        accept(ast);
        if (hasError)
            return;
        qSwap(_expr, r);
        if (r.result().loadTriggersSideEffect())
            r.result().loadInAccumulator(); // triggers side effects
    }
}

void Codegen::condition(ExpressionNode *ast, const BytecodeGenerator::Label *iftrue,
                        const BytecodeGenerator::Label *iffalse, bool trueBlockFollowsCondition)
{
    if (ast) {
        Result r(iftrue, iffalse, trueBlockFollowsCondition);
        qSwap(_expr, r);
        accept(ast);
        qSwap(_expr, r);
        if (r.format() == ex) {
            Q_ASSERT(iftrue == r.iftrue());
            Q_ASSERT(iffalse == r.iffalse());
            bytecodeGenerator->setLocation(ast->firstSourceLocation());
            r.result().loadInAccumulator();
            if (r.trueBlockFollowsCondition())
                bytecodeGenerator->jumpNe().link(*r.iffalse());
            else
                bytecodeGenerator->jumpEq().link(*r.iftrue());
        }
    }
}

Codegen::Reference Codegen::expression(ExpressionNode *ast)
{
    Result r;
    if (ast) {
        qSwap(_expr, r);
        accept(ast);
        qSwap(_expr, r);
    }
    return r.result();
}

Codegen::Result Codegen::sourceElement(SourceElement *ast)
{
    Result r(nx);
    if (ast) {
        qSwap(_expr, r);
        accept(ast);
        qSwap(_expr, r);
    }
    return r;
}

void Codegen::functionBody(FunctionBody *ast)
{
    if (ast)
        sourceElements(ast->elements);
}

void Codegen::program(Program *ast)
{
    if (ast) {
        sourceElements(ast->elements);
    }
}

void Codegen::sourceElements(SourceElements *ast)
{
    bool _requiresReturnValue = false;
    qSwap(_requiresReturnValue, requiresReturnValue);
    for (SourceElements *it = ast; it; it = it->next) {
        if (!it->next)
            qSwap(_requiresReturnValue, requiresReturnValue);
        sourceElement(it->element);
        if (hasError)
            return;
    }
}

void Codegen::statementList(StatementList *ast)
{
    bool _requiresReturnValue = requiresReturnValue;
    requiresReturnValue = false;
    for (StatementList *it = ast; it; it = it->next) {
        if (!it->next ||
            it->next->statement->kind == Statement::Kind_BreakStatement ||
            it->next->statement->kind == Statement::Kind_ContinueStatement ||
            it->next->statement->kind == Statement::Kind_ReturnStatement)
                requiresReturnValue = _requiresReturnValue;
        statement(it->statement);
        requiresReturnValue = false;
    }
    requiresReturnValue = _requiresReturnValue;
}

void Codegen::variableDeclaration(VariableDeclaration *ast)
{
    RegisterScope scope(this);

    if (!ast->expression)
        return;
    Reference rhs = expression(ast->expression);
    if (hasError)
        return;

    Reference lhs = referenceForName(ast->name.toString(), true);
    //### if lhs is a temp, this won't generate a temp-to-temp move. Same for when rhs is a const
    rhs.loadInAccumulator();
    lhs.storeConsumeAccumulator();
}

void Codegen::variableDeclarationList(VariableDeclarationList *ast)
{
    for (VariableDeclarationList *it = ast; it; it = it->next) {
        variableDeclaration(it->declaration);
    }
}


bool Codegen::visit(ArgumentList *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(CaseBlock *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(CaseClause *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(CaseClauses *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(Catch *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(DefaultClause *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(ElementList *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(Elision *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(Finally *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(FormalParameterList *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(FunctionBody *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(Program *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(PropertyAssignmentList *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(PropertyNameAndValue *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(PropertyGetterSetter *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(SourceElements *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(StatementList *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(UiArrayMemberList *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(UiImport *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(UiHeaderItemList *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(UiPragma *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(UiObjectInitializer *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(UiObjectMemberList *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(UiParameterList *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(UiProgram *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(UiQualifiedId *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(UiQualifiedPragmaId *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(VariableDeclaration *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(VariableDeclarationList *)
{
    Q_UNREACHABLE();
    return false;
}

bool Codegen::visit(Expression *ast)
{
    if (hasError)
        return false;

    statement(ast->left);
    accept(ast->right);
    return false;
}

bool Codegen::visit(ArrayLiteral *ast)
{
    if (hasError)
        return false;

    RegisterScope scope(this);

    int argc = 0;
    int args = -1;
    auto push = [this, &argc, &args](AST::ExpressionNode *arg) {
        int temp = bytecodeGenerator->newRegister();
        if (args == -1)
            args = temp;
        if (!arg) {
            auto c = Reference::fromConst(this, Primitive::emptyValue().asReturnedValue());
            (void) c.storeOnStack(temp);
        } else {
            RegisterScope scope(this);
            (void) expression(arg).storeOnStack(temp);
        }
        ++argc;
    };

    for (ElementList *it = ast->elements; it; it = it->next) {

        for (Elision *elision = it->elision; elision; elision = elision->next)
            push(0);

        push(it->expression);
        if (hasError)
            return false;
    }
    for (Elision *elision = ast->elision; elision; elision = elision->next)
        push(0);

    if (args == -1) {
        Q_ASSERT(argc == 0);
        args = 0;
    }

    Instruction::DefineArray call;
    call.argc = argc;
    call.args = Moth::StackSlot::createRegister(args);
    bytecodeGenerator->addInstruction(call);
    _expr.setResult(Reference::fromAccumulator(this));

    return false;
}

bool Codegen::visit(ArrayMemberExpression *ast)
{
    if (hasError)
        return false;

    Reference base = expression(ast->base);
    if (hasError)
        return false;
    base = base.storeOnStack();
    if (AST::StringLiteral *str = AST::cast<AST::StringLiteral *>(ast->expression)) {
        QString s = str->value.toString();
        uint arrayIndex = QV4::String::toArrayIndex(s);
        if (arrayIndex == UINT_MAX) {
            _expr.setResult(Reference::fromMember(base, str->value.toString()));
            return false;
        }
        Reference index = Reference::fromConst(this, QV4::Encode(arrayIndex));
        _expr.setResult(Reference::fromSubscript(base, index));
        return false;
    }
    Reference index = expression(ast->expression);
    _expr.setResult(Reference::fromSubscript(base, index));
    return false;
}

static QSOperator::Op baseOp(int op)
{
    switch ((QSOperator::Op) op) {
    case QSOperator::InplaceAnd: return QSOperator::BitAnd;
    case QSOperator::InplaceSub: return QSOperator::Sub;
    case QSOperator::InplaceDiv: return QSOperator::Div;
    case QSOperator::InplaceAdd: return QSOperator::Add;
    case QSOperator::InplaceLeftShift: return QSOperator::LShift;
    case QSOperator::InplaceMod: return QSOperator::Mod;
    case QSOperator::InplaceMul: return QSOperator::Mul;
    case QSOperator::InplaceOr: return QSOperator::BitOr;
    case QSOperator::InplaceRightShift: return QSOperator::RShift;
    case QSOperator::InplaceURightShift: return QSOperator::URShift;
    case QSOperator::InplaceXor: return QSOperator::BitXor;
    default: return QSOperator::Invalid;
    }
}

bool Codegen::visit(BinaryExpression *ast)
{
    if (hasError)
        return false;

    if (ast->op == QSOperator::And) {
        if (_expr.accept(cx)) {
            auto iftrue = bytecodeGenerator->newLabel();
            condition(ast->left, &iftrue, _expr.iffalse(), true);
            iftrue.link();
            condition(ast->right, _expr.iftrue(), _expr.iffalse(), _expr.trueBlockFollowsCondition());
        } else {
            auto iftrue = bytecodeGenerator->newLabel();
            auto endif = bytecodeGenerator->newLabel();

            Reference left = expression(ast->left);
            if (hasError)
                return false;
            left.loadInAccumulator();

            bytecodeGenerator->setLocation(ast->operatorToken);
            bytecodeGenerator->jumpNe().link(endif);
            iftrue.link();

            Reference right = expression(ast->right);
            if (hasError)
                return false;
            right.loadInAccumulator();

            endif.link();

            _expr.setResult(Reference::fromAccumulator(this));
        }
        return false;
    } else if (ast->op == QSOperator::Or) {
        if (_expr.accept(cx)) {
            auto iffalse = bytecodeGenerator->newLabel();
            condition(ast->left, _expr.iftrue(), &iffalse, false);
            iffalse.link();
            condition(ast->right, _expr.iftrue(), _expr.iffalse(), _expr.trueBlockFollowsCondition());
        } else {
            auto iffalse = bytecodeGenerator->newLabel();
            auto endif = bytecodeGenerator->newLabel();

            Reference left = expression(ast->left);
            if (hasError)
                return false;
            left.loadInAccumulator();

            bytecodeGenerator->setLocation(ast->operatorToken);
            bytecodeGenerator->jumpEq().link(endif);
            iffalse.link();

            Reference right = expression(ast->right);
            if (hasError)
                return false;
            right.loadInAccumulator();

            endif.link();

            _expr.setResult(Reference::fromAccumulator(this));
        }
        return false;
    }

    Reference left = expression(ast->left);
    if (hasError)
        return false;

    switch (ast->op) {
    case QSOperator::Or:
    case QSOperator::And:
        Q_UNREACHABLE(); // handled separately above
        break;

    case QSOperator::Assign: {
        if (!left.isLValue()) {
            throwReferenceError(ast->operatorToken, QStringLiteral("left-hand side of assignment operator is not an lvalue"));
            return false;
        }
        left = left.asLValue();
        if (throwSyntaxErrorOnEvalOrArgumentsInStrictMode(left, ast->left->lastSourceLocation()))
            return false;
        expression(ast->right).loadInAccumulator();
        if (hasError)
            return false;
        if (_expr.accept(nx))
            _expr.setResult(left.storeConsumeAccumulator());
        else
            _expr.setResult(left.storeRetainAccumulator());
        break;
    }

    case QSOperator::InplaceAnd:
    case QSOperator::InplaceSub:
    case QSOperator::InplaceDiv:
    case QSOperator::InplaceAdd:
    case QSOperator::InplaceLeftShift:
    case QSOperator::InplaceMod:
    case QSOperator::InplaceMul:
    case QSOperator::InplaceOr:
    case QSOperator::InplaceRightShift:
    case QSOperator::InplaceURightShift:
    case QSOperator::InplaceXor: {
        if (throwSyntaxErrorOnEvalOrArgumentsInStrictMode(left, ast->left->lastSourceLocation()))
            return false;

        if (!left.isLValue()) {
            throwSyntaxError(ast->operatorToken, QStringLiteral("left-hand side of inplace operator is not an lvalue"));
            return false;
        }
        left = left.asLValue();

        Reference tempLeft = left.storeOnStack();
        Reference right = expression(ast->right);

        if (hasError)
            return false;

        binopHelper(baseOp(ast->op), tempLeft, right).loadInAccumulator();
        _expr.setResult(left.storeRetainAccumulator());

        break;
    }

    case QSOperator::BitAnd:
    case QSOperator::BitOr:
    case QSOperator::BitXor:
        if (left.isConst()) {
            Reference right = expression(ast->right);
            if (hasError)
                return false;
            _expr.setResult(binopHelper(static_cast<QSOperator::Op>(ast->op), right, left));
            break;
        }
        // intentional fall-through!
    case QSOperator::In:
    case QSOperator::InstanceOf:
    case QSOperator::Equal:
    case QSOperator::NotEqual:
    case QSOperator::Ge:
    case QSOperator::Gt:
    case QSOperator::Le:
    case QSOperator::Lt:
    case QSOperator::StrictEqual:
    case QSOperator::StrictNotEqual:
    case QSOperator::Add:
    case QSOperator::Div:
    case QSOperator::Mod:
    case QSOperator::Mul:
    case QSOperator::Sub:
    case QSOperator::LShift:
    case QSOperator::RShift:
    case QSOperator::URShift: {
        Reference right;
        if (AST::NumericLiteral *rhs = AST::cast<AST::NumericLiteral *>(ast->right)) {
            visit(rhs);
            right = _expr.result();
        } else {
            left = left.storeOnStack(); // force any loads of the lhs, so the rhs won't clobber it
            right = expression(ast->right);
        }
        if (hasError)
            return false;

        _expr.setResult(binopHelper(static_cast<QSOperator::Op>(ast->op), left, right));

        break;
    }

    } // switch

    return false;
}

Codegen::Reference Codegen::binopHelper(QSOperator::Op oper, Reference &left, Reference &right)
{
    switch (oper) {
    case QSOperator::Add: {
        left = left.storeOnStack();
        right.loadInAccumulator();
        Instruction::Add add;
        add.lhs = left.stackSlot();
        bytecodeGenerator->addInstruction(add);
        break;
    }
    case QSOperator::Sub: {
        left = left.storeOnStack();
        right.loadInAccumulator();
        Instruction::Sub sub;
        sub.lhs = left.stackSlot();
        bytecodeGenerator->addInstruction(sub);
        break;
    }
    case QSOperator::Mul: {
        left = left.storeOnStack();
        right.loadInAccumulator();
        Instruction::Mul mul;
        mul.lhs = left.stackSlot();
        bytecodeGenerator->addInstruction(mul);
        break;
    }
    case QSOperator::BitAnd:
        if (right.isConst()) {
            left.loadInAccumulator();
            Instruction::BitAndConst bitAnd;
            bitAnd.rhs = Primitive::fromReturnedValue(right.constant).toInt32();
            bytecodeGenerator->addInstruction(bitAnd);
        } else {
            right.loadInAccumulator();
            Instruction::BitAnd bitAnd;
            bitAnd.lhs = left.stackSlot();
            bytecodeGenerator->addInstruction(bitAnd);
        }
        break;
    case QSOperator::BitOr:
        if (right.isConst()) {
            left.loadInAccumulator();
            Instruction::BitOrConst bitOr;
            bitOr.rhs = Primitive::fromReturnedValue(right.constant).toInt32();
            bytecodeGenerator->addInstruction(bitOr);
        } else {
            right.loadInAccumulator();
            Instruction::BitOr bitOr;
            bitOr.lhs = left.stackSlot();
            bytecodeGenerator->addInstruction(bitOr);
        }
        break;
    case QSOperator::BitXor:
        if (right.isConst()) {
            left.loadInAccumulator();
            Instruction::BitXorConst bitXor;
            bitXor.rhs = Primitive::fromReturnedValue(right.constant).toInt32();
            bytecodeGenerator->addInstruction(bitXor);
        } else {
            right.loadInAccumulator();
            Instruction::BitXor bitXor;
            bitXor.lhs = left.stackSlot();
            bytecodeGenerator->addInstruction(bitXor);
        }
        break;
    case QSOperator::RShift:
        if (right.isConst()) {
            left.loadInAccumulator();
            Instruction::ShrConst shr;
            shr.rhs = Primitive::fromReturnedValue(right.constant).toInt32() & 0x1f;
            bytecodeGenerator->addInstruction(shr);
        } else {
            right.loadInAccumulator();
            Instruction::Shr shr;
            shr.lhs = left.stackSlot();
            bytecodeGenerator->addInstruction(shr);
        }
        break;
    case QSOperator::LShift:
        if (right.isConst()) {
            left.loadInAccumulator();
            Instruction::ShlConst shl;
            shl.rhs = Primitive::fromReturnedValue(right.constant).toInt32() & 0x1f;
            bytecodeGenerator->addInstruction(shl);
        } else {
            right.loadInAccumulator();
            Instruction::Shl shl;
            shl.lhs = left.stackSlot();
            bytecodeGenerator->addInstruction(shl);
        }
        break;
    case QSOperator::InstanceOf:
    case QSOperator::In: {
        Instruction::BinopContext binop;
        if (oper == QSOperator::InstanceOf)
            binop.alu = QV4::Runtime::instanceof;
        else
            binop.alu = QV4::Runtime::in;
        Q_ASSERT(binop.alu != QV4::Runtime::InvalidRuntimeMethod);
        left = left.storeOnStack();
        right.loadInAccumulator();
        binop.lhs = left.stackSlot();
        bytecodeGenerator->addInstruction(binop);
        break;
    }
    case QSOperator::Equal:
    case QSOperator::NotEqual:
    case QSOperator::Gt:
    case QSOperator::Ge:
    case QSOperator::Lt:
    case QSOperator::Le:
        if (_expr.accept(cx))
            return jumpBinop(oper, left, right);
        // else: fallthrough
    default: {
        auto binopFunc = aluOpFunction(oper);
        Q_ASSERT(binopFunc != QV4::Runtime::InvalidRuntimeMethod);
        left = left.storeOnStack();
        right.loadInAccumulator();
        Instruction::Binop binop;
        binop.alu = binopFunc;
        binop.lhs = left.stackSlot();
        bytecodeGenerator->addInstruction(binop);
        break;
    }
    }

    return Reference::fromAccumulator(this);
}

static QSOperator::Op invert(QSOperator::Op oper)
{
    switch (oper) {
    case QSOperator::Equal: return QSOperator::NotEqual;
    case QSOperator::NotEqual: return QSOperator::Equal;
    case QSOperator::Gt: return QSOperator::Le;
    case QSOperator::Ge: return QSOperator::Lt;
    case QSOperator::Lt: return QSOperator::Ge;
    case QSOperator::Le: return QSOperator::Gt;
    default: Q_UNIMPLEMENTED(); return QSOperator::Invalid;
    }
}

Codegen::Reference Codegen::jumpBinop(QSOperator::Op oper, Reference &left, Reference &right)
{
    left = left.storeOnStack();
    right.loadInAccumulator();
    const BytecodeGenerator::Label *jumpTarget = _expr.iftrue();
    if (_expr.trueBlockFollowsCondition()) {
        oper = invert(oper);
        jumpTarget = _expr.iffalse();
    }

    switch (oper) {
    case QSOperator::Equal: {
        Instruction::CmpJmpEq cjump;
        cjump.lhs = left.stackSlot();
        bytecodeGenerator->addJumpInstruction(cjump).link(*jumpTarget);
        break;
    }
    case QSOperator::NotEqual: {
        Instruction::CmpJmpNe cjump;
        cjump.lhs = left.stackSlot();
        bytecodeGenerator->addJumpInstruction(cjump).link(*jumpTarget);
        break;
    }
    case QSOperator::Gt: {
        Instruction::CmpJmpGt cjump;
        cjump.lhs = left.stackSlot();
        bytecodeGenerator->addJumpInstruction(cjump).link(*jumpTarget);
        break;
    }
    case QSOperator::Ge: {
        Instruction::CmpJmpGe cjump;
        cjump.lhs = left.stackSlot();
        bytecodeGenerator->addJumpInstruction(cjump).link(*jumpTarget);
        break;
    }
    case QSOperator::Lt: {
        Instruction::CmpJmpLt cjump;
        cjump.lhs = left.stackSlot();
        bytecodeGenerator->addJumpInstruction(cjump).link(*jumpTarget);
        break;
    }
    case QSOperator::Le: {
        Instruction::CmpJmpLe cjump;
        cjump.lhs = left.stackSlot();
        bytecodeGenerator->addJumpInstruction(cjump).link(*jumpTarget);
        break;
    }
    default: Q_UNIMPLEMENTED(); Q_UNREACHABLE();
    }
    return Reference();
}

bool Codegen::visit(CallExpression *ast)
{
    if (hasError)
        return false;

    RegisterScope scope(this);

    Reference base = expression(ast->base);
    if (hasError)
        return false;
    switch (base.type) {
    case Reference::Member:
    case Reference::Subscript:
        base = base.asLValue();
        break;
    case Reference::Name:
        break;
    default:
        base = base.storeOnStack();
        break;
    }

    auto calldata = pushArgs(ast->arguments);
    if (hasError)
        return false;

    //### Do we really need all these call instructions? can's we load the callee in a temp?
    if (base.type == Reference::Member) {
        if (useFastLookups) {
            Instruction::CallPropertyLookup call;
            call.base = base.propertyBase.stackSlot();
            call.lookupIndex = registerGetterLookup(base.propertyNameIndex);
            call.callData = calldata;
            bytecodeGenerator->addInstruction(call);
        } else {
            Instruction::CallProperty call;
            call.base = base.propertyBase.stackSlot();
            call.name = base.propertyNameIndex;
            call.callData = calldata;
            bytecodeGenerator->addInstruction(call);
        }
    } else if (base.type == Reference::Subscript) {
        Instruction::CallElement call;
        call.base = base.elementBase;
        call.index = base.elementSubscript.stackSlot();
        call.callData = calldata;
        bytecodeGenerator->addInstruction(call);
    } else if (base.type == Reference::Name) {
        if (useFastLookups && base.global) {
            Instruction::CallGlobalLookup call;
            call.index = registerGlobalGetterLookup(base.unqualifiedNameIndex);
            call.callData = calldata;
            bytecodeGenerator->addInstruction(call);
        } else {
            Instruction::CallName call;
            call.name = base.unqualifiedNameIndex;
            call.callData = calldata;
            bytecodeGenerator->addInstruction(call);
        }
    } else {
        base.loadInAccumulator();

        Instruction::CallValue call;
        call.callData = calldata;
        bytecodeGenerator->addInstruction(call);
    }

    _expr.setResult(Reference::fromAccumulator(this));
    return false;
}

Moth::StackSlot Codegen::pushArgs(ArgumentList *args)
{
    int argc = 0;
    for (ArgumentList *it = args; it; it = it->next)
        ++argc;
    int calldata = bytecodeGenerator->newRegisterArray(argc + 2); // 2 additional values for CallData

    (void) Reference::fromConst(this, QV4::Encode(argc)).storeOnStack(calldata);
    (void) Reference::fromConst(this, QV4::Encode::undefined()).storeOnStack(calldata + 1);

    argc = 0;
    for (ArgumentList *it = args; it; it = it->next) {
        RegisterScope scope(this);
        Reference e = expression(it->expression);
        if (hasError)
            break;
        (void) e.storeOnStack(calldata + 2 + argc);
        ++argc;
    }

    return Moth::StackSlot::createRegister(calldata);
}

bool Codegen::visit(ConditionalExpression *ast)
{
    if (hasError)
        return true;

    RegisterScope scope(this);

    BytecodeGenerator::Label iftrue = bytecodeGenerator->newLabel();
    BytecodeGenerator::Label iffalse = bytecodeGenerator->newLabel();
    condition(ast->expression, &iftrue, &iffalse, true);

    iftrue.link();
    Reference ok = expression(ast->ok);
    if (hasError)
        return false;
    ok.loadInAccumulator();
    BytecodeGenerator::Jump jump_endif = bytecodeGenerator->jump();

    iffalse.link();
    Reference ko = expression(ast->ko);
    if (hasError)
        return false;
    ko.loadInAccumulator();

    jump_endif.link();
    _expr.setResult(Reference::fromAccumulator(this));

    return false;
}

bool Codegen::visit(DeleteExpression *ast)
{
    if (hasError)
        return false;

    Reference expr = expression(ast->expression);
    if (hasError)
        return false;

    switch (expr.type) {
    case Reference::StackSlot:
        if (!expr.stackSlotIsLocalOrArgument)
            break;
        // fall through
    case Reference::ScopedArgument:
    case Reference::ScopedLocal:
        // Trying to delete a function argument might throw.
        if (_context->isStrict) {
            throwSyntaxError(ast->deleteToken, QStringLiteral("Delete of an unqualified identifier in strict mode."));
            return false;
        }
        _expr.setResult(Reference::fromConst(this, QV4::Encode(false)));
        return false;
    case Reference::Name: {
        if (_context->isStrict) {
            throwSyntaxError(ast->deleteToken, QStringLiteral("Delete of an unqualified identifier in strict mode."));
            return false;
        }
        Instruction::DeleteName del;
        del.name = expr.unqualifiedNameIndex;
        bytecodeGenerator->addInstruction(del);
        _expr.setResult(Reference::fromAccumulator(this));
        return false;
    }
    case Reference::Member: {
        //### maybe add a variant where the base can be in the accumulator?
        expr = expr.asLValue();
        Instruction::DeleteMember del;
        del.base = expr.propertyBase.stackSlot();
        del.member = expr.propertyNameIndex;
        bytecodeGenerator->addInstruction(del);
        _expr.setResult(Reference::fromAccumulator(this));
        return false;
    }
    case Reference::Subscript: {
        //### maybe add a variant where the index can be in the accumulator?
        expr = expr.asLValue();
        Instruction::DeleteSubscript del;
        del.base = expr.elementBase;
        del.index = expr.elementSubscript.stackSlot();
        bytecodeGenerator->addInstruction(del);
        _expr.setResult(Reference::fromAccumulator(this));
        return false;
    }
    default:
        break;
    }
    // [[11.4.1]] Return true if it's not a reference
    _expr.setResult(Reference::fromConst(this, QV4::Encode(true)));
    return false;
}

bool Codegen::visit(FalseLiteral *)
{
    if (hasError)
        return false;

    _expr.setResult(Reference::fromConst(this, QV4::Encode(false)));
    return false;
}

bool Codegen::visit(FieldMemberExpression *ast)
{
    if (hasError)
        return false;

    Reference base = expression(ast->base);
    if (hasError)
        return false;
    _expr.setResult(Reference::fromMember(base, ast->name.toString()));
    return false;
}

bool Codegen::visit(FunctionExpression *ast)
{
    if (hasError)
        return false;

    RegisterScope scope(this);

    int function = defineFunction(ast->name.toString(), ast, ast->formals, ast->body ? ast->body->elements : 0);
    _expr.setResult(Reference::fromClosure(this, function));
    return false;
}

Codegen::Reference Codegen::referenceForName(const QString &name, bool isLhs)
{
    int scope = 0;
    Context *c = _context;

    // skip the innermost context if it's simple (as the runtime won't
    // create a context for it
    if (c->canUseSimpleCall()) {
        Context::Member m = c->findMember(name);
        if (m.type != Context::UndefinedMember) {
            Q_ASSERT((!m.canEscape));
            Reference r = Reference::fromStackSlot(this, m.index, true /*isLocal*/);
            if (name == QLatin1String("arguments") || name == QLatin1String("eval")) {
                r.isArgOrEval = true;
                if (isLhs && c->isStrict)
                    // ### add correct source location
                    throwSyntaxError(SourceLocation(), QStringLiteral("Variable name may not be eval or arguments in strict mode"));
            }
            return r;
        }
        const int argIdx = c->findArgument(name);
        if (argIdx != -1) {
            Q_ASSERT(!c->argumentsCanEscape && c->usesArgumentsObject != Context::ArgumentsObjectUsed);
            return Reference::fromArgument(this, argIdx);
        }
        c = c->parent;
    }

    while (c->parent) {
        if (c->forceLookupByName() || (c->isNamedFunctionExpression && c->name == name))
            goto loadByName;

        Context::Member m = c->findMember(name);
        if (m.type != Context::UndefinedMember) {
            Reference r = m.canEscape ? Reference::fromScopedLocal(this, m.index, scope)
                                      : Reference::fromStackSlot(this, m.index, true /*isLocal*/);
            if (name == QLatin1String("arguments") || name == QLatin1String("eval")) {
                r.isArgOrEval = true;
                if (isLhs && c->isStrict)
                    // ### add correct source location
                    throwSyntaxError(SourceLocation(), QStringLiteral("Variable name may not be eval or arguments in strict mode"));
            }
            return r;
        }
        const int argIdx = c->findArgument(name);
        if (argIdx != -1) {
            if (c->argumentsCanEscape || c->usesArgumentsObject == Context::ArgumentsObjectUsed) {
                return Reference::fromScopedArgument(this, argIdx, scope);
            } else {
                Q_ASSERT(scope == 0);
                return Reference::fromArgument(this, argIdx);
            }
        }

        if (!c->isStrict && c->hasDirectEval)
            goto loadByName;

        ++scope;
        c = c->parent;
    }

    {
        // This hook allows implementing QML lookup semantics
        Reference fallback = fallbackNameLookup(name);
        if (fallback.type != Reference::Invalid)
            return fallback;
    }

    if (!c->parent && !c->forceLookupByName() && _context->compilationMode != EvalCode && c->compilationMode != QmlBinding) {
        Reference r = Reference::fromName(this, name);
        r.global = true;
        return r;
    }

    // global context or with. Lookup by name
  loadByName:
    return Reference::fromName(this, name);
}

Codegen::Reference Codegen::fallbackNameLookup(const QString &name)
{
    Q_UNUSED(name)
    return Reference();
}

bool Codegen::visit(IdentifierExpression *ast)
{
    if (hasError)
        return false;

    _expr.setResult(referenceForName(ast->name.toString(), false));
    return false;
}

bool Codegen::visit(NestedExpression *ast)
{
    if (hasError)
        return false;

    accept(ast->expression);
    return false;
}

bool Codegen::visit(NewExpression *ast)
{
    if (hasError)
        return false;

    RegisterScope scope(this);

    Reference base = expression(ast->expression);
    if (hasError)
        return false;
    //### Maybe create a CreateValueA that takes an accumulator?
    base = base.storeOnStack();

    auto calldata = pushArgs(0);

    Instruction::CreateValue create;
    create.func = base.stackSlot();
    create.callData = calldata;
    bytecodeGenerator->addInstruction(create);
    _expr.setResult(Reference::fromAccumulator(this));
    return false;
}

bool Codegen::visit(NewMemberExpression *ast)
{
    if (hasError)
        return false;

    RegisterScope scope(this);

    Reference base = expression(ast->base);
    if (hasError)
        return false;
    base = base.storeOnStack();

    auto calldata = pushArgs(ast->arguments);
    if (hasError)
        return false;

    Instruction::CreateValue create;
    create.func = base.stackSlot();
    create.callData = calldata;
    bytecodeGenerator->addInstruction(create);
    _expr.setResult(Reference::fromAccumulator(this));
    return false;
}

bool Codegen::visit(NotExpression *ast)
{
    if (hasError)
        return false;

    _expr.setResult(unop(Not, expression(ast->expression)));
    return false;
}

bool Codegen::visit(NullExpression *)
{
    if (hasError)
        return false;

    if (_expr.accept(cx))
        bytecodeGenerator->jump().link(*_expr.iffalse());
    else
        _expr.setResult(Reference::fromConst(this, Encode::null()));

    return false;
}

bool Codegen::visit(NumericLiteral *ast)
{
    if (hasError)
        return false;

    _expr.setResult(Reference::fromConst(this, QV4::Encode::smallestNumber(ast->value)));
    return false;
}

bool Codegen::visit(ObjectLiteral *ast)
{
    if (hasError)
        return false;

    QMap<QString, ObjectPropertyValue> valueMap;

    RegisterScope scope(this);

    for (PropertyAssignmentList *it = ast->properties; it; it = it->next) {
        QString name = it->assignment->name->asString();
        if (PropertyNameAndValue *nv = AST::cast<AST::PropertyNameAndValue *>(it->assignment)) {
            Reference value = expression(nv->value);
            if (hasError)
                return false;

            ObjectPropertyValue &v = valueMap[name];
            if (v.hasGetter() || v.hasSetter() || (_context->isStrict && v.rvalue.isValid())) {
                throwSyntaxError(nv->lastSourceLocation(),
                                 QStringLiteral("Illegal duplicate key '%1' in object literal").arg(name));
                return false;
            }

            v.rvalue = value.storeOnStack();
        } else if (PropertyGetterSetter *gs = AST::cast<AST::PropertyGetterSetter *>(it->assignment)) {
            const int function = defineFunction(name, gs, gs->formals, gs->functionBody ? gs->functionBody->elements : 0);
            ObjectPropertyValue &v = valueMap[name];
            if (v.rvalue.isValid() ||
                (gs->type == PropertyGetterSetter::Getter && v.hasGetter()) ||
                (gs->type == PropertyGetterSetter::Setter && v.hasSetter())) {
                throwSyntaxError(gs->lastSourceLocation(),
                                 QStringLiteral("Illegal duplicate key '%1' in object literal").arg(name));
                return false;
            }
            if (gs->type == PropertyGetterSetter::Getter)
                v.getter = function;
            else
                v.setter = function;
        } else {
            Q_UNREACHABLE();
        }
    }

    QVector<QString> nonArrayKey, arrayKeyWithValue, arrayKeyWithGetterSetter;
    bool needSparseArray = false; // set to true if any array index is bigger than 16

    for (QMap<QString, ObjectPropertyValue>::iterator it = valueMap.begin(), eit = valueMap.end();
            it != eit; ++it) {
        QString name = it.key();
        uint keyAsIndex = QV4::String::toArrayIndex(name);
        if (keyAsIndex != std::numeric_limits<uint>::max()) {
            it->keyAsIndex = keyAsIndex;
            if (keyAsIndex > 16)
                needSparseArray = true;
            if (it->hasSetter() || it->hasGetter())
                arrayKeyWithGetterSetter.append(name);
            else
                arrayKeyWithValue.append(name);
        } else {
            nonArrayKey.append(name);
        }
    }

    int args = -1;
    auto push = [this, &args](const Reference &arg) {
        int temp = bytecodeGenerator->newRegister();
        if (args == -1)
            args = temp;
        (void) arg.storeOnStack(temp);
    };

    auto undefined = [this](){ return Reference::fromConst(this, Encode::undefined()); };
    QVector<QV4::Compiler::JSUnitGenerator::MemberInfo> members;

    // generate the key/value pairs
    for (const QString &key : qAsConst(nonArrayKey)) {
        const ObjectPropertyValue &prop = valueMap[key];

        if (prop.hasGetter() || prop.hasSetter()) {
            Q_ASSERT(!prop.rvalue.isValid());
            push(prop.hasGetter() ? Reference::fromClosure(this, prop.getter) : undefined());
            push(prop.hasSetter() ? Reference::fromClosure(this, prop.setter) : undefined());
            members.append({ key, true });
        } else {
            Q_ASSERT(prop.rvalue.isValid());
            push(prop.rvalue);
            members.append({ key, false });
        }
    }

    // generate array entries with values
    for (const QString &key : qAsConst(arrayKeyWithValue)) {
        const ObjectPropertyValue &prop = valueMap[key];
        Q_ASSERT(!prop.hasGetter() && !prop.hasSetter());
        push(Reference::fromConst(this, Encode(prop.keyAsIndex)));
        push(prop.rvalue);
    }

    // generate array entries with both a value and a setter
    for (const QString &key : qAsConst(arrayKeyWithGetterSetter)) {
        const ObjectPropertyValue &prop = valueMap[key];
        Q_ASSERT(!prop.rvalue.isValid());
        push(Reference::fromConst(this, Encode(prop.keyAsIndex)));
        push(prop.hasGetter() ? Reference::fromClosure(this, prop.getter) : undefined());
        push(prop.hasSetter() ? Reference::fromClosure(this, prop.setter) : undefined());
    }

    int classId = jsUnitGenerator->registerJSClass(members);

    uint arrayGetterSetterCountAndFlags = arrayKeyWithGetterSetter.size();
    arrayGetterSetterCountAndFlags |= needSparseArray << 30;

    if (args == -1)
        args = 0;

    Instruction::DefineObjectLiteral call;
    call.internalClassId = classId;
    call.arrayValueCount = arrayKeyWithValue.size();
    call.arrayGetterSetterCountAndFlags = arrayGetterSetterCountAndFlags;
    call.args = Moth::StackSlot::createRegister(args);
    bytecodeGenerator->addInstruction(call);

    _expr.setResult(Reference::fromAccumulator(this));
    return false;
}

bool Codegen::visit(PostDecrementExpression *ast)
{
    if (hasError)
        return false;

    Reference expr = expression(ast->base);
    if (hasError)
        return false;
    if (!expr.isLValue()) {
        throwReferenceError(ast->base->lastSourceLocation(), QStringLiteral("Invalid left-hand side expression in postfix operation"));
        return false;
    }
    if (throwSyntaxErrorOnEvalOrArgumentsInStrictMode(expr, ast->decrementToken))
        return false;

    _expr.setResult(unop(PostDecrement, expr));

    return false;
}

bool Codegen::visit(PostIncrementExpression *ast)
{
    if (hasError)
        return false;

    Reference expr = expression(ast->base);
    if (hasError)
        return false;
    if (!expr.isLValue()) {
        throwReferenceError(ast->base->lastSourceLocation(), QStringLiteral("Invalid left-hand side expression in postfix operation"));
        return false;
    }
    if (throwSyntaxErrorOnEvalOrArgumentsInStrictMode(expr, ast->incrementToken))
        return false;

    _expr.setResult(unop(PostIncrement, expr));
    return false;
}

bool Codegen::visit(PreDecrementExpression *ast)
{    if (hasError)
        return false;

    Reference expr = expression(ast->expression);
    if (hasError)
        return false;
    if (!expr.isLValue()) {
        throwReferenceError(ast->expression->lastSourceLocation(), QStringLiteral("Prefix ++ operator applied to value that is not a reference."));
        return false;
    }

    if (throwSyntaxErrorOnEvalOrArgumentsInStrictMode(expr, ast->decrementToken))
        return false;
    _expr.setResult(unop(PreDecrement, expr));
    return false;
}

bool Codegen::visit(PreIncrementExpression *ast)
{
    if (hasError)
        return false;

    Reference expr = expression(ast->expression);
    if (hasError)
        return false;
    if (!expr.isLValue()) {
        throwReferenceError(ast->expression->lastSourceLocation(), QStringLiteral("Prefix ++ operator applied to value that is not a reference."));
        return false;
    }

    if (throwSyntaxErrorOnEvalOrArgumentsInStrictMode(expr, ast->incrementToken))
        return false;
    _expr.setResult(unop(PreIncrement, expr));
    return false;
}

bool Codegen::visit(RegExpLiteral *ast)
{
    if (hasError)
        return false;

    auto r = Reference::fromAccumulator(this);
    r.isReadonly = true;
    _expr.setResult(r);

    Instruction::LoadRegExp instr;
    instr.regExpId = jsUnitGenerator->registerRegExp(ast);
    bytecodeGenerator->addInstruction(instr);
    return false;
}

bool Codegen::visit(StringLiteral *ast)
{
    if (hasError)
        return false;

    auto r = Reference::fromAccumulator(this);
    r.isReadonly = true;
    _expr.setResult(r);

    Instruction::LoadRuntimeString instr;
    instr.stringId = registerString(ast->value.toString());
    bytecodeGenerator->addInstruction(instr);
    return false;
}

bool Codegen::visit(ThisExpression *)
{
    if (hasError)
        return false;

    _expr.setResult(Reference::fromThis(this));
    return false;
}

bool Codegen::visit(TildeExpression *ast)
{
    if (hasError)
        return false;

    _expr.setResult(unop(Compl, expression(ast->expression)));
    return false;
}

bool Codegen::visit(TrueLiteral *)
{
    if (hasError)
        return false;

    _expr.setResult(Reference::fromConst(this, QV4::Encode(true)));
    return false;
}

bool Codegen::visit(TypeOfExpression *ast)
{
    if (hasError)
        return false;

    RegisterScope scope(this);

    Reference expr = expression(ast->expression);
    if (hasError)
        return false;

    if (expr.type == Reference::Name) {
        // special handling as typeof doesn't throw here
        Instruction::TypeofName instr;
        instr.name = expr.unqualifiedNameIndex;
        bytecodeGenerator->addInstruction(instr);
    } else {
        expr.loadInAccumulator();
        Instruction::TypeofValue instr;
        bytecodeGenerator->addInstruction(instr);
    }
    _expr.setResult(Reference::fromAccumulator(this));

    return false;
}

bool Codegen::visit(UnaryMinusExpression *ast)
{
    if (hasError)
        return false;

    _expr.setResult(unop(UMinus, expression(ast->expression)));
    return false;
}

bool Codegen::visit(UnaryPlusExpression *ast)
{
    if (hasError)
        return false;

    _expr.setResult(unop(UPlus, expression(ast->expression)));
    return false;
}

bool Codegen::visit(VoidExpression *ast)
{
    if (hasError)
        return false;

    RegisterScope scope(this);

    statement(ast->expression);
    _expr.setResult(Reference::fromConst(this, Encode::undefined()));
    return false;
}

bool Codegen::visit(FunctionDeclaration * ast)
{
    if (hasError)
        return false;

    RegisterScope scope(this);

    if (_context->compilationMode == QmlBinding)
        Reference::fromName(this, ast->name.toString()).loadInAccumulator();
    _expr.accept(nx);
    return false;
}

static bool endsWithReturn(Node *node)
{
    if (!node)
        return false;
    if (AST::cast<ReturnStatement *>(node))
        return true;
    if (Program *p = AST::cast<Program *>(node))
        return endsWithReturn(p->elements);
    if (SourceElements *se = AST::cast<SourceElements *>(node)) {
        while (se->next)
            se = se->next;
        return endsWithReturn(se->element);
    }
    if (StatementSourceElement *sse = AST::cast<StatementSourceElement *>(node))
        return endsWithReturn(sse->statement);
    if (StatementList *sl = AST::cast<StatementList *>(node)) {
        while (sl->next)
            sl = sl->next;
        return endsWithReturn(sl->statement);
    }
    if (Block *b = AST::cast<Block *>(node))
        return endsWithReturn(b->statements);
    if (IfStatement *is = AST::cast<IfStatement *>(node))
        return is->ko && endsWithReturn(is->ok) && endsWithReturn(is->ko);
    return false;
}

int Codegen::defineFunction(const QString &name, AST::Node *ast,
                            AST::FormalParameterList *formals,
                            AST::SourceElements *body)
{
    Q_UNUSED(formals);

    enterContext(ast);

    if (_context->functionIndex >= 0)
        // already defined
        return leaveContext();

    _context->name = name;
    _module->functions.append(_context);
    _context->functionIndex = _module->functions.count() - 1;

    _context->hasDirectEval |= _context->compilationMode == EvalCode || _module->debugMode; // Conditional breakpoints are like eval in the function
    // ### still needed?
    _context->maxNumberOfArguments = qMax(_context->maxNumberOfArguments, (int)QV4::Global::ReservedArgumentCount);

    BytecodeGenerator bytecode;
    BytecodeGenerator *savedBytecodeGenerator;
    savedBytecodeGenerator = bytecodeGenerator;
    bytecodeGenerator = &bytecode;

    // allocate the js stack frame (Context & js Function & accumulator)
    bytecodeGenerator->newRegisterArray(sizeof(JSStackFrame)/sizeof(Value));

    int returnAddress = -1;
    bool _requiresReturnValue = (_context->compilationMode == QmlBinding || _context->compilationMode == EvalCode);
    qSwap(requiresReturnValue, _requiresReturnValue);
    if (requiresReturnValue)
        returnAddress = bytecodeGenerator->newRegister();
    if (!_context->parent || _context->usesArgumentsObject == Context::ArgumentsObjectUnknown)
        _context->usesArgumentsObject = Context::ArgumentsObjectNotUsed;
    if (_context->usesArgumentsObject == Context::ArgumentsObjectUsed)
        _context->addLocalVar(QStringLiteral("arguments"), Context::VariableDeclaration, AST::VariableDeclaration::FunctionScope);

    bool allVarsEscape = _context->hasWith || _context->hasTry || _context->hasDirectEval;

    // variables in global code are properties of the global context object, not locals as with other functions.
    if (_context->compilationMode == FunctionCode || _context->compilationMode == QmlBinding) {
        for (Context::MemberMap::iterator it = _context->members.begin(), end = _context->members.end(); it != end; ++it) {
            const QString &local = it.key();
            if (allVarsEscape)
                it->canEscape = true;
            if (it->canEscape) {
                it->index = _context->locals.size();
                _context->locals.append(local);
            } else {
                it->index = bytecodeGenerator->newRegister();
            }
        }
    } else {
        for (Context::MemberMap::const_iterator it = _context->members.constBegin(), cend = _context->members.constEnd(); it != cend; ++it) {
            const QString &local = it.key();

            Instruction::DeclareVar declareVar;
            declareVar.isDeletable = false;
            declareVar.varName = registerString(local);
            bytecodeGenerator->addInstruction(declareVar);
        }
    }

    qSwap(_returnAddress, returnAddress);
    for (const Context::Member &member : qAsConst(_context->members)) {
        if (member.function) {
            const int function = defineFunction(member.function->name.toString(), member.function, member.function->formals,
                                                member.function->body ? member.function->body->elements : 0);
            Reference::fromClosure(this, function).loadInAccumulator();
            if (! _context->parent) {
                Reference::fromName(this, member.function->name.toString()).storeConsumeAccumulator();
            } else {
                Q_ASSERT(member.index >= 0);
                Reference local = member.canEscape ? Reference::fromScopedLocal(this, member.index, 0)
                                                   : Reference::fromStackSlot(this, member.index, true);
                local.storeConsumeAccumulator();
            }
        }
    }
    if (_context->usesArgumentsObject == Context::ArgumentsObjectUsed) {
        if (_context->isStrict) {
            Instruction::CreateUnmappedArgumentsObject setup;
            bytecodeGenerator->addInstruction(setup);
        } else {
            Instruction::CreateMappedArgumentsObject setup;
            bytecodeGenerator->addInstruction(setup);
        }
        referenceForName(QStringLiteral("arguments"), false).storeConsumeAccumulator();
    }
    if (_context->usesThis && !_context->isStrict) {
        // make sure we convert this to an object
        Instruction::ConvertThisToObject convert;
        bytecodeGenerator->addInstruction(convert);
    }

    beginFunctionBodyHook();

    sourceElements(body);

    if (hasError || !endsWithReturn(body)) {
        if (requiresReturnValue) {
            if (_returnAddress >= 0) {
                Instruction::LoadReg load;
                load.reg = Moth::StackSlot::createRegister(_returnAddress);
                bytecodeGenerator->addInstruction(load);
            }
        } else {
            Reference::fromConst(this, Encode::undefined()).loadInAccumulator();
        }
        bytecodeGenerator->addInstruction(Instruction::Ret());
    }

    _context->code = bytecodeGenerator->finalize();
    _context->registerCount = bytecodeGenerator->registerCount();
    static const bool showCode = qEnvironmentVariableIsSet("QV4_SHOW_BYTECODE");
    if (showCode) {
        qDebug() << "=== Bytecode for" << _context->name << "strict mode" << _context->isStrict
                 << "register count" << _context->registerCount;
        QV4::Moth::dumpBytecode(_context->code, _context->arguments.size());
        qDebug();
    }

    qSwap(_returnAddress, returnAddress);
    qSwap(requiresReturnValue, _requiresReturnValue);
    bytecodeGenerator = savedBytecodeGenerator;

    return leaveContext();
}

bool Codegen::visit(FunctionSourceElement *ast)
{
    if (hasError)
        return false;

    statement(ast->declaration);
    return false;
}

bool Codegen::visit(StatementSourceElement *ast)
{
    if (hasError)
        return false;

    statement(ast->statement);
    return false;
}

bool Codegen::visit(Block *ast)
{
    if (hasError)
        return false;

    RegisterScope scope(this);

    statementList(ast->statements);
    return false;
}

bool Codegen::visit(BreakStatement *ast)
{
    if (hasError)
        return false;

    if (!_context->controlFlow) {
        throwSyntaxError(ast->lastSourceLocation(), QStringLiteral("Break outside of loop"));
        return false;
    }

    ControlFlow::Handler h = _context->controlFlow->getHandler(ControlFlow::Break, ast->label.toString());
    if (h.type == ControlFlow::Invalid) {
        if (ast->label.isEmpty())
            throwSyntaxError(ast->lastSourceLocation(), QStringLiteral("Break outside of loop"));
        else
            throwSyntaxError(ast->lastSourceLocation(), QStringLiteral("Undefined label '%1'").arg(ast->label.toString()));
        return false;
    }

    _context->controlFlow->jumpToHandler(h);

    return false;
}

bool Codegen::visit(ContinueStatement *ast)
{
    if (hasError)
        return false;

    RegisterScope scope(this);

    if (!_context->controlFlow) {
        throwSyntaxError(ast->lastSourceLocation(), QStringLiteral("Continue outside of loop"));
        return false;
    }

    ControlFlow::Handler h = _context->controlFlow->getHandler(ControlFlow::Continue, ast->label.toString());
    if (h.type == ControlFlow::Invalid) {
        if (ast->label.isEmpty())
            throwSyntaxError(ast->lastSourceLocation(), QStringLiteral("Undefined label '%1'").arg(ast->label.toString()));
        else
            throwSyntaxError(ast->lastSourceLocation(), QStringLiteral("continue outside of loop"));
        return false;
    }

    _context->controlFlow->jumpToHandler(h);

    return false;
}

bool Codegen::visit(DebuggerStatement *)
{
    Q_UNIMPLEMENTED();
    return false;
}

bool Codegen::visit(DoWhileStatement *ast)
{
    if (hasError)
        return true;

    RegisterScope scope(this);

    BytecodeGenerator::Label body = bytecodeGenerator->label();
    BytecodeGenerator::Label cond = bytecodeGenerator->newLabel();
    BytecodeGenerator::Label end = bytecodeGenerator->newLabel();

    ControlFlowLoop flow(this, &end, &cond);

    statement(ast->statement);

    cond.link();
    condition(ast->expression, &body, &end, false);

    end.link();

    return false;
}

bool Codegen::visit(EmptyStatement *)
{
    if (hasError)
        return true;

    return false;
}

bool Codegen::visit(ExpressionStatement *ast)
{
    if (hasError)
        return true;

    RegisterScope scope(this);

    if (requiresReturnValue) {
        Reference e = expression(ast->expression);
        if (hasError)
            return false;
        (void) e.storeOnStack(_returnAddress);
    } else {
        statement(ast->expression);
    }
    return false;
}

bool Codegen::visit(ForEachStatement *ast)
{
    if (hasError)
        return true;

    RegisterScope scope(this);

    Reference obj = Reference::fromStackSlot(this);
    Reference expr = expression(ast->expression);
    if (hasError)
        return true;

    expr.loadInAccumulator();
    Instruction::ForeachIteratorObject iteratorObjInstr;
    bytecodeGenerator->addInstruction(iteratorObjInstr);
    obj.storeConsumeAccumulator();

    BytecodeGenerator::Label in = bytecodeGenerator->newLabel();
    BytecodeGenerator::Label end = bytecodeGenerator->newLabel();

    bytecodeGenerator->jump().link(in);

    ControlFlowLoop flow(this, &end, &in);

    BytecodeGenerator::Label body = bytecodeGenerator->label();

    statement(ast->statement);

    in.link();

    Reference lhs = expression(ast->initialiser);

    obj.loadInAccumulator();
    Instruction::ForeachNextPropertyName nextPropInstr;
    bytecodeGenerator->addInstruction(nextPropInstr);
    lhs = lhs.storeRetainAccumulator().storeOnStack();

    Reference::fromConst(this, QV4::Encode::null()).loadInAccumulator();
    bytecodeGenerator->jumpStrictNotEqual(lhs.stackSlot()).link(body);

    end.link();

    return false;
}

bool Codegen::visit(ForStatement *ast)
{
    if (hasError)
        return true;

    RegisterScope scope(this);

    statement(ast->initialiser);

    BytecodeGenerator::Label cond = bytecodeGenerator->label();
    BytecodeGenerator::Label body = bytecodeGenerator->newLabel();
    BytecodeGenerator::Label step = bytecodeGenerator->newLabel();
    BytecodeGenerator::Label end = bytecodeGenerator->newLabel();

    ControlFlowLoop flow(this, &end, &step);

    condition(ast->condition, &body, &end, true);

    body.link();
    statement(ast->statement);

    step.link();
    statement(ast->expression);
    bytecodeGenerator->jump().link(cond);

    end.link();

    return false;
}

bool Codegen::visit(IfStatement *ast)
{
    if (hasError)
        return true;

    RegisterScope scope(this);

    BytecodeGenerator::Label trueLabel = bytecodeGenerator->newLabel();
    BytecodeGenerator::Label falseLabel = bytecodeGenerator->newLabel();
    condition(ast->expression, &trueLabel, &falseLabel, true);

    trueLabel.link();
    statement(ast->ok);
    if (ast->ko) {
        if (endsWithReturn(ast)) {
            falseLabel.link();
            statement(ast->ko);
        } else {
            BytecodeGenerator::Jump jump_endif = bytecodeGenerator->jump();
            falseLabel.link();
            statement(ast->ko);
            jump_endif.link();
        }
    } else {
        falseLabel.link();
    }

    return false;
}

bool Codegen::visit(LabelledStatement *ast)
{
    if (hasError)
        return true;

    RegisterScope scope(this);

    // check that no outer loop contains the label
    ControlFlow *l = _context->controlFlow;
    while (l) {
        if (l->label() == ast->label) {
            QString error = QString(QStringLiteral("Label '%1' has already been declared")).arg(ast->label.toString());
            throwSyntaxError(ast->firstSourceLocation(), error);
            return false;
        }
        l = l->parent;
    }
    _labelledStatement = ast;

    if (AST::cast<AST::SwitchStatement *>(ast->statement) ||
            AST::cast<AST::WhileStatement *>(ast->statement) ||
            AST::cast<AST::DoWhileStatement *>(ast->statement) ||
            AST::cast<AST::ForStatement *>(ast->statement) ||
            AST::cast<AST::ForEachStatement *>(ast->statement) ||
            AST::cast<AST::LocalForStatement *>(ast->statement) ||
            AST::cast<AST::LocalForEachStatement *>(ast->statement)) {
        statement(ast->statement); // labelledStatement will be associated with the ast->statement's loop.
    } else {
        BytecodeGenerator::Label breakLabel = bytecodeGenerator->newLabel();
        ControlFlowLoop flow(this, &breakLabel);
        statement(ast->statement);
        breakLabel.link();
    }

    return false;
}

bool Codegen::visit(LocalForEachStatement *ast)
{
    if (hasError)
        return true;

    RegisterScope scope(this);

    Reference obj = Reference::fromStackSlot(this);
    Reference expr = expression(ast->expression);
    if (hasError)
        return true;

    variableDeclaration(ast->declaration);

    expr.loadInAccumulator();
    Instruction::ForeachIteratorObject iteratorObjInstr;
    bytecodeGenerator->addInstruction(iteratorObjInstr);
    obj.storeConsumeAccumulator();

    BytecodeGenerator::Label in = bytecodeGenerator->newLabel();
    BytecodeGenerator::Label end = bytecodeGenerator->newLabel();

    bytecodeGenerator->jump().link(in);
    ControlFlowLoop flow(this, &end, &in);

    BytecodeGenerator::Label body = bytecodeGenerator->label();

    Reference it = referenceForName(ast->declaration->name.toString(), true);
    statement(ast->statement);

    in.link();

    obj.loadInAccumulator();
    Instruction::ForeachNextPropertyName nextPropInstr;
    bytecodeGenerator->addInstruction(nextPropInstr);
    auto lhs = it.storeRetainAccumulator().storeOnStack();

    Reference::fromConst(this, QV4::Encode::null()).loadInAccumulator();
    bytecodeGenerator->jumpStrictNotEqual(lhs.stackSlot()).link(body);

    end.link();

    return false;
}

bool Codegen::visit(LocalForStatement *ast)
{
    if (hasError)
        return true;

    RegisterScope scope(this);

    variableDeclarationList(ast->declarations);

    BytecodeGenerator::Label cond = bytecodeGenerator->label();
    BytecodeGenerator::Label body = bytecodeGenerator->newLabel();
    BytecodeGenerator::Label step = bytecodeGenerator->newLabel();
    BytecodeGenerator::Label end = bytecodeGenerator->newLabel();

    ControlFlowLoop flow(this, &end, &step);

    condition(ast->condition, &body, &end, true);

    body.link();
    statement(ast->statement);

    step.link();
    statement(ast->expression);
    bytecodeGenerator->jump().link(cond);
    end.link();

    return false;
}

bool Codegen::visit(ReturnStatement *ast)
{
    if (hasError)
        return true;

    if (_context->compilationMode != FunctionCode && _context->compilationMode != QmlBinding) {
        throwSyntaxError(ast->returnToken, QStringLiteral("Return statement outside of function"));
        return false;
    }
    Reference expr;
    if (ast->expression) {
         expr = expression(ast->expression);
        if (hasError)
            return false;
    } else {
        expr = Reference::fromConst(this, Encode::undefined());
    }

    if (_context->controlFlow && _context->controlFlow->returnRequiresUnwind()) {
        if (_returnAddress >= 0)
            (void) expr.storeOnStack(_returnAddress);
        else
            expr.loadInAccumulator();
        ControlFlow::Handler h = _context->controlFlow->getHandler(ControlFlow::Return);
        _context->controlFlow->jumpToHandler(h);
    } else {
        expr.loadInAccumulator();
        bytecodeGenerator->addInstruction(Instruction::Ret());
    }
    return false;
}

bool Codegen::visit(SwitchStatement *ast)
{
    if (hasError)
        return true;

    RegisterScope scope(this);

    if (ast->block) {
        BytecodeGenerator::Label switchEnd = bytecodeGenerator->newLabel();

        Reference lhs = expression(ast->expression);
        if (hasError)
            return false;
        lhs = lhs.storeOnStack();

        // set up labels for all clauses
        QHash<Node *, BytecodeGenerator::Label> blockMap;
        for (CaseClauses *it = ast->block->clauses; it; it = it->next)
            blockMap[it->clause] = bytecodeGenerator->newLabel();
        if (ast->block->defaultClause)
            blockMap[ast->block->defaultClause] = bytecodeGenerator->newLabel();
        for (CaseClauses *it = ast->block->moreClauses; it; it = it->next)
            blockMap[it->clause] = bytecodeGenerator->newLabel();

        // do the switch conditions
        for (CaseClauses *it = ast->block->clauses; it; it = it->next) {
            CaseClause *clause = it->clause;
            Reference rhs = expression(clause->expression);
            if (hasError)
                return false;
            rhs.loadInAccumulator();
            bytecodeGenerator->jumpStrictEqual(lhs.stackSlot()).link(blockMap.value(clause));
        }

        for (CaseClauses *it = ast->block->moreClauses; it; it = it->next) {
            CaseClause *clause = it->clause;
            Reference rhs = expression(clause->expression);
            if (hasError)
                return false;
            rhs.loadInAccumulator();
            bytecodeGenerator->jumpStrictEqual(lhs.stackSlot()).link(blockMap.value(clause));
        }

        if (DefaultClause *defaultClause = ast->block->defaultClause)
            bytecodeGenerator->jump().link(blockMap.value(defaultClause));
        else
            bytecodeGenerator->jump().link(switchEnd);

        ControlFlowLoop flow(this, &switchEnd);

        for (CaseClauses *it = ast->block->clauses; it; it = it->next) {
            CaseClause *clause = it->clause;
            blockMap[clause].link();

            statementList(clause->statements);
        }

        if (ast->block->defaultClause) {
            DefaultClause *clause = ast->block->defaultClause;
            blockMap[clause].link();

            statementList(clause->statements);
        }

        for (CaseClauses *it = ast->block->moreClauses; it; it = it->next) {
            CaseClause *clause = it->clause;
            blockMap[clause].link();

            statementList(clause->statements);
        }

        switchEnd.link();

    }

    return false;
}

bool Codegen::visit(ThrowStatement *ast)
{
    if (hasError)
        return false;

    RegisterScope scope(this);

    Reference expr = expression(ast->expression);
    if (hasError)
        return false;

    if (_context->controlFlow) {
        _context->controlFlow->handleThrow(expr);
    } else {
        expr.loadInAccumulator();
        Instruction::ThrowException instr;
        bytecodeGenerator->addInstruction(instr);
    }
    return false;
}

void Codegen::handleTryCatch(TryStatement *ast)
{
    Q_ASSERT(ast);
    if (_context->isStrict &&
        (ast->catchExpression->name == QLatin1String("eval") || ast->catchExpression->name == QLatin1String("arguments"))) {
        throwSyntaxError(ast->catchExpression->identifierToken, QStringLiteral("Catch variable name may not be eval or arguments in strict mode"));
        return;
    }

    RegisterScope scope(this);
    BytecodeGenerator::Label noException = bytecodeGenerator->newLabel();
    {
        ControlFlowCatch catchFlow(this, ast->catchExpression);
        RegisterScope scope(this);
        statement(ast->statement);
        bytecodeGenerator->jump().link(noException);
    }
    noException.link();
}

void Codegen::handleTryFinally(TryStatement *ast)
{
    RegisterScope scope(this);
    ControlFlowFinally finally(this, ast->finallyExpression);

    if (ast->catchExpression) {
        handleTryCatch(ast);
    } else {
        RegisterScope scope(this);
        statement(ast->statement);
    }
}

bool Codegen::visit(TryStatement *ast)
{
    if (hasError)
        return true;

    Q_ASSERT(_context->hasTry);

    RegisterScope scope(this);

    if (ast->finallyExpression && ast->finallyExpression->statement) {
        handleTryFinally(ast);
    } else {
        handleTryCatch(ast);
    }

    return false;
}

bool Codegen::visit(VariableStatement *ast)
{
    if (hasError)
        return true;

    variableDeclarationList(ast->declarations);
    return false;
}

bool Codegen::visit(WhileStatement *ast)
{
    if (hasError)
        return true;

    RegisterScope scope(this);

    BytecodeGenerator::Label start = bytecodeGenerator->newLabel();
    BytecodeGenerator::Label end = bytecodeGenerator->newLabel();
    BytecodeGenerator::Label cond = bytecodeGenerator->label();
    ControlFlowLoop flow(this, &end, &cond);

    condition(ast->expression, &start, &end, true);

    start.link();
    statement(ast->statement);
    bytecodeGenerator->jump().link(cond);

    end.link();
    return false;
}

bool Codegen::visit(WithStatement *ast)
{
    if (hasError)
        return true;

    RegisterScope scope(this);

    _context->hasWith = true;

    Reference src = expression(ast->expression);
    if (hasError)
        return false;
    src = src.storeOnStack(); // trigger load before we setup the exception handler, so exceptions here go to the right place
    src.loadInAccumulator();

    ControlFlowWith flow(this);

    statement(ast->statement);

    return false;
}

bool Codegen::visit(UiArrayBinding *)
{
    Q_UNIMPLEMENTED();
    return false;
}

bool Codegen::visit(UiObjectBinding *)
{
    Q_UNIMPLEMENTED();
    return false;
}

bool Codegen::visit(UiObjectDefinition *)
{
    Q_UNIMPLEMENTED();
    return false;
}

bool Codegen::visit(UiPublicMember *)
{
    Q_UNIMPLEMENTED();
    return false;
}

bool Codegen::visit(UiScriptBinding *)
{
    Q_UNIMPLEMENTED();
    return false;
}

bool Codegen::visit(UiSourceElement *)
{
    Q_UNIMPLEMENTED();
    return false;
}

bool Codegen::throwSyntaxErrorOnEvalOrArgumentsInStrictMode(const Reference &r, const SourceLocation& loc)
{
    if (!_context->isStrict)
        return false;
    bool isArgOrEval = false;
    if (r.type == Reference::Name) {
        QString str = jsUnitGenerator->stringForIndex(r.unqualifiedNameIndex);
        if (str == QLatin1String("eval") || str == QLatin1String("arguments")) {
            isArgOrEval = true;
        }
    } else if (r.type == Reference::ScopedLocal || r.isRegister()) {
        isArgOrEval = r.isArgOrEval;
    }
    if (isArgOrEval)
        throwSyntaxError(loc, QStringLiteral("Variable name may not be eval or arguments in strict mode"));
    return isArgOrEval;
}

void Codegen::throwSyntaxError(const SourceLocation &loc, const QString &detail)
{
    if (hasError)
        return;

    hasError = true;
    QQmlJS::DiagnosticMessage error;
    error.message = detail;
    error.loc = loc;
    _errors << error;
}

void Codegen::throwReferenceError(const SourceLocation &loc, const QString &detail)
{
    if (hasError)
        return;

    hasError = true;
    QQmlJS::DiagnosticMessage error;
    error.message = detail;
    error.loc = loc;
    _errors << error;
}

QList<QQmlJS::DiagnosticMessage> Codegen::errors() const
{
    return _errors;
}

QQmlRefPointer<CompiledData::CompilationUnit> Codegen::generateCompilationUnit(bool generateUnitData)
{
    Moth::CompilationUnit *compilationUnit = new Moth::CompilationUnit;
    compilationUnit->codeRefs.resize(_module->functions.size());
    int i = 0;
    for (Context *irFunction : qAsConst(_module->functions))
        compilationUnit->codeRefs[i++] = irFunction->code;

    if (generateUnitData)
        compilationUnit->data = jsUnitGenerator->generateUnit();

    QQmlRefPointer<CompiledData::CompilationUnit> unit;
    unit.adopt(compilationUnit);
    return unit;
}

QQmlRefPointer<CompiledData::CompilationUnit> Codegen::createUnitForLoading()
{
    QQmlRefPointer<CompiledData::CompilationUnit> result;
    result.adopt(new Moth::CompilationUnit);
    return result;
}


#ifndef V4_BOOTSTRAP

QList<QQmlError> Codegen::qmlErrors() const
{
    QList<QQmlError> qmlErrors;

    // Short circuit to avoid costly (de)heap allocation of QUrl if there are no errors.
    if (_errors.size() == 0)
        return qmlErrors;

    qmlErrors.reserve(_errors.size());

    QUrl url(_fileNameIsUrl ? QUrl(_module->fileName) : QUrl::fromLocalFile(_module->fileName));
    for (const QQmlJS::DiagnosticMessage &msg: qAsConst(_errors)) {
        QQmlError e;
        e.setUrl(url);
        e.setLine(msg.loc.startLine);
        e.setColumn(msg.loc.startColumn);
        e.setDescription(msg.message);
        qmlErrors << e;
    }

    return qmlErrors;
}

#endif // V4_BOOTSTRAP

bool Codegen::RValue::operator==(const RValue &other) const
{
    switch (type) {
    case Accumulator:
        return other.isAccumulator();
    case StackSlot:
        return other.isStackSlot() && theStackSlot == other.theStackSlot;
    case Const:
        return other.isConst() && constant == other.constant;
    default:
        return false;
    }
}

Codegen::RValue Codegen::RValue::storeOnStack() const
{
    switch (type) {
    case Accumulator:
        return RValue::fromStackSlot(codegen, Reference::fromAccumulator(codegen).storeOnStack().stackSlot());
    case StackSlot:
        return *this;
    case Const:
        return RValue::fromStackSlot(codegen, Reference::storeConstOnStack(codegen, constant).stackSlot());
    default:
        Q_UNREACHABLE();
    }
}

Codegen::Reference::Reference(const Codegen::Reference &other)
{
    *this = other;
}

Codegen::Reference &Codegen::Reference::operator =(const Reference &other)
{
    type = other.type;

    switch (type) {
    case Invalid:
    case Accumulator:
        break;
    case StackSlot:
        theStackSlot = other.theStackSlot;
        break;
    case ScopedLocal:
    case ScopedArgument:
        index = other.index;
        scope = other.scope;
        break;
    case Name:
        unqualifiedNameIndex = other.unqualifiedNameIndex;
        break;
    case Member:
        propertyBase = other.propertyBase;
        propertyNameIndex = other.propertyNameIndex;
        break;
    case Subscript:
        elementBase = other.elementBase;
        elementSubscript = other.elementSubscript;
        break;
    case Const:
        constant = other.constant;
        break;
    case Closure:
        closureId = other.closureId;
        break;
    case QmlScopeObject:
    case QmlContextObject:
        qmlBase = other.qmlBase;
        qmlCoreIndex = other.qmlCoreIndex;
        qmlNotifyIndex = other.qmlNotifyIndex;
        captureRequired = other.captureRequired;
        break;
    case This:
        break;
    }

    // keep loaded reference
    isArgOrEval = other.isArgOrEval;
    codegen = other.codegen;
    isReadonly = other.isReadonly;
    stackSlotIsLocalOrArgument = other.stackSlotIsLocalOrArgument;
    global = other.global;
    return *this;
}

bool Codegen::Reference::operator==(const Codegen::Reference &other) const
{
    if (type != other.type)
        return false;
    switch (type) {
    case Invalid:
    case Accumulator:
        break;
    case StackSlot:
        return theStackSlot == other.theStackSlot;
    case ScopedLocal:
    case ScopedArgument:
        return index == other.index && scope == other.scope;
    case Name:
        return unqualifiedNameIndex == other.unqualifiedNameIndex;
    case Member:
        return propertyBase == other.propertyBase && propertyNameIndex == other.propertyNameIndex;
    case Subscript:
        return elementBase == other.elementBase && elementSubscript == other.elementSubscript;
    case Const:
        return constant == other.constant;
    case Closure:
        return closureId == other.closureId;
    case QmlScopeObject:
    case QmlContextObject:
        return qmlCoreIndex == other.qmlCoreIndex && qmlNotifyIndex == other.qmlNotifyIndex
                && captureRequired == other.captureRequired;
    case This:
        return true;
    }
    return true;
}

Codegen::RValue Codegen::Reference::asRValue() const
{
    switch (type) {
    case Invalid:
        Q_UNREACHABLE();
    case Accumulator:
        return RValue::fromAccumulator(codegen);
    case StackSlot:
        return RValue::fromStackSlot(codegen, stackSlot());
    case Const:
        return RValue::fromConst(codegen, constant);
    default:
        loadInAccumulator();
        return RValue::fromAccumulator(codegen);
    }
}

Codegen::Reference Codegen::Reference::asLValue() const
{
    switch (type) {
    case Invalid:
    case Accumulator:
        Q_UNREACHABLE();
    case Member:
        if (!propertyBase.isStackSlot()) {
            Reference r = *this;
            r.propertyBase = propertyBase.storeOnStack();
            return r;
        }
        return *this;
    case Subscript:
        if (!elementSubscript.isStackSlot()) {
            Reference r = *this;
            r.elementSubscript = elementSubscript.storeOnStack();
            return r;
        }
        return *this;
    default:
        return *this;
    }
}

Codegen::Reference Codegen::Reference::storeConsumeAccumulator() const
{
    storeAccumulator(); // it doesn't matter what happens here, just do it.
    return Reference();
}

Codegen::Reference Codegen::Reference::storeOnStack(int slotIndex) const
{
    if (isStackSlot() && slotIndex == -1)
        return *this;

    if (isStackSlot()) { // temp-to-temp move
        Reference dest = Reference::fromStackSlot(codegen, slotIndex);
        Instruction::MoveReg move;
        move.srcReg = stackSlot();
        move.destReg = dest.stackSlot();
        codegen->bytecodeGenerator->addInstruction(move);
        return dest;
    }

    Reference slot = Reference::fromStackSlot(codegen, slotIndex);
    if (isConst()) {
        Instruction::MoveConst move;
        move.constIndex = codegen->registerConstant(constant);
        move.destTemp = slot.stackSlot();
        codegen->bytecodeGenerator->addInstruction(move);
    } else {
        loadInAccumulator();
        slot.storeConsumeAccumulator();
    }
    return slot;
}

Codegen::Reference Codegen::Reference::storeRetainAccumulator() const
{
    if (storeWipesAccumulator()) {
        // a store will
        auto tmp = Reference::fromStackSlot(codegen);
        tmp.storeAccumulator(); // this is safe, and won't destory the accumulator
        storeAccumulator();
        return tmp;
    } else {
        // ok, this is safe, just do the store.
        storeAccumulator();
        return *this;
    }
}

bool Codegen::Reference::storeWipesAccumulator() const
{
    switch (type) {
    default:
    case Invalid:
    case This:
    case Const:
    case Accumulator:
        Q_UNREACHABLE();
        return false;
    case StackSlot:
    case ScopedLocal:
    case ScopedArgument:
        return false;
    case Name:
    case Member:
    case Subscript:
    case Closure:
    case QmlScopeObject:
    case QmlContextObject:
        return true;
    }
}

void Codegen::Reference::storeAccumulator() const
{
    switch (type) {
    case StackSlot: {
        Instruction::StoreReg store;
        store.reg = theStackSlot;
        codegen->bytecodeGenerator->addInstruction(store);
        return;
    }
    case ScopedLocal: {
        Instruction::StoreScopedLocal store;
        store.index = index;
        store.scope = scope;
        codegen->bytecodeGenerator->addInstruction(store);
        return;
    }
    case ScopedArgument: {
        Instruction::StoreScopedArgument store;
        store.index = index;
        store.scope = scope;
        codegen->bytecodeGenerator->addInstruction(store);
        return;
    }
    case Name: {
        Context *c = codegen->currentContext();
        if (c->isStrict) {
            Instruction::StoreNameStrict store;
            store.name = unqualifiedNameIndex;
            codegen->bytecodeGenerator->addInstruction(store);
        } else {
            Instruction::StoreNameSloppy store;
            store.name = unqualifiedNameIndex;
            codegen->bytecodeGenerator->addInstruction(store);
        }
    } return;
    case Member:
        if (codegen->useFastLookups) {
            Instruction::SetLookup store;
            store.base = propertyBase.stackSlot();
            store.index = codegen->registerSetterLookup(propertyNameIndex);
            codegen->bytecodeGenerator->addInstruction(store);
        } else {
            Instruction::StoreProperty store;
            store.base = propertyBase.stackSlot();
            store.name = propertyNameIndex;
            codegen->bytecodeGenerator->addInstruction(store);
        }
        return;
    case Subscript: {
        Instruction::StoreElement store;
        store.base = elementBase;
        store.index = elementSubscript.stackSlot();
        codegen->bytecodeGenerator->addInstruction(store);
    } return;
    case QmlScopeObject: {
        Instruction::StoreScopeObjectProperty store;
        store.base = qmlBase;
        store.propertyIndex = qmlCoreIndex;
        codegen->bytecodeGenerator->addInstruction(store);
    } return;
    case QmlContextObject: {
        Instruction::StoreContextObjectProperty store;
        store.base = qmlBase;
        store.propertyIndex = qmlCoreIndex;
        codegen->bytecodeGenerator->addInstruction(store);
    } return;
    case Invalid:
    case Accumulator:
    case Closure:
    case Const:
    case This:
        break;
    }

    Q_ASSERT(false);
    Q_UNREACHABLE();
}

void Codegen::Reference::loadInAccumulator() const
{
    switch (type) {
    case Accumulator:
        return;
    case Const: {
        Instruction::LoadConst load;
        load.index = codegen->registerConstant(constant);
        codegen->bytecodeGenerator->addInstruction(load);
    } return;
    case StackSlot: {
        Instruction::LoadReg load;
        load.reg = stackSlot();
        codegen->bytecodeGenerator->addInstruction(load);
    } return;
    case ScopedLocal: {
        Instruction::LoadScopedLocal load;
        load.index = index;
        load.scope = scope;
        codegen->bytecodeGenerator->addInstruction(load);
        return;
    }
    case ScopedArgument: {
        Instruction::LoadScopedArgument load;
        load.index = index;
        load.scope = scope;
        codegen->bytecodeGenerator->addInstruction(load);
        return;
    }
    case Name:
        if (codegen->useFastLookups && global) {
            Instruction::LoadGlobalLookup load;
            load.index = codegen->registerGlobalGetterLookup(unqualifiedNameIndex);
            codegen->bytecodeGenerator->addInstruction(load);
        } else {
            Instruction::LoadName load;
            load.name = unqualifiedNameIndex;
            codegen->bytecodeGenerator->addInstruction(load);
        }
        return;
    case Member:
        if (codegen->useFastLookups) {
            if (propertyBase.isAccumulator()) {
                Instruction::GetLookupA load;
                load.index = codegen->registerGetterLookup(propertyNameIndex);
                codegen->bytecodeGenerator->addInstruction(load);
            } else {
                Instruction::GetLookup load;
                load.base = propertyBase.storeOnStack().stackSlot();
                load.index = codegen->registerGetterLookup(propertyNameIndex);
                codegen->bytecodeGenerator->addInstruction(load);
            }
        } else {
            if (propertyBase.isAccumulator()) {
                Instruction::LoadPropertyA load;
                load.name = propertyNameIndex;
                codegen->bytecodeGenerator->addInstruction(load);
            } else {
                Instruction::LoadProperty load;
                load.base = propertyBase.storeOnStack().stackSlot();
                load.name = propertyNameIndex;
                codegen->bytecodeGenerator->addInstruction(load);
            }
        }
        return;
    case Subscript: {
        if (elementSubscript.isAccumulator()) {
            Instruction::LoadElementA load;
            load.base = elementBase;
            codegen->bytecodeGenerator->addInstruction(load);
        } else if (elementSubscript.isConst()) {
            Reference::fromConst(codegen, elementSubscript.constantValue()).loadInAccumulator();
            Instruction::LoadElementA load;
            load.base = elementBase;
            codegen->bytecodeGenerator->addInstruction(load);
        } else {
            Instruction::LoadElement load;
            load.base = elementBase;
            load.index = elementSubscript.storeOnStack().stackSlot();
            codegen->bytecodeGenerator->addInstruction(load);
        }
    } return;
    case Closure: {
        Instruction::LoadClosure load;
        load.value = closureId;
        codegen->bytecodeGenerator->addInstruction(load);
    } return;
    case QmlScopeObject: {
        Instruction::LoadScopeObjectProperty load;
        load.base = qmlBase;
        load.propertyIndex = qmlCoreIndex;
        load.captureRequired = captureRequired;
        codegen->bytecodeGenerator->addInstruction(load);
        if (!captureRequired)
            codegen->_context->scopeObjectPropertyDependencies.insert(qmlCoreIndex, qmlNotifyIndex);
    } return;
    case QmlContextObject: {
        Instruction::LoadContextObjectProperty load;
        load.base = qmlBase;
        load.propertyIndex = qmlCoreIndex;
        load.captureRequired = captureRequired;
        codegen->bytecodeGenerator->addInstruction(load);
        if (!captureRequired)
            codegen->_context->contextObjectPropertyDependencies.insert(qmlCoreIndex, qmlNotifyIndex);
    } return;
    case This: {
        Context *c = codegen->currentContext();
        Instruction::LoadReg load;
        load.reg = Moth::StackSlot::createArgument(c->arguments.size(), -1);
        codegen->bytecodeGenerator->addInstruction(load);
    } return;
    case Invalid:
        break;
    }
    Q_ASSERT(false);
    Q_UNREACHABLE();
}
