/*
  가상 키보드 HID
  참조 : http://www.arduino.cc/en/Tutorial/KeyboardSerial
*/

#include "Keyboard.h"
#include "3hand-hid-arduino.h"

const char* VERSION = "1.0.3";

bool serialDebug = false;//Serial을 통해 디버그 로그 전송여부

unsigned long sleep_cnt = 0;//4 bytes. sleep 회수
const unsigned long IDLE_TERM = 20;//Serial 입력 데이터가 없을 경우 delay()되는 시간 ms
const unsigned long READ_TIMEOUT = 3;//Serial 입력 Timout 시간 초
unsigned char msg_header[4];//MSG Header
int msg_cnt = 0;//Serial로부터 읽어들인 MSG의 char 갯수
unsigned char pressed_join_key[256];//결합키인 경우 press된 key 
unsigned char key_down_map[256];//Key Down 요청으로 이미 눌러진 key의 정보. key가 눌러지면 1값이 들어가고 up되면 0값이 들어간다
int writen_key_cnt = 0;//MSG header이후 입력된 key 갯수
char msg_seq = 0x00;//통신확인용 seq. MSG Header[1]
int msg_len = 0;//입력될 key의 길이. MSG Header[2]
bool consumeFlag = false; //오류가 발생해서 Serial 버퍼 데이터를 그냥 소진할 경우 사용
bool keyPressed = false;//결합 key입력으로 key가 down되어있는 경우 true
bool completeFlag = false;//key 입력이 완료되면 true. true일때 msg_cnt를 초기화하여 형식에 맞지않는 메시지를 확인할 때 사용한다

void debug(char* debug_log);//debug 메시지를 Serial.print()를 이용해 보낸다
void blink(int times);//LED를 깜빡인다
void writeKey(char c);//key를 물리적으로 입력한다
size_t responseSerial(char result, char sequence, char resType, char inChar);//Serial을 통해 응답한다
void resetSerialBuffer();//버퍼에 남아있는 문자를 모두 읽어버린다. DEPRECATED
bool validKey(unsigned char c);//유효한 key인지 확인한다
void gotIdx1Msg(unsigned char c);//index 1에 들어갈 sequence 값을 처리
void gotIdx2Msg(unsigned char c);//index 2에 들어갈 length 값을 처리
bool checkKeyDown();//keyDown건수가 있는지 확인한다.

void setup() {
  // open the serial port:
  Serial.begin(9600);
  // initialize control over the keyboard:
  Keyboard.begin();
  memset(msg_header, 0, 4);
  memset(key_down_map, 0, 256);
  pinMode(13, OUTPUT);//이걸 해줘야 언제나 켜져있는 LED를 정상작동시킬 수 있다
  delay(500);
  blink(3);//3번 깜빡임
}

void loop() {
  if (Serial.available() > 0) {
    unsigned char inChar = Serial.read();
//    debug("read one char");//..debug line
    if(inChar == REQ_CHECK_ALIVE || inChar == REQ_INIT 
        || inChar == REQ_KEY_WRITE || inChar == REQ_KEY_WRITE_JOIN 
        || inChar == REQ_KEY_DOWN || inChar == REQ_KEY_UP
        || inChar == REQ_DEBUG_ON || inChar == REQ_DEBUG_OFF
     ){
      if(msg_cnt == 1 && (msg_header[0] == REQ_KEY_WRITE || msg_header[0] == REQ_KEY_WRITE_JOIN)){
        //msg_header[1]의 값으로 들어온 inChar가 sequence 값인 경우
        gotIdx1Msg(inChar);
      }else if(msg_cnt == 2 && (msg_header[0] == REQ_KEY_WRITE || msg_header[0] == REQ_KEY_WRITE_JOIN)){
        //msg_header[2]의 값으로 들어온 inChar가 length값인 경우
        gotIdx2Msg(inChar);
      }else{
        //Request header인 경우
        debug("got REQ Header.");//..debug line
        msg_cnt = 0;
        msg_header[msg_cnt] = inChar;//[0] REQ Code
        msg_cnt++;
        writen_key_cnt = 0;//REQ header이후 입력된 key 갯수
        msg_len = 0;//입력될 key의 길이
        msg_seq = 0x00;//통신확인용 seq
        consumeFlag = false;
        completeFlag = false;
        if(inChar != REQ_KEY_DOWN && inChar != REQ_KEY_UP){
          bool keyDown = checkKeyDown();
          if(keyPressed || keyDown){
            Keyboard.releaseAll();
            keyPressed = false;
            if(keyDown)
              memset(key_down_map, 0, 256);
          }  
        }
      }
    }else if(consumeFlag){
      //do nothing
      debug("consumming.");//..debug line
    }else if(msg_cnt <= 0){
      responseSerial(RES_RESULT_FAIL, msg_seq, RES_TYPE_WRONG_FORMAT, inChar);
      consumeFlag = true;
    }else if(msg_cnt == 1){
      //[1] sequence
      gotIdx1Msg(inChar);
    }else if(msg_cnt == 2){
      //[2] length
      gotIdx2Msg(inChar);
    }else if(msg_cnt >= 3){
      //입력될 key 정보가 시작된다
      //this coming msg count : msg_cnt-3+1
      int coming_msg_cnt = msg_cnt-3+1;
      if(coming_msg_cnt > msg_len){
        //msg_len을 초과한경우. 즉 MSG Header에 명시된 길이를 초과한 경우
        if(msg_header[0] == REQ_KEY_WRITE_JOIN){
            //결합키 입력인 경우 press된 키를 모두 해제한다
            Keyboard.releaseAll();
        }
        responseSerial(RES_RESULT_FAIL, msg_seq, RES_TYPE_OVER_LENGTH, inChar);
        msg_cnt = 0;
        consumeFlag = true;// REQ header를 만날때까지 나머지는 모두 무시한다
      }else{
        //허용범위내 key 입력
        if(!validKey(inChar)){
          // 유효하지 않은 key 입력인 경우
          responseSerial(RES_RESULT_FAIL, msg_seq, RES_TYPE_INVALID_KEY, inChar);
        }else{
          //유효한 key 입력
          if(msg_header[0] == REQ_KEY_WRITE){
            //일반 key 입력일 경우
            writeKey(inChar);
            writen_key_cnt++;
            if(coming_msg_cnt == msg_len){
              //Serial에서 메시지를 모두 읽었을 경우
              responseSerial(RES_RESULT_OK, msg_seq, RES_TYPE_KEY_COMPLETE, inChar);//성공적인처리
              completeFlag = true;
            }
          }else if(msg_header[0] == REQ_KEY_WRITE_JOIN){
            //결합키 입력일 경우
            //press key
            pressed_join_key[writen_key_cnt] = inChar;
            delay(10);
            Keyboard.press(inChar);//key down해준다
            keyPressed = true;
            writen_key_cnt++;
            if(coming_msg_cnt == msg_len){
              //Serial에서 메시지를 모두 읽었을 경우
              //모두 입력되었으면 다시 key를 up해준다
              for(int i = writen_key_cnt-1; i >= 0 ;i--){
                delay(10);
                Keyboard.release(pressed_join_key[i]);
              }
              keyPressed = false;
              responseSerial(RES_RESULT_OK, msg_seq, RES_TYPE_KEY_COMPLETE, inChar);//성공적인처리
              completeFlag = true;
            }
          }else if(msg_header[0] == REQ_KEY_DOWN){
            debug("KEY DOWN : ");
            if(serialDebug)
              Serial.write(inChar);
            //이미 key down이 되어있는지 확인하고 되어있다면 오류를 반환한다.
            Keyboard.press(inChar);//key down해준다
            key_down_map[inChar] = 0x01;
            responseSerial(RES_RESULT_OK, msg_seq, RES_TYPE_KEY_COMPLETE, inChar);//성공적인처리
            completeFlag = true;
          }else if(msg_header[0] == REQ_KEY_UP){
            debug("KEY UP : ");
            if(serialDebug)
              Serial.write(inChar);
            Keyboard.release(inChar);
            key_down_map[inChar] = 0x00;
            responseSerial(RES_RESULT_OK, msg_seq, RES_TYPE_KEY_COMPLETE, inChar);//성공적인처리
            completeFlag = true;
          }else{
            responseSerial(RES_RESULT_FAIL, msg_seq, RES_TYPE_UNKNOWN, inChar);//알수없는 오류
            completeFlag = true;
          }
        }//유효한 key 입력
        msg_cnt++;
        if(completeFlag){//요청된 모든 key를 입력하면 msg_cnt를 초기화하여 첫번째 입력되는 byte가 REQ 코드인것을 인식하게 한다
          msg_cnt = 0;
        }
      }//허용범위내 key 입력
    }//else if(msg_cnt >= 3)
    sleep_cnt = 0L;
  }else if(sleep_cnt * IDLE_TERM > READ_TIMEOUT*1000L && msg_cnt > 0){
    //Timeout 발생. 정해진 길이만큼 읽혀지지않은체 3초가 지난경우
    //Key pressed된 것을 모두 초기화하고 TIMEOUT 오류를 반환한다.
    if(serialDebug)
      Serial.println(msg_cnt);
    debug("no input during 3 sec.");//..debug line
    if(keyPressed){
      //key down이 발생한지 3초가 지나도록 데이터가 오지않으면 초기화한다
      Keyboard.releaseAll();
      keyPressed = false;
    }
    msg_cnt = 0;
    sleep_cnt = 0L;
    responseSerial(RES_RESULT_FAIL, msg_seq, RES_TYPE_TIMEOUT, 0x00);
  }else{
    delay(IDLE_TERM);
    if(sleep_cnt >= 4294960000L){
      sleep_cnt = 0L;  
    }
    sleep_cnt++;
  }
}

void gotIdx1Msg(unsigned char inChar){
  //[1] sequence
//      debug("msg_cnt 1 got sequence value.");//..debug line
  msg_header[msg_cnt] = inChar;
  msg_cnt++;
  msg_seq = inChar;
}

void gotIdx2Msg(unsigned char inChar){
        //[2] key length
//      debug("msg_cnt 2 got length value.");//..debug line
      // MSG Header를 모두 읽었다
      msg_header[msg_cnt] = inChar;
      msg_cnt++;
      msg_len = (int)inChar;
      //특수 REQ인경우 응답
      if(msg_header[0] == REQ_CHECK_ALIVE){
        //살아있는지 확인하는 경우
        debug("got REQ_CHECK_ALIVE.");//..debug line
        debug("version : ");
        debug(VERSION);
        responseSerial(RES_RESULT_OK, msg_seq, RES_TYPE_SUCCESS, inChar);
        msg_cnt = 0;
      }else if(msg_header[0] == REQ_INIT){
        //Keyboard 초기화
        debug("got REQ_INIT.");//..debug line
        Keyboard.releaseAll();
        responseSerial(RES_RESULT_OK, msg_seq, RES_TYPE_SUCCESS, inChar);
        msg_cnt = 0;
      }else if(msg_header[0] == REQ_DEBUG_ON){
        //Debug Mode 켜기
        serialDebug = true;
        responseSerial(RES_RESULT_OK, msg_seq, RES_TYPE_SUCCESS, inChar);
        debug("set REQ_DEBUG_ON.");//..debug line
        msg_cnt = 0;
      }else if(msg_header[0] == REQ_DEBUG_OFF){
        //Debug Mode 켜기
        serialDebug = false;
        responseSerial(RES_RESULT_OK, msg_seq, RES_TYPE_SUCCESS, inChar);
        msg_cnt = 0;
      }
}

void writeKey(char c){
//  Serial.print("write key : ");
//  Serial.println(c);
  char c1 = c;
  bool shift = false;

  if(c >= 'A' && c <= 'Z'){
    shift = true;
    c1 = c + 32;
  }
  
  switch(c){
    case '!':
      c1 = '1';
      shift = true;
      break;  
    case '@':
      c1 = '2';
      shift = true;
      break;
    case '#':
      c1 = '3';
      shift = true;
      break;
    case '$':
      c1 = '4';
      shift = true;
      break;
    case '%':
      c1 = '5';
      shift = true;
      break;
    case '^':
      c1 = '6';
      shift = true;
      break;
    case '&':
      c1 = '7';
      shift = true;
      break;
    case '*':
      c1 = '8';
      shift = true;
      break;
    case '(':
      c1 = '9';
      shift = true;
      break;
    case ')':
      c1 = '0';
      shift = true;
      break;
    case '_':
      c1 = '-';
      shift = true;
      break;
    case '+':
      c1 = '=';
      shift = true;
      break;
    case '~':
      c1 = '`';
      shift = true;
      break;
    case '{':
      c1 = '[';
      shift = true;
      break;
    case '}':
      c1 = ']';
      shift = true;
      break;
    case ':':
      c1 = ';';
      shift = true;
      break;
    case '"':
      c1 = '\'';
      shift = true;
      break;      
    case '<':
      c1 = ',';
      shift = true;
      break;
    case '>':
      c1 = '.';
      shift = true;
      break;
    case '?':
      c1 = '/';
      shift = true;
      break;
    case '|':
      c1 = '\\';
      shift = true;
      break;
  }

  digitalWrite(LED_BUILTIN, HIGH); //LED 켜기
  if(shift){
    Keyboard.press(KEY_LEFT_SHIFT);
    delay(100);//이 간격이 너무 짧으면 키보드 보안 어플리케이션의 처리에 걸린다.
  }
//  Keyboard.write(c1);
  Keyboard.press(c1);
  delay(20);
  Keyboard.release(c1);
  delay(20);
  if(shift){
    Keyboard.release(KEY_LEFT_SHIFT);
    delay(20);
  }
  digitalWrite(LED_BUILTIN, LOW); //LED 끄기
}

size_t responseSerial(char result, char sequence, char resType, char inChar){
  if(Serial.availableForWrite() > 0){
    char buf[4];
    buf[0] = result;
    buf[1] = sequence;
    buf[2] = resType;
    buf[3] = inChar;
    return Serial.write(buf, 4);
  }
  return 0;
}

bool checkKeyDown(){
  for(int i=0; i < 256;i++){
    if(key_down_map[i] == 0x01){
      debug("KEY DOWN STILL ON.");//..debug line
      return true;
    }
  }
  return false;
}

void resetSerialBuffer(){
  for(int i=0; i < 1000; i++){
    if (Serial.available() > 0) {
      Serial.read();
    }else{
      break;
    }
  }
  int msg_cnt=0;
  memset(msg_header, 0, 4);
}

bool validKey(unsigned char c){
  if( c >= 0x80 && c <= 0x87){//KEY_LEFT_CTRL, KEY_RIGHT_GUI etc
    return true;
  }
  if( c >= 0xD1 && c <= 0xDA){//KEY_INSERT, KEY_HOME etc
    return true;
  }
  if( c >= 0xB0 && c <= 0xB3){//KEY_RETURN, KEY_ESC etc
    return true;
  }
  if( c >= 0xC1 && c <= 0xCD){//KEY_CAPS_LOCK, KEY_F1 etc
    return true;
  }
  if( c >= 0xF0 && c <= 0xFB){//KEY_F13, KEY_F14 etc
    return true;
  }
  if( c >= 0x20 && c <= 0x7F){// ASCII
    return true;
  }
  return false;
}

//---- other utils

void blink(int times){
  for(int i=0; i < times; i++){
    digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
    delay(100);                       // wait for a second
    digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
    if(i < times-1){
      delay(100); 
    }
  }
}

void writeKeyString(String s){
  int len = s.length();
  char arr[len];
  s.toCharArray(arr, len+1);
  for(int i=0; i < len;i++){
    Serial.print(i,DEC);
    Serial.print(' ');
    Serial.println(arr[i]);
    writeKey(arr[i]);
    //Serial.println(i,DEC);
  }
}

void debug(char* debug_log){
  if(serialDebug){
    Serial.print(debug_log);
  }
}
