#include <Arduino.h>
#include <math.h>
#include <stdio.h>

HardwareSerial TagSerial(1);

#define TAG_RX 16

// 앵커 식별자 (16-bit) — STM32 펌웨어가 송신하는 실제 값으로 채울 것
#define ANCHOR1_ID 0x0001  // TODO
#define ANCHOR2_ID 0x0002  // TODO
#define ANCHOR3_ID 0x0003  // TODO
#define ANCHOR4_ID 0x0004  // TODO

// 앵커 좌표 [m]
static const float ANCHOR_X[4] = { 0.00f,  3.87f, 3.87f,  0.28f };  
static const float ANCHOR_Y[4] = { 0.00f, 0.0f, 3.76f,  3.76f };  

// 거리 보정 오프셋 (측정값 - 실제값)
//
static const float OFFSET[4] = {
0.0f,
0.0f,
0.0f,
0.0f
};

static float dist[4];
static bool  gotDist[4];

// 6 cycle 누적
constexpr uint8_t CYCLES   = 6;
constexpr float   DELTA_THRESH = 0.30f;  // [m] outlier 판정 임계값

static float   dist_sum[4]      = {0, 0, 0, 0};
static uint16_t dist_sample[4] = {0, 0, 0, 0};
static float   last_val[4]    = {-1.0f, -1.0f, -1.0f, -1.0f};  // 부팅 직후 sentinel
static uint8_t cycles = 0;
// ── 한 줄 파싱: "Anchor: 0xAABB, Distance: 1.23m"
static void parseAndStore(const char* line) {
    uint16_t addr;
    float    d;
    if (sscanf(line, "Anchor: %hx, Distance: %fm", &addr, &d) != 2) return; 
    // 입력이 제대로 들어오는지 확인. != 2 -> 값이 2개가 나와야 정상
    // 형식 안맞으면 0이나 1 반환. return
    //둘다 잘 나오면 2 반환. 통과. 
    if (d <= 0.0f || d >= 50.0f) return;
    // 거리가 음수 나오거나 50m 같은 이상한 값 나오면 return

    int slot;
    switch (addr) {
        case ANCHOR1_ID: slot = 0; break;
        case ANCHOR2_ID: slot = 1; break;
        case ANCHOR3_ID: slot = 2; break;
        case ANCHOR4_ID: slot = 3; break;
        default: return;  // 외 ID 폐기
    }
    // d_val => 받아온 d - offset_d
    const float d_val = d - OFFSET[slot];
    // outlier -> 이전값으로 대체
    float d_val_temp = d_val;
    if (last_val[slot] >= 0.0f && fabsf(d_val - last_val[slot]) > DELTA_THRESH) {
        // last_val 이 0보다 크고, 거리값 - 이전 거리값 차이가 delta thresh보다 크면
        // 쓰레기값으로 인정. 그전값으로 그냥 덮어씌우기.
        d_val_temp = last_val[slot];
    }
    last_val[slot] = d_val_temp;
    //
    // mean용 누적
    dist_sum[slot] += d_val_temp;
    dist_sample[slot] += 1;
    gotDist[slot] = true;
}

// 최소자승법
// (x-xᵢ)² + (y-yᵢ)² = rᵢ²  를 i=1 식으로 빼서 선형화하면
//   2(xᵢ-x₁)·x + 2(yᵢ-y₁)·y = r₁² - rᵢ² + xᵢ² - x₁² + yᵢ² - y₁²
// i=2,3,4 → 3개 선형식의 over-determined 계 A·p = b
// 정규방정식 (AᵀA)·p = Aᵀ·b 를 2×2 닫힌형으로 푼다.
// 기존에서 static bool leastSquares(float* outX, float* outY) 

// static bool leastSquares(float* outX, float* outY) {
//     const float x1   = ANCHOR_X[0];
//     const float y1   = ANCHOR_Y[0];
//     const float r1sq = dist[0] * dist[0];

//     float Saa = 0, Sab = 0, Sbb = 0, Sac = 0, Sbc = 0;
//     for (int i = 1; i < 4; ++i) {
//         const float a = 2.0f * (ANCHOR_X[i] - x1);
//         const float b = 2.0f * (ANCHOR_Y[i] - y1);
//         const float c = r1sq - dist[i] * dist[i]
//                       + ANCHOR_X[i] * ANCHOR_X[i] - x1 * x1
//                       + ANCHOR_Y[i] * ANCHOR_Y[i] - y1 * y1;
//         Saa += a * a;  Sbb += b * b;  Sab += a * b;
//         Sac += a * c;  Sbc += b * c;
//     }
//     const float det = Saa * Sbb - Sab * Sab;
//     // 크래머 룰 쓰기 위한 det 값 계산
//     if (fabsf(det) < 1e-6f) return false;  
//     // det의 절댓값(fabsf)가 10^-6승 거의 0에 가까우면 
//     // 호출자에게 false 값 반환.
//     // 밑에 실행도 안됨. 
//     *outX = (Sbb * Sac - Sab * Sbc) / det;
//     *outY = (Saa * Sbc - Sab * Sac) / det;
//     return true;
// }
static bool leastSquares(float* outX, float* outY, bool* anchor){
    int ref = 0;

    for(int i = 0; i<4 ;i++){
        if(anchor[i]){
            ref++;
        } 
    }
    if(ref<3) return false;

    for(int i = 0; i<4; i++){
        if(anchor[i] == true){
            const float x_ref = ANCHOR_X[i];
            const float y_ref = ANCHOR_Y[i];
            const float r_ref_sq = dist[i]*dist[i];
            
            float Saa = 0, Sab = 0, Sbb = 0, Sac = 0, Sbc = 0;
            for(int j = 0; j<4; j++){
                if(!anchor[j] || j == i) continue;
                const float a = 2.0f*(ANCHOR_X[j] - x_ref);
                const float b = 2.0f*(ANCHOR_Y[j] - y_ref);                
                const float c = r_ref_sq - dist[j] * dist[j]
                       + ANCHOR_X[j] * ANCHOR_X[j] - x_ref * x_ref
                       + ANCHOR_Y[j] * ANCHOR_Y[j] - y_ref * y_ref;
                Saa += a * a;  Sbb += b * b;  Sab += a * b;
                Sac += a * c;  Sbc += b * c;
            }

        const float det = Saa * Sbb - Sab * Sab;
        // 크래머 룰 쓰기 위한 det 값 계산
        if (fabsf(det) < 1e-6f) return false;  
        // det의 절댓값(fabsf)가 10^-6승 거의 0에 가까우면 
        // 호출자에게 false 값 반환.
         // 밑에 실행도 안됨. 
        *outX = (Sbb * Sac - Sab * Sbc) / det;
        *outY = (Saa * Sbc - Sab * Sac) / det;
        return true;
        }
    }
    return false;
}

// 6 cycle 누적창에서 "살아있다"고 인정할 최소 표본 수 (과반수)
constexpr uint16_t MIN_SAMPLES = CYCLES / 2;

static void tryComputePosition() {
    // 이번 라인까지의 cycle 진행 체크 — anchor[] 스냅샷은 LS에 쓰지 않음
    int cnt = 0;
    for(int i = 0; i < 4; i++){
        if(gotDist[i]) cnt++;
    }
    if (cnt < 3) return;  // 이번 cycle 자체가 3개 미만이면 cycle 인정 안 함

    gotDist[0] = gotDist[1] = gotDist[2] = gotDist[3] = false;

    cycles++;
    if (cycles < CYCLES) return;

    // 6 cycle 누적창 종료 — 표본 수 기준으로 살아있는 앵커 확정
    bool anchor[4] = {false, false, false, false};
    int alive = 0;
    for(int i = 0; i < 4; ++i){
        if(dist_sample[i] >= MIN_SAMPLES){
            dist[i] = dist_sum[i] / dist_sample[i];
            anchor[i] = true;
            alive++;
        } else {
            dist[i] = 0.0f;  // LS에 들어가지 않으므로 의미 없음 — 로그용
        }
        dist_sum[i] = 0.0f;
        dist_sample[i] = 0;
    }
    cycles = 0;

    if (alive < 3) return;  // 누적창 끝나도 3개 미만이면 측위 포기

    float x, y;
    if (leastSquares(&x, &y, anchor)) {
        Serial.printf("Position: x=%.2f, y=%.2f (n=%d d1=%.2f d2=%.2f d3=%.2f d4=%.2f)\n",
                      x, y, alive, dist[0], dist[1], dist[2], dist[3]);
    }
}

void setup() {
    Serial.begin(115200);
    TagSerial.begin(115200, SERIAL_8N1, TAG_RX, -1);
    delay(2000);
    Serial.println("ESP32 Localization (LS, 4-anchor) Ready");
}

void loop() {
    static char    buf[64];
    static uint8_t idx = 0;

    while (TagSerial.available()) {
        const char c = (char)TagSerial.read();  // 1글자 읽기
        if (c == '\n' || c == '\r') {
            if (idx > 0) {
                buf[idx] = '\0';
               // Serial.println(buf);          // 수신 원문 그대로 PC로 흘림
                parseAndStore(buf);
                tryComputePosition();
                idx = 0;
            }
        } else if (idx < sizeof(buf) - 1) {
            buf[idx++] = (uint8_t)c;
        } else {
            idx = 0;  // 라인 길이 초과 — 폐기
        }
    }
}
