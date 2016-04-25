#!/bin/bash 

# Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
#
# This script sets up the environment variables required for compiling HCC-HSAIL
# applications in debug mode.
# This script does not support the HCC-LC (HCC-Lightning Compiler)
#
# This script should be called as "source rocm-gdb-debug-flags.sh"
# before calling any compile scripts or Makefiles. Alternatively these 
# variables can be added to the debug options in any HCC application's
# build

export LIBHSAIL_OPTIONS_APPEND='-g -include-source'
export PROGRAM_FINALIZE_OPTIONS_APPEND='-g -O0 -amd-reserved-num-vgprs=4'
export PROGRAM_CREATE_OPTIONS_APPEND='-g'
