#include <Windows.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    using Microsoft::WRL::ComPtr;

    void RequireSuccess(const HRESULT result, const char* operation)
    {
        if (FAILED(result))
        {
            throw std::runtime_error(operation);
        }
    }

    struct LibraryHandle
    {
        HMODULE Value;

        ~LibraryHandle()
        {
            if (Value)
            {
                FreeLibrary(Value);
            }
        }
    };
} // namespace

int wmain(const int argumentCount, wchar_t* arguments[])
{
    try
    {
        if (argumentCount != 4)
        {
            throw std::runtime_error("Usage: dxil-disassemble <dxcompiler.dll> <shader.dxil> <output.txt>");
        }
        LibraryHandle library{
            LoadLibraryExW(arguments[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)};
        if (!library.Value)
        {
            throw std::runtime_error("Cannot load the requested DXC library.");
        }
        const auto createInstance =
            reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(library.Value, "DxcCreateInstance"));
        if (!createInstance)
        {
            throw std::runtime_error("DXC library does not export DxcCreateInstance.");
        }
        ComPtr<IDxcUtils> utilities;
        RequireSuccess(createInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utilities)), "Cannot create DXC utilities.");
        ComPtr<IDxcCompiler3> compiler;
        RequireSuccess(createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)), "Cannot create DXC compiler.");
        ComPtr<IDxcBlobEncoding> shader;
        RequireSuccess(utilities->LoadFile(arguments[2], nullptr, &shader), "Cannot read DXIL input.");
        const DxcBuffer buffer{shader->GetBufferPointer(), shader->GetBufferSize(), 0};
        ComPtr<IDxcResult> result;
        RequireSuccess(compiler->Disassemble(&buffer, IID_PPV_ARGS(&result)), "DXIL disassembly failed.");
        HRESULT status;
        RequireSuccess(result->GetStatus(&status), "Cannot obtain DXIL disassembly status.");
        RequireSuccess(status, "DXIL disassembly reported an error.");
        ComPtr<IDxcBlobUtf8> disassembly;
        RequireSuccess(result->GetOutput(DXC_OUT_DISASSEMBLY, IID_PPV_ARGS(&disassembly), nullptr),
                       "DXIL disassembly is missing.");
        std::ofstream output(arguments[3], std::ios::binary);
        output.write(disassembly->GetStringPointer(), static_cast<std::streamsize>(disassembly->GetStringLength()));
        if (!output)
        {
            throw std::runtime_error("Cannot write disassembly output.");
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
