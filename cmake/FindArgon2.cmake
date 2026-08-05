#[[
FindArgon2.cmake -- locate the reference Argon2 password-hashing library
(https://github.com/P-H-C/phc-winner-argon2), used for local password
hashing (see src/common/PasswordHash.h). Upstream ships no CMake package
config, so this hand-rolled module fills the gap the same way the project
already does for other system libraries via find_package.

Provides the imported target Argon2::Argon2 and sets Argon2_FOUND.
On Debian/Ubuntu: apt install libargon2-dev.
Via vcpkg: the "libargon2" port.
]]

find_path(Argon2_INCLUDE_DIR NAMES argon2.h)
find_library(Argon2_LIBRARY NAMES argon2 libargon2)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Argon2 REQUIRED_VARS Argon2_LIBRARY Argon2_INCLUDE_DIR)

if(Argon2_FOUND AND NOT TARGET Argon2::Argon2)
    add_library(Argon2::Argon2 UNKNOWN IMPORTED)
    set_target_properties(Argon2::Argon2 PROPERTIES
        IMPORTED_LOCATION "${Argon2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Argon2_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(Argon2_INCLUDE_DIR Argon2_LIBRARY)
