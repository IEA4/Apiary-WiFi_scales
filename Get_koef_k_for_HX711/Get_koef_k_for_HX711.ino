/*
 * Код для получения коэффицента перевода сырого веса с модуля весов HX711 в граммы
 */

#define gruz 4579.0  // известная масса груза в граммах
#define N 5        // количество измерений веса груза известной массы

#define DT_PIN D2         // dataPin hx711
#define SCK_PIN D1        //clockPin hx711

#include <GyverHX711.h>
GyverHX711 sensor(DT_PIN, SCK_PIN, HX_GAIN64_A); // HX_GAIN128_A - канал А усиление 128;    HX_GAIN32_B - канал B усиление 32; HX_GAIN64_A - канал А усиление 64

void setup() {
  Serial.begin(115200);
  int32_t ves_pus, ves_gruz;      // Сырой вес без груза и с грузом
  float k;                        // тут храним усреденный по N коэф перевода сырых значений в граммы
  float arr[N];                   // массив для хранения k
  float a;                        // сумма всех k
  String stroka = "Нагрузите весом " + String(gruz) + " на весы";
  for(byte i=0; i<N; i++){
    yield();
    Serial.println("Измерение " + String(i+1) + "/" + String(N));
    ves_pus = Wes();                          // получаем сырой вес пустых весов
    Serial.print("Сырой вес без груза: ");
    Serial.println(ves_pus);
    Serial.println(stroka);
    t_waiting();                              // 10 секунд чтобы успеть нагрузить
    Serial.println("Измеряю...");
    ves_gruz = Wes();                         // получаем вес нагрузки
    Serial.print("Сырой вес груза: ");
    Serial.println(ves_gruz);
    arr[i] = ((ves_gruz - ves_pus)) / gruz;    // считаем i-тый коэф перевода
    Serial.println(arr[i], 3);
    Serial.println("Уберите груз");
    t_waiting();                               // ждём 10 секунд
  }
  for(byte j=0; j<N; j++){
    a += arr[j];
  }
  Serial.print("Коэффициент перевода показаний весов в граммы, k = ");
  k = a/N;                                    // усредняем по N значений
  Serial.println(k, 3);
}

void loop() {
}

uint32_t Wes(){
  uint32_t hx;
  uint32_t t = millis();
  while (millis() - t < 1000) {
    while(!sensor.available()){
      yield();
    }
    hx = sensor.read();;
    yield();
  }
  return hx;
}

void t_waiting(){
  uint32_t t_mes = millis();
  while (millis() - t_mes < 10000) {
    yield();
  }
}
