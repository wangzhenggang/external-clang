LOCAL_PATH:= $(call my-dir)

# For the host only
# =====================================================
include $(CLEAR_VARS)
include $(CLEAR_TBLGEN_VARS)

LOCAL_MODULE := clang-bin

LOCAL_MODULE_TAGS := optional tests

LOCAL_MODULE_CLASS := EXECUTABLES

TBLGEN_TABLES := \
  DiagnosticCommonKinds.inc \
  DiagnosticDriverKinds.inc \
  DiagnosticFrontendKinds.inc \
  CC1Options.inc \
  CC1AsOptions.inc

clang_SRC_FILES := \
  cc1_main.cpp \
  cc1as_main.cpp \
  driver.cpp

LOCAL_SRC_FILES := $(clang_SRC_FILES)

LOCAL_STATIC_LIBRARIES := \
  libclangFrontendTool \
  libclangFrontend \
  libclangDriver \
  libclangSerialization \
  libclangCodeGen \
  libclangParse \
  libclangSema \
  libclangChecker \
  libclangAnalysis \
  libclangIndex \
  libclangRewrite \
  libclangAST \
  libclangLex \
  libclangBasic \
  libLLVMARMAsmPrinter \
  libLLVMARMAsmParser \
  libLLVMARMCodeGen \
  libLLVMARMDisassembler \
  libLLVMX86Info \
  libLLVMX86AsmPrinter \
  libLLVMX86AsmParser \
  libLLVMX86CodeGen \
  libLLVMX86Disassembler \
  libLLVMARMInfo \
  libLLVMAsmParser \
  libLLVMAsmPrinter \
  libLLVMBitReader \
  libLLVMBitWriter \
  libLLVMSelectionDAG \
  libLLVMInstCombine \
  libLLVMCodeGen \
  libLLVMipo \
  libLLVMipa \
  libLLVMLinker \
  libLLVMMC \
  libLLVMMCParser \
  libLLVMScalarOpts \
  libLLVMTransformUtils \
  libLLVMAnalysis \
  libLLVMCore \
  libLLVMSupport \
  libLLVMSystem \
  libLLVMTarget

LOCAL_LDLIBS += -lpthread -lm -ldl

include $(CLANG_HOST_BUILD_MK)
include $(CLANG_TBLGEN_RULES_MK)
include $(BUILD_HOST_EXECUTABLE)
