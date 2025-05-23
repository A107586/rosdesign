find_package(GTest)

if(GTest_FOUND)
    if(NOT TARGET GTest::gtest_main AND TARGET GTest::Main)
	# CMake <3.20
        add_library(GTest::gtest_main INTERFACE IMPORTED)
        target_link_libraries(GTest::gtest_main INTERFACE GTest::Main)
    endif()
else()
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()

    include(FetchContent)

    set(BUILD_GMOCK OFF)
    set(INSTALL_GTEST OFF)
    set(gtest_force_shared_crt ON)

    FetchContent_Declare(
        gtest
        URL https://github.com/google/googletest/archive/main.tar.gz
    )
    FetchContent_MakeAvailable(gtest)
endif()

add_executable(httplib-test test.cc)
target_compile_options(httplib-test PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8;/bigobj>")
target_link_libraries(httplib-test PRIVATE httplib GTest::gtest_main)
gtest_discover_tests(httplib-test)

execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/www www
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/www2 www2
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/www3 www3
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_LIST_DIR}/ca-bundle.crt ca-bundle.crt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_LIST_DIR}/image.jpg image.jpg
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND_ERROR_IS_FATAL ANY
)

if(HTTPLIB_IS_USING_OPENSSL)
    find_program(OPENSSL_COMMAND
        NAMES openssl
        PATHS ${OPENSSL_INCLUDE_DIR}/../bin
        REQUIRED
    )
    execute_process(
        COMMAND ${OPENSSL_COMMAND} genrsa 2048
        OUTPUT_FILE key.pem
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND ${OPENSSL_COMMAND} req -new -batch -config ${CMAKE_CURRENT_LIST_DIR}/test.conf -key key.pem
        COMMAND ${OPENSSL_COMMAND} x509 -days 3650 -req -signkey key.pem
        OUTPUT_FILE cert.pem
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND ${OPENSSL_COMMAND} req -x509 -new -config ${CMAKE_CURRENT_LIST_DIR}/test.conf -key key.pem -sha256 -days 3650 -nodes -out cert2.pem -extensions SAN
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND ${OPENSSL_COMMAND} genrsa 2048
        OUTPUT_FILE rootCA.key.pem
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND ${OPENSSL_COMMAND} req -x509 -new -batch -config ${CMAKE_CURRENT_LIST_DIR}/test.rootCA.conf -key rootCA.key.pem -days 1024
        OUTPUT_FILE rootCA.cert.pem
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND ${OPENSSL_COMMAND} genrsa 2048
        OUTPUT_FILE client.key.pem
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND ${OPENSSL_COMMAND} req -new -batch -config ${CMAKE_CURRENT_LIST_DIR}/test.conf -key client.key.pem
        COMMAND ${OPENSSL_COMMAND} x509 -days 370 -req -CA rootCA.cert.pem -CAkey rootCA.key.pem -CAcreateserial
        OUTPUT_FILE client.cert.pem
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND ${OPENSSL_COMMAND} genrsa -passout pass:test123! 2048
        OUTPUT_FILE key_encrypted.pem
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND ${OPENSSL_COMMAND} req -new -batch -config ${CMAKE_CURRENT_LIST_DIR}/test.conf -key key_encrypted.pem
        COMMAND ${OPENSSL_COMMAND} x509 -days 3650 -req -signkey key_encrypted.pem
        OUTPUT_FILE cert_encrypted.pem
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()
