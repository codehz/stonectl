cmake_minimum_required(VERSION 3.9)

project(stonectl)

include(ExternalProject)

set(BUILD_SHARED_LIBS OFF)
option(BUILD_STATIC_STONECTL OFF)

add_subdirectory(deps/wsrpc EXCLUDE_FROM_ALL)
add_subdirectory(deps/CLI11 EXCLUDE_FROM_ALL)
add_subdirectory(deps/api EXCLUDE_FROM_ALL)

set(ROOTFS ${CMAKE_BINARY_DIR}/rootfs)
file(MAKE_DIRECTORY ${ROOTFS}/include)

ExternalProject_Add(mbedtls_ep
  PREFIX deps/mbedtls
  INSTALL_DIR ${ROOTFS}
  URL https://tls.mbed.org/download/mbedtls-2.16.2-gpl.tgz
  UPDATE_COMMAND ""
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
)

ExternalProject_Add(cares_ep
  PREFIX deps/c-ares
  INSTALL_DIR ${ROOTFS}
  URL https://c-ares.haxx.se/download/c-ares-1.15.0.tar.gz
  URL_HASH MD5=d2391da274653f7643270623e822dff7
  CMAKE_ARGS -DCARES_STATIC=ON -DCARES_SHARED=OFF -DCARES_STATIC_PIC=OFF -DCARES_BUILD_TOOLS=OFF -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
)

ExternalProject_Add(curl_ep
  PREFIX deps/curl
  INSTALL_DIR ${ROOTFS}
  URL https://curl.haxx.se/download/curl-7.65.1.tar.gz
  URL_HASH MD5=6f31ec164741cc6aaac461d4b38ffdbe
  BUILD_IN_SOURCE ON
  CONFIGURE_COMMAND ./buildconf
  COMMAND ./configure
    --disable-shared
    --enable-static
    --disable-ldap
    --disable-sspi
    --disable-ftp
    --disable-file
    --disable-dict
    --disable-telnet
    --disable-tftp
    --disable-rtsp
    --disable-pop3
    --disable-imap
    --disable-smtp
    --disable-gopher
    --disable-smb
    --disable-pthreads
    --without-librtmp
    --without-nghttp2
    --without-libidn2
    --without-libpsl
    --without-brotli
    --without-ssl
    --with-mbedtls=${ROOTFS}
    --enable-ares=${ROOTFS}
    --prefix=<INSTALL_DIR>
  BUILD_COMMAND make
  INSTALL_COMMAND make install
)

add_dependencies(curl_ep cares_ep)
add_dependencies(curl_ep mbedtls_ep)

add_library(curl INTERFACE IMPORTED)
add_dependencies(curl curl_ep)
target_link_libraries(curl INTERFACE
  ${ROOTFS}/lib/libcurl.a
  ${ROOTFS}/lib/libcares.a
  ${ROOTFS}/lib/libmbedcrypto.a
  ${ROOTFS}/lib/libmbedx509.a
  ${ROOTFS}/lib/libmbedtls.a
)
target_include_directories(curl INTERFACE ${ROOTFS}/include)

ExternalProject_Add(zlib_ep
  PREFIX deps/zlib
  INSTALL_DIR ${ROOTFS}
  URL https://zlib.net/zlib-1.2.11.tar.xz
  URL_HASH SHA256=4ff941449631ace0d4d203e3483be9dbc9da454084111f97ea0a2114e19bf066
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
)

add_library(zlib INTERFACE IMPORTED)
add_dependencies(zlib zlib_ep)
target_link_libraries(zlib INTERFACE
  ${ROOTFS}/lib/libz.a
)
target_include_directories(zlib INTERFACE ${ROOTFS}/include)

ExternalProject_Add(tar_ep
  PREFIX deps/tar
  INSTALL_DIR ${ROOTFS}
  URL https://github.com/tklauser/libtar/archive/v1.2.20.tar.gz
  URL_HASH MD5=dcdcdf8cfbbd3df3862198b0897071b6
  BUILD_IN_SOURCE ON
  CONFIGURE_COMMAND autoreconf --force --install
  COMMAND ./configure -prefix=<INSTALL_DIR>
  BUILD_COMMAND make
  INSTALL_COMMAND make install
)

add_library(tar INTERFACE IMPORTED)
add_dependencies(tar tar_ep)
target_link_libraries(tar INTERFACE
  ${ROOTFS}/lib/libtar.a
)
target_include_directories(tar INTERFACE ${ROOTFS}/include)

ExternalProject_Add(editline_ep
  PREFIX deps/editline
  INSTALL_DIR ${ROOTFS}
  URL https://github.com/troglobit/editline/releases/download/1.16.1/editline-1.16.1.tar.xz
  URL_HASH MD5=df6d7deac03da1b627f9252db984820e
  BUILD_IN_SOURCE ON
  CONFIGURE_COMMAND ./configure -prefix=<INSTALL_DIR>
  BUILD_COMMAND make
  INSTALL_COMMAND make install
)

add_library(editline INTERFACE IMPORTED)
add_dependencies(editline editline_ep)
target_link_libraries(editline INTERFACE
  ${ROOTFS}/lib/libeditline.a
)
target_include_directories(editline INTERFACE ${ROOTFS}/include)

add_executable(stonectl src/main.cpp)
if(${BUILD_STATIC_STONECTL})
  target_link_libraries(stonectl -static)
endif()
target_link_libraries(stonectl rpcws CLI11 stone-api curl zlib tar editline stdc++fs)
set_property(TARGET stonectl PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
set_property(TARGET stonectl PROPERTY CXX_STANDARD 17)

install(TARGETS stonectl nsgod
        RUNTIME DESTINATION bin)