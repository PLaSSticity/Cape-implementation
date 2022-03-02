// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Analysis/PostDominators.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/BFS.h"
// #include "dg/Dominators/DominanceFrontiers.h"
#include "dg/Dominators/PostDominanceFrontiers.h"

#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/util/debug.h"

namespace dg {

void LLVMDependenceGraph::computePostDominators(bool addPostDomFrontiers) {
    DBG_SECTION_BEGIN(llvmdg, "Computing post-dominator frontiers (control deps.)");
    using namespace llvm;
    // iterate over all functions
    for (auto &F : getConstructedFunctions()) {
        legacy::PostDominanceFrontiers<LLVMNode, LLVMBBlock> pdfrontiers;
        // legacy::DominanceFrontiers<LLVMNode> dfrontiers;

        // root of post-dominator tree
        LLVMBBlock *root = nullptr;
        // root of dominator tree
        LLVMBBlock *droot = nullptr;

        Value *val = const_cast<Value *>(F.first);
        Function &f = *cast<Function>(val);
        PostDominatorTree *pdtree;

        DBG_SECTION_BEGIN(llvmdg, "Computing control deps. for " << f.getName().str());

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9))
        pdtree = new PostDominatorTree();
        // compute post-dominator tree for this function
        pdtree->runOnFunction(f);
#else
        PostDominatorTreeWrapperPass wrapper;
        wrapper.runOnFunction(f);
        pdtree = &wrapper.getPostDomTree();
#ifndef NDEBUG
        wrapper.verifyAnalysis();
#endif
#endif

        DominatorTree dt;
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9))
        // compute dominator tree for this function
        dt.runOnFunction(f);
#else
        dt.recalculate(f);
#endif

        // add immediate post-dominator edges
        auto &our_blocks = F.second->getBlocks();
        bool built = false;
        for (auto &it : our_blocks) {
            LLVMBBlock *BB = it.second;
            BasicBlock *B = cast<BasicBlock>(const_cast<Value *>(it.first));
            DomTreeNode *N = pdtree->getNode(B);
            // when function contains infinite loop, we're screwed
            // and we don't have anything
            // FIXME: just check for the root,
            // don't iterate over all blocks, stupid...
            if (N) {
                DomTreeNode *idom = N->getIDom();
                BasicBlock *idomBB = idom ? idom->getBlock() : nullptr;
                built = true;

                if (idomBB) {
                    LLVMBBlock *pb = our_blocks[idomBB];
                    assert(pb && "Do not have constructed BB");
                    BB->setIPostDom(pb);
                    assert(cast<BasicBlock>(BB->getKey())->getParent() == cast<BasicBlock>(pb->getKey())->getParent() && "BBs are from diferent functions");
                    // if we do not have idomBB, then the idomBB is a root BB
                } else {
                    // PostDominatorTree may has special root without BB set
                    // or it is the node without immediate post-dominator
                    if (!root) {
                        root = new LLVMBBlock();
                        root->setKey(nullptr);
                        F.second->setPostDominatorTreeRoot(root);
                    }

                    BB->setIPostDom(root);
                }
            }
            N = dt.getNode(B);
            if (!N)
                continue;

            DomTreeNode *idom = N->getIDom();
            BasicBlock *idomBB = idom ? idom->getBlock() : nullptr;

            for (const auto &dom : N->getChildren()) {
                BasicBlock *dom_llvm_block = dom->getBlock();
                // const auto it = all_blocks.find(static_cast<const llvm::Value *>(dom_llvm_block));
                // assert( it != all_blocks.end() && "Do not have constructed domBB" );
                LLVMBBlock *dom_block = our_blocks[dom_llvm_block];
                if (dom_block != root)
                    BB->addDominator(dom_block);
            }

            if (idomBB) {
                LLVMBBlock *db = our_blocks[idomBB];
                assert(db && "Do not have constructed BB");
                BB->setIDom(db);
            } else {
                if (BB != droot) {
                    if (!droot) {
                        droot = new LLVMBBlock();
                        droot->setKey(nullptr);
                        // F.second->setPostDominatorTreeRoot(droot);
                    }
                    BB->setIDom(droot);
                }
            }
        }

        // well, if we haven't built the pdtree, this is probably infinite loop
        // that has no pdtree. Until we have anything better, just add sound control
        // edges that are not so precise - to predecessors.
        if (!built && addPostDomFrontiers) {
            for (auto &it : our_blocks) {
                LLVMBBlock *BB = it.second;
                for (const LLVMBBlock::BBlockEdge &succ : BB->successors()) {
                    // in this case we add only the control dependencies,
                    // since we have no pd frontiers
                    BB->addControlDependence(succ.target);
                }
            }
        }

        if (addPostDomFrontiers) {
            // assert(root && "BUG: must have root");
            if (root) {
                pdfrontiers.compute(root, true /* store also control depend. */);
            }
            // if (droot) {
            //     dfrontiers.compute(droot);
            // }
        }

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9))
        delete pdtree;
#endif
        DBG_SECTION_END(llvmdg, "Done computing control deps. for " << f.getName().str());
    }
    DBG_SECTION_END(llvmdg, "Done computing post-dominator frontiers (control deps.)");
}

} // namespace dg
