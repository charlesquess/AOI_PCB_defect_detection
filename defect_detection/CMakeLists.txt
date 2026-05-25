cmake_minimum_required(VERSION 3.16)
project(defect_detection VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# ──────────────────────────────────────────────
# 依赖查找
# ──────────────────────────────────────────────
find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui videoio imgcodecs)
find_package(spdlog QUIET)
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    # Conda 下 yaml-cpp 没有 cmake config, 但有 lib/ 文件
    find_library(YAML_CPP_LIBRARY yaml-cpp PATHS "C:/ProgramData/anaconda3/Library/lib" NO_DEFAULT_PATH)
    if(YAML_CPP_LIBRARY)
        set(yaml-cpp_FOUND TRUE)
        set(YAML_CPP_TARGET "${YAML_CPP_LIBRARY}")
        message(STATUS "Found yaml-cpp (conda): ${YAML_CPP_LIBRARY}")
    endif()
endif()
if(NOT yaml-cpp_FOUND)
    message(FATAL_ERROR "yaml-cpp not found. Install with: conda install yaml-cpp -c conda-forge")
endif()
find_package(Threads REQUIRED)

# Qt (可选) — 用于 Qt 界面
find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
    message(STATUS "Found Qt6: ${Qt6Widgets_DIR}")
else()
    message(STATUS "Qt6 not found - building OpenCV highgui UI only")
endif()

# ONNX Runtime
set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/3rdparty/onnxruntime")
if(EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
    set(ONNXRUNTIME_FOUND TRUE)
    message(STATUS "Found ONNX Runtime: ${ONNXRUNTIME_ROOT}")
else()
    set(ONNXRUNTIME_FOUND FALSE)
    message(STATUS "ONNX Runtime not found in 3rdparty")
endif()

# ──────────────────────────────────────────────
# 各模块源文件
# ──────────────────────────────────────────────
set(CAMERA_SRC    src/camera/camera.cpp)
set(PREPROC_SRC   src/preprocess/preprocessor.cpp)
set(INFERENCE_SRC src/inference/inferencer.cpp)
set(POSTPROC_SRC  src/postprocess/postprocessor.cpp)
set(COMM_SRC      src/communication/serial_client.cpp)
set(STATS_SRC     src/statistics/statistician.cpp)
set(UI_SRC        src/ui/ui_manager.cpp)
set(QT_UI_SRC     src/ui/qt_main_window.cpp)
set(UTILS_SRC     src/utils/config.cpp
                   src/utils/logger.cpp
                   src/utils/timer.cpp
                   src/utils/tracker.cpp
                   src/utils/line_scan.cpp)

# ──────────────────────────────────────────────
# 静态库目标
# ──────────────────────────────────────────────
add_library(defect_camera       STATIC ${CAMERA_SRC})
add_library(defect_preprocess   STATIC ${PREPROC_SRC})
add_library(defect_inference    STATIC ${INFERENCE_SRC})
add_library(defect_postprocess  STATIC ${POSTPROC_SRC})
add_library(defect_comm         STATIC ${COMM_SRC})
add_library(defect_statistics   STATIC ${STATS_SRC})
add_library(defect_ui           STATIC ${UI_SRC})
add_library(defect_utils        STATIC ${UTILS_SRC})

set(DEFECT_LIBS
    defect_camera defect_preprocess defect_inference defect_postprocess
    defect_comm defect_statistics defect_ui defect_utils
)

# ──────────────────────────────────────────────
# 包含目录
# ──────────────────────────────────────────────
foreach(lib ${DEFECT_LIBS})
    target_include_directories(${lib} PUBLIC include)
    target_include_directories(${lib} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    if(YAML_CPP_LIBRARY)
        target_include_directories(${lib} PUBLIC "C:/ProgramData/anaconda3/Library/include")
    endif()
endforeach()

if(ONNXRUNTIME_FOUND)
    set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/include")
    set(ONNXRUNTIME_LIB_DIR     "${ONNXRUNTIME_ROOT}/lib")
    foreach(lib ${DEFECT_LIBS})
        target_include_directories(${lib} PUBLIC ${ONNXRUNTIME_INCLUDE_DIR})
    endforeach()
    target_compile_definitions(defect_inference PRIVATE USE_ONNX_RUNTIME)
endif()

# ──────────────────────────────────────────────
# 链接依赖
# ──────────────────────────────────────────────
foreach(lib ${DEFECT_LIBS})
    target_link_libraries(${lib} PUBLIC ${OpenCV_LIBS})
    if(spdlog_FOUND)
        target_link_libraries(${lib} PUBLIC spdlog::spdlog)
    endif()
    if(yaml-cpp_FOUND)
        if(TARGET yaml-cpp)
            target_link_libraries(${lib} PUBLIC yaml-cpp)
        else()
            target_link_libraries(${lib} PUBLIC ${YAML_CPP_LIBRARY})
        endif()
    endif()
    target_link_libraries(${lib} PUBLIC Threads::Threads)
endforeach()

if(ONNXRUNTIME_FOUND)
    target_link_libraries(defect_inference PUBLIC
        "${ONNXRUNTIME_LIB_DIR}/onnxruntime.lib")
endif()

# 模块间依赖
target_link_libraries(defect_inference PUBLIC defect_preprocess defect_utils)
target_link_libraries(defect_postprocess PUBLIC defect_utils)
target_link_libraries(defect_statistics PUBLIC defect_utils)
target_link_libraries(defect_ui PUBLIC defect_postprocess defect_statistics)
target_link_libraries(defect_comm PUBLIC defect_utils)

# ──────────────────────────────────────────────
# 主程序 — OpenCV 版 (始终构建)
# ──────────────────────────────────────────────
add_executable(defect_detection src/main.cpp)
target_link_libraries(defect_detection PRIVATE ${DEFECT_LIBS})

# ──────────────────────────────────────────────
# 主程序 — Qt 版 (可选，需 Qt6)
# ──────────────────────────────────────────────
if(Qt6_FOUND)
    find_program(QT_MOC_EXECUTABLE moc
        HINTS "C:/Qt/6.6.2/msvc2019_64/bin"
        REQUIRED)

    # 手动生成 moc 文件
    set(QT_MOC_HEADER "${CMAKE_SOURCE_DIR}/include/ui/qt_main_window.hpp")
    set(QT_MOC_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/moc_qt_main_window.cpp")

    add_custom_command(OUTPUT ${QT_MOC_OUTPUT}
        COMMAND ${QT_MOC_EXECUTABLE} -o ${QT_MOC_OUTPUT} ${QT_MOC_HEADER}
        DEPENDS ${QT_MOC_HEADER}
        COMMENT "Generating MOC: qt_main_window.hpp")

    add_executable(defect_detection_qt src/main_qt.cpp ${QT_UI_SRC} ${QT_MOC_OUTPUT})
    target_include_directories(defect_detection_qt PRIVATE include)
    target_include_directories(defect_detection_qt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(defect_detection_qt PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

    target_link_libraries(defect_detection_qt PRIVATE
        defect_camera defect_preprocess defect_inference
        defect_postprocess defect_comm defect_statistics
        defect_utils)
    target_link_libraries(defect_detection_qt PRIVATE ${OpenCV_LIBS})
    target_link_libraries(defect_detection_qt PRIVATE Qt6::Widgets)

    if(ONNXRUNTIME_FOUND)
        target_include_directories(defect_detection_qt PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
        target_link_libraries(defect_detection_qt PRIVATE "${ONNXRUNTIME_LIB_DIR}/onnxruntime.lib")
        target_compile_definitions(defect_detection_qt PRIVATE USE_ONNX_RUNTIME)
    endif()

    message(STATUS "Qt UI target: defect_detection_qt")
endif()

# ──────────────────────────────────────────────
# 复制运行时文件
# ──────────────────────────────────────────────
file(COPY config/ DESTINATION ${CMAKE_BINARY_DIR}/config)
file(COPY models/ DESTINATION ${CMAKE_BINARY_DIR}/models)

if(ONNXRUNTIME_FOUND)
    file(GLOB ONNXRUNTIME_DLLS "${ONNXRUNTIME_LIB_DIR}/*.dll")
    foreach(dll ${ONNXRUNTIME_DLLS})
        file(COPY ${dll} DESTINATION ${CMAKE_BINARY_DIR}/Release)
        file(COPY ${dll} DESTINATION ${CMAKE_BINARY_DIR}/Debug)
    endforeach()
endif()

# 复制 yaml-cpp DLL（conda 下为动态库）
if(YAML_CPP_LIBRARY)
    get_filename_component(YAML_CPP_LIB_DIR "${YAML_CPP_LIBRARY}" DIRECTORY)
    file(GLOB YAML_CPP_DLLS "${YAML_CPP_LIB_DIR}/../bin/yaml-cpp*.dll")
    foreach(dll ${YAML_CPP_DLLS})
        file(COPY ${dll} DESTINATION ${CMAKE_BINARY_DIR}/Release)
        file(COPY ${dll} DESTINATION ${CMAKE_BINARY_DIR}/Debug)
    endforeach()
endif()
