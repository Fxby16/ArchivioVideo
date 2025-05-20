# ArchivioVideo

## Support
#### Linux
Supports all distributions, independent of the package manager or the compiler used.
#### Windows 
Requires MSVC. Mingw is not supported.

## Dependencies
### Preinstalled
#### Linux
- ffmpeg
- tdlib
- httplib
#### Windows 
- MYSQL C Connector
- nlohmann json
- tdlib
- httplib

### Required (need to be installed manually)
#### Linux
- openssl
- curl
- mysqlclient
- zlib, bzip2
- liblzma
- libdrm, libvdpau, libva, libva-drm, libva-x11, libX11 
#### Windows 
- OpenSSL
- FFmpeg
- libcurl

### Installation
#### Linux
`sudo pacman -Sy openssl curl mariadb-libs zlib bzip2 xz libdrm libvdpau libva libva-utils libx11`
#### Windows  
`vcpkg install openssl curl ffmpeg`

## Build
### Linux
```bash
cd Server
premake5 gmake2
make config={Debug,Release}
```
### Windows
```bash
cd Server
premake5 vs2022
```
Open the solution in Visual Studio and build the project (only Release is supported at the moment).

## Run
See [RUN.md](RUN.md) for instructions on how to run the server and client.

## Notes
The server listens on port 10000 HTTPS and 10001 HTTP.