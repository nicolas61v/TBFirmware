/* ======================================== Including the libraries. */
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "quirc.h"
#include "Arduino.h"
#include "SD.h"
#include "Audio.h"
#include "SPI.h"
#include "driver/i2s.h" // Nuevo

#define SD_CS 21 // GPIO0 (D8) --- Nuevo

// Audio -- Nuevo
#define MAX98357A_I2S_DOUT 4
#define MAX98357A_I2S_BCLK 5
#define MAX98357A_I2S_LRC  6
Audio audio;

/* ======================================== */

// I2S Pins for Microphone -- Nuevo
#define I2S_WS 15  // Word Select pin (LRC)
#define I2S_SD 14  // Serial Data pin (DOUT)
#define I2S_SCK 13 // Serial Clock pin (BCLK)

// I2S Configuration for Microphone -- Nuevo
i2s_config_t i2s_config = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
  .sample_rate = 16000,
  .bits_per_sample = i2s_bits_per_sample_t(I2S_BITS_PER_SAMPLE_16BIT),
  .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 8,
  .dma_buf_len = 64,
  .use_apll = false,
  .tx_desc_auto_clear = false,
  .fixed_mclk = 0
};

i2s_pin_config_t pin_config = {
  .bck_io_num = I2S_SCK,
  .ws_io_num = I2S_WS,
  .data_out_num = I2S_PIN_NO_CHANGE,
  .data_in_num = I2S_SD
};

// creating a task handle
TaskHandle_t QRCodeReader_Task;

/* ======================================== Select camera model */

/* ======================================== */

/* ======================================== GPIO of camera models */

#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 10
#define SIOD_GPIO_NUM 40
#define SIOC_GPIO_NUM 39

#define Y9_GPIO_NUM 48
#define Y8_GPIO_NUM 11
#define Y7_GPIO_NUM 12
#define Y6_GPIO_NUM 14
#define Y5_GPIO_NUM 16
#define Y4_GPIO_NUM 18
#define Y3_GPIO_NUM 17
#define Y2_GPIO_NUM 15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM 47
#define PCLK_GPIO_NUM 13

/* ======================================== */

/* ======================================== Variables declaration */
struct QRCodeData {
  bool valid;
  int dataType;
  uint8_t payload[1024];
  int payloadLen;
};

struct quirc *q = NULL;
uint8_t *image = NULL;
camera_fb_t * fb = NULL;
struct quirc_code code;
struct quirc_data data;
quirc_decode_error_t err;
struct QRCodeData qrCodeData;
String QRCodeResult = "";
/* ======================================== */

/* __________ VOID SETUP() */
void setup() {
  // Disable brownout detector.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  /* ---------------------------------------- Init serial communication speed (baud rate). */
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  /* ---------------------------------------- */

  if (!SD.begin(SD_CS)) { // Nuevo
    Serial.println("Error inicializando la tarjeta SD!");
    return;
  } else {
    Serial.println("Tarjeta SD inicializada correctamente."); // Nuevo
  }

  audio.setPinout(MAX98357A_I2S_BCLK, MAX98357A_I2S_LRC, MAX98357A_I2S_DOUT); // Nuevo
  audio.setVolume(100);

  /* ---------------------------------------- Camera configuration. */
  Serial.println("Start configuring and initializing the camera...");
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 15;
  config.fb_count = 1;
  
  #if defined(CAMERA_MODEL_ESP_EYE)
    pinMode(13, INPUT_PULLUP);
    pinMode(14, INPUT_PULLUP);
  #endif

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    ESP.restart();
  }
  
  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QVGA);
  
  Serial.println("Configure and initialize the camera successfully.");
  Serial.println();
  /* ---------------------------------------- */

  // Inicializar el I2S para el micrófono -- Nuevo
  Serial.println("Inicializando I2S...");
  err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Error instalando el driver I2S: %d\n", err);
    while (true);
  }

  err = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Error configurando los pines I2S: %d\n", err);
    while (true);
  }
  Serial.println("I2S inicializado correctamente.");

  /* ---------------------------------------- create "QRCodeReader_Task" using the xTaskCreatePinnedToCore() function */
  xTaskCreatePinnedToCore(
             QRCodeReader,          /* Task function. */
             "QRCodeReader_Task",   /* name of task. */
             10000,                 /* Stack size of task */
             NULL,                  /* parameter of the task */
             1,                     /* priority of the task */
             &QRCodeReader_Task,    /* Task handle to keep track of created task */
             0);                    /* pin task to core 0 */
  Serial.println("QRCodeReader_Task created successfully."); // Nuevo
  /* ---------------------------------------- */
}
/* __________ */
void loop() {
  // put your main code here, to run repeatedly:
  delay(1);
}
/* __________ */

/* __________ The function to be executed by "QRCodeReader_Task" */
// This function is to instruct the camera to take or capture a QR Code image, then it is processed and translated into text.
void QRCodeReader( void * pvParameters ){
  /* ---------------------------------------- */
  Serial.println("QRCodeReader is ready.");
  Serial.print("QRCodeReader running on core ");
  Serial.println(xPortGetCoreID());
  Serial.println();
  /* ---------------------------------------- */

  /* ---------------------------------------- Loop to read QR Code in real time. */
  while(1){
      q = quirc_new();
      if (q == NULL){
        Serial.print("can't create quirc object\r\n");  
        continue;
      }
    
      fb = esp_camera_fb_get();
      if (!fb)
      {
        Serial.println("Camera capture failed");
        continue;
      }   
      
      Serial.println("Camera capture succeeded."); // Nuevo
      
      quirc_resize(q, fb->width, fb->height);
      image = quirc_begin(q, NULL, NULL);
      memcpy(image, fb->buf, fb->len);
      quirc_end(q);
      
      int count = quirc_count(q);
      if (count > 0) {
        quirc_extract(q, 0, &code);
        err = quirc_decode(&code, &data);
    
        if (err){
          Serial.println("Decoding FAILED");
          QRCodeResult = "Decoding FAILED";
        } else {
          Serial.printf("Decoding successful:\n");
          dumpData(&data);
        } 
        Serial.println();
      } else {
        Serial.println("No QR code detected."); // Nuevo
      }
      
      esp_camera_fb_return(fb);
      fb = NULL;
      image = NULL;  
      quirc_destroy(q);
  }
  /* ---------------------------------------- */
}
/* __________ */

/* __________ Function to display the results of reading the QR Code on the serial monitor. */
void dumpData(const struct quirc_data *data)
{
  Serial.printf("Version: %d\n", data->version);
  Serial.printf("ECC level: %c\n", "MLHQ"[data->ecc_level]);
  Serial.printf("Mask: %d\n", data->mask);
  Serial.printf("Length: %d\n", data->payload_len);
  Serial.printf("Payload: %s\n", data->payload);
  
  QRCodeResult = (const char *)data->payload;

  // Verificar si el texto del QR es "prender" o "apagar"
  if (strcmp(QRCodeResult.c_str(), "encender") == 0) {
    if (audio.connecttoFS(SD, "/sound2.wav")) { //Nuevo
      Serial.println("Reproduciendo Audio");
    } else {
      Serial.println("Error al reproducir Audio");
    }

    while (audio.isRunning()) { // Nuevo
      audio.loop();
    }
    Serial.println("Encendiendo el pin 12");
    digitalWrite(12, HIGH); // Asumimos que el pin 12 está configurado correctamente
  } else if (strcmp(QRCodeResult.c_str(), "apagar") == 0) {
    if (audio.connecttoFS(SD, "/sound1.wav")) { //Nuevo
      Serial.println("Reproduciendo Audio");
    } else {
      Serial.println("Error al reproducir Audio");
    }

    while (audio.isRunning()) { // Nuevo
      audio.loop();
    }
    Serial.println("Apagando el pin 12");
    digitalWrite(12, LOW); // Asumimos que el pin 12 está configurado correctamente
  } else if (strcmp(QRCodeResult.c_str(), "api") == 0) { // Nuevo
    // Grabación de audio por 10 segundos
    const char *audio_filename = "/recorded_audio.wav";
    File audioFile = SD.open(audio_filename, FILE_WRITE);
    if (!audioFile) {
      Serial.println("Error abriendo el archivo para grabación.");
      return;
    }

    Serial.println("Grabando audio por 10 segundos...");
    int16_t i2s_buffer[1024];
    size_t bytes_read;
    uint32_t start_time = millis();
    while (millis() - start_time < 10000) { // Grabar por 10 segundos
      i2s_read(I2S_NUM_0, (void*) i2s_buffer, sizeof(i2s_buffer), &bytes_read, portMAX_DELAY);
      audioFile.write((uint8_t*) i2s_buffer, bytes_read);
    }
    audioFile.close();
    Serial.println("Grabación completada.");
  }
}
