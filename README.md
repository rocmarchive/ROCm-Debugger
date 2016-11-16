# ROCm Debugger

## Overview
The ROCm Debugger provides a gdb-based debugging environment for debugging host application and GPU kernels running on Radeon Open Compute platforms (ROCm).
It can support all language runtimes (such as HIP and HCC) built on top of ROCm.  Initially, the debugging support within the GPU kernels starts with the 
HSAIL 1.0 programming language. GPU Kernel debugging requires a compilation path that goes through HSAIL kernel (such as [libHSAIL/HSAILAsm](https://github.com/HSAFoundation/HSAIL-Tools)).

There are two packages included in this release:
* ROCm gdb package that contains the rocm-gdb tool 
  * based on GDB 7.11, the GNU source-level debugger
* ROCm GPU Debug SDK package that contains the necessary header, library and sample files to run the rocm-gdb tool

The ROCm Debugger extends the existing [HSA Debugger](https://github.com/HSAFoundation/HSA-Debugger-AMD) with new features for ROCm .

## Table of Contents
* [Major Features](#Major)
* [What's New](#WhatsNew)
* [System Requirements](#System)
* [Package Contents](#Package)
* [Installation](#Installation)
* [Usage Examples](TUTORIAL.md)
* [Known Issues](#Known)
* [ROCm GDB LICENSE](gdb/LICENSE.txt) and [SDK LICENSE](gpudebugsdk/LICENSE.txt)

<A NAME="Major">
## Major Features
* Seamless host application and GPU kernel source debugging using a familiar gdb-based debugging environment on ROCm
* Set GPU kernel breakpoints, single stepping and inspect registers within HSAIL kernel source
* View active GPU states (active work-groups, work-items and wavefronts information)
* Disassemble GPU kernel at GPU kernel function and source breakpoint
* Trace GPU kernel launches into an output file

<A NAME="WhatsNew">
## What's New in Nov 2016 Release (version 1.3)
* Compatible with [ROCm 1.3 release](https://github.com/RadeonOpenCompute/ROCm)
* Support for AMD code object loader extension
* Initial support for Polaris GPUs
* Detect and gracefully fail on unsupported devices

## What's New in Aug 2016 Release (version 1.2)
* Compatible with [ROCm 1.2 release](https://github.com/RadeonOpenCompute/ROCm)
* Update gdb base to gdb v7.11.
* Initial support for provided GPU debug information via the GDB machine interface
* Support for debugging applications that use SIGUSR2. (Provided by [Pull Request#1](https://github.com/RadeonOpenCompute/ROCm-GDB/pull/1) from Didier Nadeaud)
* Add support to report HSAIL source text along with line number when single stepping.

## What's New in April 2016 Release (version 1.0)
* Compatible with [ROCm 1.0 release](https://github.com/RadeonOpenCompute/ROCm)
* Support 6th Generation AMD A-series APU processors (codenamed “Carrizo”)
* Support AMD Radeon™ R9 Fury, Fury X and Fury Nano GPUs  (codenamed “Fiji”)
* Support [CodeXL 2.0](https://github.com/GPUOpen-Tools/CodeXL/tree/v2.0)
* Add support to gdb *disassemble* command to disassemble and show the GPU isa disassembly text
* Add ability to trace GPU kernel launches 
* Add gdb *help rocm* command to show the list of rocm debugging related commands
* Add support to report the hardware slot scheduling information for wavefronts

<A NAME="System">
## System Requirements
* Boltzmann system
  * CPU: CPUs with PCIe Gen3 Atomics: Haswell-class Intel(c) Core CPUs v3 or newer and Intel Xeon E5 v3 or newer.
  * GPU: AMD Radeon™ R9 Fury, Fury X and Fury Nano GPUs  (codenamed “Fiji”)
  * Refer to the [ROCm platform requirements](https://radeonopencompute.github.io/hardware.html) for additional information
* or 6th Generation AMD A-series APU processors (codenamed “Carrizo”).
* OS: 64-bit Ubuntu 14.04
* [ROCm 1.2 platform](https://github.com/RadeonOpenCompute/ROCm)

To debug within a GPU kernel, the GPU kernel must be assembled using the latest [LibHSAIL/HSAILAsm](https://github.com/HSAFoundation/HSAIL-Tools) (from April 4th 2016 or newer) built with *BUILD_WITH_LIBBRIGDWARF=1*.

<A NAME="Package">
## Package Contents
The directory structure of the ROCm Debugger packages:
* *gpudebugsdk*
  * *include*
    * *AMDGPUDebug.h*, *FacilitiesInterface.h*
  * *bin/x86_64*
    * *amd-debug-lock*, *rocm-gdb-debug-flags.sh*
  * *lib/x86_64*
    * *libAMDGPUDebugHSA-x64.so*, *libAMDHSADebugAgent-x64.so*, *libAMDHwDbgFacilities-x64.so*
  * *samples*
    * *Common*
	    * *HSAResourceManager.h*, *HSAResourceManager.cpp*, *HSAExtensionFinalizer.h*, *HSAExtensionFinalizer.cpp*
	* *MatrixMultiplication*
	  * *Makefile*, *MatrixMul.cpp*, *matrixMul_kernel.brig*, *matrixMul_kernel.hsail*
  * *LICENSE.txt*
* *gdb*
  * *bin/x86_64*
    * *rocm-gdb*, *amd-gdb*, *.gdbinit*, *data-directory*
  * *LICENSE.txt*
* *ubuntu*
  * *rocm-gpudebugsdk_\<VERSION\>_amd64.deb*
  * *rocm-gdb_\<VERSION\>_amd64.deb*
  
If you download the ROCm Debugger packages or files separately, you must create the same directory structure as shown above in order to run rocm-gdb successfully.
  
<A NAME="Installation">
## Installation
First, make sure that the ROCm platform is setup correctly.
* [Install ROCm](https://github.com/RadeonOpenCompute/ROCm#installing-from-amd-rocm-repositories)
* [Verify the setup by running HSAIL *vector_copy* sample successfully](https://github.com/RadeonOpenCompute/ROCm#verify-installation)
  * Note that with the default *vector_copy* sample, you can't single step within the GPU kernel as the GPU kernel is not compiled with debugging support.
  * As part of the ROCm debugger package, there is a sample *MatrixMultiplication* that can be used with rocm-gdb.
  
###ROCm Debugger Installation
1. If you did not install ROCm Debugger as part of the ROCm installation, you can download the ROCm Debugger debian packages (*rocm-gpudebugsdk_\<VERSION\>_amd64.deb* and *rocm-gdb_\<VERSION\>_amd64.deb*) independently and install them as follows.
    * `sudo dpkg -i rocm-gpudebugsdk_<VERSION>_amd64.deb`
    * `sudo dpkg -i rocm-gdb_<VERSION>_amd64.deb`
      * The installed files will be placed in */opt/rocm/gpudebugsdk* and */opt/rocm/gdb* folders.
    * Note that both *rocm-gpudebugsdk* and *rocm-gdb* debian packages are included as part of the [ROCm](https://github.com/RadeonOpenCompute/ROCm#installing-from-amd-rocm-repositories) repo install.
2. Verify the setup
  * Run the *MatrixMultiplication* sample provided in the GPU Debug SDK package
    * `cd /opt/rocm/gpudebugsdk/samples/MatrixMultiplication`
    * `make`
      * The *Makefile* assumes that the hsa header files are located at */opt/rocm/hsa/include*.  If you encounter a compilation failure, please update the *HSADIR* within the *Makefile* to the directory of the hsa header files in the system.
      * Note that *matrixMul_kernel.hsail* is included for reference only. This sample will load the pre-built brig binary (*matrixMul_kernel.brig*) to run the kernel.
    * `/opt/rocm/bin/rocm-gdb MatrixMul`
      * Tips: include the */opt/rocm/bin* in your *PATH* environment variable
  
## Usage Examples
Check out the [tutorial](TUTORIAL.md) for some usage examples.

<A NAME="Known">
## Known Issues for November 2016 Release
* Debugging hsa code objects that contain more than one BRIG module are not supported
* Debugging HSAIL kernels that contain global (or read only) variables are not supported
* Debugging HSAIL kernels that contain HSAIL function calls are not supported
* Using rocm-gdb objects in python scripts is not yet supported
* Single stepping branch instructions could require multiple step commands


