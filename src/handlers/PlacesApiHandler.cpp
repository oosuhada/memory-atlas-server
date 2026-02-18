#include "../include/handlers/PlacesApiHandler.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/json.hpp>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <chrono>
#include <mutex>
#include <thread>
#include <cmath> // std::round 함수 사용을 위해 추가

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace json = boost::json;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

PlacesApiHandler::PlacesApiHandler(const std::string& api_key)
    : m_apiKey(api_key) {
    std::cout << "PlacesApiHandler created with API key" << std::endl;
}

// 템플릿 함수 구현
http::response<http::string_body> PlacesApiHandler::handleNearbySearch(
    const http::request<http::string_body, http::basic_fields<std::allocator<char>>>& req) {
    
    std::cout << "handleNearbySearch 호출됨, API 키 길이: " << m_apiKey.length() << std::endl;
    
    try {
        // 요청 본문 파싱 (이제 req.body()는 std::string)
        const std::string& body_str = req.body();
        json::value req_json = json::parse(body_str);
        
        std::cout << "요청 본문 파싱 성공: " << body_str << std::endl;
        
        // 클라이언트 요청에서 필요한 파라미터 추출
        double latitude = req_json.at("latitude").as_double();
        double longitude = req_json.at("longitude").as_double();
        
        // radius 값을 double 또는 int64로 유연하게 처리
        double radius = 1500.0; // 기본값 설정
        if (req_json.as_object().contains("radius")) {
            const auto& radius_val = req_json.at("radius");
            if (radius_val.is_double()) {
                radius = radius_val.as_double();
            } else if (radius_val.is_int64()) {
                radius = static_cast<double>(radius_val.as_int64());
                std::cout << "Warning: Received radius as integer, converting to double: " << radius << std::endl;
            } else {
                std::cerr << "Warning: Invalid type for radius, using default." << std::endl;
            }
        }
        
        std::cout << "위치 정보 추출: lat=" << latitude << ", lng=" << longitude 
                  << ", 반경=" << radius << "m" << std::endl;
        
        // Google Places API 요청 데이터 구성
        json::object request_data;
        json::object location_restriction;
        json::object circle;
        json::object center;
        
        center["latitude"] = latitude;
        center["longitude"] = longitude;
        circle["center"] = center;
        circle["radius"] = radius;
        location_restriction["circle"] = circle;
        request_data["locationRestriction"] = location_restriction;

        // 필요한 파라미터 추가 (fieldMask는 searchNearby에서 지원하지 않음)
        request_data["includedPrimaryTypes"] = json::array{"restaurant", "cafe", "bakery", "bar"};
        request_data["maxResultCount"] = 5; // 클라이언트가 5개만 표시하므로 최적화
        request_data["rankPreference"] = "DISTANCE"; // 거리순 정렬 추가
        
        // Google Places API 호출 (POST 사용)
        json::value response_data = this->requestGooglePlacesApi(
            http::verb::post, // 메서드 명시
            "https://places.googleapis.com/v1/places:searchNearby", 
            json::value(request_data));
        
        // ===== Google API 오류 확인 및 전파 =====
        if (response_data.is_object() && response_data.as_object().contains("__error_status_code")) {
            int status_code = response_data.at("__error_status_code").to_number<int>();
            std::string error_body = response_data.at("__error_body").as_string().c_str();
            return this->createErrorResponse(static_cast<http::status>(status_code), error_body);
        }
        // ===== 추가 끝 =====

        // 응답 반환 (정상)
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/json");
        // CORS 헤더는 HttpServer에서 중앙 관리하므로 여기서는 설정하지 않음
        res.keep_alive(req.keep_alive());
        // JSON을 최소화하여 직렬화 (공백 제거)
        res.body() = json::serialize(response_data);
        res.prepare_payload();
        return res;
    }
    catch (const std::exception& e) {
        // 오류 로깅 최적화 (전체 본문 대신 길이만 출력)
        std::cerr << "Error parsing request body (length: " << req.body().length() << "): " << e.what() << std::endl;
        return this->createErrorResponse(http::status::bad_request, 
                                  std::string("Error processing request: ") + e.what());
    }
}

http::response<http::string_body> PlacesApiHandler::handleTextSearch(
    const http::request<http::string_body, http::basic_fields<std::allocator<char>>>& req) {
    
    try {
        // 요청 본문 파싱
        const std::string& body_str = req.body();
        json::value req_json = json::parse(body_str);
        
        // 클라이언트 요청에서 필요한 파라미터 추출
        std::string query = req_json.at("query").as_string().c_str();
        double latitude = req_json.as_object().contains("latitude") ? 
                         req_json.at("latitude").as_double() : 37.5665;
        double longitude = req_json.as_object().contains("longitude") ? 
                          req_json.at("longitude").as_double() : 126.9780;
                          
        // radius 값을 double 또는 int64로 유연하게 처리
        double radius = 50000.0; // 기본값 설정
        if (req_json.as_object().contains("radius")) {
            const auto& radius_val = req_json.at("radius");
            if (radius_val.is_double()) {
                radius = radius_val.as_double();
            } else if (radius_val.is_int64()) {
                radius = static_cast<double>(radius_val.as_int64());
                std::cout << "Warning: Received radius as integer, converting to double: " << radius << std::endl;
            } else {
                std::cerr << "Warning: Invalid type for radius, using default." << std::endl;
            }
        }
        
        // Google Places API 요청 데이터 구성
        json::object request_data;
        
        request_data["textQuery"] = query;
        
        // 특정 랜드마크나 역 이름 검색 시 위치 제한 완화
        bool isLandmarkSearch = (query.find("역") != std::string::npos || 
                                query.find("공항") != std::string::npos ||
                                query.find("터미널") != std::string::npos ||
                                query.find("대학") != std::string::npos);
        
        if (!isLandmarkSearch && radius > 0) {
            // 일반 검색의 경우 locationBias 사용
            json::object location_bias;
            json::object circle;
            json::object center;
            
            center["latitude"] = latitude;
            center["longitude"] = longitude;
            circle["center"] = center;
            circle["radius"] = radius; // 반경을 그대로 사용 (2배 제거)
            location_bias["circle"] = circle;
            request_data["locationBias"] = location_bias;
        }
        // 랜드마크 검색의 경우 위치 제한 없이 전국 검색

        // 필요한 파라미터 추가
        request_data["maxResultCount"] = 5; // 클라이언트가 5개만 표시하므로 최적화
        
        // 한국어 검색 결과 우선
        request_data["languageCode"] = "ko";
        
        // Google Places API 호출 (POST 사용)
        json::value response_data = this->requestGooglePlacesApi(
            http::verb::post, // 메서드 명시
            "https://places.googleapis.com/v1/places:searchText", 
            json::value(request_data));
        
        // ===== Google API 오류 확인 및 전파 =====
        if (response_data.is_object() && response_data.as_object().contains("__error_status_code")) {
            int status_code = response_data.at("__error_status_code").to_number<int>();
            std::string error_body = response_data.at("__error_body").as_string().c_str();
            return this->createErrorResponse(static_cast<http::status>(status_code), error_body);
        }
        // ===== 추가 끝 =====

        // 응답 반환 (정상)
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/json");
        // CORS 헤더는 HttpServer에서 중앙 관리하므로 여기서는 설정하지 않음
        res.keep_alive(req.keep_alive());
        // JSON을 최소화하여 직렬화 (공백 제거)
        res.body() = json::serialize(response_data);
        res.prepare_payload();
        return res;
    }
    catch (const std::exception& e) {
        // 오류 로깅 최적화 (전체 본문 대신 길이만 출력)
        std::cerr << "Error parsing request body (length: " << req.body().length() << "): " << e.what() << std::endl;
        return this->createErrorResponse(http::status::bad_request, 
                                  std::string("Error processing request: ") + e.what());
    }
}

http::response<http::string_body> PlacesApiHandler::handlePlaceDetails(
    const std::string& place_id) {
    
    try {
        // fieldMask를 사용하여 필요한 필드(사진 포함)를 명시적으로 요청
        std::string fields = "id,displayName,formattedAddress,location,rating,userRatingCount,reviews,photos";
        std::string api_url = "https://places.googleapis.com/v1/places/" + place_id + "?fields=" + fields;
        
        // Google Places API 호출 (GET 사용)
        json::value response_data = this->requestGooglePlacesApi(
            http::verb::get, 
            api_url, 
            json::object()); 
        
        // ===== Google API 오류 확인 및 전파 =====
        if (response_data.is_object() && response_data.as_object().contains("__error_status_code")) {
            int status_code = response_data.at("__error_status_code").to_number<int>();
            std::string error_body = response_data.at("__error_body").as_string().c_str();
            // Google API 오류 시 상태 코드와 본문을 그대로 클라이언트에 전달
            http::response<http::string_body> error_res{static_cast<http::status>(status_code), 11};
            error_res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            error_res.set(http::field::content_type, "application/json"); // Google 오류는 JSON일 수 있음
            // CORS 헤더 추가
            // CORS 헤더는 HttpServer에서 중앙 관리하므로 여기서는 설정하지 않음
            error_res.body() = error_body;
            error_res.prepare_payload();
            return error_res;
            // 또는 createErrorResponse 사용:
            // return this->createErrorResponse(static_cast<http::status>(status_code), error_body);
        }
        // ===== 추가 끝 =====

        // 응답 반환 (정상)
        http::response<http::string_body> res{http::status::ok, 11}; 
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/json");
        // CORS 헤더는 HttpServer에서 중앙 관리하므로 여기서는 설정하지 않음
        res.body() = json::serialize(response_data); // 여기서 response_data는 파싱된 Google 응답
        res.prepare_payload();
        return res;
    }
    catch (const std::exception& e) {
        // 오류 로깅 시 요청 본문 출력 제거 (req 객체 없음)
        return this->createErrorResponse(http::status::internal_server_error, 
                                  std::string("Error processing place details request: ") + e.what());
    }
}

json::value PlacesApiHandler::requestGooglePlacesApi(
    http::verb method, // HTTP 메서드 파라미터 추가
    const std::string& endpoint, 
    const json::value& requestData) {
    
    try {
        // 디버그 로그 주석 처리 (I/O 부하 감소)
        // std::cout << "Google Places API 요청 (" << method << "): " << endpoint << std::endl;
        // if (method == http::verb::post) {
        //     std::cout << "요청 데이터: " << json::serialize(requestData) << std::endl;
        // }
        // std::cout << "API 키 길이: " << m_apiKey.length() << std::endl;
        
        // SSL 컨텍스트 및 IO 컨텍스트 설정
        net::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);
        ctx.set_default_verify_paths();
        
        // HTTPS 연결 설정
        tcp::resolver resolver(ioc);
        ssl::stream<tcp::socket> stream(ioc, ctx);
        
        // 호스트 이름 추출
        std::string host = "places.googleapis.com";
        auto const results = resolver.resolve(host, "443");
        
        // 연결 설정 (재시도 로직 포함)
        int retry_count = 0;
        const int max_retries = 3;
        boost::system::error_code last_error;
        
        while (retry_count < max_retries) {
            try {
                boost::system::error_code ec;
                net::connect(stream.next_layer(), results.begin(), results.end(), ec);
                
                if (!ec) {
                    // 연결 성공
                    break;
                }
                
                last_error = ec;
                std::cerr << "[PlacesApiHandler] Connect attempt " << (retry_count + 1) 
                          << " failed to " << host << ": " 
                          << ec.message() << " [" << ec.value() << "]" << std::endl;
                
                // EADDRNOTAVAIL (99) 에러인 경우 재시도
                if (ec.value() == 99 && retry_count < max_retries - 1) {
                    // 잠시 대기 (지수 백오프)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100 * (retry_count + 1)));
                    
                    // 새로운 IO context와 소켓으로 재시도
                    ioc.restart();
                    ssl::stream<tcp::socket> new_stream(ioc, ctx);
                    stream = std::move(new_stream);
                    retry_count++;
                } else {
                    throw boost::system::system_error(ec);
                }
            } catch (const std::exception& e) {
                if (retry_count >= max_retries - 1) {
                    std::cerr << "[PlacesApiHandler] All connection attempts failed: " << e.what() << std::endl;
                    throw;
                }
                retry_count++;
            }
        }
        
        if (retry_count >= max_retries && last_error) {
            throw boost::system::system_error(last_error);
        }
        stream.handshake(ssl::stream_base::client);
        
        // HTTP 요청 준비 (메서드 파라미터 사용)
        http::request<http::string_body> req{method, endpoint, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req.set(http::field::content_type, "application/json");
        req.set("X-Goog-Api-Key", m_apiKey);
        
        // 메서드에 따라 다른 FieldMask 설정
        if (method == http::verb::get) { ///< Place Details (GET)
            // handlePlaceDetails에서 이미 fields 파라미터로 지정하므로 X-Goog-FieldMask 헤더는 설정하지 않음
            // req.set("X-Goog-FieldMask", "id,displayName,formattedAddress,location");
        } else { ///< Nearby Search, Text Search (POST)
            req.set("X-Goog-FieldMask", "places.id,places.displayName,places.formattedAddress,places.location");
        }
        
        // POST 요청일 때만 본문 설정
        if (method == http::verb::post) {
            req.body() = json::serialize(requestData);
        }
        req.prepare_payload();
        
        // 요청 전송
        http::write(stream, req);
        
        // 응답 수신
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        beast::error_code ec;
        http::read(stream, buffer, res);
        
        // http::read 이후 오류 코드 로깅 추가
        if (ec && ec != http::error::end_of_stream) {
            std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] Error after http::read: " 
                      << ec.message() << " (Code: " << ec.value() << ")" << std::endl;
            // 기존 예외 처리는 유지 (필요시)
            // throw beast::system_error{ec, "Read failed"};
        }
        // 정상적인 EOF는 로그하지 않음 (I/O 부하 감소)
        // else if (ec == http::error::end_of_stream) {
        //      std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] Normal EOF after http::read." << std::endl;
        // } else {
        //      std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] No error after http::read." << std::endl;
        // }

        // 연결 종료
        // beast::error_code ec; // ec 변수는 이미 선언됨
        stream.shutdown(ec);

        // stream.shutdown 이후 오류 코드 로깅 추가
        if(ec == net::error::eof) {
            // 정상적인 EOF는 로그하지 않음 (I/O 부하 감소)
            // std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] Normal EOF during shutdown." << std::endl;
            ec = {}; ///< Clear the error code for EOF
        } else if (ec == ssl::error::stream_truncated) {
            // 정상적인 stream_truncated도 로그하지 않음 (I/O 부하 감소)
            // std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] Stream truncated during shutdown (Code: " 
            //           << ec.value() << "). Ignored." << std::endl;
            ec = {}; ///< stream_truncated 오류는 일단 무시하고 로깅만
        } else if (ec) {
             std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] Error during stream shutdown: " 
                      << ec.message() << " (Code: " << ec.value() << ")" << std::endl;
            // 기존 예외 처리 유지
            throw beast::system_error{ec};
        }
        
        // 응답 본문 파싱 및 변환
        json::value response_json;
        try {
             response_json = json::parse(res.body());
        } catch (const std::exception& parse_error) {
            std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] Failed to parse Google API response as JSON: " 
                      << parse_error.what() << "\nResponse body (first 200 chars): " 
                      << (res.body().length() > 200 ? res.body().substr(0, 200) + "..." : res.body()) << std::endl;
            // JSON 파싱 실패 시, 원본 상태 코드와 본문을 오류로 반환
            json::object error_obj;
            error_obj["__error_status_code"] = res.result_int(); ///< 원본 상태 코드 저장
            error_obj["__error_body"] = res.body(); ///< 원본 본문 저장
            return error_obj;
        }
        
        // 정상 응답 로그 주석 처리 (I/O 부하 감소)
        // std::cout << "Google API 응답 코드: " << res.result_int() << std::endl;
        // std::cout << "응답 내용 일부: " << (res.body().length() > 100 ? res.body().substr(0, 100) + "..." : res.body()) << std::endl;
        
        // ===== Google API 오류 상태 코드 확인 =====
        ///< @brief Google API 오류 응답을 감지하여 클라이언트에 전달하기 위해 특수 필드에 저장한다.
        ///< 이 값들은 이후 호출자에 의해 적절한 HTTP 오류 응답으로 변환된다.
        if (res.result_int() < 200 || res.result_int() >= 300) {
             std::cerr << "[PlacesApiHandler::requestGooglePlacesApi] Google API returned error status: " << res.result_int() << std::endl;
             json::object error_obj;
             error_obj["__error_status_code"] = res.result_int(); ///< 원본 상태 코드 저장
             error_obj["__error_body"] = json::serialize(response_json); ///< 파싱된 JSON 오류 메시지 저장
             return error_obj;
        }
        // ===== 오류 처리 끝 =====

        // 클라이언트에 맞는 응답 형식으로 변환 (성공 시)
        json::object result;
        if (method == http::verb::get) { ///< Place Details 응답 처리
             ///< @note 현재는 Google API 응답을 그대로 반환합니다. 추후 응답 형식 변환이 필요한 경우
             ///< transformPlaceDetails 함수를 구현하여 사용할 수 있습니다.
             result = response_json.as_object();
        } else if (response_json.is_object() && response_json.as_object().contains("places")) { ///< Nearby/Text Search 응답 처리
             json::array places_array = response_json.at("places").as_array();
             json::array transformed_places;
             
             for (const auto& place : places_array) {
                 json::object transformed_place;
                 
                 // ID 처리 (필수 필드)
                 if (place.as_object().contains("id")) {
                     transformed_place["id"] = place.at("id").as_string();
                 } else if (place.as_object().contains("name")) {
                     // name 필드에서 ID 추출 (places/ChIJ... 형식)
                     std::string name_str = place.at("name").as_string().c_str();
                     auto pos = name_str.find("places/");
                     if (pos != std::string::npos) {
                         transformed_place["id"] = name_str.substr(pos + 7);
                     } else {
                         transformed_place["id"] = name_str;
                     }
                 }
                 
                 // 장소 이름 처리
                 if (place.as_object().contains("displayName") && 
                     place.at("displayName").as_object().contains("text")) {
                     transformed_place["name"] = place.at("displayName").at("text").as_string();
                 } else {
                     transformed_place["name"] = "이름 없음";
                 }
                 
                 // 주소 처리
                 if (place.as_object().contains("formattedAddress")) {
                     transformed_place["addr"] = place.at("formattedAddress").as_string();
                 } else if (place.as_object().contains("vicinity")) {
                     transformed_place["addr"] = place.at("vicinity").as_string();
                 } else {
                     transformed_place["addr"] = "주소 정보 없음";
                 }
                 
                 // 위치 정보 처리 (좌표 정밀도를 소수점 6자리로 제한)
                 json::object location;
                 if (place.as_object().contains("location")) {
                     double lat = place.at("location").at("latitude").as_double();
                     double lng = place.at("location").at("longitude").as_double();
                     // 소수점 6자리로 제한 (약 0.1m 정밀도)
                     lat = std::round(lat * 1000000) / 1000000.0;
                     lng = std::round(lng * 1000000) / 1000000.0;
                     location["lat"] = lat;
                     location["lng"] = lng;
                 } else {
                     // 기본 위치 (강남역)
                     location["lat"] = 37.4979;
                     location["lng"] = 127.0276;
                 }
                 transformed_place["loc"] = location;
                 
                 transformed_places.push_back(transformed_place);
             }
             
             result["places"] = transformed_places;
        }
        
        return result;
    }
    catch (const std::exception& e) {
        std::cerr << "Error in requestGooglePlacesApi: " << e.what() << std::endl;
        
        // 오류 시 빈 응답 반환
        json::object error_object;
        error_object["error"] = e.what();
        return error_object;
    }
}

http::response<http::string_body> PlacesApiHandler::createErrorResponse(
    http::status status_code,
    const std::string& error) {
    
    http::response<http::string_body> res{status_code, 11}; // HTTP/1.1 가정
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "application/json");
    // CORS 헤더는 HttpServer에서 중앙 관리하므로 여기서는 설정하지 않음
    
    json::object error_obj;
    error_obj["error"] = error;
    res.body() = json::serialize(error_obj);
    
    res.prepare_payload();
    return res;
}

http::response<http::string_body> PlacesApiHandler::handlePlacePhoto(
    const std::string& photo_reference) {
    
    try {
        // 디버그 로그 주석 처리 (I/O 부하 감소)
        // std::cout << "handlePlacePhoto 호출됨, photo_reference: " << photo_reference << std::endl;
        
        // Google Places API v1 형식의 photo reference 처리
        // 형식: places/ChIJXyQvMaRtZjURAln5cV-8xL4/photos/AXQCQNTelAdECPdcBwjlBRkJ0hUmnWUg...
        std::string actual_photo_reference = photo_reference;
        
        // 전체 경로에서 실제 photo reference 추출
        size_t photos_pos = photo_reference.find("/photos/");
        if (photos_pos != std::string::npos) {
            // "/photos/" 이후의 부분만 추출
            actual_photo_reference = photo_reference.substr(photos_pos + 8);
            // 디버그 로그 주석 처리 (I/O 부하 감소)
            // std::cout << "추출된 photo reference: " << actual_photo_reference << std::endl;
        }
        
        // Google Places Photo API URL 구성
        std::string api_url = "https://maps.googleapis.com/maps/api/place/photo";
        api_url += "?maxwidth=1600"; // 최대 너비 설정
        api_url += "&photoreference=" + actual_photo_reference;
        api_url += "&key=" + m_apiKey;
        
        // SSL 컨텍스트 및 IO 컨텍스트 설정
        net::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);
        ctx.set_default_verify_paths();
        
        // HTTPS 연결 설정
        tcp::resolver resolver(ioc);
        ssl::stream<tcp::socket> stream(ioc, ctx);
        
        // 호스트 이름 추출
        std::string host = "maps.googleapis.com";
        auto const results = resolver.resolve(host, "443");
        
        // 연결 설정
        net::connect(stream.next_layer(), results.begin(), results.end());
        stream.handshake(ssl::stream_base::client);
        
        // HTTP 요청 준비
        http::request<http::string_body> req{http::verb::get, api_url, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req.prepare_payload();
        
        // 요청 전송
        http::write(stream, req);
        
        // 응답 수신 (이미지 데이터를 위해 dynamic_body 사용)
        beast::flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(stream, buffer, res);
        
        // 연결 종료
        beast::error_code ec;
        stream.shutdown(ec);
        if(ec == net::error::eof || ec == ssl::error::stream_truncated) {
            ec = {}; // 정상적인 종료로 간주
        }
        
        // 정상 응답 로그 주석 처리 (I/O 부하 감소)
        // std::cout << "Google API 응답 코드: " << res.result_int() << std::endl;
        // std::cout << "응답 내용 일부: " << (res.body().length() > 100 ? res.body().substr(0, 100) + "..." : res.body()) << std::endl;

        // 302 리다이렉트 처리
        if (res.result_int() == 302) {
            // Location 헤더에서 리다이렉트 URL 추출
            auto location = res.find(http::field::location);
            if (location != res.end()) {
                std::string redirect_url = std::string(location->value());
                // 디버그 로그 주석 처리 (I/O 부하 감소)
                // std::cout << "리다이렉트 URL: " << redirect_url << std::endl;
                
                // 리다이렉트 URL 파싱
                // URL 형식: https://lh3.googleusercontent.com/...
                size_t host_start = redirect_url.find("://") + 3;
                size_t path_start = redirect_url.find("/", host_start);
                
                std::string redirect_host = redirect_url.substr(host_start, path_start - host_start);
                std::string redirect_path = redirect_url.substr(path_start);
                
                // 새로운 연결로 리다이렉트된 URL에 접속
                tcp::resolver redirect_resolver(ioc);
                ssl::stream<tcp::socket> redirect_stream(ioc, ctx);
                
                auto const redirect_results = redirect_resolver.resolve(redirect_host, "443");
                net::connect(redirect_stream.next_layer(), redirect_results.begin(), redirect_results.end());
                redirect_stream.handshake(ssl::stream_base::client);
                
                // 리다이렉트 요청
                http::request<http::string_body> redirect_req{http::verb::get, redirect_path, 11};
                redirect_req.set(http::field::host, redirect_host);
                redirect_req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
                redirect_req.prepare_payload();
                
                http::write(redirect_stream, redirect_req);
                
                // 실제 이미지 응답 수신
                beast::flat_buffer redirect_buffer;
                http::response<http::dynamic_body> redirect_res;
                http::read(redirect_stream, redirect_buffer, redirect_res);
                
                // 연결 종료
                beast::error_code redirect_ec;
                redirect_stream.shutdown(redirect_ec);
                if(redirect_ec == net::error::eof || redirect_ec == ssl::error::stream_truncated) {
                    redirect_ec = {};
                }
                
                // 리다이렉트된 응답 사용
                res = std::move(redirect_res);
            }
        }
        
        // 오류 상태 확인
        if (res.result_int() < 200 || res.result_int() >= 300) {
            // 오류 응답을 그대로 전달
            http::response<http::string_body> error_res{res.result(), 11};
            error_res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            error_res.set(http::field::content_type, "text/plain");
            error_res.set(http::field::access_control_allow_origin, "*");
            error_res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
            error_res.set(http::field::access_control_allow_headers, "Content-Type, Authorization, Accept");
            
            // dynamic_body를 string으로 변환
            error_res.body() = beast::buffers_to_string(res.body().data());
            error_res.prepare_payload();
            return error_res;
        }
        
        // 성공 응답 생성 (이미지 데이터)
        http::response<http::string_body> img_res{http::status::ok, 11};
        img_res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        
        // Content-Type 헤더 복사 (이미지 타입 유지)
        if(res.count(http::field::content_type) > 0) {
            img_res.set(http::field::content_type, res[http::field::content_type]);
        } else {
            img_res.set(http::field::content_type, "image/jpeg"); // 기본값
        }
        
        // CORS 헤더는 HttpServer에서 중앙 관리하므로 여기서는 설정하지 않음
        
        // 이미지 데이터를 string으로 변환하여 저장
        img_res.body() = beast::buffers_to_string(res.body().data());
        img_res.prepare_payload();
        
        return img_res;
    }
    catch (const std::exception& e) {
        std::cerr << "Error in handlePlacePhoto: " << e.what() << std::endl;
        return this->createErrorResponse(http::status::internal_server_error, 
                                  std::string("Error fetching place photo: ") + e.what());
    }
}

