# Writes fw_version.h with the current git commit. Run as a build step (not
# at configure time), so a rebuild after a commit never keeps an old hash.
# The file is only rewritten when the version changed, to avoid recompiles.
execute_process(
    COMMAND git -c safe.directory=* describe --always --dirty --abbrev=7
    WORKING_DIRECTORY ${SRC}
    OUTPUT_VARIABLE version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT version)
    set(version "unknown")
endif()
set(content "#define FW_VERSION \"${version}\"\n")
set(old "")
if(EXISTS ${OUT})
    file(READ ${OUT} old)
endif()
if(NOT old STREQUAL content)
    file(WRITE ${OUT} "${content}")
endif()
