---
glob: "**/*.cpp,**/*.h,**/*.ino"
description: "임베디드 C++ (ESP32-S3 / Arduino) 코딩 규칙. .cpp/.h/.ino 작업 시 자동 로드."
---

# 임베디드 C++ 코딩 규칙 (ESP32-S3 / Arduino)

## 메모리

- **`String` 클래스 남용 금지** — heap 단편화. 한 번 쓰고 버리는 임시 텍스트만 허용
- 동적 할당(`new` / `malloc`) 최소화 — 정적 버퍼 · 스택 우선
- 큰 정적 버퍼는 `static` 키워드(BSS) 또는 `PSRAM` 활용

## 시간 · 블로킹

- **`delay(ms)` 사용 금지** (메인 루프) — `millis()` 기반 상태머신으로
- 단, `setup()` 안의 일회성 초기화 대기는 허용
- ISR 안에서 `Serial.print`, `delay`, heap 할당 모두 금지 — **플래그만** 세팅하고 메인 루프에서 처리

## 시리얼

- 디버그용과 데이터 로깅용 출력 **포맷 분리**
  - 사람용: `[INFO] Position x=1.23 y=0.45`
  - 로깅용 CSV: `t,x,y,d1,d2,d3,d4` 한 줄
- `Serial.flush()` 는 부트로더 진입 등 꼭 필요한 경우에만

## 부동소수점

- ESP32-S3는 single-precision FPU 보유 — `float` 자유롭게 사용
- `double`은 SW 에뮬레이션 가능성 — 가능하면 `float`로 통일
- 부동소수점 비교는 `fabsf(a - b) < epsilon` 패턴 (직접 `==` 금지)

## 헤더 가드

- `#pragma once` 통일

## 명명 규칙

- 함수 · 변수: `camelCase`
- 상수 · 매크로: `UPPER_SNAKE_CASE`
- 타입 · 클래스 · 구조체: `PascalCase`
- 파일명: `snake_case.cpp` / `snake_case.h`

## 에러 처리

- 임베디드는 try/catch 없음 — 반환값 / out-param / 에러 코드로
- **외부 입력은 반드시 범위 검증**: 거리값 `0 < d < 50m`, anchor ID 화이트리스트 등
- 실패 시 시리얼로 한 줄 경고 (단, ISR 안에서는 금지)

## 테스트

- PlatformIO Unity (`test/` 폴더)
- 알고리즘 함수(`trilaterate`, `leastSquares` 등)는 **하드웨어 의존성 없이** 작성 — PC에서도 unit test 가능하게
- HW 의존(UART 수신 등)과 알고리즘은 파일 분리

## 모듈 분리 가이드

이 프로젝트의 권장 구조:

| 폴더 | 책임 |
|------|------|
| `src/ranging/` | UART 수신, 패킷 파싱, 거리 큐 관리 |
| `src/localization/` | trilateration, least squares 등 순수 수학 |
| `src/calibration/` | 오프셋 측정 / 저장 / 적용 |
| `src/io/` | Serial 출력, CSV 로깅, (옵션) WiFi/WebSocket |

`main.cpp` 의 `setup()` / `loop()` 는 위 모듈을 조립하는 역할만 — 로직 직접 작성 금지.
