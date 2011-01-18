LOCAL_PATH:= $(call my-dir)

# For the host only
# =====================================================
include $(CLEAR_VARS)
include $(CLEAR_TBLGEN_VARS)

TBLGEN_TABLES :=	

clang_frontend_SRC_FILES :=	\
		Analyzer.cpp  ASTLocation.cpp  CallGraph.cpp  DeclReferenceMap.cpp  \
		Handlers.cpp  IndexProvider.cpp  Program.cpp    SelectorMap.cpp \
		Entity.cpp GlobalSelector.cpp  Indexer.cpp  


LOCAL_SRC_FILES := $(clang_frontend_SRC_FILES)

LOCAL_MODULE:= libclangIndex

include $(CLANG_HOST_BUILD_MK)
include $(CLANG_TBLGEN_RULES_MK)
include $(BUILD_HOST_STATIC_LIBRARY)
