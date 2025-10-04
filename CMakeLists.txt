cmake_minimum_required(VERSION 3.15)
project(vehiculo_autonomo_telemetria C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(vehiculo_autonomo_telemetria
        src/server.c
        src/ejemplo.c
)

# En Windows hay que enlazar con Winsock2
if (WIN32)
    target_link_libraries(vehiculo_autonomo_telemetria ws2_32)
endif()
