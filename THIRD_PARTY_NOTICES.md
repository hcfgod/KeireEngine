# Third-Party Notices

This project uses the following third-party dependencies:

- spdlog is a private logging backend, copyright Gabi Melman and contributors, under the MIT License. See `Vendor/spdlog/LICENSE`.
- fmt is bundled privately by spdlog, copyright Victor Zverovich and contributors, under the MIT License. See `Vendor/spdlog/include/spdlog/fmt/bundled/fmt.license.rst`.
- doctest is a test-only header dependency, copyright Viktor Kirilov and contributors, under the MIT License. See `Vendor/doctest/LICENSE.txt`.
- SDL 3 is a statically linked platform dependency, copyright Sam Lantinga and contributors, under the zlib License. See `Vendor/SDL/LICENSE.txt`.
- JSON for Modern C++ is an implementation-only configuration dependency, copyright Niels Lohmann and contributors, under the MIT License. See `Vendor/json/LICENSE.MIT`.
- Dear ImGui is a privately compiled UI implementation dependency, copyright Omar Cornut and contributors, under the MIT License. See `Vendor/imgui/LICENSE.txt`.
- Zstandard is a privately compiled asset compression dependency, copyright Meta Platforms, Inc. and contributors, under the BSD 3-Clause License. See `Vendor/zstd/LICENSE`.
- EnTT is a private header-only ECS storage dependency, copyright Michele Caini and contributors, under the MIT License. See `Vendor/entt/LICENSE`.
- GLM is a private header-only mathematics implementation dependency, copyright G-Truc Creation, under the Happy Bunny License or MIT License. See `Vendor/glm/copying.txt`.
- SDL_shadercross is the host-side shader compiler frontend, copyright Sam Lantinga and contributors, under the zlib License. See `Vendor/SDL_shadercross/LICENSE.txt`.
- DirectX Shader Compiler is used by the packaged host shader compiler, copyright Microsoft Corporation and contributors, under the University of Illinois/NCSA Open Source License. See `Vendor/SDL_shadercross/external/DirectXShaderCompiler/LICENSE.TXT` and its `ThirdPartyNotices.txt`.
- SPIRV-Cross is used by the host shader compiler for cross-compilation and reflection, copyright the Khronos Group and contributors, under the Apache License 2.0. See `Vendor/SDL_shadercross/external/SPIRV-Cross/LICENSE`.
- SPIRV-Headers and SPIRV-Tools are used by the host shader compiler, copyright the Khronos Group and contributors, under the Apache License 2.0. See their `LICENSE` files below `Vendor/SDL_shadercross/external`.
- Assimp is a privately linked static-mesh importer, copyright the Assimp project contributors, under the BSD 3-Clause License. See `Vendor/assimp/LICENSE`.
- zlib is bundled and linked privately by Assimp, copyright Jean-loup Gailly and Mark Adler, under the zlib License. See `Vendor/assimp/contrib/zlib/LICENSE`.
- stb_image is a private image decoder from stb, by Sean Barrett and contributors, under the MIT License or public domain dedication. See `Vendor/stb/LICENSE`.

SDK archives include these license texts under `third-party/licenses`, including the doctest license for complete project attribution even though doctest headers are not redistributed. SDL headers, its static archive, and its official CMake package are redistributed under `third-party/SDL3`; the host shader compiler and required runtime libraries are redistributed under `bin`. Assimp and its private zlib are redistributed only as static link dependencies under `lib`. nlohmann/json, spdlog/fmt, Dear ImGui, Zstandard, EnTT, GLM, stb, Assimp headers, and shader compiler source trees are not redistributed because those implementation dependencies do not cross Kéire's public API.
