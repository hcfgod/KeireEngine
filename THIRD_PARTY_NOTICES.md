# Third-Party Notices

This project uses the following third-party dependencies:

- spdlog is a runtime header dependency, copyright Gabi Melman and contributors, under the MIT License. See `Vendor/spdlog/LICENSE`.
- fmt is bundled by spdlog and distributed with its headers, copyright Victor Zverovich and contributors, under the MIT License. See `Vendor/spdlog/include/spdlog/fmt/bundled/fmt.license.rst`.
- doctest is a test-only header dependency, copyright Viktor Kirilov and contributors, under the MIT License. See `Vendor/doctest/LICENSE.txt`.
- SDL 3 is a statically linked platform dependency, copyright Sam Lantinga and contributors, under the zlib License. See `Vendor/SDL/LICENSE.txt`.
- JSON for Modern C++ is an implementation-only configuration dependency, copyright Niels Lohmann and contributors, under the MIT License. See `Vendor/json/LICENSE.MIT`.
- Dear ImGui is a privately compiled KeireCore UI implementation dependency, copyright Omar Cornut and contributors, under the MIT License. See `Vendor/imgui/LICENSE.txt`.

SDK archives include these license texts under `third-party/licenses`, including the doctest license for complete project attribution even though doctest headers are not redistributed.
SDL headers, its static archive, and its official CMake package are redistributed under `third-party/SDL3`; nlohmann/json and Dear ImGui headers are not redistributed because neither dependency crosses Kéire's public API.
