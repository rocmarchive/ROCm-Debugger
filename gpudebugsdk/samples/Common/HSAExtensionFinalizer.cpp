//==============================================================================
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief  Data structure to keep HSA extension function pointer table, and call hsa_ext_* through it.
//==============================================================================

#include <cstring> // memset
#include <iostream>

#include "HSAExtensionFinalizer.h"

namespace AMDT
{
static bool GetExtensionTable(const uint16_t& extension, const uint16_t& major, const uint16_t minor, void* pTable)
{
    bool isSupported = false;
    hsa_status_t status = hsa_system_extension_supported(extension, major, minor, &isSupported);

    if (HSA_STATUS_SUCCESS != status || true != isSupported)
    {
        if (HSA_STATUS_ERROR_NOT_INITIALIZED == status)
        {
            std::cerr << "HSA haven't initialized yet.\n";
            return false;
        }

        if (HSA_STATUS_ERROR_INVALID_ARGUMENT == status)
        {
            std::cerr << "Extension 0x" << std::hex << extension << " is not a valid extension.\n";
            std::cerr << std::dec;
            return false;
        }

        std::cerr << "HSA Runtime " << major << "." << minor << " doesn't support this extension: " << extension << ".\n";
        return false;
    }

    status = hsa_system_get_extension_table(extension, major, minor, pTable);

    if (HSA_STATUS_SUCCESS != status)
    {
        std::cerr << "Get extension functions table failed. Extension: " << extension << "\n";
        return false;
    }

    return true;
}

HSAFinalizer::HSAFinalizer() : m_pTable(nullptr)
{
}

HSAFinalizer::~HSAFinalizer()
{
    if (nullptr != m_pTable)
    {
        delete m_pTable;
        m_pTable = nullptr;
    }
}

bool HSAFinalizer::GetExtensionTable(const uint16_t& major, const uint16_t& minor)
{
    if (nullptr == m_pTable)
    {
        m_pTable = new HSAFinalizerTable;

        if (nullptr == m_pTable)
        {
            std::cerr << "Cannot allocate Finalizer functions table.\n";
            return false;
        }

        memset(m_pTable, 0, sizeof(*m_pTable));
    }

    bool ret = AMDT::GetExtensionTable(HSA_EXTENSION_FINALIZER, major, minor, m_pTable);

    if (!ret)
    {
        std::cerr << "Fail to get finalizer extension table.\n";
        return false;
    }

    return true;
}
}// namespace AMDT
