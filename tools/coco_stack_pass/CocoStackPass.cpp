/**
 * CocoStackPass.cpp - CodeGen Pass for generating stack maps
 *
 * LLVM 22.x New Pass Manager API
 */

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <vector>
#include <cstdint>
#include <string>

using namespace llvm;

// Stack pointer descriptor (matches coco_stack_map.h)
struct CocoPtrDesc {
    int32_t frame_offset;
    uint16_t size;
    uint8_t  kind;
    uint8_t  flags;
};

// Pointer kinds
enum PtrKind {
    KIND_FRAME_PTR    = 0,
    KIND_RETURN_ADDR  = 1,
    KIND_LOCAL_PTR    = 2,
    KIND_SPILL_REG    = 3,
    KIND_MAYBE_PTR    = 4,
};

// Function stack map entry
struct StackMapEntry {
    uint64_t func_addr = 0;
    uint64_t func_size = 0;
    uint32_t frame_size = 0;
    std::vector<CocoPtrDesc> pointers;
    std::string func_name;
};

// Global state for the pass
static std::vector<StackMapEntry> g_entries;
static std::string g_output_path = "output.coco_stackmap";

static uint8_t classifyStackObject(MachineFunction &MF, int objIdx) {
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    uint64_t size = MFI.getObjectSize(objIdx);
    if (size == 8) return KIND_MAYBE_PTR;
    return 0xFF;
}

static int32_t computeFpOffset(int64_t spOffset, int frameSize, int fpSpOffset) {
    // For ARM64 with frame pointer:
    // FP = SP_new + fpSpOffset (where fpSpOffset is typically callee-saved area size)
    // MIR offset is relative to original SP before frame setup
    // FP-relative offset = spOffset + (frameSize - fpSpOffset)
    //
    // Simplified: for most ABIs, local variables below FP have negative offsets
    // The exact calculation depends on target ABI, but prologepilog provides accurate data

    // Use local-offset semantics: objects below FP have negative offsets
    // MIR offset values after prologepilog need conversion
    int64_t fpOffset;

    if (spOffset < 0) {
        // Negative SP offset means below frame base
        // Convert to FP-relative: typically local-offset in MIR
        fpOffset = spOffset; // This will be refined in actual implementation
    } else {
        fpOffset = spOffset - frameSize;
    }

    if (fpOffset > INT32_MAX || fpOffset < INT32_MIN) return 0;
    return (int32_t)fpOffset;
}

static void emitStackMap() {
    std::error_code EC;
    raw_fd_ostream out(g_output_path, EC, sys::fs::OF_None);

    if (EC) {
        errs() << "Error: " << EC.message() << "\n";
        return;
    }

    uint32_t magic = 0xC0C0;
    uint32_t version = 1;

    out << magic << version << (uint32_t)g_entries.size();

    for (const auto &entry : g_entries) {
        out << entry.func_addr << entry.func_size << entry.frame_size;
        out << (uint32_t)entry.pointers.size();

        for (const auto &ptr : entry.pointers) {
            out.write((const char*)&ptr, sizeof(ptr));
        }
    }

    out.close();
    outs() << "Stack map generated for " << g_entries.size() << " functions\n";
}

// New Pass Manager MachineFunction Pass
class CocoStackMapPass : public PassInfoMixin<CocoStackMapPass> {
public:
    PreservedAnalyses run(MachineFunction &MF, MachineFunctionAnalysisManager &MAM) {
        const MachineFrameInfo &MFI = MF.getFrameInfo();
        const Function &F = MF.getFunction();

        if (F.isDeclaration()) return PreservedAnalyses::all();

        StackMapEntry entry;
        entry.frame_size = MFI.getStackSize();
        entry.func_name = F.getName().str();

        // Frame pointer and return address
        entry.pointers.push_back({0, 8, KIND_FRAME_PTR, 0});
        entry.pointers.push_back({8, 8, KIND_RETURN_ADDR, 0});

        // Stack objects
        for (int i = MFI.getObjectIndexBegin(); i < MFI.getNumObjects(); ++i) {
            if (MFI.isDeadObjectIndex(i)) continue;
            if (MFI.isVariableSizedObjectIndex(i)) {
                entry.pointers.push_back({0, 0, KIND_MAYBE_PTR, 0xFF});
                continue;
            }

            int64_t spOffset = MFI.getObjectOffset(i);
            uint64_t size = MFI.getObjectSize(i);
            int32_t fpOffset = computeFpOffset(spOffset, MFI.getStackSize(), 48); // ARM64: FP at SP+48 for 64-byte frame
            uint8_t kind = classifyStackObject(MF, i);

            if (kind != 0xFF) {
                entry.pointers.push_back({fpOffset, (uint16_t)size, kind, 0});
            }
        }

        g_entries.push_back(entry);
        return PreservedAnalyses::all();
    }

    static void emitFinalStackMap() {
        emitStackMap();
    }
};

// Pass registration callback
static void registerPassBuilderCallbacks(PassBuilder &PB) {
    // Register machine function pass parsing callback (overload for MachineFunctionPassManager)
    PB.registerPipelineParsingCallback(
        [](StringRef Name, MachineFunctionPassManager &MFPM,
           ArrayRef<PassBuilder::PipelineElement> Pipeline) {
            if (Name == "coco-stack-map") {
                MFPM.addPass(CocoStackMapPass());
                return true;
            }
            return false;
        });
}

// Plugin entry point
extern "C" LLVM_ATTRIBUTE_VISIBILITY_DEFAULT
PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION,
        "CocoStackMapPlugin",
        "1.0",
        registerPassBuilderCallbacks,
        nullptr  // No PreCodeGenCallback
    };
}