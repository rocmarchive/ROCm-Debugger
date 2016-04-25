//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief  HSA Runtime resource management class
//==============================================================================
/// \todo: Considering having an HSA application counter to keep track of how many applications are using HSA runtime.


#include <cstdlib>   // _putenv_s, _dupenv_s on Windows, setenv, unsetenv on Linux.
#include <cstring>    // memset, memcpy
#include <string>
#include <stdarg.h>
#include <iostream>

#define USE_EXISTING_FINALIZER_EXTENSION
#include "HSAExtensionFinalizer.h"

#include "HSAResourceManager.h"

namespace AMDT
{

// Local struct to query gpu and cpu agent.
typedef struct AgentList
{
    std::vector<AgentInfo> m_vecGPU;
    std::vector<AgentInfo> m_vecCPU;
} AgentList;

static bool gs_bVerbosePrint = false;

// Static local function declaration
static bool         InitAQL(hsa_kernel_dispatch_packet_t& aqlPacket);
static hsa_status_t FindMemRegions_Callback(hsa_region_t region, void* data);
static hsa_status_t QueryDevice_Callback(hsa_agent_t agent, void* pData);

HSAFinalizer HSAResourceManager::ms_finalizer;
uint16_t     HSAResourceManager::ms_hsaCount = 0;
AgentInfo    HSAResourceManager::ms_gpu;
AgentInfo    HSAResourceManager::ms_cpu;
bool         HSAResourceManager::ms_hasRuntime = false;
bool         HSAResourceManager::ms_profilingEnabled = false;
hsa_queue_t* HSAResourceManager::ms_pQueue = nullptr;

// ------------------------------------- Public Functions -------------------------------------
HSAResourceManager::HSAResourceManager()
{
    ms_hsaCount++;
}

HSAResourceManager::~HSAResourceManager()
{
    if (!CleanUp())
    {
        std::cerr << "Error in HSAResourceManager::~HSAResourceManager(): CleanUp() failed\n";
    }

    ms_hsaCount--;

    if (0 == ms_hsaCount)
    {
        if (!HSAResourceManager::ShutDown())
        {
            std::cerr << "Error in HSAResourceManager::~HSAResourceManager(): ShutDown() failed\n";
        }
    }

    //std::cout << "HSA resource management object remain: " << ms_hsaCount << "\n";

}

bool HSAResourceManager::InitRuntime(bool verbosePrint)
{
    bool ret = true;

    if (!ms_hasRuntime)
    {
        hsa_status_t status = hsa_init();
        ret = HSA_CHECK_STATUS(status);

        if (true != ret)
        {
            std::cerr << "Error in HSAResourceManager::InitRuntime(): Initialing HSA Runtime failed, exiting...\n";
            status = hsa_shut_down();

            if (!HSA_CHECK_STATUS(status))
            {
                std::cerr << "Error in HSAResourceManager::InitRuntime(): Shutting down HSA Runtime failed.\n";
            }
        }

        if (true != ms_finalizer.GetExtensionTable(1, 0))
        {
            std::cerr << "Error in HSAResourceManager::InitRuntime(): Fail to get fianlizer extension table.\n";
            ret &= false;
        }

        gs_bVerbosePrint = verbosePrint;
        AgentList agentList; // Local agentList to get agents information
        status = hsa_iterate_agents(QueryDevice_Callback, &agentList);

        if (!HSA_CHECK_STATUS(status))
        {
            std::cerr << "Error in HSAResourceManager::InitRuntime() when querying all HSA devices.\n";
            ret &= false;
        }

        // At least one GPU and one CPU should present
        if (0 == agentList.m_vecGPU.size())
        {
            std::cerr << "Error in HSAResourceManager::InitRuntime(): Can't find any GPU device.\n";
            return false;
        }

        if (0 == agentList.m_vecCPU.size())
        {
            std::cerr << "Error in HSAResourceManager::InitRuntime(): Can't find any CPU device.\n";
            return false;
        }

        // Choose the first agent from the agent vector.
        ms_gpu = agentList.m_vecGPU[0];
        ms_cpu = agentList.m_vecCPU[0];

        // Find all memory region
        status = hsa_agent_iterate_regions(ms_gpu.m_device, FindMemRegions_Callback, &ms_gpu);
        ret &= HSA_CHECK_STATUS(status);

        status = hsa_agent_iterate_regions(ms_cpu.m_device, FindMemRegions_Callback, &ms_cpu);
        ret &= HSA_CHECK_STATUS(status);

        ms_hasRuntime = true;
    }

    // Cache hsa version number
    {
        uint16_t major = 0;
        uint16_t minor = 0;

        if (true != GetHsaVersion(major, minor) || (0 == major && 0 == minor))
        {
            std::cerr << "Error in caching hsa version numbers.\n";
            ret &= false;
        }
    }

    return ret;
}

bool HSAResourceManager::PrintHsaVersion()
{
    uint16_t maj = 0;
    uint16_t min = 0;

    bool ret = GetHsaVersion(maj, min);

    if (ret)
    {
        std::cout << "HSA version: " << maj << "." << min << "\n";
    }
    else
    {
        std::cerr << "Error in HSAResourceManager::PrintHsaVersion(): GetHsaVersion() failed.\n";
    }

    return ret;
}

bool HSAResourceManager::GetHsaVersion(uint16_t& major, uint16_t& minor)
{
    static uint16_t s_major = 0;
    static uint16_t s_minor = 0;

    if (0 != s_major || 0 != s_minor)
    {
        major = s_major;
        minor = s_minor;
        return true;
    }

    bool ret = true;
    hsa_status_t status;

    if (false == ms_hasRuntime)
    {
        status = hsa_init();

        if (!HSA_CHECK_STATUS(status))
        {
            std::cerr << "Cannot init HSA runtime when querying HSA version.\n";
            return false;
        }
    }

    status = hsa_system_get_info(HSA_SYSTEM_INFO_VERSION_MAJOR, &s_major);

    if (!HSA_CHECK_STATUS(status))
    {
        ret = false;
        std::cerr << "Error in HSAResourceManager::GetHsaVersion(): Get HSA Major version number failed\n";
    }

    status = hsa_system_get_info(HSA_SYSTEM_INFO_VERSION_MINOR, &s_minor);

    if (!HSA_CHECK_STATUS(status))
    {
        ret = false;
        std::cerr << "Error in HSAResourceManager::GetHsaVersion(): Get HSA Minor version number failed\n";
    }

    major = s_major;
    minor = s_minor;

    if (false == ms_hasRuntime)
    {
        status = hsa_shut_down();

        if (!HSA_CHECK_STATUS(status))
        {
            std::cerr << "Cannot shutdown HSA runtime after querying HSA version.\n";
            ret = false;
        }
    }

    return ret;
}

bool HSAResourceManager::CreateDefaultQueue(bool enableKernelTimestamps)
{
    bool ret = true;

    if (!DestroyQueue())
    {
        ret = false;
        std::cerr << "Error in CreateDefaultQueue(): Destroying previous existing queue failed\n";
        return ret;
    }

    uint32_t queueSize = 0;
    hsa_status_t status = hsa_agent_get_info(ms_gpu.m_device, HSA_AGENT_INFO_QUEUE_MAX_SIZE, &queueSize);

    if (!HSA_CHECK_STATUS(status))
    {
        std::cerr << "Error in HSAResourceManager::CreateDefaultQueue(): Get queue max size failed.\n";
        ret = false;
    }

    ms_gpu.m_maxQueueSize = queueSize;

    status = hsa_queue_create(ms_gpu.m_device,                 // HSA agent
                              queueSize,              // Number of packets the queue is expected to hold
                              HSA_QUEUE_TYPE_SINGLE,  // Type of the queue
                              nullptr,                // callback related to the queue. No specific requirement so it should be nullptr.
                              nullptr,                // Data that is passed to callback. nullptr because no callback here.
                              UINT32_MAX,             // Private segment size. Hint indicating the maximum expected usage per work item. No particular value required, so it should be UINT32_MAX
                              UINT32_MAX,             // Group segment size. Also no particular value required.
                              &ms_pQueue);            // The queue we want to create.

    if (!HSA_CHECK_STATUS(status) || nullptr == ms_pQueue)
    {
        std::cerr << "Error in HSAResourceManager::CreateDefaultQueue(): Create queue failed.\n";
        ret = false;
    }

    ms_profilingEnabled = enableKernelTimestamps;

    if (enableKernelTimestamps)
    {
        status = hsa_amd_profiling_set_profiler_enabled(ms_pQueue, 1);

        if (!HSA_CHECK_STATUS(status) || nullptr == ms_pQueue)
        {
            std::cerr << "Error in HSAResourceManager::CreateDefaultQueue(): hsa_amd_profiling_set_profiler_enabled() failed.\n";
            ret = false;
        }
    }

    return ret;
}

bool HSAResourceManager::SetQueue(hsa_queue_t* pQueue)
{
    bool ret = true;

    if (!DestroyQueue())
    {
        ret = false;
        std::cerr << "Error in SetQueue(): Destroying previous existing queue failed\n";
        return ret;
    }

    ms_pQueue = pQueue;

    return ret;
};

// Simple function to trim head and tail space of a string.
static void TrimHeadAndTailSpace(std::string& s)
{
    if (s.size() == 0)
    {
        return;
    }

    std::size_t nsi = s.find_first_not_of(' ');
    s.erase(0, nsi);

    // if there are all spaces in the string
    if (s.size() == 0)
    {
        return;
    }

    nsi = s.find_last_not_of(' ');
    s.erase(nsi + 1);

    return;
}

bool HSAResourceManager::CreateAQLPacketFromBrig(
    const void*                   pBRIG,
    const std::string&            kernelSymbol,
    const bool                    bCreateSignal,
    hsa_kernel_dispatch_packet_t& aqlPacketOut,
    const std::string&            finalizerFlags,
    const hsa_profile_t           hsaProfile,
    const std::size_t&            kernargOffset)
{

    if (nullptr == pBRIG)
    {
        std::cerr << "Error in HSAResourceManager::Finalize(): pBrig cannot be nullptr.\n";
        return false;
    }

    hsa_executable_t hsaExecutable = { 0 };
    hsa_code_object_t codeObj = {0};
    {
        // Simple string processing, current finalizer v3 is weak for option parameter parsing.
        std::string fFlags = finalizerFlags;
        TrimHeadAndTailSpace(fFlags);

        if (!Finalize(pBRIG, fFlags, hsaProfile, hsaExecutable, codeObj))
        {
            std::cerr << "Error in HSAResourceManager::CreateAQLPacketFromBrig(): Finalize() failed\n";
            return false;
        }

        if (0 == hsaExecutable.handle)
        {
            std::cerr << "executable fail to create.\n";
        }

        m_aqlInfos[&aqlPacketOut].m_executable = hsaExecutable;

        if (0 == codeObj.handle)
        {
            std::cerr << "codeObj fail to create.\n";
        }

        m_aqlInfos[&aqlPacketOut].m_codeObj = codeObj;

    }

    if (!CreateAQLFromExecutable(hsaExecutable, kernelSymbol, bCreateSignal, aqlPacketOut, kernargOffset)
        || (0 == aqlPacketOut.kernel_object))
    {
        std::cerr << "Error in HSAResourceManager::CreateAQLPacketFromBrig(): Failed to create aql from executable.\n";
        return false;
    }

    if (bCreateSignal && 0 == m_aqlInfos[&aqlPacketOut].m_completionSignal.handle)
    {
        std::cerr << "completion_signal fail in aqlInfo.\n";
    }

    if (nullptr == m_aqlInfos[&aqlPacketOut].m_kernArgBuffer.GetArgBufferPointer())
    {
        std::cerr << "kernarg buffer fail in aqlInfo.\n";
    }

    return true;
}

bool HSAResourceManager::CreateAQLFromExecutable(
    const hsa_executable_t&       hsaExecutable,
    const std::string&            kernelSymbol,
    const bool                    bCreateSignal,
    hsa_kernel_dispatch_packet_t& aql,
    const std::size_t&            kernargOffset)
{
    if (0 == hsaExecutable.handle)
    {
        std::cerr << "Error in HSAResourceManager::CreateAQLFromExecutable(): Input hsa_executable_t is invalid.\n";
        return false;
    }

    bool ret = InitAQL(aql);

    if (!ret)
    {
        std::cerr << "Error in HSAResourceManager::CreateAQLFromExecutable(): InitAQL() failed.\n";
        return false;
    }

    m_aqlInfos[&aql].m_executable = hsaExecutable;

    // Get symbol handle
    hsa_executable_symbol_t symbolOffset;
    hsa_status_t status = hsa_executable_get_symbol(hsaExecutable, nullptr, kernelSymbol.c_str(), ms_gpu.m_device, 0, &symbolOffset);

    if (!HSA_CHECK_STATUS(status))
    {
        std::cerr << "Error in HSAResourceManager::CreateAQLFromExecutable(): hsa_executable_get_symbol failed.\n";
        return false;
    }

    // Get code object handle
    status = hsa_executable_symbol_get_info(
                 symbolOffset, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT, &aql.kernel_object);

    if (!HSA_CHECK_STATUS(status) || aql.kernel_object == 0)
    {
        std::cerr << "Error in HSAResourceManager::CreateAQLFromExecutable(): hsa_executable_symbol_get_info failed.\n";
        return false;
    }

    // Get private segment size
    status = hsa_executable_symbol_get_info(
                 symbolOffset, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE, &aql.private_segment_size);

    if (!HSA_CHECK_STATUS(status))
    {
        std::cerr << "hsa_executable_symbol_get_info: query private_segment_size failed.\n";
        return false;
    }

    // Get kernel args size
    std::size_t kernArgSize = 0;
    status = hsa_executable_symbol_get_info(
                 symbolOffset, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE, &kernArgSize);

    if (!HSA_CHECK_STATUS(status))
    {
        std::cerr << "hsa_executable_symbol_get_info: query kernelarg_segment_size failed.\n";
        return false;
    }

    ret = m_aqlInfos[&aql].m_kernArgBuffer.AllocateBuffer(kernArgSize, kernargOffset);

    if (!ret)
    {
        std::cerr << "Cannot allocate kernel arg buffer for aql.\n";
        return false;
    }

    aql.kernarg_address = m_aqlInfos[&aql].m_kernArgBuffer.GetArgBufferPointer();

    if (bCreateSignal)
    {
        ret = CreateSignal(aql.completion_signal);

        if (!ret)
        {
            std:: cerr << "Error in HSAResourceManager::CreateAQLFromExecutable(): Create signal failed.\n";
            return false;
        }
    }

    m_aqlInfos[&aql].m_completionSignal = aql.completion_signal;

    return true;
}

bool HSAResourceManager::Finalize(const void*              pBRIG,
                                  const std::string&       compileFlags,
                                  const hsa_profile_t      hsaProfile,
                                        hsa_executable_t&  executableOut,
                                        hsa_code_object_t& codeObjOut)
{
    if (nullptr == pBRIG)
    {
        std::cerr << "Error in HSAResourceManager::Finalize(): pBrig cannot be nullptr.\n";
        return false;
    }

    hsa_status_t status;

    // Create HSA program  ----------------------------
    hsa_ext_program_t program = {0};
#if defined(_WIN64) || defined(_LP64)
    status = ms_finalizer.m_pTable->hsa_ext_program_create(
                 HSA_MACHINE_MODEL_LARGE,
                 hsaProfile,
                 HSA_DEFAULT_FLOAT_ROUNDING_MODE_ZERO,
                 "-g",
                 &program
             );
#else
    status = ms_finalizer.m_pTable->hsa_ext_program_create(
                 HSA_MACHINE_MODEL_SMALL,
                 hsaProfile,
                 HSA_DEFAULT_FLOAT_ROUNDING_MODE_ZERO,
                 "-g",
                 &program
             );
#endif

    if (!HSA_CHECK_STATUS(status) || 0 == program.handle)
    {
        std::cerr << "Error in HSAResourceManager::Finalize(): Create HSA program failed.\n";
        return false;
    }

    // Add BRIG module to HSA program  ----------------------------
    hsa_ext_module_t programModule = reinterpret_cast<hsa_ext_module_t>(const_cast<void*>(pBRIG));
    status = ms_finalizer.m_pTable->hsa_ext_program_add_module(program, programModule);

    if (!HSA_CHECK_STATUS(status))
    {
        std::cerr << "Error in HSAResourceManager::Finalize(): Adding BRIG module failed.\n";
        return false;
    }

    // Finalize hsail program --------------------------------------------------------
    hsa_isa_t isa;
    status = hsa_agent_get_info(ms_gpu.m_device, HSA_AGENT_INFO_ISA, &isa);

    if (!HSA_CHECK_STATUS(status))
    {
        std::cerr << "Error in HSAResourceManager::Finalize(): Fail to get ISA.\n";
        return false;
    }

    hsa_ext_control_directives_t control_directives;
    memset(&control_directives, 0, sizeof(hsa_ext_control_directives_t));

    status = ms_finalizer.m_pTable->hsa_ext_program_finalize(
                 program,
                 isa,
                 0,
                 control_directives,
                 compileFlags.c_str(),
                 HSA_CODE_OBJECT_TYPE_PROGRAM,
                 &codeObjOut
             );

    if (!HSA_CHECK_STATUS(status))
    {
        std::cerr << "Failed to finalize program.\n";
        return false;
    }

    m_codeObjSet.insert(codeObjOut.handle);

    status = ms_finalizer.m_pTable->hsa_ext_program_destroy(program);

    if (!HSA_CHECK_STATUS(status))
    {
        std::cerr << "Error in HSAResourceManager::Finalize(): Failed to destroy program.\n";
    }

    program.handle = 0;

    // Create executable
    status = hsa_executable_create(
                 hsaProfile, HSA_EXECUTABLE_STATE_UNFROZEN, "", &executableOut);

    if (!HSA_CHECK_STATUS(status))
    {
        std::cerr << "Failed to create hsa executable.\n";
        return false;
    }

    m_executableSet.insert(executableOut.handle);

    // Load code object.
    status = hsa_executable_load_code_object(executableOut, ms_gpu.m_device, codeObjOut, nullptr);

    if (!HSA_CHECK_STATUS(status))
    {
        std::cerr << "Failed to load code object.\n";
        return false;
    }

    // Freeze executable
    status = hsa_executable_freeze(executableOut, nullptr);

    if (!HSA_CHECK_STATUS(status))
    {
        std::cerr << "Failed to freeze executable.\n";
        return false;
    }

    return true;
}

bool HSAResourceManager::CopyKernelDispatchPacket(
    const hsa_kernel_dispatch_packet_t& aqlPacket,
    const bool                          bCopySignal,
    hsa_kernel_dispatch_packet_t& aqlPacketOut) const
{
    if (nullptr == memcpy(&aqlPacketOut, &aqlPacket, sizeof(hsa_kernel_dispatch_packet_t)))
    {
        std::cerr << "Error in HSAResourceManager::CopyKernelDispatchPacket(): memcpy() fail.\n";
        return false;
    }

    if (!bCopySignal)
    {
        aqlPacketOut.completion_signal.handle = 0;
    }

    return true;
}

bool HSAResourceManager::AppendKernelArgs(const void* pAddr, const std::size_t size, hsa_kernel_dispatch_packet_t& aql)
{
    return m_aqlInfos[&aql].m_kernArgBuffer.AppendKernelArgs(pAddr, size);
}

bool HSAResourceManager::RegisterKernelArgsBuffer(hsa_kernel_dispatch_packet_t& aql)
{
    aql.kernarg_address = m_aqlInfos[&aql].m_kernArgBuffer.GetArgBufferPointer();

    if (nullptr == aql.kernarg_address)
    {
        std::cerr << "Error in RegisterKernelArgsBuffer(): argument buffer hasn't been allocated yet.\n";
        return false;
    }

    return true;
}

bool HSAResourceManager::DeregisterKernelArgsBuffer(hsa_kernel_dispatch_packet_t& aql)
{
    bool ret = m_aqlInfos[&aql].m_kernArgBuffer.DestroyBuffer();

    aql.kernarg_address = nullptr;

    return ret;
}

bool HSAResourceManager::Dispatch(hsa_kernel_dispatch_packet_t& aql)
{
    if (nullptr == ms_pQueue)
    {
        std::cerr << "No queue!\n";
        return false;
    }

    // Verify if we have register the kernel args buffer.
    // Assumming we have only one kernel in the application.
    if (nullptr != m_aqlInfos[&aql].m_kernArgBuffer.GetArgBufferPointer()
        && nullptr == aql.kernarg_address)
    {
        this->RegisterKernelArgsBuffer(aql);
    }

    const uint32_t& queueSize = ms_pQueue->size;
    const uint32_t queueMask = queueSize - 1;

    // Write to queue
    uint64_t index = hsa_queue_load_write_index_relaxed(ms_pQueue);
    ((hsa_kernel_dispatch_packet_t*)(ms_pQueue->base_address))[index & queueMask] = aql;
    hsa_queue_store_write_index_relaxed(ms_pQueue, index + 1);

    // Ring doorbell.
    hsa_signal_store_release(ms_pQueue->doorbell_signal, static_cast<hsa_signal_value_t>(index));

    return true;
}

bool HSAResourceManager::WaitForCompletion(hsa_signal_t& completionSignal, uint64_t timeout, bool outputTimingData)
{
    bool ret = true;

    if (0 != hsa_signal_wait_acquire(completionSignal,         // signal
                                     HSA_SIGNAL_CONDITION_EQ,  // condition
                                     0,                        // compare_value
                                     timeout,                  // time_out_hint
                                     HSA_WAIT_STATE_ACTIVE)    // wait_state_hint
       )
    {
        std::cerr << "Error in HSAResourceManager::WaitForCompletion(): Signal wait return unexpected value\n";
        ret = false;
    }

    if (outputTimingData && ms_profilingEnabled)
    {
        hsa_amd_profiling_dispatch_time_t dispatch_times;
        dispatch_times.start = 0;
        dispatch_times.end = 0;
        hsa_status_t status = hsa_amd_profiling_get_dispatch_time(ms_gpu.m_device, completionSignal, &dispatch_times);

        if (!HSA_CHECK_STATUS(status))
        {
            std::cerr << "Error in HSAResourceManager::WaitForCompletion(): hsa_amd_profiling_get_dispatch_time() failed.\n";
        }

        std::cout << "Kernel dispatch executed in " << double((dispatch_times.end - dispatch_times.start) / 1e6) << " milliseconds.\n";
    }

    return ret;
}

bool HSAResourceManager::CreateSignal(hsa_signal_t& signalOut)
{
    bool ret = true;
    hsa_signal_t signal;
    // The initial value of the signal is 1.
    hsa_status_t status = hsa_signal_create(1, 0, nullptr, &signal);

    if (!HSA_CHECK_STATUS(status))
    {
        std::cerr << "Error in HSAResourceManager::CreateSignal(): hsa_signal_create failed\n";
        signal.handle = 0;
        ret = false;
    }
    else
    {
        m_signals.push_back(signal);
        signalOut = signal;
    }

    return ret;
}

bool HSAResourceManager::DestroySignal(hsa_signal_t& signal)
{
    bool ret = true;
    hsa_status_t status;

    for (unsigned int i = 0; i < m_signals.size(); ++i)
    {
        if (signal.handle == m_signals[i].handle)
        {
            m_signals[i] = m_signals[m_signals.size() - 1];
            m_signals.pop_back();
            break;
        }
    }

    status = hsa_signal_destroy(signal);

    if (!HSA_CHECK_STATUS(status))
    {
        std::cerr << "Error in HSAResourceManager::DestoySignal(): hsa_signal_destroy() failed\n";
        ret &= false;
    }

    return ret;
}

bool HSAResourceManager::HasRuntime()
{
    return ms_hasRuntime;
}

bool HSAResourceManager::ShutDown()
{
    bool ret = true;

    if (ms_hasRuntime)
    {
        if (!DestroyQueue())
        {
            ret = false;
            std::cerr << "Error in HSAResourceManager::ShutDown(): Destroying queue failed\n";
        }

        hsa_status_t status = hsa_shut_down();

        if (!HSA_CHECK_STATUS(status))
        {
            ret = false;
            std::cerr << "Error in HSAResourceManager::ShutDown():  Shutting down HSA runtime failed.\n";
        }

        ms_hasRuntime = false;
    }

    return ret;
}

bool HSAResourceManager::CleanUp()
{
    bool ret = true;
    hsa_status_t status;

    // Clean up signals
    for (std::size_t i = 0; i < m_signals.size(); ++i)
    {
        if (0 != m_signals[i].handle)
        {
            // TODO: Check the correct way to terminate a kernel.
            // If there is still kernel running, it should be terminated at this time.
            /**
                        hsa_signal_value_t sv = hsa_signal_wait_acquire(
                                            m_signals[i],
                                            HSA_EQ,                         // condition
                                            0,                              // compare_value
                                            0,                              // time_out_hint
                                            HSA_WAIT_ACTIVE);               // wait_state_hint
                        if (static_cast<hsa_signal_value_t>(0) != sv)
                        {
                            std::cout << "HSAResourceManager::CleanUp(): A kernel may not exit normally.\n";
                        }
            **/
            status = hsa_signal_destroy(m_signals[i]);
            bool lret = HSA_CHECK_STATUS(status);
            ret &= lret;

            if (true != lret)
            {
                std::cerr << "Error in HSAResourceManager::CleanUp(): Destroying signal " << i << " failed.\n";
            }

            m_signals[i].handle = 0;
        }
    }

    m_signals.clear();

    for (std::unordered_map<hsa_kernel_dispatch_packet_t*, AQLInfo>::iterator iter = m_aqlInfos.begin();
         iter != m_aqlInfos.end(); ++iter)
    {
        iter->second.m_kernArgBuffer.DestroyBuffer();
    }

    m_aqlInfos.clear();

    // Destroy executable
    for (std::unordered_set<uint64_t>::iterator iter = m_executableSet.begin();
         iter != m_executableSet.end(); ++iter)
    {
        hsa_executable_t tmp = {*iter};
        status = hsa_executable_destroy(tmp);

        if (!HSA_CHECK_STATUS(status))
        {
            std::cerr << "Fail to destroy hsa executable.\n";
            ret = false;
        }
    }

    m_executableSet.clear();

    // Destroy code object
    for (std::unordered_set<uint64_t>::iterator iter = m_codeObjSet.begin();
         iter != m_codeObjSet.end(); ++iter)
    {
        hsa_code_object_t tmp = {*iter};
        status = hsa_code_object_destroy(tmp);

        if (!HSA_CHECK_STATUS(status))
        {
            std::cerr << "Fail to destroy code object.\n";
            ret = false;
        }
    }

    m_codeObjSet.clear();

    m_aqlInfos.clear();

    return ret;
}

bool HSAResourceManager::DestroyQueue()
{
    bool ret = true;

    if (nullptr != ms_pQueue)
    {
        hsa_status_t status = hsa_queue_destroy(ms_pQueue);
        ret = HSA_CHECK_STATUS(status);

        if (true != ret)
        {
            std::cerr << "Error in HSAResourceManager::DestroyQueue(): hsa_queue_destroy() falied.\n";
        }
        else
        {
            ms_pQueue = nullptr;
        }
    }

    return ret;
}

void* HSAResourceManager::AllocateCoarseLocalMemory(size_t size)
{
    if (0 == ms_gpu.coarseRegion.handle)
    {
        std::cerr << "AllocateCoarseLocalMemory(): No coarse memory region present, exit" << std::endl;
        return nullptr;
    }

    void* pBuffer = nullptr;
    hsa_status_t status = hsa_memory_allocate(ms_gpu.coarseRegion, size, &pBuffer);
    return HSA_CHECK_STATUS(status) ? pBuffer : nullptr;
}

void* HSAResourceManager::AllocateSysMemory(size_t size)
{
    if (0 == ms_gpu.kernargRegion.handle)
    {
        std::cerr << "AllocateSysMemory(): No kernel arg region present, exit." << std::endl;
        return nullptr;
    }

    void* pBuffer = nullptr;
    hsa_status_t status = hsa_memory_allocate(ms_gpu.kernargRegion, size, &pBuffer);
    return HSA_CHECK_STATUS(status) ? pBuffer : nullptr;
}

bool HSAResourceManager::FreeHSAMemory(void* pBuffer)
{
    if (nullptr == pBuffer)
    {
        return true;
    }

    return HSA_CHECK_STATUS(hsa_memory_free(pBuffer));
}

bool HSAResourceManager::CopyHSAMemory(void* pDest, const void* pSrc,
                                       std::size_t size, bool hostToDev)
{
    if (nullptr == pDest || nullptr == pSrc)
    {
        std::cerr << "HSAResourceManager::CopyHSAMemory(): Input source or destination buffer cannot be nullptr.\n";
        return false;
    }

    if (0 == size)
    {
        std::cout << "Copy size is 0, nothing need to be done.\n";
        return 0;
    }

    void* pBuffer = (hostToDev) ? pDest : const_cast<void*>(pSrc);
    hsa_status_t status = hsa_memory_assign_agent(pBuffer, ms_gpu.m_device, HSA_ACCESS_PERMISSION_RW);

    if (!HSA_CHECK_STATUS(status))
    {
        return false;
    }

    status = hsa_memory_copy(pDest, pSrc, size);
    return (HSA_CHECK_STATUS(status));
}

// Accessors
const AgentInfo& HSAResourceManager::GPUInfo()
{
    return ms_gpu;
}

const AgentInfo& HSAResourceManager::CPUInfo()
{
    return ms_cpu;
}

const hsa_agent_t& HSAResourceManager::GPU()
{
    return ms_gpu.m_device;
}

const hsa_agent_t& HSAResourceManager::CPU()
{
    return ms_cpu.m_device;
}

const uint32_t& HSAResourceManager::GPUChipID()
{
    return ms_gpu.m_chipID;
}

const uint32_t& HSAResourceManager::CPUChipID()
{
    return ms_cpu.m_chipID;
}

hsa_queue_t* const& HSAResourceManager::Queue()
{
    return ms_pQueue;
}

AQLInfo& HSAResourceManager::GetAqlInfo(hsa_kernel_dispatch_packet_t& aql)
{
    return m_aqlInfos[&aql];
}

bool InitAQL(hsa_kernel_dispatch_packet_t& aqlPacketOut)
{
    bool ret = true;
    memset(&aqlPacketOut, 0, sizeof(hsa_kernel_dispatch_packet_t));

    aqlPacketOut.header |= HSA_PACKET_TYPE_KERNEL_DISPATCH << HSA_PACKET_HEADER_TYPE;

    // using FENCE_SCOPE_SYSTEM base on example on HSA runtime document, version 1_00_20150130.
    aqlPacketOut.header |= HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_ACQUIRE_FENCE_SCOPE;
    aqlPacketOut.header |= HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_RELEASE_FENCE_SCOPE;
    aqlPacketOut.setup |= 1 << HSA_KERNEL_DISPATCH_PACKET_SETUP_DIMENSIONS;

    aqlPacketOut.workgroup_size_x = 1;
    aqlPacketOut.workgroup_size_y = 1;
    aqlPacketOut.workgroup_size_z = 1;
    aqlPacketOut.grid_size_x = 1;
    aqlPacketOut.grid_size_y = 1;
    aqlPacketOut.grid_size_z = 1;

    return ret;
}

// ------------------ Definitions of HSAKernelArgBuffer ------------------------

HSAKernelArgBuffer::HSAKernelArgBuffer() :
    m_pArgBuffer(nullptr),
    m_argBufferSize(0),
    m_location(0)
{
}

HSAKernelArgBuffer::~HSAKernelArgBuffer()
{
    this->DestroyBuffer();
}

bool HSAKernelArgBuffer::AllocateBuffer(
    const std::size_t bufSize,
    const std::size_t offsetSize,
    const std::size_t clearedValue)
{
    if (nullptr == m_pArgBuffer)
    {
        m_argBufferSize = bufSize;
        m_pArgBuffer = HSAResourceManager::AllocateSysMemory(m_argBufferSize);

        if (nullptr == m_pArgBuffer)
        {
            std::cerr << "Error in AllocateBuffer(): Fail to AllocateSysMemory for argument buffer.\n";
            return false;
        }

        // Need to add arguments offsets.
        // They are the OpenCL compiler generated offsets, can be zero.
        m_location += offsetSize;
        memset(m_pArgBuffer, static_cast<int>(clearedValue), m_argBufferSize);
    }
    else
    {
        std::cerr << "Error in AllocateBuffer(): Please call DestroyBuffer() first before re-using it.\n";
        return false;
    }

    return true;
}

bool HSAKernelArgBuffer::DestroyBuffer()
{
    bool ret = HSAResourceManager::FreeHSAMemory(m_pArgBuffer);
    m_pArgBuffer = nullptr;
    m_argBufferSize = 0;
    m_location = 0;
    return ret;
}

bool HSAKernelArgBuffer::AppendKernelArgs(const void* pAddr, const std::size_t size)
{
    if (nullptr == m_pArgBuffer)
    {
        std::cerr << "Error in AppendKernelArgs(): Please call AllocateBuffer() first.\n";
        return false;
    }

    if (nullptr == pAddr)
    {
        std::cerr << "Error in AppendKernelArgs(): Address of input arguments is nullptr.\n";
        return false;
    }

    if (m_location + size > m_argBufferSize)
    {
        std::cerr << "Error in AppendKernelArgs(): Exceed argument buffer size.\n";
        return false;
    }

    memcpy(reinterpret_cast<unsigned char*>(m_pArgBuffer) + m_location, pAddr, size);
    m_location += size;

    return true;
}

void* HSAKernelArgBuffer::GetArgBufferPointer() const
{
    return m_pArgBuffer;
}
// ----------------- End of Definitions of HSAKernelArgBuffer ------------------

/// \brief Output device type as string
///
/// \param[in] dt Device type
/// \return Device type as string
static std::string ConvertDeviceTypeToString(hsa_device_type_t dt)
{
    std::string ret;

    switch (dt)
    {
        case HSA_DEVICE_TYPE_CPU:
            ret = "CPU";
            break;

        case HSA_DEVICE_TYPE_GPU:
            ret = "GPU";
            break;

        case HSA_DEVICE_TYPE_DSP:
            ret = "DSP";
            break;

        default:
            ret = "Unknown";
            break;
    }

    return ret;
}

hsa_status_t QueryDevice_Callback(hsa_agent_t agent, void* pData)
{
    if (pData == nullptr)
    {
        std::cerr << "QueryDevice_Callback: pData cannot be nullptr.\n";
        return HSA_STATUS_ERROR_INVALID_ARGUMENT;
    }

    hsa_device_type_t deviceType;
    hsa_status_t err;
    AgentList* pAgentList = reinterpret_cast<AgentList*>(pData);
    AgentInfo agentInfo;

    agentInfo.m_device = agent;
    // Query type of device
    err = hsa_agent_get_info(agent, HSA_AGENT_INFO_DEVICE, &deviceType);

    if (!HSA_CHECK_STATUS(err))
    {
        std::cerr << "Error in QueryDevice_Callback(): Obtaining device type failed.\n";
    }

    // Query chip id
    err = hsa_agent_get_info(agent, static_cast<hsa_agent_info_t>(HSA_AMD_AGENT_INFO_CHIP_ID), &agentInfo.m_chipID);

    if (!HSA_CHECK_STATUS(err))
    {
        std::cerr << "Error in InitRuntime(): Obtaining chip id failed.\n";
        agentInfo.m_chipID = 0;
    }

    // Query HSA profile of the agent.
    err = hsa_agent_get_info(agent, HSA_AGENT_INFO_PROFILE, &agentInfo.m_profile);

    if (!HSA_CHECK_STATUS(err))
    {
        std::cerr << "Error in InitRuntime(): Obtaining hsa profile failed.\n";
    }

    switch (deviceType)
    {
        case HSA_DEVICE_TYPE_CPU:
            pAgentList->m_vecCPU.push_back(agentInfo);
            break;

        case HSA_DEVICE_TYPE_GPU:
            pAgentList->m_vecGPU.push_back(agentInfo);
            break;

        default:
            break;
    }

    if (gs_bVerbosePrint)
    {
        char deviceName[64];
        memset(deviceName, '\0', sizeof(deviceName));
        // Query the name of device
        err = hsa_agent_get_info(agent, HSA_AGENT_INFO_NAME, deviceName);

        if (!HSA_CHECK_STATUS(err))
        {
            std::cerr << "Error in QueryDevice_Callback(): Obtaining device name failed.\n";
        }

        std::cout << "HSA device attributes:\n";
        std::cout << "\tname: " << deviceName << "\n";
        std::cout << "\ttype: " << ConvertDeviceTypeToString(deviceType) << "\n";
        std::cout << std::hex << "\tchip ID: 0x" << agentInfo.m_chipID << "\n";
        std::cout << std::dec; // reset digit output format.
        std::cout << "\tHSA profile: " << ((agentInfo.m_profile == 0) ? ("Base") : ("Full")) << "\n";
    }

    return err;
}

hsa_status_t FindMemRegions_Callback(hsa_region_t region, void* data)
{
    if (nullptr == data)
    {
        std::cerr << "FindMemRegions(): data cannot be nullptr.\n";
        return HSA_STATUS_ERROR;
    }

    hsa_region_global_flag_t flags;
    hsa_region_segment_t segment_id;

    hsa_status_t status = hsa_region_get_info(region, HSA_REGION_INFO_SEGMENT, &segment_id);
    bool ret = HSA_CHECK_STATUS(status);

    if (!ret)
    {
        return status;
    }

    if (HSA_REGION_SEGMENT_GLOBAL != segment_id)
    {
        return HSA_STATUS_SUCCESS;
    }

    AgentInfo* pRegion = reinterpret_cast<AgentInfo*>(data);
    status = hsa_region_get_info(region, HSA_REGION_INFO_GLOBAL_FLAGS, &flags);
    ret = HSA_CHECK_STATUS(status);

    if (!ret)
    {
        return status;
    }

    if (flags & HSA_REGION_GLOBAL_FLAG_COARSE_GRAINED)
    {
        pRegion->coarseRegion = region;
    }

    if (flags & HSA_REGION_GLOBAL_FLAG_KERNARG)
    {
        pRegion->kernargRegion = region;
    }

    if (flags & HSA_REGION_GLOBAL_FLAG_FINE_GRAINED)
    {
        pRegion->fineRegion = region;
    }

    return HSA_STATUS_SUCCESS;
}

bool HsaCheckStatus(hsa_status_t s)
{
    bool ret = true;

    if (HSA_STATUS_SUCCESS == s)
    {
        ret = true;
    }
    else
    {
        ret = false;
        std::cerr << "\nHSA status is not HSA_STATUS_SUCCESS.\n";
        std::cerr << HsaStatusStrings(s) << "\n";
    }

    return ret;
}

bool HsaCheckStatus(hsa_status_t s, const std::string& fileName, int lineNum)
{
    bool ret = true;

    if (HSA_STATUS_SUCCESS == s)
    {
        ret = true;
    }
    else
    {
        ret = false;
        std::cout << "In " << fileName << ", line " << lineNum << "\n";
        std::cerr << "HSA status is not HSA_STATUS_SUCCESS.\n";
        std::cerr << "Error code: " << s << ".\n";
        std::cerr << HsaStatusStrings(s) << ".\n";
    }

    return ret;
}

std::string HsaStatusStrings(hsa_status_t s)
{
    const char* pszbuff = nullptr;
    hsa_status_string(s, &pszbuff);

    std::string str;

    if (pszbuff != nullptr)
    {
        str = pszbuff;
    }
    else
    {
        str = "hsa_status_string return nullptr string. Input HSA status code: " + std::to_string(static_cast<int>(s));
    }

    return str;
}

#if defined(_WIN32) || defined(_WIN64)
// setenv() and unsetenv() wrapper
static int setenv(const char* name, const char* value, int overwrite)
{
    int ret = 0;
    char* pBuffer = nullptr;
    size_t len = 0;
    errno_t err = _dupenv_s(&pBuffer, &len, name);

    if (err)
    {
        return 1;
    }

    if (nullptr == pBuffer || 1 == overwrite)
    {
        ret = _putenv_s(name, value);
    }

    return ret;
}

static int unsetenv(const char* name)
{
    int ret = 0;
    char* pBuffer = nullptr;
    size_t len = 0;
    errno_t err = _dupenv_s(&pBuffer, &len, name);

    if (err)
    {
        return 1;
    }

    if (nullptr != pBuffer)
    {
        ret = _putenv_s(name, "");
    }

    return ret;
}

// GetEnv() wrapper
static const std::string GetEnv(const std::string& name)
{
    std::string value = "";
    char* pBuffer = nullptr;
    size_t len = 0;
    errno_t err = _dupenv_s(&pBuffer, &len, name.c_str());

    if (err)
    {
        return value;
    }

    if (nullptr != pBuffer)
    {
        value = pBuffer;
    }
    return value;
}

#else // Linux platform
// GetEnv() wrapper
static const std::string GetEnv(const std::string& name)
{
    std::string value = "";
    char* pBuffer = getenv(name.c_str());

    if (nullptr != pBuffer)
    {
        value = pBuffer;
    }

    return value;
}
#endif

bool SetSoftCPMode(bool bEnable, bool verbosePrint)
{
    using namespace std;
    bool ret = true;
    const string emulateStr = "HSA_EMULATE_AQL";
    const string toolsLibStr = "HSA_TOOLS_LIB";

    if (bEnable)
    {
        string emulateVar = "1";

#ifdef _WIN64
        string toolsLibVar = "hsa-runtime-tools64.dll";
#elif defined(_WIN32)
        string toolsLibVar = "hsa-runtime-tools.dll";
#else   // Only Linux x64 platform is supported for now. 03/18/2015
        string toolsLibVar = "libhsa-runtime-tools64.so.1";
#endif

        if (0 != setenv(emulateStr.c_str(), emulateVar.c_str(), 1))
        {
            cerr << "Error in setting " << emulateStr << "\n";
            ret &= false;
        }

        if (0 != setenv(toolsLibStr.c_str(), toolsLibVar.c_str(), 1))
        {
            cerr << "Error in setting " << toolsLibStr << "\n";
            ret &= false;
        }
    }
    else
    {
        if (0 != unsetenv(emulateStr.c_str()))
        {
            cerr << "Error in unsetting " << emulateStr << "\n";
            ret &= false;
        }

        if (0 != unsetenv(toolsLibStr.c_str()))
        {
            cerr << "Error in unsetting " << toolsLibStr << "\n";
            ret &= false;
        }
    }

    // Print out the environment variable just set/unset.
    if (verbosePrint)
    {
        std::string value = GetEnv(emulateStr);

        if (value != "")
        {
            cout << "Set " << emulateStr << " = " << value << "\n";
        }
        else
        {
            cout << emulateStr << " is unset.\n";
        }

        value = GetEnv(toolsLibStr);

        if (value != "")
        {
            cout << "Set " << toolsLibStr << " = " << value << "\n";
        }
        else
        {
            cout << toolsLibStr << " is unset.\n";
        }
    }

    return ret;
}

} // namespace AMDT
