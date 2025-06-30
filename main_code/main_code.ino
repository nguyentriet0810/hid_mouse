#include <Wire.h>
#include <OneButton.h>
#include <MPU6050_tockn.h>
#include <BleConnectionStatus.h>
#include <BleMouse.h>

#define LED1 12
#define LED2 13
#define LED3 14
#define LED4 21

#define NUTTRAI 10
#define NUTPHAI 11

#define MOUSE_LEFT 1
#define MOUSE_RIGHT 2
#define MOUSE_MIDDLE 4
#define MOUSE_BACK 8
#define MOUSE_FORWARD 16

BleMouse bleMouse("MOUSE", "HID cua triet", 99);

// Setup a new OneButton on pin PIN_INPUT2.
OneButton button1(NUTTRAI, true);
// Setup a new OneButton on pin PIN_INPUT2.
OneButton button2(NUTPHAI, true);

MPU6050 mpu6050(Wire);
int value;

long timer = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(5, 4);
  mpu6050.begin();
  mpu6050.calcGyroOffsets(true);
  Serial.println("");

  Serial.println("Starting LED...");
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(LED4, OUTPUT);
  digitalWrite(LED1, HIGH);
  digitalWrite(LED2, HIGH);
  digitalWrite(LED3, HIGH);
  digitalWrite(LED4, HIGH);
  Serial.println("Done LED...");

  Serial.println("Starting NUTTRAI...");
  // link the button 1 functions.
  button1.attachClick(click1);
  button1.attachDoubleClick(doubleclick1);
  button1.attachLongPressStart(longPressStart1);
  button1.attachLongPressStop(longPressStop1);
  button1.attachDuringLongPress(longPress1);
  Serial.println("Done NUTTRAI...");

  Serial.println("Starting NUTPHAI...");
  // link the button 2 functions.
  button2.attachClick(click2);
  button2.attachDoubleClick(doubleclick2);
  button2.attachLongPressStart(longPressStart2);
  button2.attachLongPressStop(longPressStop2);
  button2.attachDuringLongPress(longPress2);
  Serial.println("Done NUTPHAI...");

  bleMouse.begin();  // Khởi động BLE HID (chuột)
  Serial.println("Đã khởi động BLE Mouse HID");
}

void loop() {
  float x, y, z;
  button1.tick();
  button2.tick();

  if (bleMouse.isConnected()) {
    digitalWrite(LED1, LOW);
    mpu6050.update();

    if (millis() - timer > 15) {
      if ((mpu6050.getAngleX() >= 6) || (mpu6050.getAngleX() <= -6)) {
        y = mpu6050.getAngleX();
      } else {
        y = 0;
      }
      if (mpu6050.getAngleY() > 7 || (mpu6050.getAngleY() <= -7)) {
        x = -mpu6050.getAngleY();
      } else {
        x = 0;
      }
      if (mpu6050.getAngleZ() > 5 || (mpu6050.getAngleZ() <= -5)) {
        z = mpu6050.getAngleZ();
      } else {
        z = 0;
      }

      // Chuyển đổi thành tốc độ chuột (giá trị tùy chỉnh)
      int deltaX = (int)(x / 4.0);
      int deltaY = (int)(y / 4.0);

      // Giới hạn phạm vi để tránh di chuyển quá nhanh
      deltaX = constrain(deltaX, -13, 13);
      deltaY = constrain(deltaY, -13, 13);

      // Gửi hành vi chuột
      bleMouse.move(deltaX, deltaY);
      if (value == 1) {
        bleMouse.click(MOUSE_LEFT);
        value = 0;
      } else if (value == 2) {
        bleMouse.click(MOUSE_LEFT);
        delay(50);
        bleMouse.click(MOUSE_LEFT);
        value = 0;
      } else if (value == 3) {
        bleMouse.click(MOUSE_RIGHT);
        value = 0;
      } else if (value == 4) {
        bleMouse.press(MOUSE_LEFT);
      } else if (value == 5) {
        bleMouse.release(MOUSE_LEFT);
        value = 0;
      } else if (value == 6) {
        bleMouse.move(0, 0, -1, 0);
        value = 0;
      } else if (value == 7) {
        bleMouse.move(0, 0, 1, 0);
        value = 0;
      } else {
        value = 0;
      }
      Serial.printf("Move Mouse X: %d Y: %d\n", deltaX, deltaY);
      timer = millis();
    }
  } else {
    digitalWrite(LED1, HIGH);
  }
}

// This function will be called when the button1 was pressed 1 time (and no 2. button press followed).
void click1() {
  Serial.println("Mouse left 1 click.");
  value = 1;
}  // click1


// This function will be called when the button1 was pressed 2 times in a short timeframe.
void doubleclick1() {
  Serial.println("Mouse left doubleclick.");
  value = 2;
}  // doubleclick1


// This function will be called once, when the button1 is pressed for a long time.
void longPressStart1() {
  Serial.println("Mouse left longPress start");
  value = 4;
}  // longPressStart1


// This function will be called often, while the button1 is pressed for a long time.
void longPress1() {
  Serial.println("Mouse left longPress...");
}  // longPress1


// This function will be called once, when the button1 is released after beeing pressed for a long time.
void longPressStop1() {
  Serial.println("Mouse left longPress stop");
  value = 5;
}  // longPressStop1


// ... and the same for button 2:

void click2() {
  Serial.println("Mouse right 1 click.");
  value = 3;
}  // click2


void doubleclick2() {
  Serial.println("Button 2 doubleclick.");
  value = 6;
}  // doubleclick2


void longPressStart2() {
  Serial.println("Button 2 longPress start");
}  // longPressStart2


void longPress2() {
  Serial.println("Button 2 longPress...");
}  // longPress2

void longPressStop2() {
  Serial.println("Button 2 longPress stop");
  value = 7;
}  // longPressStop2
