# HttpServerLib 라이브러리 정의
add_library(HttpServerLib
    # Beast HTTP 서버
    HttpServer.cpp
    # Places API 핸들러
    PlacesApiHandler.cpp
    # HttpsServer 구현
    HttpsServer.cpp
)

# ChatServerLib에 대한 타겟 포함 디렉토리 설정
target_include_directories(HttpServerLib PUBLIC
    # ... existing code ...
) 