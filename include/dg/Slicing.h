#ifndef DG_SLICING_H_
#define DG_SLICING_H_

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>

#include <set>

#include "dg/ADT/Queue.h"
#include "dg/DependenceGraph.h"
#include "dg/legacy/Analysis.h"
#include "dg/legacy/BFS.h"
#include "dg/legacy/NodesWalk.h"

#ifdef ENABLE_CFG
#include "dg/BBlock.h"
#endif

using namespace llvm;
using namespace std;

namespace dg {

// this class will go through the nodes
// and will mark the ones that should be in the slice
template <typename NodeT>
class WalkAndMark : public legacy::NodesWalk<NodeT, dg::ADT::QueueFIFO<NodeT *>> {
    using Queue = dg::ADT::QueueFIFO<NodeT *>;

public:
    ///
    // forward_slc makes searching the dependencies
    // in forward direction instead of backward
    WalkAndMark(bool forward_slc = false)
        : legacy::NodesWalk<NodeT, Queue>(
              forward_slc ? (legacy::NODES_WALK_CD | legacy::NODES_WALK_DD |
                             legacy::NODES_WALK_USE | legacy::NODES_WALK_ID)
                          : (legacy::NODES_WALK_REV_CD | legacy::NODES_WALK_REV_DD |
                             legacy::NODES_WALK_USER | legacy::NODES_WALK_ID |
                             legacy::NODES_WALK_REV_ID)),
          forward_slice(forward_slc) {}

    uint16_t mark(const std::set<NodeT *> &start, uint32_t slice_id, LLVMPointerAnalysis *pta, uint16_t pass_id, uint16_t buff_id) {
        map<BBlock<NodeT> *, set<BBlock<NodeT> *>> lm;
        WalkData data(slice_id, this, forward_slice ? &markedBlocks : nullptr, pta, pass_id, &lm);
        allocId = buff_id;
        this->walk(start, markSlice, &data);
        return allocId;
    }

    uint16_t mark(NodeT *start, uint32_t slice_id, LLVMPointerAnalysis *pta, uint16_t pass_id, uint16_t buff_id) {
        map<BBlock<NodeT> *, set<BBlock<NodeT> *>> lm;
        WalkData data(slice_id, this, forward_slice ? &markedBlocks : nullptr, pta, pass_id, &lm);
        allocId = buff_id;
        this->walk(start, markSlice, &data);
        return allocId;
    }

    bool isForward() const { return forward_slice; }
    // returns marked blocks, but only for forward slicing atm
    const std::set<BBlock<NodeT> *> &getMarkedBlocks() { return markedBlocks; }

private:
    uint32_t allocId;
    bool forward_slice{false};
    std::set<BBlock<NodeT> *> markedBlocks;

    struct WalkData {
        WalkData(uint32_t si, WalkAndMark *wm,
                 std::set<BBlock<NodeT> *> *mb = nullptr, LLVMPointerAnalysis *pta = nullptr, uint16_t pi = -1,
                 map<BBlock<NodeT> *, set<BBlock<NodeT> *>> *lm = nullptr)
            : slice_id(si), analysis(wm)
#ifdef ENABLE_CFG
              ,
              markedBlocks(mb)
#endif
              ,
              PTA(pta), pass_id(pi), loopMap(lm) {
        }

        uint32_t slice_id;
        WalkAndMark *analysis;
#ifdef ENABLE_CFG
        std::set<BBlock<NodeT> *> *markedBlocks;
#endif
        LLVMPointerAnalysis *PTA;
        uint16_t pass_id;
        map<BBlock<NodeT> *, set<BBlock<NodeT> *>> *loopMap;
    };

    // This tries to get debug info from the instruction before which a new
    // instruction will be inserted, and if there's no debug info in that
    // instruction, tries to get the info instead from the previous instruction (if
    // any). If none of these has debug info and a DISubprogram is provided, it
    // creates a dummy debug info with the first line of the function, because IR
    // verifier requires all inlinable callsites should have debug info when both a
    // caller and callee have DISubprogram. If none of these conditions are met,
    // returns empty info.
    static DebugLoc getOrCreateDebugLoc(const Instruction *InsertBefore,
                                        DISubprogram *SP) {
        assert(InsertBefore);
        if (InsertBefore->getDebugLoc())
            return InsertBefore->getDebugLoc();
        const Instruction *Prev = InsertBefore->getPrevNode();
        if (Prev && Prev->getDebugLoc())
            return Prev->getDebugLoc();
        if (SP)
            return llvm::DILocation::get(SP->getContext(), SP->getLine(), 1, SP);
        return DebugLoc();
    }

    static void setDebugLoc(Instruction *nIns, Instruction *ins) {
        nIns->setDebugLoc(getOrCreateDebugLoc(ins, ins->getParent()->getParent()->getSubprogram()));
        // errs() << "set Debug Loc\n";
    }

    static void addTransactionStart(Instruction *brInst) {
        IRBuilder<> builder(brInst);
        Module *M = brInst->getModule();
        // add “call void @startTransaction()"
        // #include <immintrin.h> // clang -c -emit-llvm *.c -mrtm -o *.bc
        // void startTransaction() {
        //      unsigned long status;
        //      if ((status = _xbegin()) != _XBEGIN_STARTED) {
        //          fprintf(stderr, "Cannot begin transation with err code: %d\n", status);
        //          exit(status);
        //      }
        //  }

        auto st = M->getOrInsertFunction("_Z16startTransactionv",
                                         FunctionType::getVoidTy(brInst->getContext()));
        Function *stFunc = cast<Function>(st);
        auto nCI = builder.CreateCall(stFunc);
        errs() << "startTransaction added.\n";
        if (!nCI->getDebugLoc()) {
            setDebugLoc(nCI, brInst);
        }
    }

    static Instruction *getFuncRet(Function *F) {
        if (F == NULL)
            return NULL;

        // F is a pointer to a Function instance
        for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I)
            if (Instruction *Inst = dyn_cast<ReturnInst>(&*I))
                return Inst;

        return NULL;
    }

    static void preloadBB(Instruction *txStart, BasicBlock *B, set<StringRef> *funcs, IRBuilder<> builder, Function *fm) {
        assert(B && "empty block");

        for (auto iit = B->begin(); iit != B->end(); iit++) {
            if (CallInst *CI = dyn_cast<CallInst>(&*iit)) {
                if (auto func = CI->getCalledFunction()) {
                    auto name = func->getName();
                    if (!name.contains("llvm.dbg.") && funcs->insert(name).second) {
                        outs() << "'" << name << "', ";

                        vector<Value *> args1;
                        args1.push_back(builder.CreateGlobalStringPtr(name));
                        auto nCI = builder.CreateCall(fm, args1);
                        if (!nCI->getDebugLoc()) {
                            setDebugLoc(nCI, txStart);
                        }
                        for (auto bit = func->begin(); bit != func->end(); bit++) {
                            // errs() << "block code preloaded\n";
                            // Taking the address of the entry block is illegal.
                            // if (&*bit != &(func->getEntryBlock()))
                            preloadBB(txStart, &*bit, funcs, builder, fm);
                        }
                    }
                }
            }
        }
    }

    static void preloadBlockCode(Instruction *txStart, BBlock<NodeT> *BB, set<StringRef> *funcs, IRBuilder<> builder, Function *fm) {
        Instruction *Inst = dyn_cast<Instruction>(BB->getFirstNode()->getKey());
        BasicBlock *B = Inst->getParent();
        preloadBB(txStart, B, funcs, builder, fm);
    }

    static void preloadTransactionCode(BBlock<NodeT> *start, BBlock<NodeT> *end) {
        assert(start != end && "branch start and end should be different.");
        if (start->getSlice() == 777)
            return;

        set<StringRef> *funcs = start->getLastNode()->getFuncs();

        Instruction *txStart = dyn_cast<Instruction>(start->getLastNode()->getKey());

        IRBuilder<> builder(txStart);
        Module *M = txStart->getModule();
        Instruction *Inst = dyn_cast<Instruction>(start->getFirstNode()->getKey());
        BasicBlock *B = Inst->getParent();
        auto c = M->getOrInsertFunction("_Z15preloadInstAddrPc", builder.getVoidTy(), builder.getInt8PtrTy());
        Function *fm = cast<Function>(c);
        auto name = B->getParent()->getName();
        if (!name.contains("llvm.dbg.") && funcs->insert(name).second) {
            outs() << "'" << name << "', ";
            vector<Value *> args1;
            args1.push_back(builder.CreateGlobalStringPtr(name));
            auto nCI = builder.CreateCall(fm, args1);
            if (!nCI->getDebugLoc()) {
                setDebugLoc(nCI, txStart);
            }
        }

        start->setSlice(777);
        queue<BBlock<NodeT> *> que;
        set<BBlock<NodeT> *> myset;
        myset.insert(start);
        myset.insert(end);
        que.push(start);
        while (!que.empty()) {
            auto cur = que.front();
            que.pop();

            if (cur->getSlice() != 777) {
                cur->setSlice(777);
                preloadBlockCode(txStart, cur, funcs, builder, fm);

                for (NodeT *nd : cur->getNodes()) {
                    if (nd->getSlice() == 0)
                        continue;
                    Instruction *ndInst = dyn_cast<Instruction>(nd->getKey());
                    if (ndInst->getOpcode() == Instruction::Load ||
                        ndInst->getOpcode() == Instruction::Store) {
                        // handlePreloadingForSensitiveAccesses(data, PTA, nd, ndInst, slice_id, NULL, allocs,
                        // mallocs, globals);
                    }
                    // nd->setSlice(0);
                }
            }

            for (auto suc : cur->successors()) {
                if ((myset.insert(suc.target)).second) {
                    que.push(suc.target);
                }
            }
        }
    }

    static void addTransactionEnd(BBlock<NodeT> *BB, bool isBr) {
        // errs() << "start addTransactionEnd.\n";
        // getIPostDom returns immediate postDominators.
        BBlock<NodeT> *S = BB->getIPostDom();
        NodeT *first = S->getFirstNode();
        if (!isBr) {
            S = BB;
            first = S->getLastNode();
        }

        if (first == NULL) {
            errs() << "cannot find first node when adding xend\n";
            return;
        }

        Instruction *Inst = dyn_cast<Instruction>(first->getKey());
        BasicBlock::iterator it(Inst);
        while (Inst->getOpcode() == Instruction::PHI) {
            it++;
            Inst = &*it;
        }

        IRBuilder<> builder(Inst);
        Module *M = Inst->getModule();
        auto c = M->getOrInsertFunction("_Z14endTransactionv",
                                        FunctionType::getVoidTy(Inst->getContext()));
        Function *xend = cast<Function>(c);
        auto nCI = builder.CreateCall(xend);
        errs() << "xend added.\n";
        if (!nCI->getDebugLoc()) {
            setDebugLoc(nCI, Inst);
        }

        if (isBr)
            preloadTransactionCode(BB, S);
    }

    static void
    addTransactionEndForLoop(BBlock<NodeT> *preh, const set<BBlock<NodeT> *> *loop, uint32_t slice_id) {
        auto curB = preh;
        while (curB && (curB = curB->getIPostDom())) {
            if (curB == NULL || loop->count(curB) == 0) {
                break;
            }
        }
        assert(curB && "empty IPostDom");
        if (curB->getSlice() != slice_id) {
            curB->setSlice(slice_id);
            NodeT *first = curB->getFirstNode();
            Instruction *Inst = dyn_cast<Instruction>(first->getKey());
            BasicBlock::iterator it(Inst);
            while (Inst->getOpcode() == Instruction::PHI) {
                it++;
                Inst = &*it;
            }
            IRBuilder<> builder(Inst);
            Module *M = Inst->getModule();
            auto c = M->getOrInsertFunction("_Z14endTransactionv",
                                            FunctionType::getVoidTy(Inst->getContext()));
            Function *xend = cast<Function>(c);
            auto nCI = builder.CreateCall(xend);
            errs() << "xend added for loop.\n";
            if (!nCI->getDebugLoc()) {
                setDebugLoc(nCI, Inst);
            }
        }
        preloadTransactionCode(preh, curB);
    }

    template <typename IT>
    static bool checkAddressDependency(IT begin, IT end, Value *addr, uint32_t slice_id) {
        Value *val;
        Instruction *Inst;
        NodeT *n;
        // errs() << "AD check started\n";
        for (IT i = begin; i != end; ++i) {
            // errs() << "checking AD...\n";
            n = *i;
            if (n->getSlice() <= 0 || n->getSlice() == slice_id - 1) {
                // not secret dependent or pointsTo location
                continue;
            }
            val = n->getKey();
            Inst = dyn_cast<Instruction>(val);
            // check if the output of Inst matches the address operand of the current inst.
            if (Inst == addr) {
                // address dependency
                // errs() << "AD identified.\n";
                n->setSlice(slice_id);
                return true;
            }
        }
        return false;
    }

    static void
    addPreLoad(Instruction *Inst, Value *lVals[], const set<uint32_t> &allocs, const set<uint32_t> &mallocs, const set<GlobalVariable *> &globals) {
        (void)lVals;
        IRBuilder<> builder(Inst);
        /*
                for (auto lval : lVals) {
                    // pre-load locals
                    builder.CreateLoad(lval);
                }*/

        Module *M = Inst->getModule();
        auto c = M->getOrInsertFunction("_Z17iterateAllocStacki", builder.getVoidTy(),
                                        builder.getInt32Ty());
        Function *lm = cast<Function>(c);
        for (auto i : allocs) {
            // pre-load non-local allocs
            vector<Value *> args1;
            args1.push_back(builder.getInt32(i));
            // errs() << "load bid " << bid << "\n";
            auto nCI = builder.CreateCall(lm, args1);
            if (!nCI->getDebugLoc()) {
                setDebugLoc(nCI, Inst);
            }
        }

        c = M->getOrInsertFunction("_Z16iterateMallocSeti", builder.getVoidTy(), builder.getInt32Ty());
        lm = cast<Function>(c);
        for (auto i : mallocs) {
            // pre-load non-local allocs
            vector<Value *> args1;
            args1.push_back(builder.getInt32(i));
            // errs() << "load bid " << bid << "\n";
            auto nCI = builder.CreateCall(lm, args1);
            if (!nCI->getDebugLoc()) {
                setDebugLoc(nCI, Inst);
            }
        }

        c = M->getOrInsertFunction("_Z13iterateGlobaliPv", builder.getVoidTy(), builder.getInt32Ty(),
                                   builder.getInt8PtrTy());
        lm = cast<Function>(c);
        for (auto gv : globals) {
            // pre-load globals
            vector<Value *> args1;
            Type *T = gv->getValueType();
            int size = M->getDataLayout().getTypeAllocSize(T); // # Byte
            // errs() << "Global type size in bytes: " << size << "\n";
            args1.push_back(builder.getInt32(size));
            Value *pv = builder.CreateBitCast(gv, builder.getInt8PtrTy());
            args1.push_back(pv);
            auto nCI = builder.CreateCall(lm, args1);
            if (!nCI->getDebugLoc()) {
                setDebugLoc(nCI, Inst);
            }
        }
    }

    static void processHighestBr(NodeT *bn, uint32_t slice_id, Value *lVals[], const set<uint32_t> &allocs, const set<uint32_t> &mallocs, const set<GlobalVariable *> &globals) {
        Instruction *Inst = dyn_cast<Instruction>(bn->getKey());
        BasicBlock::iterator it(Inst);
        while (Inst->getOpcode() == Instruction::PHI) {
            it++;
            Inst = &*it;
        }
        BBlock<NodeT> *CD = bn->getBBlock();
        if (CD->getSlice() != slice_id && CD->getSlice() != 777) {
            if (allocs.empty() && mallocs.empty() && globals.empty()) {
                return;
            }
            CD->setSlice(slice_id);
            addTransactionStart(Inst);
            addTransactionEnd(CD, Inst->getOpcode() == Instruction::Br);
        }
        addPreLoad(Inst, lVals, allocs, mallocs, globals);
    }

    static void
    placeTransForLoop(NodeT *bn, const set<BBlock<NodeT> *> *blks, uint32_t slice_id, Value *lVals[], const set<uint32_t> &allocs, const set<uint32_t> &mallocs, const set<GlobalVariable *> &globals) {
        // Instruction *Inst = dyn_cast<Instruction>(bn->getKey());
        BBlock<NodeT> *CD = bn->getBBlock();
        // pre-header found
        auto S = CD->getIDom();
        auto node = S->getLastNode();
        Value *val = node->getKey();
        Instruction *sInst = dyn_cast<Instruction>(val);
        if (node->getSlice() != 888) {
            if (allocs.empty() && mallocs.empty() && globals.empty()) {
                return;
            }
            node->setSlice(888);
            addTransactionStart(sInst);
            addTransactionEndForLoop(S, blks, slice_id);
        }
        addPreLoad(sInst, lVals, allocs, mallocs, globals);
    }

    static bool
    processBBlockIDomsAndNodeRevCDs(BBlock<NodeT> *BB, NodeT *ND, uint32_t slice_id, Value *lVals[], const set<uint32_t> &allocs, const set<uint32_t> &mallocs, const set<GlobalVariable *> &globals, bool isLoop, const set<BBlock<NodeT> *> *blks) {
        BBlock<NodeT> *CD = NULL;
        if (BB && BB->getSlice() > 0) {
            CD = BB->getIDom();
            if (CD && CD->getSlice() > 0) {
                // find the top-level br.
                if (processBBlockIDomsAndNodeRevCDs(CD, NULL, slice_id, lVals, allocs, mallocs, globals, isLoop, blks)) {
                    return true;
                }
            }
            if (auto *inst = BB->getFirstNode()) {
                DependenceGraph<NodeT> *dg = inst->getDG();
                // errs() << *(inst->getKey()) << ":: cur inst\n";
                for (auto it = inst->rev_control_begin(); it != inst->rev_control_end(); it++) {
                    auto *node = *it;
                    // errs() << *(node->getKey()) << ":: get CD inst\n";
                    auto *func = dyn_cast<Function>(node->getKey());
                    if (!func && dg && dg->getNode(node->getKey())) {
                        // errs() << "revCD inst is in the same dg: ignore\n";
                        continue;
                    }
                    auto *bb = node->getBBlock();
                    //errs() << bb << ": get inst bb\n";
                    //errs() << "get sensitive inst bb\n";
                    if (processBBlockIDomsAndNodeRevCDs(bb, node, slice_id, lVals, allocs, mallocs, globals, isLoop, blks)) {
                        return true;
                    }
                }
            }
        }

        if (ND && ND->getSlice() > 0) {
            DependenceGraph<NodeT> *dg = ND->getDG();
            for (auto it = ND->rev_control_begin(); it != ND->rev_control_end(); it++) {
                auto *node = *it;
                // errs() << *(node->getKey()) << ":: get CD inst\n";
                auto *func = dyn_cast<Function>(node->getKey());
                if (!func && dg && dg->getNode(node->getKey())) {
                    // errs() << "revCD inst is in the same dg: ignore\n";
                    continue;
                }
                // errs() << (node->getKey()) << " " << *(node->getKey()) << ": get CD inst " << node->getSlice() << "\n";
                auto *bb = node->getBBlock();
                // if (isLoop && blks->count(bb)) {
                //     errs() << "revCD inst is in the same loop: ignore\n";
                //     continue;
                // }
                //errs() << bb << ": get inst bb\n";
                //errs() << "get sensitive inst bb\n";
                if (processBBlockIDomsAndNodeRevCDs(bb, node, slice_id, lVals, allocs, mallocs, globals, isLoop, blks)) {
                    return true;
                }
            }
        }

        if (CD && CD->getSlice() > 0) {
            if (auto *last = CD->getLastNode()) {
                if (last->getSlice() <= 0)
                    return false;
                Instruction *Inst = dyn_cast<Instruction>(last->getKey());
                // find the immediate br
                if (Inst && Inst->getOpcode() == Instruction::Br) {
                    // errs() << "br sid: " << last->getSlice() << "\n";
                    processHighestBr(last, slice_id, lVals, allocs, mallocs, globals);
                    return true;
                }
            }
        }

        return false;
    }

    static bool
    processBBlockRevCDs(bool isLoop, bool addDep, BBlock<NodeT> *BB, const set<BBlock<NodeT> *> *blks,
                        uint32_t slice_id, Value *lVals[], const set<uint32_t> &allocs, const set<uint32_t> &mallocs, const set<GlobalVariable *> &globals) {
        if (!BB)
            return false;
        //errs() << "search for CD\n";
        // errs() << "cur BB first in processBBlockRevCDs: " << *(BB->getFirstNode()->getKey()) << "\n";

        for (BBlock<NodeT> *CD : BB->revControlDependence()) {
            //errs() << "get CD\n";
            if (CD->getSlice() <= 0) {
                // not secret dependent
                continue;
            }

            if (isLoop && blks->count(CD)) {
                // errs() << "revCD blk is in the same loop: ignore\n";
                continue;
            }

            if (auto *last = CD->getLastNode()) {
                if (last->getSlice() <= 0)
                    continue;
                Instruction *Inst = dyn_cast<Instruction>(last->getKey());
                // find the immediate br
                if (Inst && Inst->getOpcode() == Instruction::Br) {
                    // dbgs() << "the immediate br found: " << *Inst << "\n";
                    // look for highest br
                    if (!processBBlockIDomsAndNodeRevCDs(CD, NULL, slice_id, lVals, allocs, mallocs, globals, isLoop, blks)) {
                        // errs() << "use the immediate br as highest\n";
                        //errs() << "1 br sid: " << last->getSlice() << "\n";
                        processHighestBr(last, slice_id, lVals, allocs, mallocs, globals);
                    }
                    // it should be true that one block only have one sensitive br
                    return true;
                }
            }
        }

        auto *inst = BB->getFirstNode();
        DependenceGraph<NodeT> *dg = inst->getDG();

        for (auto it = inst->rev_control_begin(); it != inst->rev_control_end(); it++) {
            auto *node = *it;
            // errs() << *(node->getKey()) << ":: get CD inst\n";
            auto *func = dyn_cast<Function>(node->getKey());
            if (!func && dg && dg->getNode(node->getKey())) {
                // errs() << "revCD inst is in the same dg: ignore\n";
                continue;
            }
            auto *bb = node->getBBlock();
            // if (isLoop && blks->count(bb)) {
            //     errs() << "revCD inst is in the same loop: ignore\n";
            //     continue;
            // }
            //errs() << bb << ": get inst bb\n";
            //errs() << "get sensitive inst bb\n";
            if (processBBlockIDomsAndNodeRevCDs(bb, node, slice_id, lVals, allocs, mallocs, globals, isLoop, blks)) {
                return true;
            }
        }

        // never find a immediate br: add transaction as per the current block
        if (isLoop) {
            // BB->setSlice(slice_id);
            placeTransForLoop(BB->getFirstNode(), blks, slice_id, lVals, allocs, mallocs, globals);
        } else if (addDep) {
            // errs() << "handle address dependency\n";
            // BB->setSlice(slice_id);
            processHighestBr(BB->getFirstNode(), slice_id, lVals, allocs, mallocs, globals);
        }
        return false;
    }

    inline static uint64_t getConstantValue(const Value *op) {
        uint64_t size = 0;
        if (const ConstantInt *C = dyn_cast<ConstantInt>(op)) {
            size = C->getLimitedValue();
            // if the size cannot be expressed as an uint64_t,
            // just set it to 0 (that means unknown)
            if (size == ~(static_cast<uint64_t>(0)))
                size = 0;
        }

        return size;
    }

    // static set<BBlock<NodeT>*> collectLoopBlks(BBlock<NodeT> *header, BBlock<NodeT> *hpred, WalkData *data) {
    static set<BBlock<NodeT> *> collectLoopBlks(BBlock<NodeT> *header, BBlock<NodeT> *hpred) {
        // map<BBlock<NodeT>*, set<BBlock<NodeT>*>> loopMap = *(data->loopMap);
        // if (loopMap.find(header) != loopMap.end()) return &(loopMap[header]);

        set<BBlock<NodeT> *> myset;
        stack<BBlock<NodeT> *> mystack;

        myset.insert(header);
        myset.insert(hpred);
        mystack.push(hpred);
        while (!mystack.empty()) {
            auto cur = mystack.top();
            mystack.pop();
            for (auto pred : cur->predecessors()) {
                if ((myset.insert(pred)).second) {
                    mystack.push(pred);
                }
            }
        }

        return (myset);
    }

    static set<BBlock<NodeT> *> *getLoopBlks(BBlock<NodeT> *blk, WalkData *data, BBlock<NodeT> *&header) {
        map<BBlock<NodeT> *, set<BBlock<NodeT> *>> loopMap = *(data->loopMap);

        for (auto &elem : loopMap) {
            auto loop = elem.second;
            if (loop.find(blk) != loop.end()) {
                header = elem.first;
                return &(elem.second);
            }
        }
        return NULL;
    }

    static void
    getBrLoopBlocks(set<BBlock<NodeT> *> &ret, BBlock<NodeT> *brBlk, WalkData *data, BBlock<NodeT> *&header) {
        (void)data;
        /*if (auto res = getLoopBlks(brBlk, data, header)) {
                    errs() << "block in a loop identified already\n";
                    for (auto blk : *res) {
                        ret.insert(blk);
                    }
                    return;
                }*/

        // find a backedge first in the CFG
        auto blk = brBlk;
        while (blk) {
            for (auto pred : blk->predecessors()) {

                auto cur = pred;
                while (auto idom = cur->getIDom()) {
                    if (idom == blk) {
                        // a backedge found: pred->blk
                        auto sset = collectLoopBlks(blk, pred);
                        if (sset.find(brBlk) != sset.end()) {
                            header = blk;
                            ret = sset;
                            /*for (auto blk : sset) {
                                        ret.insert(blk);
                                        errs() << "getLoop: " << blk << "\n";
                                    }*/
                            // copy(sset->begin(), sset->end(), inserter(ret, ret.begin()));
                            // errs() << "finsih copy\n";
                        }
                        return;
                    }
                    cur = idom;
                }
            }
            blk = blk->getIDom();
        }

        return;
    }

    static bool isCondExit(BBlock<NodeT> *bb, const set<BBlock<NodeT> *> *loop) {
        for (auto suc : bb->successors()) {
            if (loop->find(suc.target) == loop->end()) {
                return true;
            }
        }
        return false;
    }

    static bool
    isCondExit(BBlock<NodeT> *bb, const set<BBlock<NodeT> *> *loop, vector<BBlock<NodeT> *> *exits) {
        // assert(loop->find(bb)!=loop->end() && "wrong loop identified for a br");
        for (auto suc : bb->successors()) {
            if (loop->count(suc.target) == 0) {
                exits->push_back(suc.target);
                return true;
            }
        }
        return false;
    }

    static void
    handlePreloadingForSensitiveAccesses(WalkData *data, LLVMPointerAnalysis *PTA, NodeT *n, Instruction *Inst,
                                         uint32_t slice_id, Value *lVals[], set<uint32_t> &allocs, set<uint32_t> &mallocs, set<GlobalVariable *> &globals, unsigned opIdx) {
        (void)lVals;
        DependenceGraph<NodeT> *dg = n->getDG();
        Module *M = Inst->getModule();
        vector<int> vect;
        if (opIdx <= 1) {
            vect.push_back(opIdx);
        } else {
            vect.push_back(0);
            vect.push_back(1);
        }
        // vect.push_back(0);
        auto sec = new StringRef("secret");
        for (auto id : vect) {
            PSNode *pts = PTA->getPointsToNode(Inst->getOperand(id));
            for (const auto &ptr : pts->pointsTo) {
                Value *vl = ptr.target->getUserData<Value>();
                if (vl == NULL) {
                    // errs() << "NULL pt value at a " << (opIdx==0?"load":"store") << "\n";
                    continue;
                }
                GlobalVariable *gv;
                if ((gv = dyn_cast<GlobalVariable>(vl)) && !gv->getName().contains("ecc_sets")) {
                    if (gv->hasAttribute(*sec)) {
                        errs() << "sec as an operand of a (opIdx: " << id << ")\n";
                        return;
                    }

                    if (gv->hasName()) {
                        if (gv->getName().contains(".str.")) {
                            return;
                        }
                    }

                    globals.insert(gv);

                    if (gv->getName().contains("ecc_sets")) {
                        DILocation *loc = Inst->getDebugLoc();
                        unsigned int line = loc->getLine();
                        StringRef file = loc->getFilename();
                        // StringRef dir = loc->getDirectory();
                        errs() << *Inst << " ; file: " << file << ", line: " << line << "\n";
                    }
                }
                if (AllocaInst *AI = dyn_cast<AllocaInst>(vl)) {
                    if (NodeT *g = dg->getNode(vl)) {
                        // just mark local nodes
                        // not preload locals
                        g->setSlice(slice_id + 2);
                        continue;
                    }
                    uint32_t bid = ptr.target->getBufferId();
                    if (!ptr.target->isBuffered()) {
                        bid = ++(data->analysis->allocId);
                        ptr.target->setBufferId(bid);

                        BasicBlock::iterator iit(AI);
                        if (++iit == AI->getParent()->end()) {
                            iit--;
                            errs() << "reached end of a block for AI\n";
                        }
                        IRBuilder<> builder(&*iit);

                        auto c = M->getOrInsertFunction("_Z14pushAllocStackiliPv", builder.getVoidTy(),
                                                        builder.getInt32Ty(), builder.getInt64Ty(),
                                                        builder.getInt32Ty(),
                                                        builder.getInt8PtrTy()); //Type::getInt8PtrTy(ct)
                        Function *fm = cast<Function>(c);

                        vector<Value *> args1;
                        args1.push_back(builder.getInt32(bid));
                        Value *arraySize = AI->getArraySize();
                        Value *as = builder.CreateIntCast(arraySize, builder.getInt64Ty(), false);
                        args1.push_back(as);
                        Type *T = AI->getAllocatedType();
                        int size = M->getDataLayout().getTypeAllocSize(T); // # Byte
                        args1.push_back(builder.getInt32(size));           // elem size in bytes
                        Value *pv = builder.CreateBitCast(vl, builder.getInt8PtrTy());
                        args1.push_back(pv);

                        auto nCI = builder.CreateCall(fm, args1);
                        if (!nCI->getDebugLoc()) {
                            setDebugLoc(nCI, &*iit);
                        }
                        // we need ret of AI's func rather than n's
                        // if (Instruction *ret = getFuncRet(n->getBBlock())) {
                        if (Instruction *ret = getFuncRet(AI->getFunction())) {
                            IRBuilder<> retBld(ret);
                            auto c = M->getOrInsertFunction("_Z13popAllocStacki", builder.getVoidTy(),
                                                            builder.getInt32Ty()); //Type::getInt8PtrTy(ct)
                            Function *fm = cast<Function>(c);
                            vector<Value *> args1;
                            args1.push_back(builder.getInt32(bid));
                            auto nCI = retBld.CreateCall(fm, args1);
                            if (!nCI->getDebugLoc()) {
                                setDebugLoc(nCI, ret);
                            }
                        }
                    }
                    // assert(bid < 100 && "bid overflown");
                    allocs.insert(bid);
                    // errs() << "adding allocs bid " << bid << "\n";
                }
                if (CallInst *CI = dyn_cast<CallInst>(vl)) {
                    Function *fun = CI->getCalledFunction();
                    // if (fun) {
                    //    errs() << "got call func: " << fun->getName() << "\n";
                    // }
                    if (fun && fun->getName().equals("malloc")) {
                        if (NodeT *g = dg->getNode(vl)) {
                            // just mark local nodes
                            g->setSlice(slice_id + 2);
                        }
                        uint32_t bid = ptr.target->getBufferId();
                        if (!ptr.target->isBuffered()) {
                            bid = ++(data->analysis->allocId);
                            ptr.target->setBufferId(bid);
                            BasicBlock::iterator iit(CI);
                            if (++iit == CI->getParent()->end()) {
                                iit--;
                                errs() << "reached end of a block for CI\n";
                            }
                            IRBuilder<> builder(&*iit);
                            auto c = M->getOrInsertFunction("_Z15insertMallocSetiiPv", builder.getVoidTy(),
                                                            builder.getInt32Ty(), builder.getInt32Ty(),
                                                            builder.getInt8PtrTy()); //Type::getInt8PtrTy(ct)
                            Function *fm = cast<Function>(c);

                            vector<Value *> args1;
                            args1.push_back(builder.getInt32(bid));

                            Value *op = CI->getOperand(0);
                            int size = getConstantValue(op); // # Byte
                            // errs() << "malloc size: " << size/4 << "\n";
                            args1.push_back(builder.getInt32(size)); // bytes
                            Value *pv = builder.CreateBitCast(vl, builder.getInt8PtrTy());
                            args1.push_back(pv);
                            auto nCI = builder.CreateCall(fm, args1);
                            if (!nCI->getDebugLoc()) {
                                setDebugLoc(nCI, &*iit);
                            }
                        }
                        // assert(bid < 100 && "bid overflown");
                        mallocs.insert(bid);
                        // errs() << "adding mallocs bid " << bid << "\n";
                    }
                }
            }
        }
    }

    static bool markSlice(NodeT *n, WalkData *data) {
        uint32_t slice_id = (data->slice_id - 1) * 5 + 1;
        uint16_t pass_id = data->pass_id;

        LLVMPointerAnalysis *PTA = data->PTA;
        Instruction *Inst = dyn_cast<Instruction>(n->getKey());

        if (pass_id == 0) {
            if (n->getSlice() > 0)
                return false;
            if (Inst && (Inst->getOpcode() == Instruction::Load || Inst->getOpcode() == Instruction::Store)) {
                unsigned opIdx = Inst->getOpcode() == Instruction::Load ? 0 : 1;
                PSNode *pts = PTA->getPointsToNode(Inst->getOperand(opIdx));
                auto sec = new StringRef("secret");
                for (const auto &ptr : pts->pointsTo) {
                    Value *vl = ptr.target->getUserData<Value>();
                    if (vl == NULL) {
                        continue;
                    }

                    if (GlobalVariable *gv = dyn_cast<GlobalVariable>(vl)) {
                        if (gv->hasAttribute(*sec)) {
                            errs() << "sec as an operand of a " << (opIdx == 0 ? "load" : "store") << "\n";
                            return false;
                        }
                    }
                }
            } else if (Inst && Inst->getOpcode() == Instruction::Br) {
                BBlock<NodeT> *B = n->getBBlock();
                BBlock<NodeT> *header;
                set<BBlock<NodeT> *> blks;
                getBrLoopBlocks(blks, B, data, header);
                if (blks.count(B) && isCondExit(B, &blks)) {
                    assert(header && "empty loop header");
                    errs() << "conditional exit of a loop\n";
                    for (auto blk : blks) {
                        // no need to setSlice explicitely: blk->setSlice(slice_id);
                        // errs()<<"loop blk\n";
                        for (NodeT *nd : blk->getNodes())
                            if (nd->getSlice() <= 0) {
                                // errs()<<"loop blk first\n";
                                data->analysis->enqueue(nd);
                                // first->setSlice(100);
                            }
                    }
                }
            }

            n->setSlice(slice_id);

#ifdef ENABLE_CFG
            // when we marked a node, we need to mark even
            // the basic block - if there are basic blocks
            if (BBlock<NodeT> *B = n->getBBlock()) {
                B->setSlice(slice_id);
                if (data->markedBlocks)
                    data->markedBlocks->insert(B);
            }
#endif

            // the same with dependence graph, if we keep a node from
            // a dependence graph, we need to keep the dependence graph
            if (DependenceGraph<NodeT> *dg = n->getDG()) {
                dg->setSlice(slice_id);
                if (!data->analysis->isForward()) {
                    // and keep also all call-sites of this func (they are
                    // control dependent on the entry node)
                    // This is correct but not so precise - fix it later.
                    // Now I need the correctness...
                    NodeT *entry = dg->getEntry();
                    assert(entry && "No entry node in dg");
                    data->analysis->enqueue(entry);
                } else {
                    // NodeT *exit = dg->getExit();
                    // assert(exit && "No exit node in dg");
                    // data->analysis->enqueue(exit);
                }
            }
            if (auto br = dyn_cast<BranchInst>(n->getKey()))
                if (br->isConditional())
                    return true;
            return false;
        }

        if (n->getSlice() <= 0 || !Inst)
            return true;

        // vector <Value*> lVals;
        set<uint32_t> allocs;
        set<uint32_t> mallocs;
        set<GlobalVariable *> globals;
        if (pass_id == 2 &&
            (Inst->getOpcode() == Instruction::Load || Inst->getOpcode() == Instruction::Store)) {
            unsigned OpIdx = Inst->getOpcode() == Instruction::Load ? 0 : 1;
            n->setSlice(slice_id + 1);
            handlePreloadingForSensitiveAccesses(data, PTA, n, Inst, slice_id, NULL, allocs, mallocs, globals, OpIdx);
            bool addDep = checkAddressDependency(n->rev_data_begin(), n->rev_data_end(), Inst->getOperand(OpIdx), slice_id + 3);
            if (!addDep) {
                addDep = checkAddressDependency(n->user_begin(), n->user_end(), Inst->getOperand(OpIdx), slice_id + 3);
            }
            // if (!globals.empty()) {errs() << "addDep " << addDep << "\n";}
            // errs() << *Inst << "$$$$$$$$$$\n";
            processBBlockRevCDs(false, addDep, n->getBBlock(), NULL, slice_id + 4, NULL, allocs, mallocs, globals);
        } else if (pass_id == 1 && Inst->getOpcode() == Instruction::Br) {
            BBlock<NodeT> *B = n->getBBlock();
            BBlock<NodeT> *header;
            set<BBlock<NodeT> *> blks;
            getBrLoopBlocks(blks, B, data, header);
            if (blks.count(B) && isCondExit(B, &blks)) {
                assert(header && "empty loop header2");
                errs() << "conditional exit of a loop2\n";
                for (auto blk : blks) {
                    for (NodeT *nd : blk->getNodes()) {
                        Instruction *ndInst = dyn_cast<Instruction>(nd->getKey());
                        if (ndInst->getOpcode() == Instruction::Load ||
                            ndInst->getOpcode() == Instruction::Store) {
                            handlePreloadingForSensitiveAccesses(data, PTA, nd, ndInst, slice_id, NULL, allocs,
                                                                 mallocs, globals, ndInst->getOpcode() == Instruction::Load ? 0 : 1);
                        }
                        nd->setSlice(0);
                    }
                    // errs() << "iter blk_2 " << blk << " " << blks->size() << " "<< blks->count(blk) << "\n";
                }
                processBBlockRevCDs(true, false, header, &blks, slice_id + 4, NULL, allocs, mallocs, globals);
            }
        } else if (CallInst *CI = dyn_cast<CallInst>(Inst)) {
            Function *fun = CI->getCalledFunction();
            if (!fun || pass_id != 2)
                return true;
            StringRef fname = fun->getName();
            if (fname.equals("llvm.memcpy.p0i8.p0i8.i64")) {
                // errs() << "get memcpy\n";
                // Value *op = CI->getOperand(0);
                n->setSlice(slice_id + 1);
                handlePreloadingForSensitiveAccesses(data, PTA, n, Inst, slice_id, NULL, allocs, mallocs, globals, 9);
                bool addDep = checkAddressDependency(n->rev_data_begin(), n->rev_data_end(), CI->getOperand(0), slice_id + 3);
                if (!addDep) {
                    addDep = checkAddressDependency(n->user_begin(), n->user_end(), CI->getOperand(0), slice_id + 3);
                }
                if (!addDep) {
                    addDep = checkAddressDependency(n->rev_data_begin(), n->rev_data_end(), CI->getOperand(1), slice_id + 3);
                }
                if (!addDep) {
                    addDep = checkAddressDependency(n->user_begin(), n->user_end(), CI->getOperand(1), slice_id + 3);
                }

                // if (!globals.empty()) {errs() << "addDep " << addDep << "\n";}
                processBBlockRevCDs(false, addDep, n->getBBlock(), NULL, slice_id + 4, NULL, allocs, mallocs,
                                    globals);

            } else if (fname.equals("llvm.memset.p0i8.i64")) {
                // errs() << "get memset\n";
                n->setSlice(slice_id + 1);
                handlePreloadingForSensitiveAccesses(data, PTA, n, Inst, slice_id, NULL, allocs, mallocs, globals, 0);
                bool addDep = checkAddressDependency(n->rev_data_begin(), n->rev_data_end(), CI->getOperand(0), slice_id + 3);
                if (!addDep) {
                    addDep = checkAddressDependency(n->user_begin(), n->user_end(), CI->getOperand(0), slice_id + 3);
                }
                // if (!globals.empty()) {errs() << "addDep " << addDep << "\n";}
                processBBlockRevCDs(false, addDep, n->getBBlock(), NULL, slice_id + 4, NULL, allocs, mallocs,
                                    globals);
            }
        }
        return true;
    }
};

struct SlicerStatistics {
    SlicerStatistics()
        : nodesTotal(0), nodesRemoved(0), blocksRemoved(0) {}

    // total number of nodes that were checked for removing
    uint64_t nodesTotal;
    // total number of nodes actually removed (including the
    // ones removed in blocks)
    uint64_t nodesRemoved;
    // number of whole blocks removed
    uint32_t blocksRemoved;
};

template <typename NodeT>
class Slicer : legacy::Analysis<NodeT> {
    uint32_t options;
    uint32_t slice_id;

    std::set<DependenceGraph<NodeT> *> sliced_graphs;

    // slice nodes from the graph; do it recursively for call-nodes
    void sliceNodes(DependenceGraph<NodeT> *dg, uint32_t slice_id) {
        for (auto &it : *dg) {
            NodeT *n = it.second;

            if (n->getSlice() != slice_id) {
                if (removeNode(n)) // do backend's specific logic
                    dg->deleteNode(n);

                continue;
            }

            // slice subgraphs if this node is
            // a call-site that is in the slice
            for (DependenceGraph<NodeT> *sub : n->getSubgraphs()) {
                // slice the subgraph if we haven't sliced it yet
                if (sliced_graphs.insert(sub).second)
                    sliceNodes(sub, slice_id);
            }
        }

        // slice the global nodes
        const auto &global_nodes = dg->getGlobalNodes();
        if (!global_nodes)
            return;

        for (auto &it : *global_nodes.get()) {
            NodeT *n = it.second;

            if (n->getSlice() != slice_id) {
                if (removeNode(n)) // do backend's specific logic
                    dg->deleteGlobalNode(n);
                continue;
            }
        }
    }

protected:
    // how many nodes and blocks were removed or kept
    SlicerStatistics statistics;

public:
    Slicer<NodeT>(uint32_t opt = 0)
        : options(opt) {}

    SlicerStatistics &getStatistics() { return statistics; }
    const SlicerStatistics &getStatistics() const { return statistics; }

    DebugLoc getOrCreateDebugLoc(const Instruction *InsertBefore,
                                 DISubprogram *SP) {
        assert(InsertBefore);
        if (InsertBefore->getDebugLoc())
            return InsertBefore->getDebugLoc();
        const Instruction *Prev = InsertBefore->getPrevNode();
        if (Prev && Prev->getDebugLoc())
            return Prev->getDebugLoc();
        if (SP)
            return llvm::DILocation::get(SP->getContext(), SP->getLine(), 1, SP);
        return DebugLoc();
    }

    void setDebugLoc(Instruction *nIns, Instruction *ins) {
        nIns->setDebugLoc(getOrCreateDebugLoc(ins, ins->getParent()->getParent()->getSubprogram()));
        // errs() << "set Debug Loc\n";
    }

    void handleFreeFunc(const vector<CallInst *> *allFreeCalls, LLVMPointerAnalysis *PTA) {
        for (CallInst *CI : *allFreeCalls) {
            Function *fun = CI->getCalledFunction();
            // if (fun) {
            //     errs() << "got call func: " << fun->getName() << "\n";
            // }
            if (fun && fun->getName().equals("free")) {
                Value *op = CI->getOperand(0);
                PSNode *pts = PTA->getPointsToNode(op);
                for (const auto &ptr : pts->pointsTo) {
                    Value *vl = ptr.target->getUserData<Value>();
                    if (vl == NULL) {
                        // errs() << "NULL pt value at a free \n";
                        continue;
                    }
                    // IRBuilder<> builder2(CI->getNextNode());
                    // builder2.CreateLoad(vl);
                    if (CallInst *mCI = dyn_cast<CallInst>(vl)) {
                        Function *mFunc = mCI->getCalledFunction();
                        if (mFunc && mFunc->getName().equals("malloc")) {
                            uint32_t bid = ptr.target->getBufferId();
                            errs() << "get malloc for free: " << bid << "\n";
                            if (ptr.target->isBuffered()) {
                                Module *M = CI->getModule();
                                IRBuilder<> builder(CI);
                                auto c = M->getOrInsertFunction("_Z14eraseMallocSetiPv",
                                                                builder.getInt1Ty(), builder.getInt32Ty(),
                                                                builder.getInt8PtrTy());
                                Function *fm = cast<Function>(c);

                                vector<Value *> args1;
                                args1.push_back(builder.getInt32(bid));
                                Value *pv = builder.CreateBitCast(op, builder.getInt8PtrTy());
                                args1.push_back(pv);
                                auto nCI = builder.CreateCall(fm, args1);
                                if (!nCI->getDebugLoc()) {
                                    setDebugLoc(nCI, CI);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    uint32_t
    mark(NodeT *start, LLVMPointerAnalysis *pta, uint32_t sl_id = 0, bool forward_slice = false, uint16_t pass_id = 0, uint16_t buff_id = 0,
         const vector<CallInst *> *allFreeCalls = NULL) {
        if (sl_id == 0)
            sl_id = 1;

        WalkAndMark<NodeT> wm(forward_slice);
        buff_id = wm.mark(start, sl_id, pta, pass_id, buff_id);

        ///
        // If we are performing forward slicing,
        // we are missing the control dependencies now.
        // So gather all control dependencies of the nodes that
        // we want to have in the slice and perform normal backward
        // slicing w.r.t these nodes.
        //         if (forward_slice) {
        //             std::set<NodeT *> branchings;
        //             for (auto *BB : wm.getMarkedBlocks()) {
        // #if ENABLE_CFG
        //                 for (auto cBB : BB->revControlDependence()) {
        //                     assert(cBB->successorsNum() > 1);
        //                     branchings.insert(cBB->getLastNode());
        //                 }
        // #endif
        //             }

        //             if (!branchings.empty()) {
        //                 WalkAndMark<NodeT> wm2;
        //                 buff_id = wm2.mark(branchings, sl_id, pta, pass_id, buff_id);
        //             }
        //         }

        if (pass_id == 2) {
            handleFreeFunc(allFreeCalls, pta);
        }

        return buff_id;
    }
    ///
    // Mark nodes dependent on 'start' with 'sl_id'.
    // If 'forward_slice' is true, mark the nodes depending on 'start' instead.
    uint32_t mark(NodeT *start, uint32_t sl_id = 0, bool forward_slice = false) {
        if (sl_id == 0)
            sl_id = ++slice_id;

        WalkAndMark<NodeT> wm(forward_slice);
        int buff_id = wm.mark(start, sl_id, NULL, 0, 0);

        ///
        // If we are performing forward slicing,
        // we are missing the control dependencies now.
        // So gather all control dependencies of the nodes that
        // we want to have in the slice and perform normal backward
        // slicing w.r.t these nodes.
        //         if (forward_slice) {
        //             std::set<NodeT *> branchings;
        //             for (auto *BB : wm.getMarkedBlocks()) {
        // #if ENABLE_CFG
        //                 for (auto cBB : BB->revControlDependence()) {
        //                     assert(cBB->successorsNum() > 1);
        //                     branchings.insert(cBB->getLastNode());
        //                 }
        // #endif
        //             }

        //             if (!branchings.empty()) {
        //                 WalkAndMark<NodeT> wm2;
        //                 buff_id = wm2.mark(branchings, sl_id, NULL, 0, buff_id);
        //             }
        //         }

        return buff_id;
    }

    // slice the graph and its subgraphs. mark needs to be called
    // before this routine (otherwise everything is sliced)
    uint32_t slice(DependenceGraph<NodeT> *dg, uint32_t sl_id = 0) {
#ifdef ENABLE_CFG
        // first slice away bblocks that should go away
        sliceBBlocks(dg, sl_id);
#endif // ENABLE_CFG

        // now slice the nodes from the remaining graphs
        sliceNodes(dg, sl_id);

        return sl_id;
    }

    // remove node from the graph
    // This virtual method allows to taky an action
    // when node is being removed from the graph. It can also
    // disallow removing this node by returning false
    virtual bool removeNode(NodeT *) {
        return true;
    }

#ifdef ENABLE_CFG
    virtual bool removeBlock(BBlock<NodeT> *) {
        return true;
    }

    struct RemoveBlockData {
        uint32_t sl_id;
        std::set<BBlock<NodeT> *> &blocks;
    };

    static void getBlocksToRemove(BBlock<NodeT> *BB, RemoveBlockData &data) {
        if (BB->getSlice() == data.sl_id)
            return;

        data.blocks.insert(BB);
    }

    void sliceBBlocks(BBlock<NodeT> *start, uint32_t sl_id) {
        // we must queue the blocks ourselves before we potentially remove them
        legacy::BBlockBFS<NodeT> bfs(legacy::BFS_BB_CFG);
        std::set<BBlock<NodeT> *> blocks;

        RemoveBlockData data = {sl_id, blocks};
        bfs.run(start, getBlocksToRemove, data);

        for (BBlock<NodeT> *blk : blocks) {
            // update statistics
            statistics.nodesRemoved += blk->size();
            statistics.nodesTotal += blk->size();
            ++statistics.blocksRemoved;

            // call specific handlers (overriden by child class)
            removeBlock(blk);

            // remove block from the graph
            blk->remove();
        }
    }

    // remove BBlocks that contain no node that should be in
    // sliced graph
    void sliceBBlocks(DependenceGraph<NodeT> *graph, uint32_t sl_id) {
        auto &CB = graph->getBlocks();
#ifndef NDEBUG
        uint32_t blocksNum = CB.size();
#endif
        // gather the blocks
        // FIXME: we don't need two loops, just go carefully
        // through the constructed blocks (keep temporary always-valid iterator)
        std::set<BBlock<NodeT> *> blocks;
        for (auto &it : CB) {
            if (it.second->getSlice() != sl_id)
                blocks.insert(it.second);
        }

        for (BBlock<NodeT> *blk : blocks) {
            // update statistics
            statistics.nodesRemoved += blk->size();
            statistics.nodesTotal += blk->size();
            ++statistics.blocksRemoved;

            // call specific handlers (overriden by child class)
            if (removeBlock(blk)) {
                // remove block from the graph
                blk->remove();
            }
        }

        assert(CB.size() + blocks.size() == blocksNum &&
               "Inconsistency in sliced blocks");
    }

#endif
};

} // namespace dg

#endif
