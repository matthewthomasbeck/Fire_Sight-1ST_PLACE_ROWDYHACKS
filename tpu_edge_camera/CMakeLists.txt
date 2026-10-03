cmake_minimum_required(VERSION 3.16)
project(tpu_edge_camera LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(tpu_edge_camera
  src/main.cpp
  src/config/Config.cpp
  src/util/Log.cpp
  src/util/Base64.cpp
  src/hardware/ThermalDriver.cpp
  src/hardware/CameraDriver.cpp
  src/hardware/LidarDriver.cpp
  src/hardware/GpsDriver.cpp
  src/hardware/mlx90640/MLX90640_API.cpp
  src/hardware/mlx90640/MLX90640_LINUX_I2C_Driver.cpp
  src/vision/CameraFusion.cpp
  src/vision/LidarMap.cpp
  src/vision/DisplayHud.cpp
  src/net/UplinkClient.cpp
  src/net/Auth0M2M.cpp
)

target_include_directories(tpu_edge_camera PRIVATE
  src
  src/hardware/mlx90640
)

target_compile_options(tpu_edge_camera PRIVATE -Wall -Wextra)
target_link_libraries(tpu_edge_camera PRIVATE pthread m)

# Fullscreen HUD (Pi touchscreen). Optional so headless / Mac builds still work.
find_package(OpenCV QUIET COMPONENTS core imgproc highgui)
if(OpenCV_FOUND)
  message(STATUS "OpenCV ${OpenCV_VERSION}: fullscreen HUD enabled")
  target_compile_definitions(tpu_edge_camera PRIVATE HAS_OPENCV=1)
  target_include_directories(tpu_edge_camera PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(tpu_edge_camera PRIVATE ${OpenCV_LIBS})
else()
  message(STATUS "OpenCV not found: HUD will be a no-op (apt install libopencv-dev)")
endif()
