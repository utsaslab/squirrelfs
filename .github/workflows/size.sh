#!/bin/bash

#
# Filters files containing LLVM IR bitcode to properly inspect ELF files
# statistics for build artifacts in LTO/ThinLTO mode.
#

NO_LLVM_IR_FILES=()

for f in $@; do
	if ! file "${f}" | grep -q "LLVM IR bitcode"; then
		NO_LLVM_IR_FILES+=("${f}")
	fi
done

size "${NO_LLVM_IR_FILES[@]}"
