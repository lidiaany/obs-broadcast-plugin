# CMake processor count detection module
#
# Detects the number of available logical processors on the host system
# using CMake's built-in platform-independent host system information query.
#
# Provides:
#   PROCESSOR_COUNT  - Cache variable (can be overridden manually)
#                       Defaults to NUMBER_OF_LOGICAL_CORES from the host.
#
# Usage:
#   include(processor_count)
#   message("Building with ${PROCESSOR_COUNT} parallel jobs")
#
# In build commands:
#   cmake --build <dir> --parallel ${PROCESSOR_COUNT}

include_guard(GLOBAL)

# Detect logical processor count once; allow manual override via cache
if(NOT DEFINED PROCESSOR_COUNT)
  cmake_host_system_information(RESULT _detected_processor_count QUERY NUMBER_OF_LOGICAL_CORES)

  if(_detected_processor_count GREATER 0)
    set(PROCESSOR_COUNT
        "${_detected_processor_count}"
        CACHE STRING
              "Number of logical processors for parallel build jobs"
    )
    message(DEBUG "Detected ${PROCESSOR_COUNT} logical processor(s)")
  else()
    set(PROCESSOR_COUNT
        "2"
        CACHE STRING
              "Number of logical processors for parallel build jobs (fallback)"
    )
    message(
      DEBUG
      "Could not detect processor count — defaulting to ${PROCESSOR_COUNT}"
    )
  endif()
endif()

mark_as_advanced(PROCESSOR_COUNT)
