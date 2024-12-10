/*
В НАСТРОЙКАХ ПЛАТЫ ВЫБРАТЬ Flash Size: 4MB (FS:2MB OTA:~1019KB), Erase Flash: All Flash Contents

ДЛИТЕЛЬНОСТЬ СНА НЕ БОЛЕЕ 2 ЧАСОВ. МАКСИМАЛЬНО ВОЗМОЖНАЯ ДЛИТЕЛЬНОСТЬ СНА ДЛЯ ESP8266 НЕМНОГИМ
БОЛЬШЕ ЭТОГО ВРЕМЕНИ, И ОНО НЕ ПОСТОЯННО

НЕОБХОДИМО ПРОПИСАТЬ СВОИ НАСТРОЙКИ WI-FI И ТЕЛЕГРАМ-БОТА

*/

#define DT_PIN D2         // dataPin hx711
#define SCK_PIN D1        // clockPin hx711

#define DIO_PIN D5        // 74HC595
#define SCLK_PIN D7
#define RCLK_PIN D6

#define BTN_PIN D3        // кнопка тарирования
#define ONF_PIN D8        // пин на светодиод индикатор-включения

#define AKB_PIN A0 // для измерения напряжения с аккумулятора

#define SLEEP_DURATION 7200   // длительность сна в секундах (примерно столько будет находится ESP8266 в режиме глубоко сна)// максимум 7200
#define TIMOUT_DISPLAY 4000   // длительность отображения значения на семисегментном дисплее, в мс

#define hour_add_beg 22       // после 22 часов и до 4 считаем ночь ...(усл-ие по времени возм-но. придется менять, если зн-ия будут другие)
#define hour_add_end 4        // ... это нужно, чтобы добавить в файл с рез-ми итоговый рез-т в период, когда он более-менее стабильный

#define GMT 3                // часовой пояс

#define N_ARR_SZ 24          // размер массива измерений (гистограмма из стольких измерений). Желательно чётное число для корректного отображения на гистограмме
#define N_TO_SEND 6          // кол-во дней, период отправки файла с результатами (6 -- 1 неделя)

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
int32_t delta_t;                        // точность сна (ESP спит с погрешностью; переменная нужна для коррекции этого)
uint16_t cnt;                           // счётчик числа пробуждений

volatile boolean flag_att = 0;          // флаг нажатия кнопки для тарирования и сброса всех данных
volatile uint32_t TimerOn;              // счётчик-антидребезг для кнопки
volatile byte clckd = 0;                // счётчик кликов для тарирования и перезапуска

void setup() {
  hx.sleepMode(false);                  // будим модуль весов

  pinMode(BTN_PIN, INPUT_PULLUP);       // одним концом кнопка на пине, другим на GND
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), myIsr, FALLING); // аппартное прерывание на кнопке

  pinMode(ONF_PIN, OUTPUT);             // сигнализируем активность включением сигнального светодиода
  digitalWrite(ONF_PIN, HIGH);          // (на esp8266 включается низким сигналом)

  LittleFS.begin();                     // подключаем файловую систему

  conFile.begin();                      // подкл. чтение из файла

  tmr_to_sleep = millis();               // чтобы отсечь шумы измерений, пропускаем начальные данные (исп. tmr_to_sleep потому что это глобальный таймер)
  while(millis() - tmr_to_sleep < 1000){          // холостое считывание веса в течение секунды
    while(!hx.available()) {
      yield();
    }
    int32_t hol_ves = hx.read();
  }

  if(!conFile.contains("key_cnt")){     // проверяем какой по счёту запуск, если первый, то файл чистый и потому не содержит никаких ключей
    cnt = 0;                            // первый запуск
    conFile.set("key_cnt", cnt);        // создаём ключ и записали в файл
    while (!hx.available()) {           // ждём отправки данных с hx711
      yield();
    }
    ves_pus = hx.read();                        // читаем в переменную
    conFile.set("key_ves_pus", ves_pus);        // создаём ключ и записываем в него
    delta_t = (int32_t)(0.075*SLEEP_DURATION);        // высчитываем коэф поправки на точность сна (для 7200 секунд это 9 минут)
    conFile.set("key_delta_t", delta_t);
    conFile.set("key_flag_add", 0);             // сохраняем значение флага, разрешающего запись в файл с результатами
    conFile.set("key_send", 0);                 //    ...... кол-во добавлений в файл
    File file_times = LittleFS.open("/time.txt", "w");    // СОЗДАЁМ ФАЙЛ ДЛЯ ХРАНЕНИЯ ЗНАЧЕНИЙ ВРЕМЕНИ
    file_times.print("0 " + String(delta_t));                        // записываем: end_time = 0; cor_time = delta_t;
    file_times.close();
  }
  else {
    cnt = conFile.get("key_cnt");               // переписываем в переменную данные из файла по ключу
    cnt++;
    conFile.set("key_cnt", cnt);
    ves_pus = conFile.get("key_ves_pus");
  }
  conFile.update();                   // зафиксировать изменения в файле
  delay(500);                         // физ.задержка, чтобы всё успело записаться

  ves_kg = hx_kg();                   // результат измерения записываем в переменную

  disp_print_ves(ves_kg, TIMOUT_DISPLAY);  // выводим результат измерения на дисплей

  if (flag_att) {                          // если была нажата кнопка тарирования/сброса
    if(clckd >= 2 && clckd < 5){           // от 2 до 4 раз
      while (!hx.available()) {
        yield();
      }
      ves_pus = hx.read();                      // производим тарирование
      conFile.set("key_ves_pus", ves_pus);      // запоминаем тарированный сырой вес
      conFile.update();
      delay(5);
      ves_kg = hx_kg();
      disp_print_ves(ves_kg, TIMOUT_DISPLAY >> 1);              // отображение на дисплее в два раза меньшее время
    }
    else if(clckd == 5){                        // сброс всех данных и перезапуск
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

  String key_res = String(cnt % N_ARR_SZ);  // определеям числовой ключ для записи измерения в буфер для текущего акта
  conFile.set(key_res, ves_kg);
  conFile.update();                         // фиксируем измерение

  File file_time = LittleFS.open("/time.txt", "r");     // открываем файл времени для чтения
  if(file_time){
    String t = "";                                      // храним данные с файла времени
    while (file_time.available()) {                     // посимвольно читаем
      t += (char)(file_time.read());
      delay(2);                                         // физическая задержка
    }
    String s_end_time = t.substring(0,t.indexOf(" "));          // парсим в строковую переменную время последнего запуска
    end_time = s_end_time.toInt();                              // преобразуем его в целочисленное число
    String s_cor_time = t.substring(t.indexOf(" ") + 1);        //  .... ... .... поправку длительности сна
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

  hx.sleepMode(true);                   // переводим модуль весов в режим энергосбережения

  wifiSupport();                        // подкл. к  wi-fi. Если не удастся, уходим в сон на SLEEP_DURATION

  float voltage;                        // сюда будем записывать напряжение аккумулятора
  for (byte k = 0; k < 10; k++) {
    yield();
    voltage = ((float)analogRead(AKB_PIN)) * kv;  // в Вольтах
  }

  bot.attach(newMsg);                   // подключаем обработчик сообщений
  bot.sendMessage("U = " + String(voltage) + " В", CHAT_ID);    // отправляем сообщение с напряжением , этим определим время

  tmr_to_sleep = millis();                      // глобальный таймер, поэтому используем
  while (millis() - tmr_to_sleep < 2000) {      // ждём 2 секунды, чтобы отправить следующее сообщение
    yield();
  }

  String mess = form_message(cnt, N_ARR_SZ);                       // формируем сообщение из последовательностти последних измеренных масс
  bot.sendMessage("Результаты измерений:\n" + mess , CHAT_ID);     // отправляем боту массив и гистограмму последних N_ARR_SZ измерений

  tmr_to_sleep = millis();      // отсчёт времени для перехода в режим сна
}

void loop() {
  bot.tick();

  static boolean flag_early_wake = 0;           // раннее пробуждение
  static boolean flag_to_sleep = 0;             // флажок разрешения перехода в спящий режим
  static uint32_t sleep_time = SLEEP_DURATION;  // длительность сна
  static uint32_t tmr_bot = 0;                      // отсчёт попыток получения времени
  static boolean flag_time = 0;                 // флажок получения времени
  static boolean flag_add = 0;                  // флажок разрешения добавления результата и времни в файл
  static byte count_send = 0;                   // количество добавлений результатов в файл

  if(millis() - tmr_to_sleep > 60000){          // если после включения прошло более минуты
    flag_to_sleep = 1;                          // разрешаем перейти в сон
    sleep_time += cor_time;                     //      .... на SLEEP_DURATION+cor_time секунд, чтоб снова попытаться получить время
    end_time = 0;                               // зануляем время последней активности
    time_fix(end_time, cor_time);               // записываем в файл
  }

  if (millis() - tmr_bot >= 3000) {             // через каждые 3 секунды проверяем получено ли время
    if(bot.getUnix() > 1700000000)              // больше 170млн секунд уже прошло с 1970 года
      flag_time = 1;
    tmr_bot = millis();
  }

  if(flag_time){                                // если стало известно текущее время
    if(end_time != 0) {                                 // и это не первое включение(активность) // при первом включении в файле времени end_time = 0
      delta_t = conFile.get("key_delta_t");                       // получаем время поправки
      int32_t wake_time = (int32_t)end_time + SLEEP_DURATION + cor_time;   // таким должно быть время пробуждения
      int32_t now_time = (int32_t)bot.getUnix();                              // текущее время
      if(wake_time - now_time >=  delta_t){     // оцениваем стоит ли ещё спать
        flag_early_wake = 1;                            // отмечаем раннее пробуждение, чтоб не фиксировать времена
        sleep_time = wake_time - now_time;              // спим ещё столько
      }
      else {
        uint32_t start_time = bot.getUnix() - (uint32_t)(millis()/1000);    // время пробуждения
        cor_time += SLEEP_DURATION - (int32_t)(start_time - end_time);      // корректируем поправку
        if(abs(cor_time) > SLEEP_DURATION >> 1){     // для случая если было забыто замкнуть GPIO16 и RST (>> 1 означает деление на 2)
          cor_time = 0;
        }
       }
    }
    if(flag_early_wake == 0){                       // если пробуждение не раннее
      sleep_time += cor_time;                       // длительность сна с учетом поправки
      flag_add = conFile.get("key_flag_add");
      FB_Time t = bot.getTime(GMT);                 // получаем структуру с полями времени: часы, минуты, секунды и т.д.
      if(t.hour > hour_add_beg || t.hour < hour_add_end) {       // проверяем разрешено ли по времени
        if(flag_add == 0){                          // не было ли уже добавления в файл
          end_time = bot.getUnix();
          add_result(ves_kg, end_time);             // добавляем результат
          conFile.set("key_flag_add", 1);           // запрещаем еще одно добавление в этом же разрешенном интервале времени
          count_send = conFile.get("key_send");
          count_send++;
          conFile.set("key_send", count_send);
          conFile.update();
          delay(500);
        }
      }
      else {                                        // как только произойдёт выход за интервал записи, считаем, что настал новый день
        if(flag_add == 1)                           // ... на исходе которого разрешаем запись в файл
          conFile.set("key_flag_add", 0);           // убираем запрет на добавление
          conFile.update();
          delay(500);
      }

      if(count_send >= N_TO_SEND){                  // если количество активностей больше того числа, после которого нужно отправить файл
        delay(0);                                   // здесь вызывается обработчик wi-fi, особенность работы
        yield();
        send_result();                              // отправляем результат, если файл большой, то довольно долгая отправка
        delay(0);                                   // здесь вызывается обработчик wi-fi, особенность работы
        yield();
        conFile.set("key_send", 0);
        conFile.update();
        delay(500);
      }
      end_time = bot.getUnix();                     // берем текущее время
      time_fix(end_time, cor_time);                 // фиксируем в файл
    }
    flag_to_sleep = 1;                              // разрешаем переход в режим сна
  }

  if(flag_to_sleep){                                 // если разрешён переход в режим сна, и нет необходимости ничего отправлять
    digitalWrite(ONF_PIN, LOW);                      // выключаем светодиод-индикатор
    delay(5);
    ESP.deepSleep(sleep_time*1E6);                   // уходим в сон (в микросекундах)
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
      digitalWrite(ONF_PIN, LOW);                // выключаем светодиод-индикатор
      delay(5);
      ESP.deepSleep((SLEEP_DURATION+cor_time)*1E6);                  // уходим в сон (в микросекундах)
    }
  }
}

// ЗАПИСЬ (макс. N_ARR_SZ измерений) РЕЗУЛЬТАТОВ ПРИ КАЖДОМ ПРОБУЖДЕНИИ В ФАЙЛ И ОТПРАВКА БОТУ
String form_message(uint16_t num_act, byte arr_sz){
  String message = "";                      // сюда будем собирать сообщение для отправки боту;
  if (num_act < arr_sz) {                     // если число пробуждений < arr_sz
    float arr[num_act+1];                       // создаем массив размером на 1 больше значения счётчика числа пробуждений
    for(uint16_t i=0; i<num_act+1; i++){
      arr[i] = conFile.get(String(i));      // элементы массива берем из файла по числовому ключу
      message += String(arr[i]) + " ";         // собираем в одно сообщение всю имеющуюся последовательность измерений
    }
  }
  else {                                    // если число пробуждений >= arr_sz
    float arr[arr_sz];
    String key_ves_kg;                      // ключ для веса в кг
    for (byte i = 0; i < arr_sz; i++) {
      if (num_act % arr_sz + 1 + i < arr_sz)
        key_ves_kg = String(num_act % arr_sz + 1 + i);
      else
        key_ves_kg = String(num_act % arr_sz + 1 + i - arr_sz);
      arr[i] = conFile.get(key_ves_kg);
      message += String(arr[i]) + " ";         // собираем в одно сообщение всю имеющуюся последовательность измерений
    }
    bool flag_char_plot = 0;                // флажок разрешения на построение гистограммы
    for(byte m=0; m < arr_sz; m++){
      if(round(arr[m]*10) != 0) {           // проверяем состав массива: если полностью из нулей с точностью до сотой, то не будем строить гистограмму
        flag_char_plot = 1;
        break;
      }
    }
    if(flag_char_plot){
      byte n = 0;                                 // счётчик числа различных значений
      byte l;
      for (byte m = 0; m < arr_sz; m++) {
        for(l = 0; l < m; l++){
          if(round(arr[m]*10) == round(arr[l]*10))   break; // если совпадают, то не считаем
        }
        if(m == l)  n++;
      }
      message += "\n" + CharPlot<COLON_X2>(arr, arr_sz, n, 0, 1);
    }
  }
  return message;
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
  int32_t ves_gruz;                       // сырой вес груза
  float ves_gr;                           // вес груза в граммах
  for(byte i=0; i<32; i++){               // произведем 32 измерения груза
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
  else k = 0.02;
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
  if (msg.OTA && msg.chatID == CHAT_ID) bot.update();   // обновляем по воздуху, если получен бинарный файл от сверяемого ID
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