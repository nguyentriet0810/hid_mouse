#include <Wire.h>
#include <OneButton.h>
#include <MPU6050_tockn.h>
#include <BleConnectionStatus.h>
#include <BleMouse.h>

#define NUTNHAN 10

#define MOUSE_LEFT 1
#define MOUSE_RIGHT 2
#define MOUSE_MIDDLE 4
#define MOUSE_BACK 8
#define MOUSE_FORWARD 16

BleMouse bleMouse("ESP32 Mouse", "Triet HID", 100);

MPU6050 mpu6050(Wire);
int value;

class Button {
private:
  OneButton button;
public:
  explicit Button(uint8_t pin)
    : button(pin) {
    button.attachClick([](void *scope) {
      ((Button *)scope)->Clicked();
    },
                       this);
    button.attachDoubleClick([](void *scope) {
      ((Button *)scope)->DoubleClicked();
    },
                             this);
    button.attachLongPressStart([](void *scope) {
      ((Button *)scope)->LongPressed();
    },
                                this);
  }

  void Clicked() {
    Serial.println("Click");
    value = 1;
  }

  void DoubleClicked() {
    Serial.println("DoubleClick");
    value = 2;
  }

  void LongPressed() {
    Serial.println("Long click");
    value = 3;
  }

  void handle() {
    button.tick();
  }
};

Button button(NUTNHAN);

long timer = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(5, 4);

  mpu6050.begin();
  mpu6050.calcGyroOffsets(true);

  bleMouse.begin();  // Khởi động BLE HID (chuột)
  Serial.println("Đã khởi động BLE Mouse HID");
}

void loop() {
  float x, y, z;
  if (bleMouse.isConnected()) {
    //if (millis() - timer > 80) {
    mpu6050.update();
    button.handle();

    if ((mpu6050.getAngleX() >= 5) || (mpu6050.getAngleX() <= -5)) {
      x = mpu6050.getAngleX();
    } else {
      x = 0;
    }
    if (mpu6050.getAngleY() > 5 || (mpu6050.getAngleY() <= -5)) {
      y = mpu6050.getAngleY();
    } else {
      y = 0;
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
    } else {
      value = 0;
    }
    Serial.printf("Move Mouse X: %d Y: %d\n", deltaX, deltaY);
    //timer = millis();
  }
}
