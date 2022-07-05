/*
  3hand-hid.h

#HID protocol

PC -> board : 4 + n bytes
 +--------------+--------------+----------------+--------------+
 | command (1)  | sequence (1) | key length (1) | keys (1-249) |
 +--------------+--------------+----------------+--------------+
 
arr[0] : command
        0xA0 : live check
        0xA1 : init(release all)
        0xA2 : 일반키
        0xA3 : 결합키 (특수키와의 결합 등)
        0xA4 : Key down
        0xA5 : Key up
        0xA6 : debug on. Serial port out.
        0xA7 : debug off.
arr[1] : sequence. 0 - 255
arr[2] : key length. live check인경우 0. 일반 1 - 255
arr[3-252] : 입력될 key

board -> PC : 4 bytes

 * 아래 프로토콜을 수정하여 2byte이상의 코드를 사용할 경우 방드시 Endian(PC와 Arduino의 Endian type이 같은지 확인)을 고려해야 한다.
 +--------------+--------------+-------------------+--------------+
 | result (1)   | sequence (1) | response type (1) | last key (1) |
 +--------------+--------------+-------------------+--------------+

arr[0] : result 
        0xE1 : OK
        0xE2 : FAIL
arr[1] : sequence
arr[2] : response type 
        0x11 : OK
        0x12 : Key 입력완료
        0x2- : 오류내역
        0xFD : unknown error
arr[3] : last key

#입력예제

A00100 REQ_CHECK_ALIVE
A10200 REQ_INIT
A2030131 REQ_KEY_WRITE 단일키 입력
A204053132333435 REQ_KEY_WRITE 복수키 입력
A251D6021402324255E262A28295F2B2D3D7B7D5B5D3A223B273C3E3F2C2E2F REQ_KEY_WRITE 복수키(특수문자) 입력
A205033132333435 REQ_KEY_WRITE 복수키 입력. 용량 초과
A207053132883435 REQ_KEY_WRITE 복수키 입력. 유효하지않은 key입력
A20806313288893435 REQ_KEY_WRITE 복수키 입력. 유효하지않은 복수 key입력
A20906313235 REQ_KEY_WRITE 복수키 입력. 용량미달
A30A028131 결합키. KEY_LEFT_SHIFT 0x81, '1'
A30B0281B3 결합키. KEY_LEFT_SHIFT 0x81, KEY_TAB 0xB3
A30C03808141 결합키. KEY_LEFT_CTRL 0x80, KEY_LEFT_SHIFT 0x81, 'A' 0x41
A30E04808241B2 결합키. KEY_LEFT_CTRL 0x80, KEY_LEFT_ALT 0x82, 'A' 0x41, KEY_BACKSPACE 0xB2;
A30D038131 결합키. 용량미달의 경우
*/

#ifndef HAND3_h
#define HAND3_h

//================================================================================
//  Keyboard

#define REQ_CHECK_ALIVE      0xA0 // Live Checking
#define REQ_INIT             0xA1 // 초기화 (Release All)
#define REQ_KEY_WRITE        0xA2 // 일반 key 입력
#define REQ_KEY_WRITE_JOIN   0xA3 // 결합 key 입력
#define REQ_KEY_DOWN         0xA4 // KEY Down
#define REQ_KEY_UP           0xA5 // KEY UP
#define REQ_DEBUG_ON         0xA6 // Debug 모드 켜기. Serial Port로 debug 문자를 내보내게 세팅한다
#define REQ_DEBUG_OFF        0xA7 // Debug 모드 끄기. 더이상 Serial Port로 debug문자를 내보내지 않는다

#define RES_RESULT_OK     0xE1 // 처리 성공
#define RES_RESULT_FAIL   0xE2 // 처리 실패

#define RES_TYPE_SUCCESS       0x11 // OK
#define RES_TYPE_KEY_COMPLETE  0x12 // Key 입력완료
#define RES_TYPE_WRONG_FORMAT  0x21 // 잘못된 형식의 오류
#define RES_TYPE_OVER_LENGTH   0x22 // 입력된 데이터의 길이가 초과된 경우
#define RES_TYPE_TIMEOUT       0x23 // 입력된 데이터의 길이가 미달된 경우. 3초후에도 데이터가 정해진 길이만큼 안들어오면 이 오류가 발생한다
#define RES_TYPE_INVALID_KEY   0x24 // 유효하지 않은 키값인 경우
#define RES_TYPE_UNKNOWN       0xFD // 알수없는 오류

#endif
