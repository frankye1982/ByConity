if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    set(LIBDEFLATE_LIBRARY libdeflate_static)
    set(LIBDEFLATE_INCLUDE_DIR ${ClickHouse_SOURCE_DIR}/contrib/libdeflate)
    set(USE_LIBDEFLATE 1)
else()
    set(USE_LIBDEFLATE 0)
endif()
message (STATUS "Using libdeflate=${USE_LIBDEFLATE} libdeflate=${LIBDEFLATE_LIBRARY}")
