cmake_minimum_required(VERSION 3.15)
project(corsario LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Python + pybind11
# ---------------------------------------------------------------------------
find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module)

# pybind11 is provided by the 'pybind11' pip package (declared as a
# build-system requirement in pyproject.toml); this works both for
# `pip install .` (scikit-build-core) and a manual `cmake` invocation as
# long as pybind11 is importable by the active Python interpreter.
execute_process(
  COMMAND "${Python_EXECUTABLE}" -m pybind11 --cmakedir
  OUTPUT_VARIABLE _pybind11_cmake_dir
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
list(APPEND CMAKE_PREFIX_PATH "${_pybind11_cmake_dir}")
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(_corsario_core MODULE bindings/module.cpp)
target_include_directories(_corsario_core PRIVATE include)
target_compile_features(_corsario_core PRIVATE cxx_std_17)

if(NOT MSVC)
  target_compile_options(_corsario_core PRIVATE -O3 -Wall)
endif()

# Installed as part of the "corsario" Python package, next to __init__.py.
install(TARGETS _corsario_core LIBRARY DESTINATION corsario)

# ---------------------------------------------------------------------------
# Optional: plain C++ core library + native tests (not required for the
# Python wheel, useful for `ctest`/CI and for embedding corsario in a
# non-Python C++ pipeline).
# ---------------------------------------------------------------------------
option(CORSARIO_BUILD_TESTS "Build the C++ unit tests" OFF)
if(CORSARIO_BUILD_TESTS)
  enable_testing()
  add_executable(corsario_test_core tests/test_cpp/test_reader.cpp)
  target_include_directories(corsario_test_core PRIVATE include)
  target_compile_features(corsario_test_core PRIVATE cxx_std_17)
  add_test(NAME corsario_core_tests COMMAND corsario_test_core)
endif()
