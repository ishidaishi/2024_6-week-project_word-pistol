#include <M5Stack.h>       // M5Stack GRAY使用の場合有効
#include "BluetoothSerial.h"
#include <Adafruit_NeoPixel.h>

//LEDテープ
#ifdef __AVR__
  #include <avr/power.h>
#endif
#define LEDPIN        22
#define NUMPIXELS 9
#define MOTORPIN      21

Adafruit_NeoPixel pixels(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

BluetoothSerial bts;

#define LGFX_AUTODETECT // 自動認識(D-duino-32 XS, PyBadgeはパネルID読取れないため自動認識の対象から外れているそうです)
#define LGFX_USE_V1     // v1.0.0を有効に(v0からの移行期間の特別措置とのこと。書かない場合は旧v0系で動作)
#define TFCARD_CS_PIN 4//ビデオ再生
#define LGFX_M5STACK//ビデオ再生

#include <LovyanGFX.hpp>          // lovyanGFXのヘッダを準備
#include <LGFX_AUTODETECT.hpp>    // クラス"LGFX"を準備
#include <SD.h>//ビデオ再生

//音再生
#include "AudioFileSourceID3.h"
#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
AudioGeneratorMP3 *mp3;
AudioFileSourceSD *file;
AudioOutputI2S *out;
AudioFileSourceID3 *id3;

static LGFX lcd;                  // LGFXのインスタンスを作成（クラスLGFXを使ってlcdコマンドでいろいろできるようにする）
static LGFX_Sprite canvas(&lcd);  // スプライトを使う場合はLGFX_Spriteのインスタンスを作成
int xs=100,xr=-100;//弾の座標
int shot=0,receive=0;//射撃、受信判定
int wordselect=0;//言葉の選択用変数
int wordsize=1;//言葉のサイズ
char* words;//送る言葉
char* wordr;//送られる言葉
int ybs=400,ybr=110;//送る、送られる弾のy座標
int bulletsizes=90,bulletsizer=0;//送る、送られる弾の大きさ
int bluetoo=0;//bluetooth用変数
int led=0,ledr=30;//LED用カウント変数,r=receive
double ledpix=0,ledrpix;//LED点灯数用変数
int ledoff=0;//LED点滅用変数
int i=1;//動画用変数
int countr=0,counts=0;//動画等時間管理
int selectmovie=0;//選択画面遷移変数
int decide=0;//決定画面変数
int colorR=0,colorG=0,colorB=0;//LEDテープ色
//振動モータ用
int freq = 10000;
int resolution = 10;


/* IR通信定義 */
#define IR_HZ        38000 //赤外線搬送波周波数
#define IR_DATA_T    500 //通信プロトコルのT[us]
#define IR_DUTY      682  //duty比　100%が2048、つまり682は1/3
#define IR_TOLERANCE_H  50  //IR_H信号の誤差裕度[us]
#define IR_TOLERANCE_L  200  //IR_L信号の誤差裕度[us]
int ir_tolerance[2] = {IR_TOLERANCE_L,IR_TOLERANCE_H};

#define SEND_PIN     26   //赤外線送信ピン
#define RECIEVE1_PIN  35   //赤外線受信ピン

/* IRグローバル変数 */
volatile int irSendData = 0;      //IR送信データ
volatile unsigned int irTime = 0; //IR信号受信時間
volatile int irCount = 0;         //IR信号カウンター
volatile int irTmpData = 0;       //IR一時データ
volatile int irRecieveData = 0;   //IR受信データ

/* 演出トリガー用フラグ */
int damageFlag = 0;
int shotFlag = 0;

//作成タスクのHandleへのポインタ
TaskHandle_t th[4]; 

/* IR送信用タイマー割込み設定 */
volatile int timeCounter1;
hw_timer_t *timer1 = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

/* IR送信用タイマー割込み処理 */
void IRAM_ATTR onTimer1(){
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  timeCounter1++;
  portEXIT_CRITICAL_ISR(&timerMux);
}

//音声用関数
void playMP3(char *filename) {
  // pno_cs from https://ccrma.stanford.edu/~jos/pasp/Sound_Examples.html
  file = new AudioFileSourceSD(filename);
  id3 = new AudioFileSourceID3(file);
  out = new AudioOutputI2S(0, 1);  // Output to builtInDAC
  out->SetOutputModeMono(true);
  mp3 = new AudioGeneratorMP3();
  mp3->begin(id3, out);
  while(mp3->isRunning()) {
    if (!mp3->loop()) mp3->stop();
  }
}


// 初期設定 -----------------------------------------
void setup() {
  M5.begin();   // 本体初期化

   bts.begin("MSR IoT Device");//bluetooth用
  
  //テスト用
  Serial.begin(115200);

  //LEDテープ
  #if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
  clock_prescale_set(clock_div_1);
  #endif

  pixels.begin();
  pixels.clear();
  pixels.show();

  //振動モータ
  ledcSetup(2, freq, resolution);
  ledcAttachPin(MOTORPIN, 2);

  // LCD初期設定
  lcd.init();                 // LCD初期化
  lcd.setRotation(1);         // 画面向き設定（0～3で設定、4～7は反転)
  // canvas.setColorDepth(8);    // カラーモード設定（書かなければ初期値16bit。24bit（パネル性能によっては18bit）は対応していれば選択可）
  //                             // CORE2 GRAY、のスプライトは16bit以上で表示されないため8bitに設定
  // canvas.setTextWrap(false);  // 改行をしない（画面をはみ出す時自動改行する場合はtrue）
  // canvas.setTextSize(1);      // 文字サイズ（倍率）
  // canvas.createSprite(lcd.width(), lcd.height()); // canvasサイズ（メモリ描画領域）設定（画面サイズに設定）

   lcd.setColorDepth(24);  // RGB888の24
  lcd.fillScreen(0);  // 黒で塗り潰し
  SD.begin(TFCARD_CS_PIN, SPI, 20000000);//ビデオ再生


//初期画面
  String fileName = "/"+ String(i) +".jpg";
    char jpegs[fileName.length()+1];
    fileName.toCharArray(jpegs, sizeof(jpegs));
    //Serial.print(jpegs);
    lcd.drawJpgFile(SD,jpegs, 0, 0); 


  /* I/O setup */

  // 受信ピンの入力設定
  pinMode(RECIEVE1_PIN, INPUT);
  
  // LED PWM駆動の設定(チャンネル1:赤外線,チャンネル2:振動モータ)
  ledcSetup(1, IR_HZ, 11); //11bitのときは最大39062Hz,ledcSetup ( チャンネル , 周波数 , bit数(分解能) ) ;　//PWM出力波形の初期設定
  ledcAttachPin(SEND_PIN, 1);//ledcAttachPin( 端子番号 , チャンネル ) ;　//チャンネルに対する出力端子を設定
  ledcWrite(1,0);//ledcWrite( チャンネル , Duty比 ) ;　// 指定したDuty比でPWM出力
  
  /* タイマー割込み初期化 */
  timer1 = timerBegin(0, 80, true);
  timerAttachInterrupt(timer1, &onTimer1, true);
  timerAlarmWrite(timer1, IR_DATA_T, true);
  timerAlarmEnable(timer1);

  //IR受信割り込みの設定
  attachInterrupt(digitalPinToInterrupt(RECIEVE1_PIN), irInterrupt, CHANGE);

  //LCD制御タスク
  xTaskCreatePinnedToCore(lcdControl,"lcdControl", 4096, NULL, 1, &th[0], 0);
  //xTaskCreatePinnedToCore(タスクの関数名,"タスク名",スタックメモリサイズ,NULL,タスク優先順位,タスクハンドルポインタ,Core ID);
}

//setup finish --------------------------------------------




// メイン -----------------------------------------
void loop() {
  // 送る言葉の弾丸
  // if(wordselect%3==0){
  //   words="こんにちは";        // 表示内容をcanvasに準備
  // }
  // else if(wordselect%3==1){
  //   words="ありがとう";       // 表示内容をcanvasに準備
  // }
  // else if(wordselect%3==2){
  //   words="ごめん";       // 表示内容をcanvasに準備
  // }

  /* タイマー割込み処理 */
  
  //2kHzで実行
  if (timeCounter1 > 0) {
    portENTER_CRITICAL(&timerMux);
    timeCounter1--;
    portEXIT_CRITICAL(&timerMux);

    //IR送信処理
    irSend();
  }

  /* IR受信判定 */
  
  if (irRecieveData!=0){
    if(i<49){
      if(irRecieveData == 0b1110100011) {
        Serial.println("ありがとう");
        ledcWrite(2, 512);//振動モータ
        //被弾フラグ
        receive=1;
      } else if(irRecieveData == 0b1110010011) {
        Serial.println("やるじゃないか");
        ledcWrite(2, 512);//振動モータ
        //被弾フラグ
        receive=2;
      } else if(irRecieveData == 0b1110001011) {
        Serial.println("あいしてる");
        ledcWrite(2, 512);//振動モータ
        //被弾フラグ
        receive=3;
      }
      else if(irRecieveData == 0b1110000111) {
        Serial.println("なんでやねん");
        ledcWrite(2, 512);//振動モータ
        //被弾フラグ
        receive=4;
      }
      else if(irRecieveData == 0b1110000001) {
        Serial.println("がんばって");
        ledcWrite(2, 512);//振動モータ
        //被弾フラグ
        receive=5;
      }
      else if(irRecieveData == 0b1110000000) {
        Serial.println("ごめん");
        ledcWrite(2, 512);//振動モータ
        //被弾フラグ
        receive=6;
      }
      //delay(1);
    }
    else{
    Serial.println("間違ったデータを受信");//受信データが正しいか、受信自体をしているのか確認、テスト用
    }
    irRecieveData = 0;
  }

        //受け取り画面動画再生
     if(receive){
        if(i<49){//初期位置決め
          if(receive==1){
            i=889;//ありがとう
            colorR=0;
            colorG=150;
            colorB=0;//緑
          }
          else if(receive==2){
            i=1039;//やるじゃないか
            colorR=32;
            colorG=178;
            colorB=170;//緑青
          }
          else if(receive==3){
            i=1189;//あいしてる
            colorR=255;
            colorG=20;
            colorB=147;//ピンク
          }
          else if(receive==4){
            i=1339;//なんでやねん
            colorR=150;
            colorG=0;
            colorB=0;//赤
          }
          else if(receive==5){
            i=1489;//がんばって
            colorR=255;
            colorG=255;
            colorB=0;//黄色
          }
          else if(receive==6){
            i=1639;//ごめんなさい
            colorR=138;
            colorG=43;
            colorB=226;//紺
          }
          wordselect=0;
        }
        // else if(i<=889||i>=1789){
        //   recive==0;
        // }

      i++;
      ledoff++;

      if(ledoff<=95&&ledoff>=85){
          ledr--;
          pixels.setPixelColor(ledr, pixels.Color(colorR,colorG,colorB));
        }
        else{
        pixels.clear();
        }

        if(ledoff<=68&&ledoff%2==1){
          for(int all=0;all<NUMPIXELS;all++){
           pixels.setPixelColor(all, pixels.Color(colorR,colorG,colorB));
          }
        }

       pixels.show();


      if(i==1038){
        receive=0;
        i=1;
        ledoff=0;
      }
      if(i==1188){
        receive=0;
        i=1;
        ledoff=0;
      }
      if(i==1338){
        receive=0;
        i=1;
        ledoff=0;
      }
      if(i==1488){
        receive=0;
        i=1;
        ledoff=0;
      }
      if(i==1638){
        receive=0;
        i=1;
        ledoff=0;
      }
      if(i==1788){
        receive=0;
        i=1;
        ledoff=0;
      }

      Serial.print("iは");
      Serial.println(i);

      String fileName = "/"+ String(i) +".jpg";
      char jpegs[fileName.length()+1];
      fileName.toCharArray(jpegs, sizeof(jpegs));
      //Serial.print(jpegs);
      lcd.drawJpgFile(SD,jpegs, 0, 0); 
      
      delay(1);//メモリオーバー,task watchdog?というエラーが出て、一定時間でリセットしてしまうため

    
     }

  //ボタン
  if (M5.BtnA.wasPressed()) {
    if(i>=769&&i<=888){//決定画面の時
          if(wordselect%6==0){
          irSendData = 0b1110100011;
        }
        else if(wordselect%6==1){
          irSendData = 0b1110010011;
        }
        else if(wordselect%6==2){
          irSendData = 0b1110001011;
        }
        else if(wordselect%6==3){
          irSendData = 0b1110000111;
        }
        else if(wordselect%6==4){
          irSendData = 0b1110000001;
        }
        else if(wordselect%6==5){
          irSendData = 0b1110000000;
        }
        ledcWrite(2, 512);//振動モータ
        // Serial.println(words);
        // Serial.println(irSendData);//
        shot=1;
        bluetoo=1;
        playMP3("/send.mp3");
          delay(1);
    }    
  }
  if (M5.BtnB.wasPressed()) {
      if(i==1||i==11||i==17||i==23||i==33||i==43||i==48){
    wordselect++;
    selectmovie=1;
    playMP3("/select.mp3");
    delay(1);
      }
  }
  if (M5.BtnC.wasPressed()) {
    if(i==1||i==11||i==17||i==23||i==33||i==43||i==48){
    decide=1;//決定画面
    playMP3("/decide.mp3");
    }
  // wordselect--;
  //    if(wordselect<0){
  //      wordselect=2;
  //   }
  }
  m5.update();

  bts.println(bluetoo);//bluetoothでbluetooの中身を送信

    bluetoo=0;
  
  // delay(100); // 遅延時間（ms）
}
//loop finish--------------------------------------------------



/* LCD制御タスク *///マルチコア部分、描画はこっちでやるべき、違うCPUで作業
void lcdControl(void *pvParameters) {
  while(1){
    if(receive){
      //Serial.println(countr);
      countr++;
    }

     if(countr==2){//動画1回分
       ledcWrite(2,0);//振動モータ
     }

    if(receive==0&&shot==0){//動画1回分
      colorR=0;
      colorG=0;
      colorB=0;//リセット
      countr=0;
      xr=-100;
      ledr=NUMPIXELS;
      pixels.clear();
      pixels.show();
    }

    if(shot==1){
      counts++;
    }
    
      if(counts==2){//動画1回分
      ledcWrite(2,0);//振動モータ
      }

    if(shot==0&&receive==0){//動画1回分
      //lcd.fillScreen(0);  // 黒で塗り潰し
      colorR=0;
      colorG=0;
      colorB=0;//リセット
      counts=0;
      pixels.clear();
      pixels.show();
      led=0;
      xs=100;//初期位置に戻す
    }
    

    //選択画面動画再生
    if(selectmovie){
        i++;
      String fileName = "/"+ String(i) +".jpg";
      char jpegs[fileName.length()+1];
      fileName.toCharArray(jpegs, sizeof(jpegs));
      //Serial.print(jpegs);
      lcd.drawJpgFile(SD,jpegs, 0, 0); 
      
      delay(1);//メモリオーバー,task watchdog?というエラーが出て、一定時間でリセットしてしまうため
      
      //Serial.println(i);
      if(i==11){//ありがと-やるじゃん
        selectmovie=0;
      }
      if(i==17){//やるじゃん-あいしてる
        selectmovie=0;
      }
      if(i==23){//あいしてる-なんでや
        selectmovie=0;
      }
      if(i==33){//なんでや-がんば
        selectmovie=0;
      }
      if(i==43){//がんば-ごめん
        selectmovie=0;
      }
      if(i==48){//ごめん-ありがとう
        selectmovie=0;
        i=1;
      }
    }

    //決定画面動画再生
    if(decide){
      if(i<49){//初期位置決め
          if(wordselect%6==0){
            //Serial.println(wordselect);
            i=769;//ありがとう
          }
          else if(wordselect%6==1){
            i=789;//やるじゃないか
            //Serial.println(wordselect);
          }
          else if(wordselect%6==2){
            i=809;//あいしてる
            //Serial.println(wordselect);
          }
          else if(wordselect%6==3){
            i=829;//なんでやねん
           // Serial.println(wordselect);
          }
          else if(wordselect%6==4){
            i=849;//がんばって
           // Serial.println(wordselect);
          }
          else if(wordselect%6==5){
            i=869;//ごめんなさい
            //Serial.println(wordselect);
          }
          delay(1);
        }

      i++;

      // Serial.print("iは");
      // Serial.println(i);

      if(i==788){
        decide=0;
      }
      if(i==808){
        decide=0;
      }
      if(i==828){
        decide=0;
      }
      if(i==848){
        decide=0;
      }
      if(i==868){
        decide=0;
      }
      if(i==888){
        decide=0;
      }

      String fileName = "/"+ String(i) +".jpg";
      char jpegs[fileName.length()+1];
      fileName.toCharArray(jpegs, sizeof(jpegs));
      //Serial.print(jpegs);
      lcd.drawJpgFile(SD,jpegs, 0, 0); 
      
      delay(1);//メモリオーバー,task watchdog?というエラーが出て、一定時間でリセットしてしまうため

    
    }

   
  //  Serial.print("wordselectはこれ");
  //  Serial.println(wordselect);
    
    //送信画面動画再生
    if(shot){
      if(i>=769&&i<=888){//初期位置決め,決定画面の時
          if(wordselect%6==0){
            //Serial.println(wordselect);
            i=49;//ありがとう
            colorR=0;
            colorG=150;
            colorB=0;//緑
          }
          else if(wordselect%6==1){
            i=169;//やるじゃないか
            colorR=32;
            colorG=178;
            colorB=170;//緑青
            //Serial.println(wordselect);
          }
          else if(wordselect%6==2){
            i=289;//あいしてる
            colorR=255;
            colorG=20;
            colorB=147;//ピンク
            //Serial.println(wordselect);
          }
          else if(wordselect%6==3){
            i=409;//なんでやねん
            colorR=150;
            colorG=0;
            colorB=0;//赤
            //Serial.println(wordselect);
          }
          else if(wordselect%6==4){
            i=529;//がんばって
            colorR=255;
            colorG=255;
            colorB=0;//黄色
            //Serial.println(wordselect);
          }
          else if(wordselect%6==5){
            i=649;//ごめんなさい
            colorR=138;
            colorG=43;
            colorB=226;//紺
            //Serial.println(wordselect);
          }
          wordselect=0;
        }
        else if(i<=48||i>=889){
          shot=0;
        }

      i++;
      ledoff++;
      
      //点滅
        if(ledoff<=77&&ledoff>=67){
          led++;
          pixels.setPixelColor(led, pixels.Color(colorR,colorG,colorB));
        }
        else{
        pixels.clear();
        }
        if(ledoff%5==1&&ledoff<=10){
          for(int all=0;all<NUMPIXELS;all++){
            pixels.setPixelColor(all, pixels.Color(colorR,colorG,colorB));
          }
        }
        if(ledoff%4==1&&ledoff<=20&&ledoff>=10){
          for(int all=0;all<NUMPIXELS;all++){
            pixels.setPixelColor(all, pixels.Color(colorR,colorG,colorB));
          }
        }
        if(ledoff%3==1&&ledoff<=30&&ledoff>=20){
          for(int all=0;all<NUMPIXELS;all++){
            pixels.setPixelColor(all, pixels.Color(colorR,colorG,colorB));
          }
        }
        if(ledoff%2==1&&ledoff<=40&&ledoff>=30){
          for(int all=0;all<NUMPIXELS;all++){
            pixels.setPixelColor(all, pixels.Color(colorR,colorG,colorB));
          }
        }


      
      pixels.show();

      
      if(i==168){
        shot=0;
        i=1;
        ledoff=0;
      }
      if(i==288){
        shot=0;
        i=1;
        ledoff=0;
      }
      if(i==408){
        shot=0;
        i=1;
        ledoff=0;
      }
      if(i==528){
        shot=0;
        i=1;
        ledoff=0;
      }
      if(i==648){
        shot=0;
        i=1;
        ledoff=0;
      }
      if(i==768){
        shot=0;
        i=1;
        ledoff=0;
      }

      Serial.print("ledoffは");
      Serial.println(ledoff);
    
      String fileName = "/"+ String(i) +".jpg";
      char jpegs[fileName.length()+1];
      fileName.toCharArray(jpegs, sizeof(jpegs));
      //Serial.print(jpegs);
      lcd.drawJpgFile(SD,jpegs, 0, 0); 
      
      delay(1);//メモリオーバー,task watchdog?というエラーが出て、一定時間でリセットしてしまうため

    }
     

     if(receive){
          if(receive==1){
            playMP3("/thanks.mp3");
          }
          else if(receive==2){
            playMP3("/good.mp3");
          }
          else if(receive==3){
           playMP3("/love.mp3");
          }
          else if(receive==4){
           playMP3("/why.mp3");
          }
          else if(receive==5){
          playMP3("/fight.mp3");
          }
          else if(receive==6){
           playMP3("/sorry.mp3");
          }

        }

    //     //受け取り画面動画再生
    //  if(receive){
    //     if(i<49){//初期位置決め
    //       if(receive==1){
    //         i=889;//ありがとう
    //         colorR=0;
    //         colorG=150;
    //         colorB=0;//緑
    //       }
    //       else if(receive==2){
    //         i=1039;//やるじゃないか
    //         colorR=32;
    //         colorG=178;
    //         colorB=170;//緑青
    //       }
    //       else if(receive==3){
    //         i=1189;//あいしてる
    //         colorR=255;
    //         colorG=20;
    //         colorB=147;//ピンク
    //       }
    //       else if(receive==4){
    //         i=1339;//なんでやねん
    //         colorR=150;
    //         colorG=0;
    //         colorB=0;//赤
    //       }
    //       else if(receive==5){
    //         i=1489;//がんばって
    //         colorR=255;
    //         colorG=255;
    //         colorB=0;//黄色
    //       }
    //       else if(receive==6){
    //         i=1639;//ごめんなさい
    //         colorR=138;
    //         colorG=43;
    //         colorB=226;//紺
    //       }
    //       wordselect=0;
    //     }
    //     // else if(i<=889||i>=1789){
    //     //   recive==0;
    //     // }

    //   i++;
    //   ledoff++;

    //   if(ledoff<=95&&ledoff>=85){
    //       ledr--;
    //       pixels.setPixelColor(ledr, pixels.Color(colorR,colorG,colorB));
    //     }
    //     else{
    //     pixels.clear();
    //     }

    //     if(ledoff<=68&&ledoff%2==1){
    //       for(int all=0;all<NUMPIXELS;all++){
    //        pixels.setPixelColor(all, pixels.Color(colorR,colorG,colorB));
    //       }
    //     }

    //    pixels.show();


    //   if(i==1038){
    //     receive=0;
    //     i=1;
    //     ledoff=0;
    //   }
    //   if(i==1188){
    //     receive=0;
    //     i=1;
    //     ledoff=0;
    //   }
    //   if(i==1338){
    //     receive=0;
    //     i=1;
    //     ledoff=0;
    //   }
    //   if(i==1488){
    //     receive=0;
    //     i=1;
    //     ledoff=0;
    //   }
    //   if(i==1638){
    //     receive=0;
    //     i=1;
    //     ledoff=0;
    //   }
    //   if(i==1788){
    //     receive=0;
    //     i=1;
    //     ledoff=0;
    //   }

    //   Serial.print("iは");
    //   Serial.println(i);

    //   // String fileName = "/"+ String(i) +".jpg";
    //   // char jpegs[fileName.length()+1];
    //   // fileName.toCharArray(jpegs, sizeof(jpegs));
    //   // //Serial.print(jpegs);
    //   // lcd.drawJpgFile(SD,jpegs, 0, 0); 
      
    //   // delay(1);//メモリオーバー,task watchdog?というエラーが出て、一定時間でリセットしてしまうため

    
    //  }

    

      //LEDシート制御
    //   if(i%10){
    //           for(int all=0;all<NUMPIXELS;all++){
    //       pixels.setPixelColor(all, pixels.Color(colorR, colorG,colorB));
    //       }
    //   }
    //       else{
    //     pixels.clear();
    //   }
  
   
    // pixels.show();
      // if(shot){
      //   led=2*led;
      // }

      // if(receive){
      //   ledr--;
      // }
      
      // ledpix=led/3; 
      // ledrpix=ledr/3;

      // if(led){
      // pixels.setPixelColor((int)ledpix, pixels.Color(150, 0, 0));
      // pixels.show();
      // // Serial.println("sendgawa");
      // // Serial.println(led);
      // }

      // if(ledr<30){
      // pixels.setPixelColor((int)ledrpix, pixels.Color(0, 0, 150));
      // pixels.show();
      // //Serial.println("receivegawa");
      // //Serial.println(ledr);
      // }

      // bts.println(bluetoo);//bluetoothでbluetooの中身を送信

      // bluetoo=0;

    delay(1);
  }
}
  

/* バッファ内容を赤外線送信 */
void irSend() {
  if ((irSendData & 0b1000000000) == 0b1000000000){
    //Serial.println(irSendData);//テスト用
    ledcWrite(1,IR_DUTY);
  } else {
    ledcWrite(1,0);
  }
  irSendData = (irSendData << 1) & 0b1111111111;
}

/* IR変更割り込み処理 */
void irInterrupt() {  
  
  //割り込み処理停止
  detachInterrupt(digitalPinToInterrupt(RECIEVE1_PIN));
  
  //信号H/L取得
  int irState = digitalRead(RECIEVE1_PIN);
  unsigned int signalTime = micros() - irTime;

  //T=500us未満の場合は処理しない
  if (signalTime > IR_DATA_T - ir_tolerance[irState]) {
    //Serial.print("irTmpDataは");
    //Serial.println(irTmpData);//テスト用
    //前回信号受信時間を更新
    irTime = irTime + signalTime;
  
    //信号長取得
    int irCount = int((signalTime + ir_tolerance[irState])/IR_DATA_T);

    if(irCount == 3 & irState == HIGH){
      
      //Leader Pulseを受信すれば、一時データをクリアする
      irTmpData = 0b111;

    } else if(irCount == 2 & irState == HIGH){
      
      //Footer Pulseを受信すれば、受信データを確定する
      irTmpData=(irTmpData<<2)+0b11;
      irRecieveData=irTmpData;
      irTmpData = 0;
      
    } else {
  
      //受信データに追加する
      irTmpData = irTmpData << irCount;
      if(irState == HIGH){
        irTmpData = irTmpData + (1<<irCount-1);
      }
    }
  }
  
  //割り込み再開
  attachInterrupt(digitalPinToInterrupt(RECIEVE1_PIN), irInterrupt, CHANGE);
  
}

