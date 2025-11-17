cmake_minimum_required(VERSION 3.20.0)

# Generate compile_commands.json for C/C++ Extension
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(bee-eye)

target_sources(app PRIVATE 
    src/main.c 
    src/uart_print.c 
    src/vsense.c
    src/rtc.c
    src/onewire.c
    src/lora.c
    src/messages.c
    src/ble_server.c
    src/ble_client.c
)
