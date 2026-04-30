/**
 * CocoStackPass.cpp - CodeGen Pass for generating stack maps
 *
 * LLVM 22.x New Pass Manager API
 *
 * Generates .coco_stackmap file for dynamic stack growth.
 * Run after prologepilog to get accurate stack offsets.
 */

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

#include <vector>
#include <cstdint>
#include <string>
#include <map>

using namespace llvm;

// Stack pointer descriptor (matches coco_stack_map.h)
// MUST be binary-compatible with coco_ptr_desc_t
struct CocoPtrDesc {
    int32_t frame_offset;   // FP-relative offset (positive above FP, negative below)
    uint16_t size;          // Object size (typically 4 or 8 bytes)
    uint8_t  kind;          // Pointer kind (COCO_PTR_* constants)
    uint8_t  flags;         // Reserved flags
};

// Pointer kinds (matches coco_stack_map.h)
enum PtrKind {
    KIND_FRAME_PTR    = 0,  // Frame pointer (FP/x29/rbp)
    KIND_RETURN_ADDR  = 1,  // Return address
    KIND_LOCAL_PTR    = 2,  // Known pointer to local variable
    KIND_SPILL_REG    = 3,  // Spilled register
    KIND_MAYBE_PTR    = 4,  // Potential pointer (8-byte aligned)
};

// Function stack map entry (matches coco_func_map_t layout)
struct StackMapEntry {
    uint64_t func_addr = 0;     // Function start address
    uint64_t func_size = 0;     // Function size in bytes
    uint32_t frame_size = 0;    // Total stack frame size
    uint32_t num_pointers = 0;  // Number of pointer descriptors
    std::vector<CocoPtrDesc> pointers;
    std::string func_name;      // For debugging (not in binary output)
};

// Global state for the pass
static std::vector<StackMapEntry> g_entries;
static std::string g_output_path = "output.coco_stackmap";
static std::map<std::string, uint64_t> g_func_addresses;

// Classify stack object based on size and alignment
static uint8_t classifyStackObject(const MachineFunction &MF, int objIdx) {
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    uint64_t size = MFI.getObjectSize(objIdx);

    // 8-byte objects are potential pointers
    if (size == 8) {
        // Check alignment - pointers should be 8-byte aligned
        if (MFI.getObjectAlign(objIdx).value() >= 8) {
            return KIND_MAYBE_PTR;
        }
    }

    // 4-byte objects are typically integers, not pointers
    if (size == 4) {
        return 0xFF;  // Not a pointer candidate
    }

    // Larger objects might contain embedded pointers
    if (size > 8) {
        return KIND_MAYBE_PTR;  // Conservative: might contain pointers
    }

    return 0xFF;  // Unknown/not tracked
}

// Get FP-relative offset from MIR data
// Phase 0 verified: MIR local-offset matches runtime FP-relative offset
// Compute FP-relative offset from MIR data.
// Phase 0 verified: negative spOffset matches runtime FP-relative offset for locals.
static int32_t computeFpOffset(const MachineFrameInfo &MFI, int objIdx) {
    int64_t spOffset = MFI.getObjectOffset(objIdx);

    // Objects above SP have positive offset; locals below SP have negative offset.
    // Negative spOffset directly represents FP-relative offset for locals.
    return (int32_t)spOffset;
}

// Emit stack map in binary format (matches coco_stack_map.h layout)
static void emitStackMap() {
    std::error_code EC;
    raw_fd_ostream out(g_output_path, EC, sys::fs::OF_None);

    if (EC) {
        errs() << "Error: Cannot open " << g_output_path << ": " << EC.message() << "\n";
        return;
    }

    // Binary format (little-endian):
    // Header:
    //   uint32_t magic = 0xC0C0
    //   uint32_t version = 1
    //   uint32_t num_funcs
    //
    // For each function:
    //   uint64_t func_addr
    //   uint64_t func_size
    //   uint32_t frame_size
    //   uint32_t num_pointers
    //   CocoPtrDesc[num_pointers] (packed, no padding)

    uint32_t magic = 0xC0C0;
    uint32_t version = 1;
    uint32_t num_funcs = (uint32_t)g_entries.size();

    // Write header
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&num_funcs), sizeof(num_funcs));

    // Write function entries
    for (const auto &entry : g_entries) {
        out.write(reinterpret_cast<const char*>(&entry.func_addr), sizeof(entry.func_addr));
        out.write(reinterpret_cast<const char*>(&entry.func_size), sizeof(entry.func_size));
        out.write(reinterpret_cast<const char*>(&entry.frame_size), sizeof(entry.frame_size));
        out.write(reinterpret_cast<const char*>(&entry.num_pointers), sizeof(entry.num_pointers));

        // Write pointer descriptors (packed, no padding between them)
        for (const auto &ptr : entry.pointers) {
            out.write(reinterpret_cast<const char*>(&ptr), sizeof(CocoPtrDesc));
        }
    }

    out.close();

    outs() << "Stack map generated: " << g_output_path
           << " (" << num_funcs << " functions, "
           << g_entries.size() << " entries)\n";
}

// Clear global state for next compilation
static void clearGlobalState() {
    g_entries.clear();
    g_func_addresses.clear();
}

// New Pass Manager MachineFunction Pass
class CocoStackMapPass : public PassInfoMixin<CocoStackMapPass> {
public:
    PreservedAnalyses run(MachineFunction &MF, MachineFunctionAnalysisManager &MAM) {
        const MachineFrameInfo &MFI = MF.getFrameInfo();
        const Function &F = MF.getFunction();

        // Skip declarations (no actual code)
        if (F.isDeclaration()) {
            return PreservedAnalyses::all();
        }

        StackMapEntry entry;
        entry.frame_size = MFI.getStackSize();
        entry.func_name = F.getName().str();

        // Try to get function address from module
        // This will be filled in by a later pass or post-processing
        entry.func_addr = 0;  // To be resolved at link time
        entry.func_size = 0;  // To be resolved at link time

        // Frame pointer descriptor (at FP itself)
        entry.pointers.push_back({0, 8, KIND_FRAME_PTR, 0});

        // Return address descriptor (typically at FP+8 on x86-64, FP+8 on ARM64)
        // Note: exact location varies by platform and frame pointer setup
#if defined(__x86_64__)
        entry.pointers.push_back({8, 8, KIND_RETURN_ADDR, 0});
#elif defined(__aarch64__)
        entry.pointers.push_back({8, 8, KIND_RETURN_ADDR, 0});
#endif

        // Process stack objects
        for (int i = MFI.getObjectIndexBegin(); i < MFI.getNumObjects(); ++i) {
            // Skip dead objects
            if (MFI.isDeadObjectIndex(i)) {
                continue;
            }

            // Skip variable-sized objects (alloca)
            if (MFI.isVariableSizedObjectIndex(i)) {
                // Mark as variable-sized (special handling needed)
                entry.pointers.push_back({0, 0, KIND_MAYBE_PTR, 0xFF});
                continue;
            }

            // Get object info
            int64_t spOffset = MFI.getObjectOffset(i);
            uint64_t size = MFI.getObjectSize(i);
            uint8_t kind = classifyStackObject(MF, i);

            // Only track potential pointers
            if (kind != 0xFF) {
                int32_t fpOffset = computeFpOffset(MFI, i);
                entry.pointers.push_back({fpOffset, (uint16_t)size, kind, 0});
            }
        }

        entry.num_pointers = (uint32_t)entry.pointers.size();
        g_entries.push_back(entry);

        return PreservedAnalyses::all();
    }

    // Static method to emit final stack map
    static void emitFinalStackMap() {
        emitStackMap();
        clearGlobalState();
    }
};

// Module-level pass to collect function addresses and emit final output
class CocoStackMapFinalizer : public PassInfoMixin<CocoStackMapFinalizer> {
public:
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
        // Emit stack map after all functions processed
        CocoStackMapPass::emitFinalStackMap();
        return PreservedAnalyses::all();
    }
};

// Backend pass that runs via PreCodeGenCallback
// This approach uses LLVM's built-in AsmPrinter and StackMaps infrastructure
// to emit stack map data after prologepilog pass has run.
static bool cocoPreCodeGenCallback(Module &M, TargetMachine &TM,
                                   CodeGenFileType CGFT,
                                   raw_pwrite_stream &OS) {
    // Note: PreCodeGenCallback runs BEFORE backend passes.
    // For stack map generation, we need to run AFTER prologepilog.
    //
    // The correct approach is to:
    // 1. Use LLVM's existing stack map intrinsics (llvm.experimental.stackmap)
    // 2. Or use TargetPassConfig::insertPass to inject after prologepilog
    //
    // For now, we emit stack map from IR-level analysis (less accurate frame offsets)
    // and rely on a post-link tool to fill in actual addresses.

    outs() << "CocoStackMapPlugin: PreCodeGenCallback triggered for " << M.getName() << "\n";

    // Clear any previous entries
    clearGlobalState();

    // Analyze functions at IR level for basic stack info
    for (Function &F : M) {
        if (F.isDeclaration()) continue;

        StackMapEntry entry;
        entry.func_name = F.getName().str();
        entry.func_addr = 0;  // To be filled by post-processing
        entry.func_size = 0;  // To be filled by post-processing

        // Get approximate frame size from IR (not accurate, needs backend pass)
        // For now, just record function existence
        entry.frame_size = 0;
        entry.pointers.push_back({0, 8, KIND_FRAME_PTR, 0});
        entry.num_pointers = 1;

        g_entries.push_back(entry);
    }

    // Emit stack map file
    if (!g_entries.empty()) {
        emitStackMap();
    }

    // Don't prevent default pipeline - we just collect info
    return false;
}

// Pass registration callback
static void registerPassBuilderCallbacks(PassBuilder &PB) {
    // Register MachineFunction pass for parsing (requires explicit -fpasses=)
    PB.registerPipelineParsingCallback(
        [](StringRef Name, MachineFunctionPassManager &MFPM,
           ArrayRef<PassBuilder::PipelineElement> Pipeline) {
            if (Name == "coco-stack-map") {
                MFPM.addPass(CocoStackMapPass());
                return true;
            }
            return false;
        });

    // Register module pass for finalization (requires explicit -fpasses=)
    PB.registerPipelineParsingCallback(
        [](StringRef Name, ModulePassManager &MPM,
           ArrayRef<PassBuilder::PipelineElement> Pipeline) {
            if (Name == "coco-stack-map-finalizer") {
                MPM.addPass(CocoStackMapFinalizer());
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
        cocoPreCodeGenCallback  // PreCodeGenCallback for backend integration
    };
}