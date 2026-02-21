#include <gtest/gtest.h>

/**
 * @file test_main.cpp
 * @brief Google Test 프레임워크를 초기화하고 모든 테스트를 실행하는 메인 파일.
 *
 * 이 파일은 GTest 라이브러리의 main 함수 역할을 하며,
 * 프로젝트 내에 정의된 모든 `TEST()` 및 `TEST_F()` 매크로를 찾아 실행한다.
 */

 /**
  * @brief GTest 실행 진입점 함수.
  * @param argc 커맨드 라인 인자 개수. GTest는 자체 옵션(예: --gtest_filter) 처리를 위해 이를 사용한다.
  * @param argv 커맨드 라인 인자 배열.
  * @return 테스트 결과 코드 (0: 모든 테스트 성공, 1: 하나 이상의 테스트 실패).
  *
  * `::testing::InitGoogleTest`를 호출하여 GTest 프레임워크를 초기화하고,
  * `RUN_ALL_TESTS()`를 호출하여 등록된 모든 테스트를 실행한다.
  */
int main(int argc, char** argv) {
    // Google Test 프레임워크 초기화
    ::testing::InitGoogleTest(&argc, argv);
    // 등록된 모든 테스트 실행 및 결과 반환
    return RUN_ALL_TESTS();
}