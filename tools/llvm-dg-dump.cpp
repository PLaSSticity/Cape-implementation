#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include <cassert>
#include <cstdio>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/PointerAnalysis/PointerAnalysisFI.h"
#include "dg/PointerAnalysis/PointerAnalysisFS.h"
#include "dg/PointerAnalysis/PointerAnalysisFSInv.h"
#include "dg/llvm/DataDependence/DataDependence.h"
#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/LLVMDependenceGraphBuilder.h"
#include "dg/llvm/LLVMSlicer.h"

#include "dg/llvm/LLVMDG2Dot.h"

#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

#include "TimeMeasure.h"

using namespace dg;
using namespace llvm;

int main(int argc, char *argv[]) {
    llvm::Module *M;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    bool mark_only = false;
    bool bb_only = false;
    bool threads = false;
    const char *module = nullptr;
    const char *slicing_criterion = nullptr;
    const char *dump_func_only = nullptr;
    const char *pts = "fi";
    const char *entry_func = "main";
    LLVMControlDependenceAnalysisOptions::CDAlgorithm cd_alg =
        LLVMControlDependenceAnalysisOptions::CDAlgorithm::STANDARD;

    bool cloak = false;

    using namespace debug;
    uint32_t opts = PRINT_CFG | PRINT_DD | PRINT_CD | PRINT_USE | PRINT_ID;

    // parse options
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-no-control") == 0) {
            opts &= ~PRINT_CD;
        } else if (strcmp(argv[i], "-no-use") == 0) {
            opts &= ~PRINT_USE;
        } else if (strcmp(argv[i], "-pta") == 0) {
            pts = argv[++i];
            /*
        } else if (strcmp(argv[i], "-dda") == 0) {
            dda = argv[++i];
        */
        } else if (strcmp(argv[i], "-no-data") == 0) {
            opts &= ~PRINT_DD;
        } else if (strcmp(argv[i], "-no-cfg") == 0) {
            opts &= ~PRINT_CFG;
        } else if (strcmp(argv[i], "-call") == 0) {
            opts |= PRINT_CALL;
        } else if (strcmp(argv[i], "-postdom") == 0) {
            opts |= PRINT_POSTDOM;
        } else if (strcmp(argv[i], "-bb-only") == 0) {
            bb_only = true;
        } else if (strcmp(argv[i], "-cfgall") == 0) {
            opts |= PRINT_CFG;
            opts |= PRINT_REV_CFG;
        } else if (strcmp(argv[i], "-func") == 0) {
            dump_func_only = argv[++i];
        } else if (strcmp(argv[i], "-slice") == 0) {
            slicing_criterion = argv[++i];
        } else if (strcmp(argv[i], "-mark") == 0) {
            mark_only = true;
            slicing_criterion = argv[++i];
        } else if (strcmp(argv[i], "-threads") == 0) {
            threads = true;
        } else if (strcmp(argv[i], "-entry") == 0) {
            entry_func = argv[++i];
        } else if (strcmp(argv[i], "-cd-alg") == 0) {
            const char *arg = argv[++i];
            if (strcmp(arg, "standard") == 0)
                cd_alg = LLVMControlDependenceAnalysisOptions::CDAlgorithm::STANDARD;
            else if (strcmp(arg, "classic") == 0)
                cd_alg = LLVMControlDependenceAnalysisOptions::CDAlgorithm::STANDARD;
            else if (strcmp(arg, "ntscd") == 0)
                cd_alg = LLVMControlDependenceAnalysisOptions::CDAlgorithm::NTSCD;
            else {
                errs() << "Invalid control dependencies algorithm, try: classic, ce\n";
                abort();
            }

        } else if (strcmp(argv[i], "-cloak") == 0) {
            cloak = true;
        } else {
            module = argv[i];
        }
    }

    if (!module) {
        errs() << "Usage: % IR_module [output_file]\n";
        return 1;
    }

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR <= 5))
    M = llvm::ParseIRFile(module, SMD, context);
#else
    auto _M = llvm::parseIRFile(module, SMD, context);
    // _M is unique pointer, we need to get Module *
    M = _M.get();
#endif

    if (!M) {
        llvm::errs() << "Failed parsing '" << module << "' file:\n";
        SMD.print(argv[0], errs());
        return 1;
    }

    llvmdg::LLVMDependenceGraphOptions options;

    options.CDAOptions.algorithm = cd_alg;
    options.threads = threads;
    options.PTAOptions.threads = threads;
    options.DDAOptions.threads = threads;
    options.PTAOptions.entryFunction = entry_func;
    options.DDAOptions.entryFunction = entry_func;
    if (strcmp(pts, "fs") == 0) {
        options.PTAOptions.analysisType = LLVMPointerAnalysisOptions::AnalysisType::fs;
    } else if (strcmp(pts, "fi") == 0) {
        options.PTAOptions.analysisType = LLVMPointerAnalysisOptions::AnalysisType::fi;
    } else if (strcmp(pts, "inv") == 0) {
        options.PTAOptions.analysisType = LLVMPointerAnalysisOptions::AnalysisType::inv;
    } else {
        llvm::errs() << "Unknown points to analysis, try: fs, fi, inv\n";
        abort();
    }

    auto global_annos = M->getNamedGlobal("llvm.global.annotations");
    if (global_annos) {
        auto a = llvm::dyn_cast<llvm::ConstantArray>(global_annos->getOperand(0));
        for (unsigned int i = 0; i < a->getNumOperands(); i++) {
            auto e = llvm::dyn_cast<llvm::ConstantStruct>(a->getOperand(i));

            if (auto glb = llvm::dyn_cast<llvm::GlobalVariable>(e->getOperand(0)->getOperand(0))) {
                auto anno = llvm::dyn_cast<llvm::ConstantDataArray>(
                                llvm::dyn_cast<llvm::GlobalVariable>(e->getOperand(1)->getOperand(0))->getOperand(0))
                                ->getAsCString();
                glb->addAttribute(anno); // <-- add function annotation here
            }
        }
    }

    auto sec = new llvm::StringRef("secret");
    llvm::Value *secret_vl;
    for (auto I = M->global_begin(), E = M->global_end(); I != E; ++I) {
        if (I->hasAttribute(*sec)) {
            // *(new StringRef("secret")))
            // llvm::errs() << I->getName() << " has my attribute!\n";
            mark_only = true;
            secret_vl = &(*I);
        }
    }

    llvmdg::LLVMDependenceGraphBuilder builder(M, options);
    auto dg = builder.build();

    std::set<LLVMNode *> callsites;
    // const std::vector<LLVMNode *> *txnStartCallsites;
    // const std::set<LLVMBBlock *> *txnEndCallBlocks;
    if (secret_vl) {
        dg->getSecretNodes(secret_vl, &callsites);
        // Ignore slicing_criterion when performing secret slicing.
        slicing_criterion = "";
    }
    if (cloak) {
        // txnStartCallsites = getTxnBeginCallNodes();
        // txnEndCallBlocks = getTxnEndCallBlocks();
    } else if (slicing_criterion) {
        const char *sc[] = {
            slicing_criterion,
            "klee_assume",
            NULL};

        dg->getCallSites(sc, &callsites);
    }

    if (slicing_criterion || secret_vl || cloak) {
        llvmdg::LLVMSlicer slicer;

        if (cloak) {
            // slicer.markPreloadingBlocks(txnStartCallsites, txnEndCallBlocks);
        } else if (strcmp(slicing_criterion, "ret") == 0) {
            if (mark_only)
                slicer.mark(dg->getExit());
            else
                slicer.slice(dg.get(), dg->getExit());
        } else {
            if (callsites.empty()) {
                errs() << "ERR: slicing criterion not found: "
                       << slicing_criterion << "\n";
                exit(1);
            }
            llvm::outs() << "[";
            uint32_t slid = 0;
            uint16_t buff_id = 0;
            auto *pta = builder.getPTA();
            for (LLVMNode *start : callsites) {
                buff_id = slicer.mark(start, pta, slid, true);
                //errs() << "second pass\n";
                // second pass: identify secret-dependent accesses and add transations
                //errs() << "buff_id before: "
                //       << buff_id << "\n";
#ifndef _DEBUG_
                buff_id = slicer.mark(start, pta, slid, true, 1, buff_id);
                //errs() << "third pass\n";
                //errs() << "buff_id after: "
                //       << buff_id << "\n";
                buff_id = slicer.mark(start, pta, slid, true, 2, buff_id, getAllFreeCalls());
#endif
                //errs() << "buff_id final: "
                //       << buff_id << "\n";
            }

            if (!mark_only)
                slicer.slice(dg.get(), nullptr, slid);
        }

        if (!mark_only) {
            std::string fl(module);
            fl.append(".sliced");
            std::ofstream ofs(fl);
            llvm::raw_os_ostream output(ofs);

            SlicerStatistics &st = slicer.getStatistics();
            errs() << "INFO: Sliced away " << st.nodesRemoved
                   << " from " << st.nodesTotal << " nodes\n";

#if (LLVM_VERSION_MAJOR > 6)
            llvm::WriteBitcodeToFile(*M, output);
#else
            llvm::WriteBitcodeToFile(M, output);
#endif
        }
    }

#if 1
    llvm::outs() << "]";
    string outName(module);
    outName += "_ac.ll";
    // std::error_code EC;
    // llvm::raw_fd_ostream out(outName, EC);
    ofstream myfile;
    myfile.open(outName);
    llvm::raw_os_ostream out(myfile);
    M->print(out, nullptr);
    myfile.close();
#else
    if (bb_only) {
        LLVMDGDumpBlocks dumper(dg.get(), opts);
        dumper.dump(nullptr, dump_func_only);
    } else {
        LLVMDG2Dot dumper(dg.get(), opts);
        dumper.dump(nullptr, dump_func_only);
    }
#endif
        return 0;
}
