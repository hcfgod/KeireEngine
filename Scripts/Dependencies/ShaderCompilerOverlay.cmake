# Apply the first-party host CLI integration to generated build-tree copies, never to Vendor sources.
if(NOT PROJECT_NAME STREQUAL "SDL3_shadercross")
    return()
endif()

function(keire_replace_once variable original replacement)
    string(FIND "${${variable}}" "${original}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Pinned shader compiler overlay no longer matches its upstream input")
    endif()
    string(LENGTH "${original}" original_length)
    math(EXPR remainder_start "${position} + ${original_length}")
    string(SUBSTRING "${${variable}}" ${remainder_start} -1 remainder)
    string(FIND "${remainder}" "${original}" duplicate)
    if(NOT duplicate EQUAL -1)
        message(FATAL_ERROR "Pinned shader compiler overlay has an ambiguous upstream input")
    endif()
    string(REPLACE "${original}" "${replacement}" result "${${variable}}")
    set(${variable} "${result}" PARENT_SCOPE)
endfunction()

function(keire_shader_compiler_overlay)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/KeireHostOverlay")
    file(MAKE_DIRECTORY "${output}")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/cli.c" cli)
    string(REPLACE "\r\n" "\n" cli "${cli}")
    keire_replace_once(cli "    char *includeDir = NULL;" [=[    const char *includeDirectories[17] = {0};
    size_t includeDirectoryCount = 0;]=])
    keire_replace_once(cli [=[                if (includeDir) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "'%s' can only be used once", arg);]=]
        [=[                if (includeDirectoryCount >= 17) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "At most 17 ordered include directories are supported");]=])
    keire_replace_once(cli "                includeDir = argv[i];"
        "                includeDirectories[includeDirectoryCount++] = argv[i];")
    keire_replace_once(cli "        hlslInfo.include_dir = includeDir;" "        hlslInfo.include_dir = NULL;")
    keire_replace_once(cli "        hlslInfo.props = SDL_CreateProperties();" [=[        hlslInfo.props = SDL_CreateProperties();
        SDL_SetPointerProperty(hlslInfo.props, "Keire.IncludeDirectories", (void *)includeDirectories);
        SDL_SetNumberProperty(hlslInfo.props, "Keire.IncludeDirectoryCount", (Sint64)includeDirectoryCount);]=])

    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/SDL_shadercross.c" library)
    string(REPLACE "\r\n" "\n" library "${library}")
    keire_replace_once(library "#define MAX_DEFINES 64" "#define MAX_DEFINES 129")
    keire_replace_once(library "#define MAX_DEFINE_STRING_LENGTH 256" "#define MAX_DEFINE_STRING_LENGTH 1024")
    keire_replace_once(library "    wchar_t *includeDirUtf16 = NULL;" [=[    wchar_t *includeDirUtf16 = NULL;
    wchar_t *includeDirectoriesUtf16[17] = {0};
    const char *const *includeDirectories = SDL_GetPointerProperty(info->props, "Keire.IncludeDirectories", NULL);
    Sint64 includeDirectoryCount = SDL_GetNumberProperty(info->props, "Keire.IncludeDirectoryCount", 0);
    if (includeDirectoryCount < 0 || includeDirectoryCount > 17 || (includeDirectoryCount && !includeDirectories)) {
        SDL_SetError("Invalid ordered include directory inventory");
        return NULL;
    }]=])
    keire_replace_once(library "    if (info->defines != NULL) {" [=[    for (Sint64 i = 0; i < includeDirectoryCount; ++i) {
        if (includeDirectories[i] != NULL) {
            includeDirectoriesUtf16[i] = (wchar_t *)SDL_iconv_string("WCHAR_T", "UTF-8", includeDirectories[i], SDL_utf8strlen(includeDirectories[i]) + 1);
        }
        if (includeDirectoriesUtf16[i] == NULL) {
            for (Sint64 previous = 0; previous < i; ++previous) {
                SDL_free(includeDirectoriesUtf16[previous]);
            }
            SDL_free(entryPointUtf16);
            /* Upstream types this opaque COM interface as void; every interface has the IUnknown prefix. */
            ((IDxcUtils *)includeHandler)->lpVtbl->Release(includeHandler);
            utils->lpVtbl->Release(utils);
            dxcInstance->lpVtbl->Release(dxcInstance);
            SDL_SetError("Failed to convert an ordered include directory to WCHAR_T");
            return NULL;
        }
    }

    if (info->defines != NULL) {]=])
    keire_replace_once(library "(numDefineStrings + 13)" "(numDefineStrings + 13 + includeDirectoryCount * 2)")
    keire_replace_once(library "    source.Ptr = info->source;" [=[    for (Sint64 i = 0; i < includeDirectoryCount; ++i) {
        args[argCount++] = L"-I";
        args[argCount++] = includeDirectoriesUtf16[i];
    }

    source.Ptr = info->source;]=])
    # Both the legacy include-directory conversion error path and normal completion release these allocations.
    string(REPLACE "SDL_free(defineStringsUtf16);" [=[SDL_free(defineStringsUtf16);
    for (Sint64 i = 0; i < includeDirectoryCount; ++i) {
        SDL_free(includeDirectoriesUtf16[i]);
    }]=] library "${library}")

    file(CONFIGURE OUTPUT "${output}/cli.c" CONTENT "${cli}" @ONLY)
    file(CONFIGURE OUTPUT "${output}/SDL_shadercross.c" CONTENT "${library}" @ONLY)
    foreach(target IN ITEMS shadercross SDL3_shadercross-static SDL3_shadercross-shared)
        if(TARGET ${target})
            get_target_property(sources ${target} SOURCES)
            list(TRANSFORM sources REPLACE "^src/cli[.]c$" "${output}/cli.c")
            list(TRANSFORM sources REPLACE "^src/SDL_shadercross[.]c$" "${output}/SDL_shadercross.c")
            set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
        endif()
    endforeach()
endfunction()

cmake_language(DEFER CALL keire_shader_compiler_overlay)
