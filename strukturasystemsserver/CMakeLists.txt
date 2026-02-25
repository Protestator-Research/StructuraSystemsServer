cmake_minimum_required(VERSION 3.15)
find_package(sysmllib REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(mongocxx REQUIRED)
find_package(libsodium REQUIRED)
find_package(Drogon REQUIRED)

set(SOURCES
        src/main.cpp
        src/Controller/SystemController.hpp
        src/Controller/ProjectController.hpp
        src/Controller/DataBaseController.h
        src/Controller/DataBaseController.cpp
        src/Controller/AuthenticationController.hpp
        src/Controller/ElementsController.hpp
        src/Controller/CommitController.hpp
        src/Entities/json/AuthenticationRequest.h
        src/Entities/json/AuthenticationRequest.cpp
        src/Entities/json/AuthenticationResponse.h
        src/Entities/json/AuthenticationResponse.cpp
        src/Services/ServerProjectService.h
        src/Services/ServerProjectService.cpp
        src/Services/ElementNavigationService.h
        src/Services/ElementNavigationService.cpp
        src/Services/ProjectVersioningService.h
        src/Services/ProjectVersioningService.cpp
        src/Services/AuthenticationService.h
        src/Services/AuthenticationService.cpp
        src/Controller/DigitalTwinController.hpp
        src/Entities/json/TwinResponse.cpp
        src/Entities/json/TwinResponse.h
        src/Entities/json/TwinDataResponse.cpp
        src/Entities/json/TwinDataResponse.h
        src/Entities/json/TwinRequest.cpp
        src/Entities/json/TwinRequest.h
        src/Services/DigitalTwinService.cpp
        src/Services/DigitalTwinService.h
        src/Services/JwtFilter.hpp
        src/Entities/db/User.hpp
        src/Services/AdministrationFilter.hpp)

add_executable(StructuraSystemsServer ${SOURCES})



target_include_directories(StructuraSystemsServer PUBLIC
        "${CMAKE_CURRENT_BINARY_DIR}/")
if(TARGET mongo::mongocxx_shared)
    target_link_libraries(StructuraSystemsServer PUBLIC
            sysmllib::sysmllib
            openssl::openssl
            mongo::mongocxx_shared
            libsodium::libsodium
            Drogon::Drogon)
else()
    target_link_libraries(StructuraSystemsServer PUBLIC
            sysmllib::sysmllib
            openssl::openssl
            mongo::mongocxx_static
            libsodium::libsodium
            Drogon::Drogon)
endif()

if (!UNIX)
set_target_properties(StructuraSystemsServer PROPERTIES
        WIN32_EXECUTABLE ON
        MACOSX_BUNDLE ON
)
endif()

file(GLOB_RECURSE _source_list *.cpp* *.h* *.hpp*)
foreach(_source IN ITEMS ${_source_list})
    get_filename_component(_source_path "${_source}" PATH)
    string(REPLACE "${CMAKE_SOURCE_DIR}" "" _group_path "${_source_path}")
    string(REPLACE "/" "\\" _group_path "${_group_path}")
    source_group("${_group_path}" FILES "${_source}")
endforeach()


if(MSVC)
    target_compile_options(StructuraSystemsServer PRIVATE /W4 /WX- /bigobj)
elseif (UNIX AND NOT APPLE)
    target_compile_options(StructuraSystemsServer PRIVATE -Wall -Wextra -Wpedantic -Werror)
else ()
    target_compile_options(StructuraSystemsServer PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()