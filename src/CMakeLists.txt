CMAKE_MINIMUM_REQUIRED(VERSION 3.2)

PROJECT(ScanConvert)

IF (NOT PLUSLIB_DIR)
    FIND_PACKAGE(PlusLib)
ELSE()
    FIND_PACKAGE(PlusLib REQUIRED PATHS "${PLUSLIB_DIR}/src" NO_DEFAULT_PATH)
ENDIF()

IF (PlusLib_FOUND)
    INCLUDE(${PlusLib_USE_FILE})
ELSE (PlusLib_FOUND)
    MESSAGE(FATAL_ERROR "This project requires PlusLib.")
ENDIF()

ADD_EXECUTABLE(ScanConvertTest ScanConvertTest.cpp)
TARGET_LINK_LIBRARIES(ScanConvertTest vtkPlusCommon vtkImageProcessingAlgo)
