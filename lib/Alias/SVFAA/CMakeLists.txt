add_library(SVFAA STATIC
        SVFWrapper.cpp
)
target_link_libraries(SVFAA PUBLIC AliasSpecManager LLVMBitWriter)
