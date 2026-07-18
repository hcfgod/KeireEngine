project(ZstdProject)
    location "../../Build/Projects/Zstd"
    kind "StaticLib"
    targetname(ZstdLibrary)

    ApplyCommonProjectSettings("../..")

    files
    {
        "Zstd.lua",
        "../../Vendor/zstd/lib/**.h",
        "../../Vendor/zstd/lib/common/**.c",
        "../../Vendor/zstd/lib/compress/**.c",
        "../../Vendor/zstd/lib/decompress/**.c",
        "../../Vendor/zstd/lib/dictBuilder/**.c"
    }

    removefiles { "../../Vendor/zstd/lib/decompress/huf_decompress_amd64.S" }

    vpaths
    {
        ["Common/*"] = { "../../Vendor/zstd/lib/common/**" },
        ["Compression/*"] = { "../../Vendor/zstd/lib/compress/**" },
        ["Decompression/*"] = { "../../Vendor/zstd/lib/decompress/**" },
        ["Dictionary/*"] = { "../../Vendor/zstd/lib/dictBuilder/**" },
        ["Public/*"] = { "../../Vendor/zstd/lib/*.h" },
        ["Build/*"] = { "Zstd.lua" }
    }

    externalincludedirs { "../../Vendor/zstd/lib" }
    defines { "ZSTD_DISABLE_ASM" }
    warnings "Off"

    filter {}
