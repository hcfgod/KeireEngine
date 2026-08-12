#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

namespace KeireRenderTests
{
    [[nodiscard]] bool ProbeRenderedOutput(std::string& diagnostic) noexcept;
}

namespace
{
    constexpr int SkippedExitCode = 77;

    [[nodiscard]] constexpr std::string_view NormalizeGpuBackend(const std::string_view backend) noexcept
    {
        return backend == "d3d12" ? std::string_view{"direct3d12"} : backend;
    }

    TEST_CASE("render test GPU backend aliases use canonical SDL driver names")
    {
        CHECK(NormalizeGpuBackend("d3d12") == "direct3d12");
        CHECK(NormalizeGpuBackend("direct3d12") == "direct3d12");
        CHECK(NormalizeGpuBackend("vulkan") == "vulkan");
        CHECK(NormalizeGpuBackend("metal") == "metal");
    }

    [[nodiscard]] bool BackendAvailable(const char* backend)
    {
        if (!SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, backend, SDL_HINT_OVERRIDE))
            return false;
        if (!SDL_Init(SDL_INIT_VIDEO))
            return false;

        SDL_Window* window = SDL_CreateWindow("Kéire render test probe", 64, 64, SDL_WINDOW_HIDDEN);
        constexpr auto formats =
            static_cast<SDL_GPUShaderFormat>(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                             SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB);
        SDL_GPUDevice* device = window ? SDL_CreateGPUDevice(formats, false, backend) : nullptr;
        const bool claimed = device && window && SDL_ClaimWindowForGPUDevice(device, window);
        if (claimed)
            SDL_ReleaseWindowFromGPUDevice(device, window);
        if (device)
            SDL_DestroyGPUDevice(device);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
        return claimed;
    }
} // namespace

int main(const int argc, char** argv)
{
    const char* configuredBackend = SDL_getenv("KEIRE_GPU_TEST_BACKEND");
    const std::string backend{NormalizeGpuBackend(configuredBackend ? configuredBackend : "")};
    if (backend.empty())
    {
        std::cout << "GPU render tests skipped: KEIRE_GPU_TEST_BACKEND is not set.\n";
        return SkippedExitCode;
    }
    // This runs before SDL or any test worker starts. The process-level update is intentional because SDL recreates
    // its environment after each application shutdown, and every recreation must inherit the canonical driver name.
    if (SDL_setenv_unsafe("KEIRE_GPU_TEST_BACKEND", backend.c_str(), 1) != 0)
    {
        std::cout << "GPU render tests failed: could not publish canonical backend '" << backend
                  << "' to the test environment.\n";
        return 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--probe") == 0)
    {
        if (!BackendAvailable(backend.c_str()))
        {
            std::cout << "GPU render tests skipped: backend '" << backend << "' is unavailable: " << SDL_GetError()
                      << '\n';
            return SkippedExitCode;
        }
        if (!SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, backend.c_str(), SDL_HINT_OVERRIDE))
            return 1;
        std::string diagnostic;
        if (!KeireRenderTests::ProbeRenderedOutput(diagnostic))
        {
            std::cout << "GPU render tests skipped: backend '" << backend
                      << "' cannot complete a Kéire render/readback: " << diagnostic << '\n';
            return SkippedExitCode;
        }
        return 0;
    }
    if (!SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, backend.c_str(), SDL_HINT_OVERRIDE))
        return 1;

    std::cout << "Running GPU render tests with backend '" << backend << "'.\n";
    doctest::Context context(argc, argv);
    return context.run();
}
