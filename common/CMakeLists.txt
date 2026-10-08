add_library(mmcommon STATIC src/paths.cpp src/endpoint.cpp src/manifest.cpp)
target_include_directories(mmcommon PUBLIC include ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(mmcommon PUBLIC ${QTV}::Core ${QTV}::Network sodium)
