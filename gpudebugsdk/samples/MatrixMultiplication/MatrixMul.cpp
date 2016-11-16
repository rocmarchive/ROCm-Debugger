//==============================================================================
// Copyright (c) 2015-2016 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief  A matrix multiplication sample implemented using the HSA runtime and
///         HSAIL kernel 1.0F.  This sample is provided to demonstrate
///         rocm-gdb functionality.  It loads brig file directly (that was
///         generated with debugging support).
//==============================================================================
#include <cmath>
#include <cstdlib>
#include <cstring>
#if !defined(_WIN32) && !defined(_WIN64)
#include <dirent.h>
#include <sys/stat.h>
#endif
#include <fstream>
#include <iostream>
#include <time.h>
#include <vector>

#include <hsa.h>

#include "HSAResourceManager.h"

static const std::string gs_MATRIX_MUL_KERNEL_SYMBOL = "&__OpenCL_matrixMul_kernel";
static const std::string gs_MATRIX_MUL_KERNEL_BRIG_FILE = "matrixMul_kernel.brig";

static const std::string gs_OUTPUT_MATRIX_DIR = "./outputMatrix/";

// ================================= Functions declaration ============================================

void RunTest(bool doVerify);

// Helper function to load binary file into data.
bool LoadFile(const std::string& fileName, std::vector<char>& data);

// Helper function to output matrix result to file.
void OutputMatrix(const std::string& fileName, const float* pMatrix, const size_t numElements, const size_t width);

// =====================================================================================================

int main(int argc, char** argv)
{
    bool doVerify = false;

    if (argc > 1)
    {
        std::string ipOption(argv[1]);

        if (ipOption == "--verify")
        {
            doVerify = true;
        }
        else
        {
            std::cout << "Matrixmul dispatches an HSAIL matrix multiplication kernel\n";
            std::cout << "Possible options";
            std::cout << " \t--verify\t verify correctness by comparing against a serial implementation\n";
        }

    }

    RunTest(doVerify);
    return 0;
}

void RunTest(bool doVerify)
{
    using namespace AMDT;

    // Initialize HSA runtime
    std::cout << "Initializing HSA runtime...\n";

    if (true != HSAResourceManager::InitRuntime())
    {
        std::cerr << "RunTest(): HSA runtime initialization fail, exiting...\n";
        return;
    }

    HSAResourceManager myHsa;

    // Create default queue
    if (!myHsa.CreateDefaultQueue(true))
    {
        std::cerr << "RunTest(): Error on creating default queue.\n";
        return;
    }

    // Load the kernel brig
    std::vector<char> brigData;
    if (!LoadFile(gs_MATRIX_MUL_KERNEL_BRIG_FILE, brigData))
    {
        std::cerr << "Error in RunTest(): LoadFile() failed.\n";
        return;
    }

    if (0 == brigData.size())
    {
        std::cerr << "RuntTest(): Error in loading brig.\n";
        return;
    }

    // Create AQL packet
    hsa_kernel_dispatch_packet_t aql;

    if (!myHsa.CreateAQLPacketFromBrig(
            &brigData[0],
            gs_MATRIX_MUL_KERNEL_SYMBOL,
            true,
            aql))
    {
        std::cerr << "RuntTest(): Error in finalizing and creating AQL packet.\n";
        return;
    }

    // Setup matrices dimensions
    const size_t WORK_GROUP_SIZE = 16;
    const size_t HA = 5 * WORK_GROUP_SIZE;
    const size_t WA = 3 * WORK_GROUP_SIZE;
    const size_t HB = WA;
    const size_t WB = 8 * WORK_GROUP_SIZE;
    const size_t HC = HA;
    const size_t WC = WB;

    // Setup AQL packet
    aql.setup |= 2 << HSA_KERNEL_DISPATCH_PACKET_SETUP_DIMENSIONS;
    aql.workgroup_size_x = WORK_GROUP_SIZE;
    aql.workgroup_size_y = WORK_GROUP_SIZE;
    aql.grid_size_x = WC;
    aql.grid_size_y = HC;

    // Allocate memory for computation
    size_t sizeA = HA * WA;
    size_t sizeB = HB * WB;
    size_t sizeC = HC * WC;
    float* pBufferA = (float*) HSAResourceManager::AllocateSysMemory(sizeA * sizeof(float));
    float* pBufferB = (float*) HSAResourceManager::AllocateSysMemory(sizeB * sizeof(float));
    float* pBufferC = (float*) HSAResourceManager::AllocateSysMemory(sizeC * sizeof(float));
    // Initialize buffers, generate random matrices data.
    srand(static_cast<unsigned int>(time(nullptr)));

    for (size_t i = 0; i < sizeA; ++i)
    {
        pBufferA[i] = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    }

    for (size_t i = 0; i < sizeB; ++i)
    {
        pBufferB[i] = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    }

    for (size_t i = 0; i < sizeC; ++i)
    {
        pBufferC[i] = 0.0f;
    }

#ifdef _DEBUG
    // Output matrices data to files
    OutputMatrix("matrixA.mat", pBufferA, sizeA, WA);
    OutputMatrix("matrixB.mat", pBufferB, sizeB, WB);
#endif

    // Setup aql kernel arguments.
    // Pass kernel arguments

    // We have extra 6 64-bits kernel arguments before matrix C, A and B in kernel
    // arguments, so we need to set them up in offset size.
    if (!myHsa.AppendKernelArgs(&pBufferC, sizeof(float*), aql))
    {
        std::cerr << "RunTest(): Error on pBufferC AppendKernelArgs()\n";
    }

    if (!myHsa.AppendKernelArgs(&pBufferA, sizeof(float*), aql))
    {
        std::cerr << "RunTest(): Error on pBufferA AppendKernelArgs()\n";
    }

    if (!myHsa.AppendKernelArgs(&pBufferB, sizeof(float*), aql))
    {
        std::cerr << "RunTest(): Error on pBufferB AppendKernelArgs()\n";;
    }

    if (!myHsa.AppendKernelArgs(&WA, sizeof(uint32_t), aql))
    {
        std::cerr << "RunTest(): Error on WA AppendKernelArgs()\n";
    }

    if (!myHsa.AppendKernelArgs(&WB, sizeof(uint32_t), aql))
    {
        std::cerr << "RunTest(): Error on WB AppendKernelArgs()\n";
    }

    myHsa.RegisterKernelArgsBuffer(aql);

    if (!myHsa.Dispatch(aql))
    {
        std::cerr << "RunTest(): Error on Dispatch()\n";
        return;
    }

    // Wait for completion
    std::cout << "Waiting for completion...\n";

    if (!myHsa.WaitForCompletion(aql.completion_signal, UINT64_MAX, true))
    {
        std::cerr << "Error in RunTest(): Signal return error.\n";
        return;
    }

    std::cout << "Complete.\n";

#ifdef _DEBUG
        // Output matrices data to files
        OutputMatrix("matrixC.mat", pBufferC, sizeC, WC);
#endif

    if (doVerify)
    {
        std::cout << "Calculating reference data...\n";
        std::vector<float> referenceData(sizeC, 0.0f);

        for (uint32_t i = 0; i < HA; ++i)
        {
            for (uint32_t j = 0; j < WB; ++j)
            {
                float sum = 0;

                for (uint32_t k = 0; k < HB; ++k)
                {
                    // c_ij = sum_k^HB(a_ik * b_kj);
                    sum += pBufferA[i * WA + k] * pBufferB[k * WB + j];
                }

                referenceData[i * WC + j] = sum;
            }
        }

        // Validate
        std::cout << "Validating...\n";
        bool valid = true;
        uint32_t failIndex = 0;

        for (size_t i = 0; i < referenceData.size(); ++i)
        {
            if (fabs(pBufferC[i] - referenceData[i]) > 1e-3f)
            {
                failIndex = static_cast<uint32_t>(i);
                valid = false;
                break;
            }
        }

        if (!valid)
        {
            uint32_t fi = failIndex / WC;
            uint32_t fj = failIndex % WC;
            std::cerr << "Result not correct!\n";
            std::cerr << "Fail index: " << failIndex << "(" << fi << ", " << fj << ")\n";
            std::cerr << "pBufferC[" << failIndex << "] = " << pBufferC[failIndex] << "\n";
            std::cerr << "Expected: " << referenceData[failIndex] << "\n";
        }
        else
        {
            std::cout << "Pass.\n";
        }

#ifdef _DEBUG
        // Output matrices data to files
        OutputMatrix("referenceResult.mat", &referenceData[0], referenceData.size(), WC);
#endif
    }

    // Cleanup.
    if (nullptr != pBufferA)
    {
        HSAResourceManager::FreeHSAMemory(pBufferA);
        pBufferA = nullptr;
    }
    if (nullptr != pBufferB)
    {
        HSAResourceManager::FreeHSAMemory(pBufferB);
        pBufferB = nullptr;
    }
    if (nullptr != pBufferC)
    {
        HSAResourceManager::FreeHSAMemory(pBufferC);
        pBufferC = nullptr;
    }

    myHsa.CleanUp();
    myHsa.DestroyQueue();

    // Explicit call ShutDown() function, though it will
    // be called automatically when the program exit.
    HSAResourceManager::ShutDown();
}

bool LoadFile(const std::string& fileName, std::vector<char>& data)
{
    bool ret = false;
    std::ifstream inFile(fileName, std::ios::binary);

    if (inFile.good())
    {
        // get the size of the file;
        inFile.seekg(0, std::ios::end);
        std::size_t length = inFile.tellg();
        inFile.seekg(0, std::ios::beg);

        data.clear();
        data.resize(length, 0);
        inFile.read(&data[0], length);
        ret = true;
    }
    else
    {
        std::cerr << "Error in LoadFile(): Error when open file \"" << fileName << "\"\n";
        ret = false;
    }

    inFile.close();
    return ret;
}

void OutputMatrix(const std::string& fileName, const float* pMatrix, const size_t numElements, const size_t width)
{
    static std::string realMatrixOutDir = gs_OUTPUT_MATRIX_DIR;
    static bool isFirst = true;

    // First time run to check the output matrix directory
    if (isFirst)
    {
#if defined(_WIN32) || defined(_WIN64)
        /// \todo: windows version directory creation
#else
        // Check if gs_OUTPUT_MATRIX_DIR is exist.
        DIR* pDir = opendir(gs_OUTPUT_MATRIX_DIR.c_str());

        if (nullptr == pDir)
        {
            // Create it.
            int err = mkdir(gs_OUTPUT_MATRIX_DIR.c_str(), S_IRWXU | S_IRWXG);

            if (0 != err)
            {
                std::cout << "Warning in RunTest(): Cannot create output matrix directory \"" << gs_OUTPUT_MATRIX_DIR << "\"\n";
                realMatrixOutDir = "";
            }
        }
        else
        {
            // Nothing need to be done if gs_OUTPUT_MATRIX_DIR already exist.
            closedir(pDir);
        }

#endif
        isFirst = false;
    }

    std::string realPath = realMatrixOutDir + fileName;
    std::ofstream outFile(realPath);

    if (outFile.is_open())
    {
        size_t height = numElements / width;

        for (size_t i = 0; i < height; ++i)
        {
            size_t rowOffset = i * width;
            outFile << pMatrix[rowOffset];

            for (size_t j = 1; j < width; ++j)
            {
                outFile << "\t" << pMatrix[rowOffset + j];
            }

            outFile << "\n";
        }
    }
    else
    {
        std::cerr << "Cannot open file " << realPath << "\n";
    }

    outFile.close();
}
