//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#pragma once

///---------------------------------------------------------------------------
///
/// class IRBuilder
///
///     To generate IR from the Jn bytecodes.
///
///---------------------------------------------------------------------------

class BranchReloc
{
public:
    BranchReloc(IR::BranchInstr * instr, uint32 branchOffset, uint32 offs)
        : branchInstr(instr), branchOffset(branchOffset), offset(offs), isNotBackEdge(false)
    { }

private:
    IR::BranchInstr * branchInstr;
    uint32            offset;
    bool              isNotBackEdge;

    uint32            branchOffset;
public:
    IR::BranchInstr * GetBranchInstr()
    {
        return this->branchInstr;
    }

    uint32 GetOffset() const
    {
        return this->offset;
    }

    uint32 GetBranchOffset() const
    {
        return this->branchOffset;
    }

    bool IsNotBackEdge() const
    {
        return this->isNotBackEdge;
    }

    void SetNotBackEdge()
    {
        this->isNotBackEdge = true;
    }
};

class IRBuilder
{
    friend struct IRBuilderSwitchAdapter;

public:
    IRBuilder(Func * func)
        : m_func(func)
        , m_argsOnStack(0)
        , m_loopBodyRetIPSym(nullptr)
        , m_ldSlots(nullptr)
        , m_loopCounterSym(nullptr)
        , callTreeHasSomeProfileInfo(false)
        , finallyBlockLevel(0)
        , m_saveLoopImplicitCallFlags(nullptr)
        , handlerOffsetStack(nullptr)
        , m_switchAdapter(this)
        , m_switchBuilder(&m_switchAdapter)
        , m_stackFuncPtrSym(nullptr)
        , m_loopBodyForInEnumeratorArrayOpnd(nullptr)
        , m_paramScopeDone(false)
#if DBG
        , m_callsOnStack(0)
        , m_usedAsTemp(nullptr)
#endif
#ifdef BAILOUT_INJECTION
        , seenLdStackArgPtr(false)
        , expectApplyArg(false)
        , seenProfiledBeginSwitch(false)
#endif
#ifdef BYTECODE_BRANCH_ISLAND
        , longBranchMap(nullptr)
#endif
        , m_generatorJumpTable(GeneratorJumpTable(func, this))
    {
        auto loopCount = func->GetJITFunctionBody()->GetLoopCount();
        if (loopCount > 0) {
#if DBG
            m_saveLoopImplicitCallFlags = AnewArrayZ(func->m_alloc, IR::Opnd*, loopCount);
#else
            m_saveLoopImplicitCallFlags = AnewArray(func->m_alloc, IR::Opnd*, loopCount);
#endif
        }

        // Note: use original byte code without debugging probes, so that we don't jit BPs inserted by the user.
        func->m_workItem->InitializeReader(&m_jnReader, &m_statementReader, func->m_alloc);
    };

    ~IRBuilder()
    {
        Assert(m_func->GetJITFunctionBody()->GetLoopCount() == 0 || m_saveLoopImplicitCallFlags);
        if (m_saveLoopImplicitCallFlags)
        {
            AdeleteArray(m_func->m_alloc, m_func->GetJITFunctionBody()->GetLoopCount(), m_saveLoopImplicitCallFlags);
        }
    }

    void                Build();
    void                InsertLabels();
    IR::LabelInstr *    CreateLabel(IR::BranchInstr * branchInstr, uint& offset);

private:
    void                InsertInstr(IR::Instr *instr, IR::Instr* insertBeforeInstr);
    void                AddInstr(IR::Instr *instr, uint32 offset);
    BranchReloc *       AddBranchInstr(IR::BranchInstr *instr, uint32 offset, uint32 targetOffset);
#ifdef BYTECODE_BRANCH_ISLAND
    void                ConsumeBranchIsland();
    void                EnsureConsumeBranchIsland();
    uint                ResolveVirtualLongBranch(IR::BranchInstr * branchInstr, uint offset);
#endif
    BranchReloc *       CreateRelocRecord(IR::BranchInstr * branchInstr, uint32 offset, uint32 targetOffset);
    void                LoadNativeCodeData();
    void                BuildConstantLoads();
    void                BuildImplicitArgIns();    

#define LAYOUT_TYPE(layout) \
    void                Build##layout(Js::OpCode newOpcode, uint32 offset);
#define LAYOUT_TYPE_WMS(layout) \
    template <typename SizePolicy> void Build##layout(Js::OpCode newOpcode, uint32 offset);
#include "ByteCode/LayoutTypes.h"

    void                BuildReg1(Js::OpCode newOpcode, uint32 offset, Js::RegSlot R0);
    void                BuildReg2(Js::OpCode newOpcode, uint32 offset, Js::RegSlot R0, Js::RegSlot R1, uint32 nextOffset);
    void                BuildProfiledReg2(Js::OpCode newOpcode, uint32 offset, Js::RegSlot dstRegSlot, Js::RegSlot srcRegSlot, Js::ProfileId profileId);
    void                BuildReg3(Js::OpCode newOpcode, uint32 offset, Js::RegSlot dstRegSlot, Js::RegSlot src1RegSlot,
                            Js::RegSlot src2RegSlot, Js::ProfileId profileId);
    void                BuildIsIn(Js::OpCode newOpcode, uint32 offset, Js::RegSlot dstRegSlot, Js::RegSlot src1RegSlot, Js::RegSlot src2RegSlot, Js::ProfileId profileId);
    void                BuildReg3C(Js::OpCode newOpCode, uint32 offset, Js::RegSlot dstRegSlot, Js::RegSlot src1RegSlot,
                            Js::RegSlot src2RegSlot, Js::CacheId inlineCacheIndex);
    void                BuildReg4(Js::OpCode newOpcode, uint32 offset, Js::RegSlot dstRegSlot, Js::RegSlot src1RegSlot,
                            Js::RegSlot src2RegSlot, Js::RegSlot src3RegSlot);
    void                BuildReg2B1(Js::OpCode newOpcode, uint32 offset, Js::RegSlot dstRegSlot, Js::RegSlot srcRegSlot, byte index);
    void                BuildReg3B1(Js::OpCode newOpcode, uint32 offset, Js::RegSlot dstRegSlot, Js::RegSlot src1RegSlot,
                            Js::RegSlot src2RegSlot, uint8 index);
    void                BuildReg5(Js::OpCode newOpcode, uint32 offset, Js::RegSlot dstRegSlot, Js::RegSlot src1RegSlot,
                            Js::RegSlot src2RegSlot, Js::RegSlot src3RegSlot, Js::RegSlot src4RegSlot);
    void                BuildUnsigned1(Js::OpCode newOpcode, uint32 offset, uint32 C1);
    void                BuildReg1Unsigned1(Js::OpCode newOpcode, uint32 offset, Js::RegSlot R0, int32 C1);
    void                BuildProfiledReg1Unsigned1(Js::OpCode newOpcode, uint32 offset, Js::RegSlot R0, int32 C1, Js::ProfileId profileId);
    void                BuildReg2Int1(Js::OpCode newOpcode, uint32 offset, Js::RegSlot dstRegSlot, Js::RegSlot srcRegSlot, int32 value);
    void                BuildElementC(Js::OpCode newOpcode, uint32 offset, Js::RegSlot fieldRegSlot, Js::RegSlot regSlot,
                            Js::PropertyIdIndexType propertyIdIndex);
    void                BuildElementScopedC(Js::OpCode newOpcode, uint32 offset, Js::RegSlot regSlot,
                            Js::PropertyIdIndexType propertyIdIndex);
    void                BuildElementSlot(Js::OpCode newOpcode, uint32 offset, Js::RegSlot fieldRegSlot, Js::RegSlot regSlot,
                            int32 slotId, Js::ProfileId profileId);
    void                BuildElementSlotI1(Js::OpCode newOpcode, uint32 offset, Js::RegSlot regSlot,
                            int32 slotId, Js::ProfileId profileId);
    void                BuildElementSlotI2(Js::OpCode newOpcode, uint32 offset, Js::RegSlot regSlot,
                            int32 slotId1, int32 slotId2, Js::ProfileId profileId);
    void                BuildElementSlotI3(Js::OpCode newOpcode, uint32 offset, Js::RegSlot fieldRegSlot, Js::RegSlot regSlot,
                            int32 slotId, Js::RegSlot homeObjLocation, Js::ProfileId profileId);
    void                BuildArgIn0(uint32 offset, Js::RegSlot R0);
    void                BuildArg(Js::OpCode newOpcode, uint32 offset, Js::ArgSlot argument, Js::RegSlot srcRegSlot);
    void                BuildArgIn(uint32 offset, Js::RegSlot dstRegSlot, uint16 argument);
    void                BuildArgInRest();
    void                BuildElementP(Js::OpCode newOpcode, uint32 offset, Js::RegSlot regSlot, Js::CacheId inlineCacheIndex);
    void                BuildElementCP(Js::OpCode newOpcode, uint32 offset, Js::RegSlot instance, Js::RegSlot regSlot, Js::CacheId inlineCacheIndex);
    void                BuildProfiledElementCP(Js::OpCode newOpcode, uint32 offset, Js::RegSlot instance, Js::RegSlot regSlot, Js::CacheId inlineCacheIndex, Js::ProfileId profileId);
    void                BuildElementC2(Js::OpCode newOpcode, uint32 offset, Js::RegSlot instanceSlot, Js::RegSlot instance2Slot,
                            Js::RegSlot regSlot, Js::PropertyIdIndexType propertyIdIndex);
    void                BuildElementScopedC2(Js::OpCode newOpcode, uint32 offset, Js::RegSlot instance2Slot,
                            Js::RegSlot regSlot, Js::PropertyIdIndexType propertyIdIndex);
    void                BuildElementU(Js::OpCode newOpcode, uint32 offset, Js::RegSlot instance, Js::PropertyIdIndexType propertyIdIndex);
    void                BuildElementI(Js::OpCode newOpcode, uint32 offset, Js::RegSlot baseRegSlot, Js::RegSlot indexRegSlot,
                            Js::RegSlot regSlot, Js::ProfileId profileId);
    void                BuildElementUnsigned1(Js::OpCode newOpcode, uint32 offset, Js::RegSlot baseRegSlot, uint32 index, Js::RegSlot regSlot);
    IR::Instr *         BuildCallI_Helper(Js::OpCode newOpcode, uint32 offset, Js::RegSlot Return, Js::RegSlot Function, Js::ArgSlot ArgCount,
                            Js::ProfileId profileId, Js::CallFlags flags = Js::CallFlags_None, Js::InlineCacheIndex inlineCacheIndex = Js::Constants::NoInlineCacheIndex);
    IR::Instr *         BuildProfiledCallI(Js::OpCode opcode, uint32 offset, Js::RegSlot returnValue, Js::RegSlot function,
                            Js::ArgSlot argCount, Js::ProfileId profileId, Js::CallFlags flags = Js::CallFlags_None, Js::InlineCacheIndex inlineCacheIndex = Js::Constants::NoInlineCacheIndex);
    IR::Instr *         BuildProfiledCallIExtended(Js::OpCode opcode, uint32 offset, Js::RegSlot returnValue, Js::RegSlot function,
                            Js::ArgSlot argCount, Js::ProfileId profileId, Js::CallIExtendedOptions options, uint32 spreadAuxOffset, Js::CallFlags flags = Js::CallFlags_None);
    IR::Instr *         BuildProfiledCallIWithICIndex(Js::OpCode opcode, uint32 offset, Js::RegSlot returnValue, Js::RegSlot function,
                            Js::ArgSlot argCount, Js::ProfileId profileId, Js::InlineCacheIndex inlineCacheIndex);
    void                BuildProfiledCallIExtendedFlags(Js::OpCode opcode, uint32 offset, Js::RegSlot returnValue, Js::RegSlot function,
                            Js::ArgSlot argCount, Js::ProfileId profileId, Js::CallIExtendedOptions options, uint32 spreadAuxOffset);
    void                BuildProfiledCallIExtendedWithICIndex(Js::OpCode opcode, uint32 offset, Js::RegSlot returnValue, Js::RegSlot function,
                            Js::ArgSlot argCount, Js::ProfileId profileId, Js::CallIExtendedOptions options, uint32 spreadAuxOffset);
    void                BuildProfiledCallIExtendedFlagsWithICIndex(Js::OpCode opcode, uint32 offset, Js::RegSlot returnValue, Js::RegSlot function,
                            Js::ArgSlot argCount, Js::ProfileId profileId, Js::CallIExtendedOptions options, uint32 spreadAuxOffset);
    void                BuildProfiled2CallI(Js::OpCode opcode, uint32 offset, Js::RegSlot returnValue, Js::RegSlot function,
                            Js::ArgSlot argCount, Js::ProfileId profileId, Js::ProfileId profileId2);
    void                BuildProfiled2CallIExtended(Js::OpCode opcode, uint32 offset, Js::RegSlot returnValue, Js::RegSlot function,
                            Js::ArgSlot argCount, Js::ProfileId profileId, Js::ProfileId profileId2, Js::CallIExtendedOptions options, uint32 spreadAuxOffset);
    void                BuildLdSpreadIndices(uint32 offset, uint32 spreadAuxOffset);
    IR::Instr *         BuildCallIExtended(Js::OpCode newOpcode, uint32 offset, Js::RegSlot returnValue, Js::RegSlot function,
                            Js::ArgSlot argCount, Js::CallIExtendedOptions options, uint32 spreadAuxOffset, Js::CallFlags flags = Js::CallFlags_None);
    void                BuildCallCommon(IR::Instr *instr, StackSym *symDst, Js::ArgSlot argCount, Js::CallFlags flags = Js::CallFlags_None);
    void                BuildRegexFromPattern(Js::RegSlot dstRegSlot, uint32 patternIndex, uint32 offset);
    void                BuildClass(Js::OpCode newOpcode, uint32 offset, Js::RegSlot constructor, Js::RegSlot extends);
    void                BuildBrReg1(Js::OpCode newOpcode, uint32 offset, uint targetOffset, Js::RegSlot srcRegSlot);
    void                BuildBrReg2(Js::OpCode newOpcode, uint32 offset, uint targetOffset, Js::RegSlot src1RegSlot, Js::RegSlot src2RegSlot);
    void                BuildBrBReturn(Js::OpCode newOpcode, uint32 offset, Js::RegSlot DestRegSlot, uint32 forInLoopLevel, uint32 targetOffset);

    IR::IndirOpnd *     BuildIndirOpnd(IR::RegOpnd *baseReg, IR::RegOpnd *indexReg);
    IR::IndirOpnd *     BuildIndirOpnd(IR::RegOpnd *baseReg, uint32 offset);
#if DBG_DUMP || defined(ENABLE_IR_VIEWER)
    IR::IndirOpnd *     BuildIndirOpnd(IR::RegOpnd *baseReg, uint32 offset, const char16 *desc);
#endif
    IR::SymOpnd *       BuildFieldOpnd(Js::OpCode newOpCode, Js::RegSlot reg, Js::PropertyId propertyId, Js::PropertyIdIndexType propertyIdIndex, PropertyKind propertyKind, uint inlineCacheIndex = -1);
    PropertySym *       BuildFieldSym(Js::RegSlot reg, Js::PropertyId propertyId, Js::PropertyIdIndexType propertyIdIndex, uint inlineCacheIndex, PropertyKind propertyKind);
    SymID               BuildSrcStackSymID(Js::RegSlot regSlot);
    IR::RegOpnd *       BuildDstOpnd(Js::RegSlot dstRegSlot, IRType type = TyVar, bool isCatchObjectSym = false);
    IR::RegOpnd *       BuildSrcOpnd(Js::RegSlot srcRegSlot, IRType type = TyVar);
    IR::AddrOpnd *      BuildAuxArrayOpnd(AuxArrayValue auxArrayType, uint32 auxArrayOffset);
    IR::Opnd *          BuildAuxObjectLiteralTypeRefOpnd(int objectId);
    IR::Opnd *          BuildForInEnumeratorOpnd(uint forInLoopLevel);
    IR::RegOpnd *       EnsureLoopBodyForInEnumeratorArrayOpnd();
private:
    uint                AddStatementBoundary(uint statementIndex, uint offset);
    void                CheckBuiltIn(PropertySym * propertySym, Js::BuiltinFunction *puBuiltInIndex);
    bool                IsFloatFunctionCallsite(Js::BuiltinFunction index, size_t argc);
    IR::Instr *         BuildProfiledFieldLoad(Js::OpCode loadOp, IR::RegOpnd *dstOpnd, IR::SymOpnd *srcOpnd, Js::CacheId inlineCacheIndex, bool *pUnprofiled);
    IR::Instr *         BuildProfiledSlotLoad(Js::OpCode loadOp, IR::RegOpnd *dstOpnd, IR::SymOpnd *srcOpnd, Js::ProfileId profileId, bool *pUnprofiled);

    SymID               GetMappedTemp(Js::RegSlot reg)
    {
        AssertMsg(this->RegIsTemp(reg), "Processing non-temp reg as a temp?");
        AssertMsg(this->tempMap, "Processing non-temp reg without a temp map?");

        Js::RegSlot tempIndex = reg - this->firstTemp;
        AssertOrFailFast(tempIndex < m_func->GetJITFunctionBody()->GetTempCount());
        return this->tempMap[tempIndex];
    }

    void                SetMappedTemp(Js::RegSlot reg, SymID tempId)
    {
        AssertMsg(this->RegIsTemp(reg), "Processing non-temp reg as a temp?");
        AssertMsg(this->tempMap, "Processing non-temp reg without a temp map?");

        Js::RegSlot tempIndex = reg - this->firstTemp;
        AssertOrFailFast(tempIndex < m_func->GetJITFunctionBody()->GetTempCount());
        this->tempMap[tempIndex] = tempId;
    }

    BOOL                GetTempUsed(Js::RegSlot reg)
    {
        AssertMsg(this->RegIsTemp(reg), "Processing non-temp reg as a temp?");
        AssertMsg(this->fbvTempUsed, "Processing non-temp reg without a used BV?");

        Js::RegSlot tempIndex = reg - this->firstTemp;
        AssertOrFailFast(tempIndex < m_func->GetJITFunctionBody()->GetTempCount());
        return this->fbvTempUsed->Test(tempIndex);
    }

    void                SetTempUsed(Js::RegSlot reg, BOOL used)
    {
        AssertMsg(this->RegIsTemp(reg), "Processing non-temp reg as a temp?");
        AssertMsg(this->fbvTempUsed, "Processing non-temp reg without a used BV?");

        Js::RegSlot tempIndex = reg - this->firstTemp;
        AssertOrFailFast(tempIndex < m_func->GetJITFunctionBody()->GetTempCount());
        if (used)
        {
            this->fbvTempUsed->Set(tempIndex);
        }
        else
        {
            this->fbvTempUsed->Clear(tempIndex);
        }
    }

    BOOL                RegIsTemp(Js::RegSlot reg)
    {
        return reg >= this->firstTemp;
    }

    BOOL                RegIsConstant(Js::RegSlot reg)
    {
        return this->m_func->GetJITFunctionBody()->RegIsConstant(reg);
    }

    bool                IsParamScopeDone() const { return m_paramScopeDone; }
    void                SetParamScopeDone(bool done = true) { m_paramScopeDone = done; }

    Js::RegSlot         InnerScopeIndexToRegSlot(uint32) const;
    Js::RegSlot         GetEnvReg() const;
    Js::RegSlot         GetEnvRegForEvalCode() const;
    Js::RegSlot         GetEnvRegForInnerFrameDisplay() const;
    void                AddEnvOpndForInnerFrameDisplay(IR::Instr *instr, uint offset);
    void                EmitClosureRangeChecks();
    void                DoClosureRegCheck(Js::RegSlot reg);
    void                BuildInitCachedScope(int auxOffset, int offset);

    void                GenerateLoopBodySlotAccesses(uint offset);
    void                GenerateLoopBodyStSlots(SymID loopParamSymId, uint offset);
    IR::Instr *         GenerateLoopBodyStSlot(Js::RegSlot regSlot, uint offset = Js::Constants::NoByteCodeOffset);
    bool                IsLoopBody() const;
    bool                IsLoopBodyInTry() const;
    uint                GetLoopBodyExitInstrOffset() const;
    IR::SymOpnd *       BuildLoopBodySlotOpnd(SymID symId);
    void                EnsureLoopBodyLoadSlot(SymID symId, bool isCatchObjectSym = false);
    void                SetLoopBodyStSlot(SymID symID, bool isCatchObjectSym);
    bool                IsLoopBodyOuterOffset(uint offset) const;
    bool                IsLoopBodyReturnIPInstr(IR::Instr * instr) const;
    IR::Opnd *          InsertLoopBodyReturnIPInstr(uint targetOffset, uint offset);
    IR::Instr *         CreateLoopBodyReturnIPInstr(uint targetOffset, uint offset);
    StackSym *          EnsureStackFuncPtrSym();

    void                InsertBailOutForDebugger(uint offset, IR::BailOutKind kind, IR::Instr* insertBeforeInstr = nullptr);
    void                InsertBailOnNoProfile(uint offset);
    void                InsertBailOnNoProfile(IR::Instr *const insertBeforeInstr);
    bool                DoBailOnNoProfile();

    void                InsertIncrLoopBodyLoopCounter(IR::LabelInstr *loopTopLabelInstr);
    void                InsertInitLoopBodyLoopCounter(uint loopNum);
    void                InsertDoneLoopBodyLoopCounter(uint32 lastOffset);

    IR::RegOpnd *       InsertConvPrimStr(IR::RegOpnd * srcOpnd, uint offset, bool forcePreOpBailOutIfNeeded);
    IR::Opnd *          GetEnvironmentOperand(uint32 offset);
    bool                DoLoadInstructionArrayProfileInfo();
    bool                AllowNativeArrayProfileInfo();

#ifdef BAILOUT_INJECTION
    void InjectBailOut(uint offset);
    void CheckBailOutInjection(Js::OpCode opcode);

    bool seenLdStackArgPtr;
    bool expectApplyArg;
    bool seenProfiledBeginSwitch;
#endif
    JitArenaAllocator *    m_tempAlloc;
    JitArenaAllocator *    m_funcAlloc;
    Func *              m_func;
    IR::Instr *         m_lastInstr;
    IR::Instr **        m_offsetToInstruction;
    uint32              m_offsetToInstructionCount;
    uint32              m_functionStartOffset;
    Js::ByteCodeReader  m_jnReader;
    Js::StatementReader<Js::FunctionBody::ArenaStatementMapList> m_statementReader;
    SList<IR::Instr *> *m_argStack;
    SList<BranchReloc*> *branchRelocList;
    typedef Pair<uint, bool> handlerStackElementType;
    SList<handlerStackElementType>         *handlerOffsetStack;
    SymID *             tempMap;
    BVFixed *           fbvTempUsed;
    Js::RegSlot         firstTemp;
    IRBuilderSwitchAdapter m_switchAdapter;
    SwitchIRBuilder     m_switchBuilder;

    BVFixed *           m_ldSlots;
    BVFixed *           m_stSlots;
#if DBG
    BVFixed *           m_usedAsTemp;
#endif
    StackSym *          m_loopBodyRetIPSym;
    StackSym*           m_loopCounterSym;
    StackSym *          m_stackFuncPtrSym;
    bool                m_paramScopeDone;
    bool                callTreeHasSomeProfileInfo;
    uint                finallyBlockLevel;

    // Keep track of how many args we have on the stack whenever
    // we make a call so that the max stack used over all calls can be
    // used to estimate how much stack we should probe for at the
    // beginning of a JITted function.
#if DBG
    uint32              m_callsOnStack;
#endif
    uint32              m_argsOnStack;
    Js::PropertyId      m_loopBodyLocalsStartSlot;
    IR::Opnd**          m_saveLoopImplicitCallFlags;
    IR::RegOpnd *       m_loopBodyForInEnumeratorArrayOpnd;
#ifdef BYTECODE_BRANCH_ISLAND
    typedef JsUtil::BaseDictionary<uint32, uint32, JitArenaAllocator> LongBranchMap;
    LongBranchMap * longBranchMap;
    static IR::Instr * const VirtualLongBranchInstr;
#endif

    class GeneratorJumpTable {
        Func* const m_func;
        IRBuilder* const m_irBuilder;

        // for-in enumerators are allocated on the heap for jit'd loop body
        // and on the stack for all other cases (the interpreter frame will
        // reuses them when bailing out). But because we have the concept of
        // "bailing in" for generator, reusing enumerators allocated on the stack
        // would not work. So we have to allocate them on the generator's interpreter
        // frame instead. This operand is loaded as part of the jump table, before we
        // jump to any of the resume point.
        IR::RegOpnd* m_forInEnumeratorArrayOpnd = nullptr;

        IR::RegOpnd* m_generatorFrameOpnd = nullptr;

        // This label is used to insert any initialization code that might be needed
        // when bailing in and before jumping to any of the resume points.
        // As of now, we only need to load the operand for for-in enumerator.
        IR::LabelInstr* m_initLabel = nullptr;

        IR::RegOpnd* CreateForInEnumeratorArrayOpnd();

    public:
        GeneratorJumpTable(Func* func, IRBuilder* irBuilder);
        IR::Instr* BuildJumpTable();
        IR::LabelInstr* GetInitLabel() const;
        IR::RegOpnd* EnsureForInEnumeratorArrayOpnd();
    };

    GeneratorJumpTable m_generatorJumpTable;
};
