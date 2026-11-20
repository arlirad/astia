# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Arlirad

set(_astia_root "${CMAKE_CURRENT_LIST_DIR}/..")

file(READ "${_astia_root}/.gitmodules" _astia_gitmodules)
foreach(_astia_submodule
        "path = user/zsh"
        "url = https://github.com/zsh-users/zsh.git"
        "path = user/nano"
        "url = https://github.com/madnight/nano.git"
        "path = user/gnulib"
        "url = https://github.com/coreutils/gnulib.git")
    if(NOT _astia_gitmodules MATCHES "${_astia_submodule}")
        message(FATAL_ERROR ".gitmodules is missing: ${_astia_submodule}")
    endif()
endforeach()
if(NOT EXISTS "${_astia_root}/user/nano/autogen.sh" AND
   NOT EXISTS "${_astia_root}/user/nano/configure")
    message(FATAL_ERROR "the pinned nano source is missing bootstrap files")
endif()

execute_process(
    COMMAND git -C "${_astia_root}" ls-files --stage -- user/zsh user/nano user/gnulib
    OUTPUT_VARIABLE _astia_gitlinks
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _astia_gitlinks_status)
if(_astia_gitlinks_status OR
   NOT _astia_gitlinks MATCHES
       "160000 ddee3e7a751a4f03d5a1041fd44bb7af7bc0cebb 0[ \t]+user/zsh" OR
   NOT _astia_gitlinks MATCHES
       "160000 326e4146b2267d8683a8eb3333aa49c9023d0dbf 0[ \t]+user/nano")
    message(FATAL_ERROR "userspace submodules are not pinned to the reviewed revisions")
endif()
if(NOT _astia_gitlinks MATCHES
       "160000 f05b5a23f6cef8833402a298d1576a0118912ac8 0[ \t]+user/gnulib")
    message(FATAL_ERROR "gnulib is not pinned to the reviewed revision")
endif()

foreach(_astia_launcher
        "${_astia_root}/toolchain/run-zsh-qemu.sh"
        "${_astia_root}/toolchain/run-zsh-vga-qemu.sh")
    file(READ "${_astia_launcher}" _astia_launcher_text)
    if(NOT _astia_launcher_text MATCHES "zsh_source=\\$\\{ZSH_SOURCE:-\\$root/user/zsh\\}")
        message(FATAL_ERROR "${_astia_launcher} does not default to user/zsh")
    endif()
    if(NOT _astia_launcher_text MATCHES "ZSH_SOURCE does not exist")
        message(FATAL_ERROR "${_astia_launcher} lost explicit override diagnostics")
    endif()
    if(_astia_launcher_text MATCHES "git clone")
        message(FATAL_ERROR "${_astia_launcher} must not clone an unpinned branch")
    endif()
endforeach()

file(READ "${_astia_root}/toolchain/build-zsh.sh" _astia_zsh_build)
file(READ "${_astia_root}/toolchain/build-nano.sh" _astia_nano_build)
file(READ "${_astia_root}/toolchain/init-userspace.sh" _astia_userspace_init)
file(READ "${_astia_root}/coreutils/build.sh" _astia_coreutils_build)
foreach(_astia_text _astia_zsh_build _astia_nano_build _astia_coreutils_build)
    if(NOT "${${_astia_text}}" MATCHES "astia")
        message(FATAL_ERROR "workflow source unexpectedly empty")
    endif()
endforeach()
if(NOT _astia_userspace_init MATCHES "user/nano")
    message(FATAL_ERROR "userspace initializer unexpectedly empty")
endif()
if(NOT _astia_zsh_build MATCHES "astia-config-key" OR
   NOT _astia_nano_build MATCHES "astia-config-key" OR
   NOT _astia_zsh_build MATCHES "source_revision" OR
   NOT _astia_nano_build MATCHES "source_revision" OR
   NOT _astia_nano_build MATCHES "user/gnulib" OR
   _astia_nano_build MATCHES "git clone" OR
   _astia_nano_build MATCHES "git pull" OR
   _astia_coreutils_build MATCHES "rm -rf \\\"\\$out\\\"" OR
   NOT _astia_userspace_init MATCHES
       "https://ftp\\.gnu\\.org/gnu/nano/nano-8\\.6\\.tar\\.xz" OR
   NOT _astia_userspace_init MATCHES
       "f7abfbf0eed5f573ab51bd77a458f32d82f9859c55e9689f819d96fe1437a619")
    message(FATAL_ERROR "userspace build caches are not incremental")
endif()
