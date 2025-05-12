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
    includedirs { "Source", "Dependencies" }

    filter "system:windows"
        includedirs { "Dependencies/Windows/tdlib/include", "Dependencies/Windows/MySQL Connector C 6.1/include" }
        libdirs { "Dependencies/Windows/tdlib/lib/Release", "Dependencies/Windows/MySQL Connector C 6.1/lib" }

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
            "tdapi",
            "tdmtproto",
            "Ws2_32",
            "Crypt32",
            "Psapi",
            "Normaliz",
            "libssl",
            "libcrypto",
            "avformat",
            "avutil"
        }

        defines { "TDJSON_STATIC_DEFINE", "TD_ENABLE_STATIC", "TDJSON_STATIC_LIBRARY" }

        postbuildcommands {
            '{COPY} "Dependencies/dlls/*" "%{cfg.targetdir}"',
            '{COPY} "./*.pem" "%{cfg.targetdir}"'
        }

    filter "system:linux"
        libdirs { "Dependencies/Linux/ffmpeg-static/lib", "Dependencies/Linux/td" }
        includedirs { "Dependencies/Linux/ffmpeg-static/include", "Dependencies/Linux/td" }
        links { 
            "tdjson", 
            "mysqlclient", 
            "curl", 
            "ssl", 
            "crypto", 
            "avformat", 
            "avcodec", 
            "avutil", 
            "swresample",
            "lzma",
            "drm",
            "X11", 
            "vdpau",
            "va",
            "va-drm",
            "va-x11", 
            "z", 
            "bz2" 
        }

        postbuildcommands {
            '{COPY} "./*.pem" "%{cfg.targetdir}"'
        }

    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"