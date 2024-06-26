/*
ОТОБРАЖЕНИЕ НА СЕМИСЕГМЕНТНОМ ДИСПЛЕЕ 74HC595 РЕЗУЛЬТАТА ТЕКУЩЕГО ИЗМЕРЕНИЯ.
ОТПРАВКА ТЕЛЕГРАМ-БОТУ ПОСЛЕДНИХ N ИЗМЕРЕНИЙ КАК МАССИВА И ГИСТОГРАММЫ (ОЦЕНИТЬ
ПЕРСПЕКТИВУ БУДУЩИХ ИЗМЕНЕНИЙ), А ТАКЖЕ ФАЙЛА С МАССИВОМ ИЗМЕРЕНИЙ unix-ВРЕМЕН (ИСТОРИЯ ПОКАЗАНИЙ)

В НАСТРОЙКАХ ПЛАТЫ ВЫБРАТЬ Flash Size: 4MB (FS:3MB OTA:~512KB), Erase Flash: All Flash Contents

ДЛИТЕЛЬНОСТЬ СНА НЕ БОЛЕЕ 2 ЧАСОВ. МАКСИМАЛЬНО ВОЗМОЖНАЯ ДЛИТЕЛЬНОСТЬ СНА ДЛЯ ESP8266 НЕМНОГИМ
БОЛЬШЕ ЭТОГО ВРЕМЕНИ, ПЛЮС ОНО НЕ ПОСТОЯННО

НЕОБХОДИМО ПРОПИСАТЬ СВОИ НАСТРОЙКИ WI-FI И ТЕЛЕГРАММ-БОТА И КОЭФФИЦИЕНТ ПЕРЕВОДА СЫРЫХ ЗНАЧЕНИЙ В ГРАММЫ ДЛЯ HX711
*/

#define DT_PIN D2         // dataPin hx711
#define SCK_PIN D1        // clockPin hx711

#define DIO_PIN D5        // 74HC595
#define SCLK_PIN D7
#define RCLK_PIN D6

#define BTN_PIN D3        // кнопка тарирования
#define ONF_PIN D8        // пин на светодиод индикатор-включения

#define akkum_in A0       // для измерения напряжения с аккумулятора

#define SLEEP_DURATION 900   // длительность сна в секундах (примерно столько будет находится ESP8266 в режиме глубоко сна)// максимум 7200
#define TRY_SLEEP 600        // время сна в секундах, если не удалось получить время (по истечении пробуждение и новая попытка получить время)

#define TIMOUT_DISPLAY 10000 // длительность отображения значения на семисегментном дисплее, в мс

#define GMT 3                // часовой пояс

#define N_ARR_SZ 4          // размер массива измерений
#define N_TO_ADD 1           // (1,2,3...) кол-во пробуждений после сна длительностью SLEEP_DURATION, после которого будут добавлены результаты в файл
#define N_TO_SEND 2          // (1,2,3...) кол-во записей в файл, после которого будет отправка файла боту // отправка файла результата каждые N_TO_SEND*N_TO_ADD пробуждений

const float  k = -11.6;             // коэффициент преобразования сырого веса в граммы
const float kv = 0.004089;          // коэффициент перевода значений c пина в Вольты

#define AP_SSID "your WiFi-SSID"            // название wi-fi сети
#define AP_PASS "your WiFi-PASS"            // пароль от нее
#define BOT_TOKEN "your bot token"          // токен телеграм-бота
#define CHAT_ID "your chat id"              // ID-чата

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <rtc_utils.h>                  // обёртка для более удобного использования RTC памяти на esp8266: https://github.com/GyverLibs/rtc_utils
#include <LittleFS.h>                   // для проверки файлов (веб-файловый менеджер): https://github.com/earlephilhower/arduino-esp8266littlefs-plugin
#include <CharPlot.h>                   // создание символьной графики: https://github.com/GyverLibs/CharDisplay

#include <FastBot.h>                    // телеграм бот на esp8266/esp32: https://github.com/GyverLibs/FastBot
FastBot bot(BOT_TOKEN);                 // инструкция как создать и настроить Telegram бота:   https://kit.alexgyver.ru/tutorials/telegram-basic/

#include <PairsFile.h>                  // хранение данных в формате ключ: значение:  https://github.com/GyverLibs/Pairs
PairsFile conFile(&LittleFS, "/net.dat", 3000);

#include <GyverHX711.h>                       // работа с датчиками (АЦП) HX711: https://github.com/GyverLibs/GyverHX711
GyverHX711 hx(DT_PIN, SCK_PIN, HX_GAIN64_A);  // HX_GAIN128_A - канал А усиление 128; HX_GAIN64_A - канал А усиление 64; HX_GAIN32_B - канал B усиление 32

#include <GyverSegment.h>                     //библиотека для работы с дисплеями: https://github.com/GyverLibs/GyverSegment
Disp595_4 disp(DIO_PIN, SCLK_PIN, RCLK_PIN);

float ves_kg;                           // переменная для хранения измерний
int32_t ves_pus;                        // сырой вес пустых весов

int32_t cor_time;                       // время поправки на длительность сна
uint32_t end_time;                      // время ухода в сон в предыдущий период активности

uint32_t tmr_to_sleep;                  // таймер перехода в спящий режим
int32_t delta_t;                        // точность сна
byte cnt;                               // счётчик числа пробуждений

volatile boolean flag_att = 0;          // флаг нажатия кнопки для тарирования
volatile uint32_t TimerOn;              // счётчик-антидребезг для кнопки
volatile byte clckd = 0;                // счётчик кликов для тарирования и перезапуска

void setup() {
//  Serial.begin(115200);
//  Serial.println();

  hx.sleepMode(false);                  // будим модуль весов

  pinMode(BTN_PIN, INPUT_PULLUP);       // одним концом кнопка на пине, другим на GND
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), myIsr, FALLING); // аппартное прерывание на кнопке

  pinMode(ONF_PIN, OUTPUT);             // сигнализируем активность включением сигнального светодиода
  digitalWrite(ONF_PIN, HIGH);          // (на esp8266 включается низким сигналом)

  LittleFS.begin();                     // подключаем файловую систему

  conFile.begin();                      // прочитать из файла

  uint16_t t1 = millis();               // чтобы отсечь шумы измерений, пропускаем начальные данные
  while(millis() - t1 < 1000){          // холостое считывание веса в течение секунды
    while(!hx.available()) {
      yield();
    }
    int32_t hol_ves = hx.read();
  }

  if(!conFile.contains("key_cnt")){     // проверяем какой по счёту запуск, если первый, то файл чистый и потому не содержит никаких ключей
    cnt = 0;                            // первый запуск
//    Serial.println("Первый запуск");
    conFile.set("key_cnt", cnt);        // записали в файл
    while (!hx.available()) {           // ждём отправки данных с hx711
      yield();
    }
    ves_pus = hx.read();                        // читаем в переменную
    conFile.set("key_ves_pus", ves_pus);
    delta_t = (int32_t)(0.075*SLEEP_DURATION);        // высчитываем коэф поправки на точность сна
    conFile.set("key_delta_t", delta_t);
    conFile.set("key_add", 0);                  // зануляем счётчик числа на добавление в файл
    conFile.set("key_send", 0);                 //     ... добавлений в файл
    File file_times = LittleFS.open("/time.txt", "w");    // создаем файл для хранения времени
    file_times.print("0 0");                        // записываем: end_time = 0; cor_time = 0;
    file_times.close();
  }
  else {
    cnt = conFile.get("key_cnt");               // записываем данные из файла по ключу
    cnt++;
    conFile.set("key_cnt", cnt);
    ves_pus = conFile.get("key_ves_pus");
  }
  conFile.update();                   // зафиксировать изменения в файл
  delay(500);                         // физ.задержка, чтобы всё успело записаться

  ves_kg = hx_kg();                   // тут записываем результат измерения

//  Serial.print("ves_pus: ");
//  Serial.println(ves_pus);
//  Serial.print("ves_kg: ");
//  Serial.println(ves_kg);

  disp_print_ves(ves_kg, TIMOUT_DISPLAY);  // выводим результат измерения на дисплей

  wifiSupport();                           // подключаемся к  wi-fi сети

  if (flag_att) {                          // если была нажата кнопка
    if(clckd >= 2 && clckd < 5){           // от 2 до 4 раз
//      Serial.println("tare");
      while (!hx.available()) {
        yield();
      }
      ves_pus = hx.read();                      // производим тарирование
      conFile.set("key_ves_pus", ves_pus);      // запоминаем тарированный сырой вес
      conFile.update();
      delay(5);
      ves_kg = hx_kg();
      disp_print_ves(ves_kg, TIMOUT_DISPLAY >> 1);
    }
    else if(clckd == 5){                        // сброс всех данных и перезапуск
//      Serial.println("restart");
      conFile.remove("key_cnt");                // удаляем ключ счётчика числа запусков
      conFile.update();
      delay(5);
      LittleFS.remove("/result.txt");          // ... файл с результами
      delay(5);
      LittleFS.remove("/time.txt");            // ... файл с фиксируемыми временами
      delay(5);
      ESP.restart();
    }
    flag_att = 0;
  }

  float voltage;                        // сюда будем записывать напряжение аккумулятора
  for (byte k = 0; k < 10; k++) {
    yield();
    voltage = ((float)analogRead(akkum_in)) * kv;  // в Вольтах
  }

  bot.attach(newMsg);                   // подключаем обработчик сообщений
  bot.sendMessage("U = " + String(voltage) + " В", CHAT_ID);    // отправляем сообщение для определения времени

  File file_time = LittleFS.open("/time.txt", "r");     // открываем файл времени для чтения
  if(file_time){
    String t = "";                                      // храним данные с файла времени
    while (file_time.available()) {                     // посимвольно читаем
      t += (char)(file_time.read());
      delay(2);                                         // физическая задержка
    }
    String s_end_time = t.substring(0,t.indexOf(" "));          // парсим в строковую переменную время последнего запуска
    end_time = s_end_time.toInt();                              // преобразуем его в целочисленное число
    String s_cor_time = t.substring(t.indexOf(" ") + 1);        //  .... ... .... поправка на длительность сна
    cor_time = s_cor_time.toInt();
  }
  file_time.close();            // закрываем файл времени

  uint16_t count = 0;          // в RTC-память будем записывать количество пробуждений после подачи питания
  rtc_read(&count);
  if(count == 0){               // если это запуск после отключения питания
    end_time = 0;               // зануляем фиксируемое время, потому что перерыв может быть любым, в том числе и больше времени сна
  }
  count++;
  rtc_write(&count);            // запоминаем в RTC-память

  tmr_to_sleep = millis();      // глобальный таймер, поэтому используем
  while (millis() - tmr_to_sleep < 2000) {      // ждём 2 секунды, чтобы отпрвить следующее сообщение
    yield();
  }
  send_res_in_mess(ves_kg);     // ...  отправляем боту

  tmr_to_sleep = millis();      // отсчёт времени для перехода в режим сна
}

void loop() {
  bot.tick();

  static boolean flag_early_wake = 0;           // раннее пробуждение
  static boolean flag_to_sleep = 0;             // флажок разрешения перехода в спящий режим
  static uint32_t sleep_time = SLEEP_DURATION;         // длительность сна
  static uint32_t tmr_bot;                      // отсчёт попыток получения времени
  static boolean flag_time = 0;                 // флажок получения времени

  static byte count_add_res = 0;                // количество пробуждений (активностей) после последнего добавления результата в файл
  static byte count_send = 0;                   // количество добавлений результатов в файл

  if(millis() - tmr_to_sleep > 60000){          // если после включения прошло более минуты
    flag_to_sleep = 1;                          // разрешаем перейти в сон
    sleep_time = TRY_SLEEP;                     //      .... на TRY_SLEEP секунд, чтоб снова попытаться получить время
    end_time = 0;                               // зануляем время последней активности
    time_fix(end_time, cor_time);               // записываем в файл
  }

  if (millis() - tmr_bot >= 3000) {             // через каждые 3 секунды проверяем получено ли время
    if(bot.getUnix() > 1700000000)              // больше 170млн секунд уже прошло с 1970 года
      flag_time = 1;
    tmr_bot = millis();
  }

  if(flag_time){                                // если стало известно текущее время
    if(end_time != 0) {                                 // если это не первое включение(активность) // при первом включении в файле времени end_time = 0
      delta_t = conFile.get("key_delta_t");                       // получаем коэф поправки
      int32_t wake_time = (int32_t)end_time + SLEEP_DURATION + cor_time;   // таким должно быть время пробуждения
      int32_t now_time = (int32_t)bot.getUnix();                              // текущее время
      if(cor_time != 0 && (wake_time - now_time) >=  delta_t){     // если время поправки уже задано  и см. условие  -- оцениваем стоит ли ещё спать
//        Serial.println("Early wake");
        flag_early_wake = 1;                            // отмечаем раннее пробуждение, чтоб не фиксировать времена
        sleep_time = wake_time - now_time;              // спим ещё столько
      }
      else {
        uint32_t start_time = bot.getUnix() - (uint32_t)(millis()/1000);    // время пробуждения
        cor_time += SLEEP_DURATION - (int32_t)(start_time - end_time);      // корректируем поправку
        if(abs(cor_time) > SLEEP_DURATION >> 1){     // для случая если было забыто замкнуть GPIO16 и RST
          cor_time = 0;
        }
       }
    }
    if(flag_early_wake == 0){                       // если пробуждение не раннее
      sleep_time += cor_time;                       // длительность сна с учетом поправки
      count_add_res = conFile.get("key_add");
      count_add_res++;
      if(count_add_res >= N_TO_ADD){               // если пора записывать в файл
        end_time = bot.getUnix();
        add_result(ves_kg, end_time);
//        Serial.println("Add_result");
        count_add_res = 0;                           // зануляем счетчик
        count_send = conFile.get("key_send");
        count_send++;
        conFile.set("key_send", count_send);
      }
      conFile.set("key_add", count_add_res);
      conFile.update();
      delay(500);
      if(count_send >= N_TO_SEND){                  // если количество активностей больше того числа, после которого нужно отправить файл
        delay(0);                                   // здесь вызывается обработчик wi-fi, особенность работы
        yield();
        send_result();                              // отправляем результат, если файл большой, то довольно долгая отправка
        delay(0);                                   // здесь вызывается обработчик wi-fi, особенность работы
        yield();
//        Serial.println("Send_res");
        conFile.set("key_send", 0);
      }
      conFile.update();
      delay(500);
      end_time = bot.getUnix();                     // берем текущее время
      time_fix(end_time, cor_time);                 // фиксируем в файл
    }
    flag_to_sleep = 1;                              // разрешаем переход в режим сна
  }

  if(flag_to_sleep){                                 // если разрешён переход в режим сна, и нет необходимости ничего отправлять
//    Serial.print("cor_time: ");
//    Serial.println(cor_time);
//    Serial.print("end_time: ");
//    Serial.println(end_time);
//    Serial.print("sleep_time: ");
//    Serial.println(sleep_time);
    digitalWrite(ONF_PIN, LOW);                // выключаем светодиод-индикатор
    hx.sleepMode(true);                             // переводим модуль весов в режим энергосбережения
    delay(5);
    ESP.deepSleep(sleep_time*1E6);                  // уходим в сон (в микросекундах)
  }
}

// УСТАНОВЛЕНИЕ wi-fi СВЯЗИ
void wifiSupport() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(AP_SSID, AP_PASS);
  byte trycon = 0;                            // счётчик числа попыток подключения
  while (WiFi.status() != WL_CONNECTED) {
    if (trycon++ < 30)
      delay(500);                             // 30 попыток подключиться с полусекундным интервалом
    else {
      delay(1000);
      ESP.restart();                          // в случае ошибки подключения перезапуск МК
    }
  }
}

// ЗАПИСЬ (макс. N_ARR_SZ измерений) РЕЗУЛЬТАТОВ ПРИ КАЖДОМ ПРОБУЖДЕНИИ В ФАЙЛ И ОТПРАВКА БОТУ
void send_res_in_mess(float ves_kilo){
  String key_res = String(cnt % N_ARR_SZ);  // ключ для записи измерения
  conFile.set(key_res, ves_kilo);           // записываем
  conFile.update();                         // фиксируем измерение
  String mess = "";                         // сюда будем собирать сообщение для отправки боту
  if (cnt < N_ARR_SZ) {                     // если число пробуждений < N_ARR_SZ
    float arr[cnt+1];                       // создаем массив размером на 1 больше значения счётчика числа пробуждений
    for(byte i=0; i<cnt+1; i++){
      arr[i] = conFile.get(String(i));      // элементы массива берем из файла по ключу
      mess += String(arr[i]) + " ";         // собираем в одно сообщение всю имеющуюся последовательность измерений
//      Serial.print(" ");
//      Serial.print(arr[i]);                 // выводим эти данные в монитор-порта через пробел
    }
//    Serial.println();
    bot.sendMessage("Результаты измерений за предыдущие пробуждения:\n" + mess, CHAT_ID);     // отправляем боту
  }
  else {                                    // если число пробуждений >= N_ARR_SZ
    float arr[N_ARR_SZ];
    for (byte i = 0; i < N_ARR_SZ; i++) {
      if (cnt % N_ARR_SZ + 1 + i < N_ARR_SZ)
        key_res = String(cnt % N_ARR_SZ + 1 + i);
      else
        key_res = String(cnt % N_ARR_SZ + 1 + i - N_ARR_SZ);
      arr[i] = conFile.get(key_res);
      mess += String(arr[i]) + " ";         // собираем в одно сообщение всю имеющуюся последовательность измерений
//      Serial.print(" ");
//      Serial.print(arr[i]);
    }
    byte n = 0;                                 // счётчик числа различных значений
    byte l;
    for (byte m = 0; m < N_ARR_SZ; m++) {
      for(l = 0; l < m; l++){
        if(arr[m] == arr[l])   break;
      }
      if(m == l)  n++;
    }
//    Serial.println(CharPlot<COLON_X2>(arr, N_ARR_SZ, n));                       // рисуем в мониторе порта гистограмму последних N_ARR_SZ измерений
    bot.sendMessage("Результаты измерений за предыдущие " + String(N_ARR_SZ) + " пробуждений:"+ "\n" + mess + "\n" + CharPlot<COLON_X2>(arr, N_ARR_SZ, n, 0, 1), CHAT_ID);     // отправляем боту массив и гистограмму последних N_ARR_SZ измерений
  }
}

//ОБРАБОТЧИК АППАРТНОГО ПРЕРЫВАНИЯ
IRAM_ATTR void myIsr() {
  if(millis() - TimerOn >= 200){             // чтобы учитывать именно нажатия, а не дребезг
    clckd++;                                 // увеличиваем счётчик нажатий
    if(clckd >= 2){                          // если было 2 и более  нажатий
      flag_att = 1;                          // поднимаем флажок нажатия кнопки
    }
    TimerOn = millis();                      // отсчёт начала учёта времени антидребезга
  }
}

//ФУНКЦИЯ ПЕЧАТИ НА СЕМИСЕГМЕНТНИК
void disp_print_ves(float ves_kilo, uint16_t t_display) {
  disp.power(true);                     // вкл. дисплей
  disp.printRight(true);                // печатать справа
  disp.setCursorEnd();                  // курсор в конец
  disp.clear();                         // очитить
  disp.print(ves_kilo);                 // передаем то, что будем печатать
  disp.update();                        // печатаем
  uint32_t tmr_print = millis();        // отсчёт времени показа на дисплее
  while (millis() - tmr_print < t_display) {
    yield();
    disp.tick();
  }
  disp.clear();
  disp.update();
  disp.power(false);        // выкл. дисплей
}

//ПОЛУЧЕНИЕ ВЕСА ГРУЗА В КГ С МОДУЛЯ ВЕСОВ
float hx_kg() {
  int32_t ves_gruz;                        // сырой вес груза
  float ves_gr;                           // вес груза в граммах
  for(byte i=0; i<16; i++){               // произведем 16 измерений груза
    while (!hx.available()) {
      yield();
    }
    ves_gruz = expRAA((float)hx.read());            // фильтруем значение
  }
  float ves_klg;                                    // возвращаемый вес
  ves_gr = round((ves_gruz - ves_pus) / k);
  if (ves_gr < 100000)                              // если вес менее 100 кг, точность два знака после запятой
    ves_klg = (round(ves_gr / 10.0)) / 100.0;
  else
    ves_klg = (round(ves_gr / 100.0)) / 10.0;       // >= 100, один знак
  return ves_klg;
}

// ФИЛЬТР ВЕСА
float expRAA(float newVal) {
  static float filVal = 0;
  float k;                  // резкость фильтра зависит от модуля разности значений
  if (abs(newVal - filVal) > 100) k = 0.95;
  else k = 0.03;
  filVal += (newVal - filVal) * k;
  return filVal;
}

// запись времен в файл времени
void time_fix(uint32_t t, int32_t ct){
  File file_time = LittleFS.open("/time.txt", "w");  // открываем файл для записи
  if(file_time){
    String s_time = String(t) + " " + String(ct);
    file_time.print(s_time);
  }
  file_time.close();
}

// ОБРАБОТЧИК СООБЩЕНИЙ
void newMsg(FB_msg& msg) {
  FB_Time t(msg.unix, GMT);           // получаем unix-время с учётом часового пояса
}

// добавления результата в файл с результатами
void add_result(float val, uint32_t n_time){
  File file_add = LittleFS.open("/result.txt", "a");  // открываем файл для добавления данных
  if(file_add){
    String s = String(val) + " " + String(n_time).substring(2) + "\n" ;         // первые две цифры времени убираем, они постоянны, экономия памяти
    file_add.print(s);
    delay(2);
   }
   file_add.close();
}

// отправки результатов измерения
void send_result(){
  File file_send = LittleFS.open("/result.txt", "r");  // открываем файл для чтения
  if(file_send){
    bot.sendFile(file_send, FB_DOC, "result.txt", CHAT_ID);  // отправляем чат-боту файл
    delay(30);
  }
  file_send.close();
}