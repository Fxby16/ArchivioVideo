workspace "ArchivioVideo"
    configurations { "Debug", "Release" }
    platforms { "x64" }

project "ArchivioVideoServer"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    targetdir "bin/%{cfg.buildcfg}"
    objdir "obj/%{cfg.buildcfg}"

    files { "Source/**.cpp", "Source/**.hpp" }
    includedirs { "Source" }

    links { "civetweb" }

    filter "system:windows"
        includedirs { "Dependencies/tdlib/include", "Dependencies/MySQL Connector C 6.1/include" }
        libdirs { "Dependencies/tdlib/lib/Release", "Dependencies/MySQL Connector C 6.1/lib" }

        links {
            "libcurl",
            "libmysql",
            "tdjson_static",
            "tdjson_private",
            "tdclient",
            "tdcore",
            "tdactor",
            "tdsqlite",
            "tddb",
            "tde2e",
            "tdnet",
            "tdutils",
            "tdtl",
            "tdapi",
            "tdmtproto",
            "Ws2_32",
            "Crypt32",
            "Psapi",
            "Normaliz"
        }

        defines { "TDJSON_STATIC_DEFINE", "TD_ENABLE_STATIC", "TDJSON_STATIC_LIBRARY" }

    filter "system:linux"
        links { "tdjson", "mysqlclient", "curl" }

    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"