cmake_minimum_required(VERSION 3.20)

# The repository-level project is the canonical entry point for IDEs.  The
# kernel remains independently configurable from kernel/, while libc keeps
# its separate freestanding toolchain and install/sysroot workflow.
project(astia LANGUAGES NONE)

add_subdirectory(kernel)
