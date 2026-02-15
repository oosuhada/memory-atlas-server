# ======================================================================
# CMakeLists.txt for CherryRecorder-Server
# ======================================================================

# ----------------------------------------------------------------------
# CMake 최소 요구 버전 및 정책 설정
# ----------------------------------------------------------------------
# 비교적 최신 기능(예: vcpkg 통합, C++20 지원 등)을 사용하므로 3.20 권장
# 너무 오래된 CMake와의 호환성이 필요 없다면 이 정도로 충분합니다.
cmake_minimum_required(VERSION 3.20)
# 이 CMakeLists 파일이 작성될 때 기준으로 삼은 CMake 정책 버전을 명시합니다.
# minimum_required 버전과 일치시키는 것이 일반적입니다.
cmake_policy(VERSION 3.20)
# Set policy for FindBoost removal warning (CMP0167)
cmake_policy(SET CMP0167 NEW)
# 주석: set(CMAKE_POLICY_VERSION_MINIMUM ...)은 특별한 경우가 아니면 사용하지 않습니다.

# ----------------------------------------------------------------------
# vcpkg toolchain 설정
# ----------------------------------------------------------------------
# vcpkg 툴체인 파일 경로를 설정합니다. (CACHE STRING은 사용자가 변경 가능하도록 함)
set(CMAKE_TOOLCHAIN_FILE
    "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "Vcpkg toolchain file")
message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")

# ----------------------------------------------------------------------
# 프로젝트 및 C++ 표준 설정
# ----------------------------------------------------------------------
# 프로젝트 이름과 사용 언어(C++)를 정의합니다.
project("CherryRecorder-Server" LANGUAGES CXX)

# 전역적으로 사용할 C++ 표준 및 설정을 지정합니다.
set(CMAKE_CXX_STANDARD 20)          # C++20 표준 사용
set(CMAKE_CXX_STANDARD_REQUIRED ON) # 지정된 표준을 반드시 사용하도록 강제
set(CMAKE_CXX_EXTENSIONS OFF)       # 컴파일러별 확장 기능 사용 금지 (표준 준수)

# Enable trace logging for spdlog
add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

message(STATUS "Project Name: ${PROJECT_NAME}")
message(STATUS "CXX Standard: ${CMAKE_CXX_STANDARD}")

# ----------------------------------------------------------------------
# 플랫폼별 설정 (Windows / MSVC)
# ----------------------------------------------------------------------
if(WIN32)
  # 타겟 Windows 버전 정의 (Windows 10 이상 권장)
  add_definitions(-D_WIN32_WINNT=0x0A00)
  message(STATUS "Targeting Windows 10 or later (_WIN32_WINNT=0x0A00)")

  if(MSVC)
    # MSVC 컴파일러 UTF-8 소스 코드 처리 옵션 및 C++ 표준 명시
    # CMAKE_CXX_STANDARD 설정만으로는 부족할 경우를 대비해 명시적으로 플래그 추가
    add_compile_options(/utf-8 /std:c++20 /Zc:__cplusplus)
    # Enable big object files for complex templates
    add_compile_options(/bigobj)
    message(STATUS "MSVC: Added /utf-8, /std:c++20, /Zc:__cplusplus compile options.")

    # MSVC Hot Reload (Edit and Continue) 지원 설정
    if(POLICY CMP0141)
      cmake_policy(SET CMP0141 NEW)
      set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
      message(STATUS "MSVC: Enabled Edit and Continue debug information format.")
    endif()
  endif()
endif()

# ----------------------------------------------------------------------
# 라이브러리 타겟 정의 (CherryRecorder-Server)
# ----------------------------------------------------------------------
# 애플리케이션 핵심 로직을 포함하는 정적 라이브러리(기본값)를 정의합니다.
# --- Boost 라이브러리 찾기 (Asio + Beast) ---
# find_package 시 필요한 컴포넌트를 명시한다.
# Boost::headers는 많은 Boost 라이브러리에서 필요하다.
find_package(Boost 1.70.0 REQUIRED COMPONENTS system asio beast json)
find_package(OpenSSL REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Threads REQUIRED) # Find pthreads on non-Windows

message(STATUS "Boost include directories: ${Boost_INCLUDE_DIRS}")
message(STATUS "OpenSSL include directories: ${OPENSSL_INCLUDE_DIR}")
message(STATUS "spdlog include directories: ${spdlog_INCLUDE_DIRS}") # Check variable name from find_package

# ----------------------------------------------------------------------
# Chat Library Target (ChatLib)
# ----------------------------------------------------------------------
add_library(ChatLib) # Default: STATIC library
target_sources(ChatLib PRIVATE
    src/MessageHistory.cpp
    src/ChatRoom.cpp
    src/ChatSession.cpp
    src/ChatListener.cpp
    src/ChatServer.cpp
)
target_include_directories(ChatLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> # Public headers
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIRS}> # Boost needed by headers
    $<BUILD_INTERFACE:${spdlog_INCLUDE_DIRS}> # spdlog needed by implementation details (log macros if used in headers) - consider PRIVATE if only in .cpp
)
target_link_libraries(ChatLib PUBLIC # Libraries needed by ChatLib and potentially its users
    Boost::asio
    Boost::beast
    Boost::system
    spdlog::spdlog
)
# Link Threads conditionally for ChatLib if it uses threads directly
if(Threads_FOUND AND NOT WIN32)
    target_link_libraries(ChatLib PUBLIC Threads::Threads)
endif()
message(STATUS "Configured target: ChatLib (Chat Server Logic)")

# --- 라이브러리 타겟 정의: HttpServerLib (HTTP Health Check 및 API) ---
add_library(HttpServerLib STATIC)
target_sources(HttpServerLib PRIVATE
    src/HttpServer.cpp
    src/handlers/PlacesApiHandler.cpp # API 핸들러
)
target_include_directories(HttpServerLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> # HttpServer.hpp 포함
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIRS}>               # Beast/Asio 헤더 필요
)
target_link_libraries(HttpServerLib PUBLIC # 필요한 라이브러리 링크
    Boost::beast
    Boost::system
    Boost::asio # HttpServer 내부에서 Asio 사용 시 필요
    Boost::json # Places API 응답 처리에 필요
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
)

# 플랫폼별 스레드 라이브러리 링크 (HttpServer 가 스레드 사용 시)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    if(Threads_FOUND)
        target_link_libraries(HttpServerLib PUBLIC Threads::Threads)
    endif()
endif()
message(STATUS "Configured target: HttpServerLib library (HTTP Health)")

# --- 라이브러리 타겟 정의: ChatServerLib (채팅 서버 로직) ---
add_library(ChatServerLib STATIC)
target_sources(ChatServerLib PRIVATE
    src/ChatServer.cpp
    src/ChatRoom.cpp
    src/MessageHistory.cpp
    # 세션 관리
    src/ChatSession.cpp
    # WebSocket 리스너 및 세션
    src/WebSocketListener.cpp
    src/WebSocketSession.cpp
)
target_include_directories(ChatServerLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> # ChatServer.hpp 등
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIRS}>              
)
target_link_libraries(ChatServerLib PUBLIC 
    Boost::asio
    Boost::beast # WebSocket 사용
    Boost::system
    spdlog::spdlog
)
# 플랫폼별 스레드 라이브러리 링크 (ChatServer 가 스레드 사용 시)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    if(Threads_FOUND)
        target_link_libraries(ChatServerLib PUBLIC Threads::Threads)
    endif()
endif()
message(STATUS "Configured target: ChatServerLib library (WebSocket Chat)")

# ----------------------------------------------------------------------
# 실행 파일 타겟 정의
# ----------------------------------------------------------------------

# Beast 기반 실행 파일 (모든 플랫폼)
add_executable(CherryRecorder-Server-App)
target_sources(CherryRecorder-Server-App 
    PRIVATE
        src/main.cpp
)
target_include_directories(CherryRecorder-Server-App 
    PRIVATE 
        ${Boost_INCLUDE_DIRS}
)
target_link_libraries(CherryRecorder-Server-App
    PRIVATE
        ChatServerLib
        HttpServerLib
)
if(NOT WIN32)
    target_link_libraries(CherryRecorder-Server-App PRIVATE Threads::Threads)
endif()
message(STATUS "Configured target: CherryRecorder-Server-App executable (Beast-based)")

# CherryRecorder-App 별칭 추가 (Dockerfile 호환성)
add_executable(CherryRecorder-App ALIAS CherryRecorder-Server-App)
message(STATUS "Created alias: CherryRecorder-App -> CherryRecorder-Server-App")

# ----------------------------------------------------------------------
# 테스트 설정 (gtest)
# ----------------------------------------------------------------------
# BUILD_TESTING 옵션 정의 (기본값 OFF)
# 이 옵션을 ON으로 설정해야만 테스트 빌드가 활성화됩니다.
option(BUILD_TESTING "Build the tests" OFF)

if(BUILD_TESTING)
    message(STATUS "BUILD_TESTING is ON. Configuring tests...")

    # vcpkg를 통해 gtest를 찾습니다.
    # vcpkg.json의 "features" 섹션에 "test"가 정의되어 있어야 합니다.
    # (예: "gtest": {"host": true, "features": ["test"]})
    # CMake 실행 시 -DVCPKG_MANIFEST_FEATURES=test 플래그가 필요할 수 있습니다.
    find_package(GTest REQUIRED)
    
    # googletest 활성화
    include(GoogleTest)

    # 테스트 실행 파일 정의
    add_executable(CherryRecorder-Tests
        tests/test_main.cpp
        tests/test_chat_server.cpp
        tests/test_http_server.cpp
    )

    # 테스트 실행 파일에 필요한 라이브러리 링크
    target_link_libraries(CherryRecorder-Tests PRIVATE
        ChatServerLib
        HttpServerLib
        OpenSSL::SSL
        OpenSSL::Crypto
        GTest::gmock_main # gmock과 gtest를 모두 포함하며 main 함수 제공
        GTest::gtest
        GTest::gmock
    )
    
    # gtest_discover_tests를 사용하여 CTest에 테스트를 추가합니다.
    gtest_discover_tests(CherryRecorder-Tests)
    
    message(STATUS "Configured target: CherryRecorder-Tests executable")

else()
    message(STATUS "BUILD_TESTING is OFF. Skipping test configuration.")
endif()

# ----------------------------------------------------------------------
# 설치 설정 (선택 사항)
# ----------------------------------------------------------------------
# 설치(install) 명령어를 사용할 경우, 실행 파일과 라이브러리를
# 지정된 디렉토리에 복사하도록 설정할 수 있습니다.
# 예: install(TARGETS CherryRecorder-Server-App DESTINATION bin)
# 현재 프로젝트에서는 로컬 빌드 및 실행이 주 목적이므로 생략합니다.

# ----------------------------------------------------------------------
# CPack 설정 (선택 사항)
# ----------------------------------------------------------------------
# 애플리케이션 배포 패키지(예: .zip, .tar.gz)를 생성하기 위한 설정입니다.
# 현재 프로젝트에서는 필요하지 않으므로 생략합니다.

# 항상 Release 모드로 설정
add_compile_definitions(RELEASE_MODE)
message(STATUS "Using Release mode compilation definitions.")