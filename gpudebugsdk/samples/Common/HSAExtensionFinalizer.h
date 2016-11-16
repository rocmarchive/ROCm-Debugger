//==============================================================================
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief  Data structure to keep HSA extension function pointer table, and call hsa_ext_* through it.
//==============================================================================

#ifndef HSA_EXTENSION_FINALIZER_H_
#define HSA_EXTENSION_FINALIZER_H_

#include <hsa.h>

#ifdef USE_EXISTING_FINALIZER_EXTENSION
    #include <hsa_ext_finalize.h>
#else
    // \todo: Add these struct definitions here.
    struct hsa_ext_program_t;

    struct hsa_ext_program_info_t;

    struct hsa_ext_control_directives_t;

    struct BrigModuleHeader;
    typedef struct BrigModuleHeader* BrigModule_t;

    typedef BrigModule_t hsa_ext_module_t;
#endif

namespace AMDT
{
class HSAFinalizer
{
private:
    typedef struct HSAFinalizerTable
    {
        hsa_status_t(*hsa_ext_program_create)(
            hsa_machine_model_t machine_model,
            hsa_profile_t profile,
            hsa_default_float_rounding_mode_t default_float_rounding_mode,
            const char* options,
            hsa_ext_program_t* program);

        hsa_status_t(*hsa_ext_program_destroy)(hsa_ext_program_t program);

        hsa_status_t(*hsa_ext_program_add_module)(hsa_ext_program_t program,
                                                  hsa_ext_module_t module);

        hsa_status_t(*hsa_ext_program_iterate_modules)(
            hsa_ext_program_t program,
            hsa_status_t(*callback)(hsa_ext_program_t program,
                                    hsa_ext_module_t module, void* data),
            void* data);

        hsa_status_t(*hsa_ext_program_get_info)(
            hsa_ext_program_t program, hsa_ext_program_info_t attribute,
            void* value);

        hsa_status_t(*hsa_ext_program_finalize)(
            hsa_ext_program_t program, hsa_isa_t isa, int32_t call_convention,
            hsa_ext_control_directives_t control_directives, const char* options,
            hsa_code_object_type_t code_object_type, hsa_code_object_t* code_object);
    } HSAFinalizerTable;

public:
    HSAFinalizer();
    ~HSAFinalizer();

    bool GetExtensionTable(const uint16_t& major, const uint16_t& minor);

    HSAFinalizerTable* m_pTable;
};

}// namespace AMDT
#endif // HSA_EXTENSION_FINALIZER_H_
