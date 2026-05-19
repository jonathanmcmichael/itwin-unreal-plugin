set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
set(VCPKG_BUILD_TYPE release)  # host tool for building ffmpeg

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ffmpeg/ffmpeg
    REF "n${VERSION}"
    SHA512 1dee3967057619dd7f2f78c63de85bb97af16c974bd9225c2336d42c7c8765c04f77490aac36af2daf953bc52c7faa37750a09265e133708f6a1709028573834
    HEAD_REF master
    PATCHES
        0002-fix-msvc-link.patch
        0003-fix-windowsinclude.patch
        0004-dependencies.patch
        0005-fix-nasm.patch
        0007-fix-lib-naming.patch
        0013-define-WINVER.patch
        0024-fix-osx-host-c11.patch
        0040-ffmpeg-add-av_stream_get_first_dts-for-chromium.patch # Do not remove this patch. It is required by chromium
        0045-use-prebuilt-bin2c.patch
        0046-fix-msvc-detection.patch
        0047-fix-msvc-utf8.patch
        0048-fix-CVE-2026-30997.patch
)

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}/ffbuild")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/ffbuild"
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING.LGPLv2.1")
