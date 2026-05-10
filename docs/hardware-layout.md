# Hardware Layout

## 측위 토폴로지

- **Anchor 4개** (고정 설치) — 거리 측정의 기준점
- **Tag 1개** (이동) — 위치를 계산할 대상
- **Center**: ESP32-S3 — Tag와 UART 연결, 거리값 받아 위치 계산

## Anchor 좌표

원점·축 정의는 [coordinate-system.md](coordinate-system.md) 참조.

| # | Anchor ID (16-bit) | x [m] | y [m] | 비고 |
|---|--------------------|-------|-------|------|
| 1 | `0x{TODO}` | 0.00 | 0.00 | 기준점 (원점) |
| 2 | `0x{TODO}` | `{TODO}` | 0.00 | X축 양의 방향 |
| 3 | `0x{TODO}` | `{TODO}` | `{TODO}` | 대각 |
| 4 | `0x{TODO}` | 0.00 | `{TODO}` | Y축 양의 방향 |

> 실측 후 `0x{TODO}` 와 좌표를 채우세요.
> ID는 STM32 펌웨어가 보내는 anchor 식별자(`Anchor: 0xXXXX, ...`)와 정확히 일치해야 합니다.

## 결선

### Tag(STM32) → Center(ESP32-S3)

| Tag 핀 | ESP32-S3 핀 | 비고 |
|--------|-------------|------|
| UART TX | `GPIO16` (RX) | 단방향 — Tag가 일방적으로 송신 |
| GND | GND | **공통 GND 필수** |

- 보드레이트: `115200`, 8N1
- COM 포트(개발 PC): `COM7`

## STM32+BU01 펌웨어 위치

- 별도 레포: `C:\PBL_ANCHOR_TAG`
- **anchor 4개는 같은 코드** — anchor ID(주소값)만 바꿔서 4번 빌드/업로드
- **tag 1개도 같은 레포** 안에 (anchor 코드와 분리 또는 빌드 플래그 분기)
- 패킷 포맷은 [uart-protocol.md](uart-protocol.md)
