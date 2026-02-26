#include "HttpServer.hpp"
#include <boost/beast/version.hpp>
#include <boost/asio/dispatch.hpp> // net::dispatch 사용
#include <boost/core/ignore_unused.hpp> // boost::ignore_unused 사용

#include <chrono>
#include <algorithm>
#include <exception> // std::terminate, std::exception
#include <memory>
#include <thread> // std::thread
#include <vector>
#include <string> // std::string 사용
#include <cstdio> // fprintf 사용
#include <sstream> // std::stringstream (스레드 ID 로깅용)
#include <stdexcept>
#include <mutex>
#include <boost/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>

// 필요한 네임스페이스 정의 (HttpServer.hpp 와 동일하게)
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
namespace json = boost::json;

namespace {
std::mutex memory_mutex;
std::vector<json::object> memory_moments;
bool memory_store_loaded = false;

std::filesystem::path memory_store_path() {
    if (const char* configured = std::getenv("MEMORY_ATLAS_DATA_FILE"); configured != nullptr && *configured != '\0') {
        return configured;
    }
    return std::filesystem::path("data") / "memories.json";
}

void load_memory_store_locked() {
    if (memory_store_loaded) {
        return;
    }
    memory_store_loaded = true;

    const auto path = memory_store_path();
    std::ifstream input(path);
    if (!input.is_open()) {
        return;
    }

    try {
        std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (contents.empty()) {
            return;
        }
        const auto parsed = json::parse(contents);
        if (!parsed.is_array()) {
            fprintf(stderr, "Memory store ignored because '%s' is not a JSON array.\n", path.string().c_str());
            return;
        }
        for (const auto& item : parsed.as_array()) {
            if (item.is_object()) {
                memory_moments.emplace_back(item.as_object());
            }
        }
        fprintf(stdout, "Loaded %zu Memory Atlas moments from %s.\n", memory_moments.size(), path.string().c_str());
    }
    catch (const std::exception& exception) {
        fprintf(stderr, "Memory store load failed for '%s': %s\n", path.string().c_str(), exception.what());
    }
}

void persist_memory_store_locked() {
    const auto path = memory_store_path();
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    json::array payload;
    payload.reserve(memory_moments.size());
    for (const auto& memory : memory_moments) {
        payload.emplace_back(memory);
    }

    const auto temporary_path = path.string() + ".tmp";
    {
        std::ofstream output(temporary_path, std::ios::trunc);
        if (!output.is_open()) {
            throw std::runtime_error("Unable to open Memory Atlas persistence file for writing.");
        }
        output << json::serialize(payload);
        output.flush();
        if (!output.good()) {
            throw std::runtime_error("Unable to flush Memory Atlas persistence file.");
        }
    }
    std::filesystem::rename(temporary_path, path);
}
}

/**
 * @file HttpServer.cpp
 * @brief Boost.Beast를 이용한 비동기 HTTP 서버 관련 클래스 구현 파일.
 *
 * `HttpSession`, `HttpListener`, `HttpServer` 클래스의 실제 구현을 포함한다.
 * `HttpSession`은 개별 클라이언트 연결의 HTTP 요청/응답 처리를 담당한다.
 * 현재는 `/health` 경로에 대한 GET 요청만 처리하며, 그 외 경로는 404 Not Found를 반환한다.
 * @see HttpSession, HttpListener, HttpServer
 */

 /**
  * @class HttpSession
  * @brief 개별 HTTP 클라이언트 연결을 처리하는 세션 클래스.
  * @details `std::enable_shared_from_this`를 상속하여 비동기 작업 중 객체 생존을 보장한다.
  * Beast의 비동기 작업을 사용하여 요청을 읽고 응답을 보낸다.
  * 현재 `/health` 엔드포인트를 지원한다.
  */
#include "handlers/PlacesApiHandler.hpp"

// 에러 출력 헬퍼 함수
void fail(beast::error_code ec, char const* what)
{
    fprintf(stderr, "Error: %s: %s\n", what, ec.message().c_str());
}

class HttpSession : public std::enable_shared_from_this<HttpSession>
{
    beast::tcp_stream stream_; ///< @brief TCP 소켓을 감싸는 Beast 스트림 객체. 비동기 I/O 작업을 수행한다.
    beast::flat_buffer buffer_; ///< @brief HTTP 메시지 읽기/쓰기를 위한 버퍼.
    http::request<http::string_body> req_; ///< @brief 수신한 HTTP 요청 메시지 객체. 본문은 문자열로 저장.
    std::shared_ptr<PlacesApiHandler> places_handler_; ///< @brief 장소 API 요청 처리 핸들러.

public:
    /**
     * @brief HttpSession 생성자.
     * @param socket 클라이언트와 연결된 TCP 소켓. 소유권이 이동된다.
     * @param places_handler Places API 요청 처리 핸들러.
     */
    explicit HttpSession(tcp::socket&& socket, std::shared_ptr<PlacesApiHandler> places_handler)
        : stream_(std::move(socket)), places_handler_(places_handler) {
        fprintf(stdout, "[HttpSession %p] Created.\n", (void*)this);
    }

    /**
    * @brief HttpSession 소멸자.
    *
    * 세션 객체가 소멸될 때 로그를 남긴다. 스트림과 소켓은 자동으로 정리된다.
    */
    ~HttpSession() {
        fprintf(stdout, "[HttpSession %p] Destroyed.\n", (void*)this);
    }

    /**
     * @brief 세션을 시작하여 비동기적으로 HTTP 요청 읽기를 시작한다.
     *
     * `net::dispatch`를 사용하여 `do_read()` 메서드를 스트림의 실행자(executor) 컨텍스트에서 실행하도록 예약한다.
     */
    void run() {
        // I/O 작업이 스트림의 실행자(기본적으로 소켓의 io_context와 동일한 strand 또는 스레드)에서 실행되도록 보장
        net::dispatch(stream_.get_executor(),
            // bind_front_handler: 멤버 함수 호출 시 첫 번째 인자로 this (shared_ptr)를 자동으로 전달
            beast::bind_front_handler(&HttpSession::on_run_dispatched, shared_from_this()));
    }

private:




    /**
    * @brief `run()`에서 `net::dispatch`된 후 실제 실행되는 함수.
    *
    * 첫 번째 `do_read()`를 호출하여 요청 읽기 프로세스를 시작한다.
    */
    void on_run_dispatched() {
        fprintf(stdout, "[HttpSession %p] Starting read loop.\n", (void*)this);
        do_read();
    }

    /**
     * @brief 비동기적으로 HTTP 요청 메시지를 읽는다.
     *
     * 요청 객체(`req_`)를 초기화하고, 스트림 타임아웃을 설정한 후,
     * `http::async_read`를 호출하여 요청 헤더와 본문을 비동기적으로 읽는다.
     * 완료 시 `on_read` 콜백 함수가 호출된다.
     */
    void do_read() {
        // 새 요청을 위해 파서(요청 객체) 초기화
        req_ = {};

        // 읽기 타임아웃 설정 (Beast 권장). 30초 동안 데이터 수신 없으면 타임아웃.
        stream_.expires_after(std::chrono::seconds(30));

        // 비동기적으로 요청 읽기 시작
        fprintf(stdout, "[HttpSession %p] Waiting to read request...\n", (void*)this);
        http::async_read(stream_, buffer_, req_,
            // 완료 시 on_read 호출. shared_from_this()로 객체 생존 보장.
            beast::bind_front_handler(&HttpSession::on_read, shared_from_this()));
    }

    /**
     * @brief 비동기 읽기 작업 완료 시 호출되는 콜백 함수.
     * @param ec 작업 결과 에러 코드.
     * @param bytes_transferred 전송된 바이트 수 (현재 사용 안 함).
     *
     * 에러를 확인하고, 정상 수신 시 요청 경로(`/health` 또는 기타)에 따라
     * 적절한 핸들러 함수(`handle_health_check_request` 또는 `handle_not_found_request`)를 호출한다.
     * 오류 발생 시 또는 클라이언트 연결 종료 시(`http::error::end_of_stream`) `do_close`를 호출하여 세션을 종료한다.
     */
    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred); // bytes_transferred 사용하지 않으므로 경고 방지

        // 클라이언트가 연결을 정상적으로 종료한 경우 (Keep-Alive 아닐 때 다음 요청 전에 발생 가능)
        if (ec == http::error::end_of_stream) {
            fprintf(stdout, "[HttpSession %p] Read finished: End of stream.\n", (void*)this);
            return do_close();
        }
        // 그 외 읽기 오류 (타임아웃 포함)
        if (ec) {
            fprintf(stderr, "[HttpSession %p] Read error: %s\n", (void*)this, ec.message().c_str());
            return do_close();
        }

        // 읽기 성공, 요청 처리 시작
        fprintf(stdout, "[HttpSession %p] Read successful. Request: %s %s\n", (void*)this,
            std::string(http::to_string(req_.method())).c_str(), std::string(req_.target()).c_str());


        // ----- 요청 라우팅 -----
        if (req_.method() == http::verb::get && req_.target() == "/health") {
            handle_health_check_request(); // Health Check 요청 처리
        }
        else if (req_.method() == http::verb::options) {
            handle_options_request();
        }
        else if (req_.method() == http::verb::get && req_.target() == "/maps/key") {
            handle_maps_key_request(); // Google Maps API 키 요청 처리
        }
        else if (req_.method() == http::verb::post && req_.target() == "/places/nearby") {
            fprintf(stdout, "[HttpSession %p] Places API 요청 감지: /places/nearby\n", (void*)this);
            handle_places_nearby_request(); // 주변 장소 검색 요청 처리
        }
        else if (req_.method() == http::verb::post && req_.target() == "/places/search") {
            fprintf(stdout, "[HttpSession %p] Places API 요청 감지: /places/search\n", (void*)this);
            handle_places_search_request(); // 장소 검색 요청 처리
        }
        else if (req_.method() == http::verb::get && req_.target() == "/memories") {
            handle_memory_list_request();
        }
        else if (req_.method() == http::verb::post && req_.target() == "/memories") {
            handle_memory_create_request();
        }
        else if (req_.method() == http::verb::delete_ && req_.target().starts_with("/memories/")) {
            handle_memory_delete_request(std::string(req_.target()).substr(std::string("/memories/").size()));
        }
        else if (req_.method() == http::verb::post && req_.target() == "/places/details") {
            // fprintf(stdout, "[HttpSession %p] Places API 요청 감지: /places/details (POST - Deprecated)\n", (void*)this);
            // handle_place_details_request(); // 기존 방식 제거
            handle_not_found_request(); // POST /places/details는 이제 지원하지 않음
        }
        else if (req_.method() == http::verb::get && req_.target().starts_with("/places/details/")) {
            fprintf(stdout, "[HttpSession %p] Places API Details GET 요청 감지: %s\n", (void*)this, std::string(req_.target()).c_str());
            // 경로에서 Place ID 추출
            std::string target_path(req_.target()); // string_view를 std::string으로 변환
            std::string place_id = target_path.substr(std::string("/places/details/").length());
            if (!place_id.empty()) {
                handle_place_details_request(place_id); // 추출한 ID 전달
            }
            else {
                // Place ID가 없는 경우 잘못된 요청 처리
                handle_bad_request("Missing Place ID in /places/details/ request.");
            }
        }
        else if (req_.method() == http::verb::get && req_.target().starts_with("/place/photo/")) {
            fprintf(stdout, "[HttpSession %p] Places API Photo GET 요청 감지: %s\n", (void*)this, std::string(req_.target()).c_str());
            // 경로에서 Photo Reference 추출
            std::string target_path(req_.target()); // string_view를 std::string으로 변환
            std::string photo_reference = target_path.substr(std::string("/place/photo/").length());
            if (!photo_reference.empty()) {
                handle_place_photo_request(photo_reference); // 추출한 참조 ID 전달
            }
            else {
                // Photo Reference가 없는 경우 잘못된 요청 처리
                handle_bad_request("Missing Photo Reference in /place/photo/ request.");
            }
        }
        else if (req_.target() == "/status") {
            // HTTP 200 OK 응답 생성
            http::response<http::string_body> res{http::status::ok, req_.version()};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "application/json"); // Content-Type 설정
            res.keep_alive(req_.keep_alive());

            // JSON 응답 본문 설정
            res.body() = "{\"status\": \"ok\"}"; // 간단한 상태 메시지
            res.prepare_payload(); // Content-Length 등 자동 계산

            // 응답 전송 (기존 send_response 메서드 사용)
            send_response(std::move(res));
            return; // 처리 완료
        }
        else {
            // 다른 모든 요청은 404 Not Found 처리
            handle_not_found_request();
            // TODO: 실제 서비스 로직(정적 파일 제공, API 처리 등) 추가 구현 필요
            // 예: handle_static_file_request(req_.target());
        }
    }

    /**
     * @brief 주변 장소 검색 요청 처리
     */
    void handle_places_nearby_request() {
        fprintf(stdout, "[HttpSession %p] Handling /places/nearby request.\n", (void*)this);
        http::response<http::string_body> res = places_handler_->handleNearbySearch(req_);

        send_response(std::move(res));
    }
    
    /**
     * @brief 장소 검색 요청 처리
     */
    void handle_places_search_request() {
        fprintf(stdout, "[HttpSession %p] Handling /places/search request.\n", (void*)this);
        http::response<http::string_body> res = places_handler_->handleTextSearch(req_);

        send_response(std::move(res));
    }

    /**
     * @brief Memory Atlas에 저장된 순간 목록을 반환한다.
     *
     * JSON 파일 기반 영속 저장소를 사용해 프로세스 재시작 뒤에도 기억을 유지한다.
     * 저장 경로는 MEMORY_ATLAS_DATA_FILE 환경변수로 재정의할 수 있다.
     */
    void handle_memory_list_request() {
        json::array memories;
        {
            std::scoped_lock lock(memory_mutex);
            load_memory_store_locked();
            for (const auto& memory : memory_moments) {
                memories.emplace_back(memory);
            }
        }

        json::object payload;
        payload["count"] = static_cast<std::int64_t>(memories.size());
        payload["items"] = std::move(memories);

        http::response<http::string_body> res{http::status::ok, req_.version()};
        res.set(http::field::server, "MemoryAtlas");
        res.set(http::field::content_type, "application/json; charset=utf-8");
        res.keep_alive(req_.keep_alive());
        res.body() = json::serialize(payload);
        res.prepare_payload();
        send_response(std::move(res));
    }

    /**
     * @brief 장소에 연결된 개인 기억을 생성한다.
     *
     * 필수 값은 place와 title이며, sense는 향/음악/맛/기분 같은 자유로운 감각 메모다.
     */
    void handle_memory_create_request() {
        try {
            const auto parsed = json::parse(req_.body());
            if (!parsed.is_object()) {
                handle_bad_request("Memory payload must be a JSON object.");
                return;
            }

            const auto& input = parsed.as_object();
            const auto* place_value = input.if_contains("place");
            const auto* title_value = input.if_contains("title");

            if (place_value == nullptr || !place_value->is_string() || place_value->as_string().empty()) {
                handle_bad_request("Field 'place' is required.");
                return;
            }
            if (title_value == nullptr || !title_value->is_string() || title_value->as_string().empty()) {
                handle_bad_request("Field 'title' is required.");
                return;
            }

            const auto now = std::chrono::system_clock::now();
            const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();

            json::object memory;
            memory["id"] = std::to_string(timestamp);
            memory["place"] = place_value->as_string();
            memory["title"] = title_value->as_string();
            memory["sense"] = input.if_contains("sense") != nullptr && input.at("sense").is_string()
                ? input.at("sense").as_string()
                : json::string("");
            const auto* latitude_value = input.if_contains("latitude");
            const auto* longitude_value = input.if_contains("longitude");
            const bool has_latitude = latitude_value != nullptr
                && (latitude_value->is_double() || latitude_value->is_int64() || latitude_value->is_uint64());
            const bool has_longitude = longitude_value != nullptr
                && (longitude_value->is_double() || longitude_value->is_int64() || longitude_value->is_uint64());
            memory["latitude"] = has_latitude ? *latitude_value : json::value(nullptr);
            memory["longitude"] = has_longitude ? *longitude_value : json::value(nullptr);
            memory["createdAtEpochMs"] = timestamp;

            {
                std::scoped_lock lock(memory_mutex);
                load_memory_store_locked();
                memory_moments.insert(memory_moments.begin(), memory);
                persist_memory_store_locked();
            }

            http::response<http::string_body> res{http::status::created, req_.version()};
            res.set(http::field::server, "MemoryAtlas");
            res.set(http::field::content_type, "application/json; charset=utf-8");
            res.keep_alive(req_.keep_alive());
            res.body() = json::serialize(memory);
            res.prepare_payload();
            send_response(std::move(res));
        }
        catch (const std::exception& exception) {
            fprintf(stderr, "[HttpSession %p] Invalid memory payload: %s\n", (void*)this, exception.what());
            handle_bad_request("Invalid JSON payload for memory.");
        }
    }

    void handle_memory_delete_request(const std::string& memory_id) {
        if (memory_id.empty()) {
            handle_bad_request("Memory id is required.");
            return;
        }

        bool removed = false;
        try {
            std::scoped_lock lock(memory_mutex);
            load_memory_store_locked();
            const auto before = memory_moments.size();
            memory_moments.erase(
                std::remove_if(
                    memory_moments.begin(),
                    memory_moments.end(),
                    [&memory_id](const json::object& memory) {
                        const auto* id = memory.if_contains("id");
                        return id != nullptr && id->is_string() && id->as_string() == memory_id;
                    }),
                memory_moments.end());
            removed = memory_moments.size() != before;
            if (removed) {
                persist_memory_store_locked();
            }
        }
        catch (const std::exception& exception) {
            fprintf(stderr, "Memory delete persistence failure: %s\n", exception.what());
            handle_server_error("Unable to persist memory deletion.");
            return;
        }

        if (!removed) {
            http::response<http::string_body> res{http::status::not_found, req_.version()};
            res.set(http::field::server, "MemoryAtlas");
            res.set(http::field::content_type, "application/json; charset=utf-8");
            res.keep_alive(req_.keep_alive());
            res.body() = "{\"error\":\"Memory not found\"}";
            res.prepare_payload();
            send_response(std::move(res));
            return;
        }

        http::response<http::string_body> res{http::status::no_content, req_.version()};
        res.set(http::field::server, "MemoryAtlas");
        res.keep_alive(req_.keep_alive());
        res.prepare_payload();
        send_response(std::move(res));
    }

    void handle_options_request() {
        http::response<http::string_body> res{http::status::no_content, req_.version()};
        res.set(http::field::server, "MemoryAtlas");
        res.keep_alive(req_.keep_alive());
        res.prepare_payload();
        send_response(std::move(res));
    }
    
    /**
     * @brief 장소 상세 정보 요청 처리 (Place ID 인자 받도록 수정)
     * @param place_id URL 경로에서 추출한 장소 ID
     */
    void handle_place_details_request(const std::string& place_id) { // place_id 인자 추가
        fprintf(stdout, "[HttpSession %p] Handling /places/details request for ID: %s\n", (void*)this, place_id.c_str());
        
        // Google Places API 상세 정보 요청 처리 후 응답 반환
        ///< @note 현재는 Google API 응답 형식을 그대로 클라이언트에 반환합니다.
        ///< @todo 필요 시 응답 형식을 변환하는 transformPlaceDetails 함수를 구현할 수 있습니다.
        http::response<http::string_body> res = places_handler_->handlePlaceDetails(place_id);
        send_response(std::move(res));
    }

    /**
     * @brief 장소 사진 요청 처리
     * @param photo_reference URL 경로에서 추출한 사진 참조 ID
     */
    void handle_place_photo_request(const std::string& photo_reference) { 
        fprintf(stdout, "[HttpSession %p] Handling /place/photo request for reference: %s\n", (void*)this, photo_reference.c_str());
        
        // Google Places Photo API를 통해 이미지 가져오기
        http::response<http::string_body> res = places_handler_->handlePlacePhoto(photo_reference);
        send_response(std::move(res));
    }

    /**
     * @brief Google Maps API 키를 제공하는 엔드포인트
     * 
     * 클라이언트의 웹 플랫폼에서 Google Maps를 사용할 때 필요한 API 키를 제공한다.
     * 이 방식을 통해 API 키를 소스 코드에 노출하지 않고도 안전하게 관리할 수 있다.
     */
    void handle_maps_key_request() {
        fprintf(stdout, "[HttpSession %p] Handling /maps/key request.\n", (void*)this);
        
        // 환경변수에서 API 키 가져오기
        const char* api_key = std::getenv("GOOGLE_MAPS_API_KEY");
        if (api_key == nullptr || api_key[0] == '\0') {
            // API 키가 없는 경우 오류 처리
            handle_bad_request("Google Maps API key is not configured on the server");
            return;
        }
        
        // 응답 객체 생성
        http::response<http::string_body> res{ http::status::ok, req_.version() };
        res.set(http::field::server, "WebServer");
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(req_.keep_alive());
        
        // API 키를 응답 본문으로 설정
        res.body() = api_key;
        

        
        // 응답 전송
        res.prepare_payload();
        send_response(std::move(res));
    }

    /**
     * @brief `/health` 경로에 대한 GET 요청을 처리한다.
     *
     * 상태 코드 200 OK와 "OK" 문자열 본문을 포함하는 HTTP 응답을 생성하고 `send_response`를 통해 전송한다.
     */
    void handle_health_check_request() {
        fprintf(stdout, "[HttpSession %p] Handling /health request.\n", (void*)this);
        // 응답 객체 생성 (상태코드 OK, 요청과 동일한 HTTP 버전 사용)
        http::response<http::string_body> res{ http::status::ok, req_.version() };
        // 응답 헤더 설정
        res.set(http::field::server, "WebServer"); // Server 헤더
        res.set(http::field::content_type, "text/plain");         // Content-Type 헤더
        res.keep_alive(req_.keep_alive());                       // 요청의 Keep-Alive 설정 따름
        // 응답 본문 설정
        res.body() = "OK";
        // 응답 본문 길이에 맞춰 Content-Length 헤더 등 자동 계산 및 설정
        res.prepare_payload();
        // 생성된 응답 전송
        send_response(std::move(res));
    }

    /**
     * @brief 잘못된 요청(Bad Request) 처리 함수 추가
     * @param why 오류 사유 문자열
     */
    void handle_bad_request(beast::string_view why) {
        fprintf(stdout, "[HttpSession %p] Handling Bad Request (400): %s\n", (void*)this, std::string(why).c_str());
        http::response<http::string_body> res{http::status::bad_request, 11};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        res.body() = std::string(why);
        res.prepare_payload();
        send_response(std::move(res));
    }

    void handle_server_error(beast::string_view why) {
        http::response<http::string_body> res{http::status::internal_server_error, req_.version()};
        res.set(http::field::server, "MemoryAtlas");
        res.set(http::field::content_type, "application/json; charset=utf-8");
        res.keep_alive(req_.keep_alive());
        json::object payload;
        payload["error"] = std::string(why);
        res.body() = json::serialize(payload);
        res.prepare_payload();
        send_response(std::move(res));
    }

    /**
     * @brief 정의되지 않은 경로에 대한 요청을 처리한다.
     *
     * 상태 코드 404 Not Found와 에러 메시지 본문을 포함하는 HTTP 응답을 생성하고 `send_response`를 통해 전송한다.
     */
    void handle_not_found_request() {
        fprintf(stdout, "[HttpSession %p] Handling unknown request (404 Not Found).\n", (void*)this);
        // 응답 객체 생성 (상태코드 Not Found, 요청과 동일한 HTTP 버전)
        http::response<http::string_body> res{ http::status::not_found, req_.version() };
        // 응답 헤더 설정
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(req_.keep_alive());
        // 응답 본문 설정
        res.body() = "The resource '" + std::string(req_.target()) + "' was not found.";
        // 응답 본문 길이에 맞춰 Content-Length 헤더 등 자동 계산 및 설정
        res.prepare_payload();
        // 생성된 응답 전송
        send_response(std::move(res));
    }

    /**
     * @brief 생성된 HTTP 응답 메시지를 클라이언트에게 비동기적으로 전송한다.
     * @param res 전송할 HTTP 응답 객체 (`http::response<http::string_body>`). 소유권이 이동된다.
     *
     * 응답 객체를 `std::shared_ptr`로 감싸 비동기 쓰기 작업 중 생존을 보장한다.
     * `http::async_write`를 호출하여 응답을 전송하고, 완료 시 `on_write` 콜백 함수가 호출된다.
     */
    void send_response(http::response<http::string_body>&& res) { // rvalue reference로 받아 move 사용
        res.set(http::field::access_control_allow_origin, "*");
        res.set(http::field::access_control_allow_methods, "GET, POST, DELETE, OPTIONS");
        res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
        // 응답 객체의 수명을 비동기 작업 완료까지 연장하기 위해 shared_ptr 사용
        auto sp = std::make_shared<http::response<http::string_body>>(std::move(res));

        // 쓰기 타임아웃 설정 (Beast 권장). 30초.
        stream_.expires_after(std::chrono::seconds(30));

        fprintf(stdout, "[HttpSession %p] Writing response (Status: %d)...\n", (void*)this, sp->result_int());
        // 비동기적으로 응답 쓰기 시작
        http::async_write(stream_, *sp, // shared_ptr이 가리키는 응답 객체 전달
            // 완료 시 on_write 호출. shared_ptr(sp)와 shared_from_this()(self) 캡처로 생존 보장.
            [self = shared_from_this(), sp](beast::error_code ec, std::size_t bytes_transferred) {
                // on_write 콜백에서 응답 객체 정보(예: need_eof) 접근 가능
                self->on_write(sp->need_eof(), ec, bytes_transferred);
            });
    }

    /**
     * @brief 비동기 쓰기 작업 완료 시 호출되는 콜백 함수.
     * @param close 연결 종료 필요 여부. 응답 객체의 `need_eof()` 결과. (HTTP/1.0 또는 Connection: close 헤더)
     * @param ec 작업 결과 에러 코드.
     * @param bytes_transferred 전송된 바이트 수 (현재 사용 안 함).
     *
     * 에러를 확인하고, 오류 발생 시 `do_close`를 호출한다.
     * 오류가 없고 `close` 플래그가 true이면 `do_close`를 호출한다.
     * 오류가 없고 `close` 플래그가 false (Keep-Alive)이면 `do_read`를 호출하여 다음 요청을 기다린다.
     */
    void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if (ec) { // 쓰기 오류 발생
            fprintf(stderr, "[HttpSession %p] Write error: %s (%d).\n", (void*)this, ec.message().c_str(), ec.value());
            return do_close(); // 오류 시 연결 종료
        }

        fprintf(stdout, "[HttpSession %p] Write successful (%zu bytes).\n", (void*)this, bytes_transferred);

        if (close) {
            // HTTP/1.0 이거나 응답에 Connection: close 헤더가 있으면 연결 종료
            fprintf(stdout, "[HttpSession %p] Closing connection after write (need_eof is true).\n", (void*)this);
            return do_close();
        }

        // Keep-Alive 연결이면, 다음 요청을 읽기 위해 do_read() 호출
        fprintf(stdout, "[HttpSession %p] Keep-Alive connection, waiting for next request.\n", (void*)this);
        do_read();
    }

    /**
     * @brief 소켓 연결을 정상적으로 종료한다.
     *
     * TCP 연결의 전송(send) 방향을 먼저 닫고, 스트림 객체가 소멸될 때 소켓이 완전히 닫히도록 한다.
     * (Beast::tcp_stream 소멸자가 내부적으로 close 호출)
     */
    void do_close() {
        fprintf(stdout, "[HttpSession %p] Closing connection.\n", (void*)this);
        beast::error_code ec;
        ///< 전송 방향 셧다운 시도. 오류 발생 가능성 있음 (이미 닫혔거나 등)
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
        if (ec) {
            fprintf(stderr, "[HttpSession %p] Shutdown(send) warning: %s\n", (void*)this, ec.message().c_str());
        }
        ///< 스트림/소켓의 close는 HttpSession 객체가 소멸될 때 자동으로 처리됨.
        ///< 명시적 close가 필요하다면 여기에 추가. stream_.close(ec);
    }
};

// --- HttpListener 구현 ---

/**
 * @brief HttpListener 생성자 구현부.
 */
HttpListener::HttpListener(
    net::io_context& ioc,
    tcp::endpoint endpoint)
    : ioc_(ioc)
    , acceptor_(net::make_strand(ioc))
{
    beast::error_code ec;

    // 지정된 endpoint에 TCP acceptor를 열고 
    // reuse_address 옵션 활성화 (동일 포트 재사용 가능)
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
        fail(ec, "open");
        return;
    }

    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
        fail(ec, "set_option");
        return;
    }

    // 지정된 endpoint에 바인딩
    acceptor_.bind(endpoint, ec);
    if (ec) {
        fail(ec, "bind");
        return;
    }

    // 요청 대기 큐 시작
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
        fail(ec, "listen");
        return;
    }
    
    // 여기서 PlacesApiHandler 초기화 (한 번만 생성)
    const char* api_key = std::getenv("GOOGLE_MAPS_API_KEY");
    std::string google_api_key;
    if (api_key != nullptr && strlen(api_key) > 0) {
        fprintf(stdout, "[HttpListener %p] Google Maps API 키 로드됨 (길이: %zu)\n", 
               (void*)this, strlen(api_key));
        google_api_key = api_key;
    } else {
        fprintf(stderr, "[HttpListener %p] GOOGLE_MAPS_API_KEY가 없어 Places 기능만 비활성 상태로 시작합니다.\n",
               (void*)this);
    }
    
    // 장소 API 핸들러 생성 (멤버 변수에 저장)
    places_handler_ = std::make_shared<PlacesApiHandler>(google_api_key);
    fprintf(stdout, "[HttpListener %p] PlacesApiHandler 생성됨 (싱글톤)\n", (void*)this);
}

/**
 * @brief 리스너 실행 함수 구현부.
 */
void HttpListener::run() {
    fprintf(stdout, "[HttpListener %p] Starting accept loop...\n", (void*)this);
    // 첫 번째 비동기 accept 호출 시작
    do_accept();
}

/**
 * @brief 비동기 accept 함수 구현부.
 */
void HttpListener::do_accept() {
    // 다음 연결 요청을 비동기적으로 기다린다.
    // 새 연결에 대한 소켓 생성 및 accept 작업은 지정된 strand(acceptor_ 생성 시 지정)에서 실행됨.
    acceptor_.async_accept(
        net::make_strand(ioc_), // 새 연결 소켓도 별도의 strand에서 처리하는 것이 일반적 (동시성 관리)
        // 완료 시 on_accept 호출. shared_from_this()로 HttpListener 객체 생존 보장.
        beast::bind_front_handler(&HttpListener::on_accept, shared_from_this()));
}

/**
 * @brief 비동기 accept 완료 콜백 함수 구현부.
 */
void HttpListener::on_accept(beast::error_code ec, tcp::socket socket) {
    // 오류 발생 시 보고
    if (ec) {
        fail(ec, "accept");
    }
    else {
        // 클라이언트의 IP 주소를 출력
        fprintf(stdout, "[HttpListener %p] Accepted connection from %s:%d\n",
               (void*)this,
               socket.remote_endpoint().address().to_string().c_str(),
               socket.remote_endpoint().port());

        // 새 연결에 대한 HttpSession 객체 생성 및 실행
        // std::move(socket)으로 소켓 소유권 이전
        std::make_shared<HttpSession>(std::move(socket), places_handler_)->run();
    }

    // 오류 발생 여부와 관계없이 다음 연결 수락 준비 (리스너가 중지되지 않는 한 계속)
    ///< @todo 리스너 중지 로직 필요 시 추가 (예: 특정 오류 발생 시 또는 외부 신호 수신 시)
    do_accept();
}

// --- HttpServer 구현 ---

/**
 * @brief HttpServer 생성자 구현부.
 */
HttpServer::HttpServer(const std::string& address, unsigned short port, int threads)
    : address_(address)
    , port_(port)
    , threads_(threads)
    // io_context 생성 시 스레드 수(concurrency hint) 지정. 1 이상이어야 함.
    , ioc_(threads_ > 0 ? threads_ : 1)
{
    fprintf(stdout, "[HttpServer %p] Created with address=%s, port=%d, threads=%d\n",
        (void*)this, address_.c_str(), port_, threads_);
}

/**
 * @brief 서버 실행 함수 구현부.
 */
void HttpServer::run() {
    auto const addr = net::ip::make_address(address_); ///< 문자열 주소를 Asio 주소 객체로 변환
    auto const port = static_cast<unsigned short>(port_);
    int num_threads = threads_; ///< 사용할 실제 스레드 수

    // 스레드 수 결정 (0 이하면 시스템 코어 수 기반으로 결정)
    if (num_threads <= 0) {
        unsigned int hardware_threads = std::thread::hardware_concurrency();
        ///< hardware_concurrency()가 0 또는 예상치 못한 값 반환 시 기본값(예: 2) 사용
        num_threads = (hardware_threads > 0) ? static_cast<int>(hardware_threads) : 2;
        fprintf(stdout, "[HttpServer %p] Thread count not specified or invalid, using system hardware concurrency: %d\n", (void*)this, num_threads);
    }
    // 실제 io_context가 사용할 동시성 수준(생성 시 지정됨)과 다를 수 있으므로 정보 로깅
    fprintf(stdout, "[HttpServer %p] Starting on %s:%d with %d IO threads...\n",
        (void*)this, address_.c_str(), port, num_threads);


    // Listener 생성 및 실행 (io_context 및 엔드포인트 전달)
    try {
        listener_ = std::make_shared<HttpListener>(ioc_, tcp::endpoint{ addr, port });
        listener_->run(); ///< Listener의 비동기 accept 루프 시작
    }
    catch (const std::exception& e) {
        fprintf(stderr, "[HttpServer %p] Failed to create or run listener: %s\n", (void*)this, e.what());
        // 리스너 생성 실패 시 서버 실행 불가 처리
        return; ///< 또는 예외 재throw
    }

    // IO 스레드 생성 및 io_context 실행 시작
    io_threads_.reserve(num_threads); ///< 벡터 메모리 미리 할당
    for (int i = 0; i < num_threads; ++i) {
        io_threads_.emplace_back([this, i] { ///< 각 스레드가 실행할 람다 함수
            std::stringstream thread_id_ss; ///< 로깅을 위한 스레드 ID 추출
            thread_id_ss << std::this_thread::get_id();
            std::string thread_id_str = thread_id_ss.str();
            try {
                fprintf(stdout, "[HTTP IO Thread #%d ID: %s] Starting ioc.run().\n", i, thread_id_str.c_str());
                ///< 각 스레드는 io_context의 이벤트 루프 실행 (블로킹 호출)
                ///< io_context가 중지되거나 모든 작업이 완료될 때까지 실행됨
                ioc_.run();
                fprintf(stdout, "[HTTP IO Thread #%d ID: %s] ioc.run() finished cleanly.\n", i, thread_id_str.c_str());
            }
            catch (const std::exception& e) {
                ///< io_context.run() 내부에서 발생한 예외 처리
                fprintf(stderr, "[HTTP IO Thread #%d ID: %s] Exception caught in ioc.run(): %s\n", i, thread_id_str.c_str(), e.what());
                ///< 필요 시 추가 에러 처리 (예: 서버 강제 종료)
                ///< std::terminate();
            }
            catch (...) {
                ///< 그 외 알 수 없는 예외 처리
                fprintf(stderr, "[HTTP IO Thread #%d ID: %s] Unknown exception caught in ioc.run().\n", i, thread_id_str.c_str());
                ///< std::terminate();
            }
            });
    }
    ///< run() 함수는 IO 스레드들을 시작시킨 후 반환됨. 메인 스레드는 다른 작업을 하거나 대기 상태로 들어감.
    fprintf(stdout, "[HttpServer %p] run() method finished, IO threads are running.\n", (void*)this);
}

/**
 * @brief 서버 중지 함수 구현부.
 */
void HttpServer::stop() {
    fprintf(stdout, "[HttpServer %p] Initiating stop sequence...\n", (void*)this);

    // 1. Acceptor 중지 (새 연결 수락 중단)
    ///< @todo HttpListener에 acceptor를 닫는 메서드를 추가하면 더 명확한 종료가 가능함
    ///< 현재는 io_context 중지만으로 acceptor의 async_accept 콜백이 취소(operation_aborted)되도록 유도
    fprintf(stdout, "[HttpServer %p] Stopping listener (via io_context stop)...\n", (void*)this);


    // 2. io_context 중지 요청
    ///< 모든 IO 스레드의 ioc_.run() 호출이 반환되도록 함
    ///< 아직 처리되지 않은 비동기 작업 핸들러는 실행되지 않을 수 있음 (즉시 중단 아님)
    fprintf(stdout, "[HttpServer %p] Requesting io_context stop (ioc_.stop())...\n", (void*)this);
    if (!ioc_.stopped()) {
        ioc_.stop();
        fprintf(stdout, "[HttpServer %p] io_context stop requested.\n", (void*)this);
    }
    else {
        fprintf(stdout, "[HttpServer %p] io_context was already stopped.\n", (void*)this);
    }

    // 3. 모든 IO 스레드 종료 대기
    fprintf(stdout, "[HttpServer %p] Waiting for %zu IO threads to join...\n", (void*)this, io_threads_.size());
    for (size_t i = 0; i < io_threads_.size(); ++i) {
        std::thread& t = io_threads_[i];
        std::stringstream ss; ///< 스레드 ID 로깅용
        ss << t.get_id();
        if (t.joinable()) {
            fprintf(stdout, "[HttpServer %p] Joining IO thread #%zu (ID: %s)...\n", (void*)this, i, ss.str().c_str());
            t.join(); ///< 해당 스레드가 완전히 종료될 때까지 대기
            fprintf(stdout, "[HttpServer %p] IO thread #%zu (ID: %s) joined.\n", (void*)this, i, ss.str().c_str());
        }
        else {
            ///< 이미 종료되었거나 시작되지 않은 스레드일 수 있음
            fprintf(stderr, "[HttpServer %p] Warning: IO thread #%zu (ID: %s) is not joinable (already joined or not started?).\n", (void*)this, i, ss.str().c_str());
        }
    }
    io_threads_.clear(); ///< 스레드 벡터 비우기
    fprintf(stdout, "[HttpServer %p] All HTTP IO threads finished.\n", (void*)this);

    // 4. 리소스 정리 (스레드 종료 후 안전하게 수행)
    fprintf(stdout, "[HttpServer %p] Resetting listener and shared resources...\n", (void*)this);
    if (listener_) {
        ///< Listener 소멸자에서 acceptor가 닫히도록 보장해야 함
        listener_.reset(); ///< shared_ptr 참조 카운트 감소, 0이 되면 소멸자 호출
    }

    fprintf(stdout, "[HttpServer %p] Stop sequence complete.\n", (void*)this);
}
