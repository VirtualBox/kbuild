#!kmk_ash -xe
kmk_sed \
    -e 's/CLANGXXMACHO/CLANGCCMACHO/g' \
    -e '/^TOOL_CLANGCCMACHO_LD/s/clang++/clang/' \
    -e 's/for building C++ code/for building C code/' \
    --output CLANGCCMACHO.kmk CLANGXXMACHO.kmk

kmk_sed \
    -e "s/TOOL_CLANGXX/TOOL_CLANGCC/g" \
    -e "s/\(TOOL_CLANGCC_LD.*\)[+][+]/\1/" \
    -e "s/for building C[+][+] code/for building C code/g" \
    --output CLANGCC.kmk CLANGXX.kmk

