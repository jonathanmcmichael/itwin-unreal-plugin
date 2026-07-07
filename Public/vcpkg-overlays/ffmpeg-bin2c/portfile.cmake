set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
set(VCPKG_BUILD_TYPE release)  # host tool for building ffmpeg

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ffmpeg/ffmpeg
    REF "n${VERSION}"
    SHA512 c72f4062aecc16d8b2b1e8678d5efe3af4cfaa0cc7c0997052248f9e499e60c2463acf07877cf3b78b246ce3e8078cb043e8d97e90a6b50d06af32ff7369a788
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
        0048-backport-23039.patch
        0049-fix-twolame-pkgconfig.patch
        0050-fix-test-ld-absolute-lib-paths.patch
	0051-fix-CVE-2026-58049.patch
)

# Bentley/AdvViz: Remove this unused file because of CVEs CVE-2026-30998 and CVE-2026-30999
file(REMOVE "${SOURCE_PATH}/tools/zmqsend.c")

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}/ffbuild")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/ffbuild"
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING.LGPLv2.1")
