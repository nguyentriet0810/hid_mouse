#include <Wire.h>
#include <MPU6050_tockn.h>
#include <BleConnectionStatus.h>
#include <BleMouse.h>

MPU6050 mpu6050(Wire);
BleMouse bleMouse("ESP32 Mouse", "Triet HID", 100);

long timer = 0;

Button button(0);

void setup() {
  Serial.begin(115200);
  Wire.begin(5, 4);  // SDA = GPIO5, SCL = GPIO4

  mpu6050.begin();
  mpu6050.calcGyroOffsets(true);

  bleMouse.begin();  // Khởi động BLE HID (chuột)
  Serial.println("Đã khởi động BLE Mouse HID");
}

void loop() {
  mpu6050.update();

  if (bleMouse.isConnected()) {
    if (millis() - timer > 80) {
      float x = mpu6050.getAngleX();
      float y = mpu6050.getAngleY();

      // Chuyển đổi thành tốc độ chuột (giá trị tùy chỉnh)
      int deltaX = (int)(x / 2.0);
      int deltaY = (int)(y / 2.0);  

      // Giới hạn phạm vi để tránh di chuyển quá nhanh
      deltaX = constrain(deltaX, -10, 10);
      deltaY = constrain(deltaY, -10, 10);

      // Gửi hành vi chuột
      bleMouse.move(deltaX, deltaY);

      Serial.printf("Move Mouse X: %d Y: %d\n", deltaX, deltaY);
      timer = millis();
    }
  }
}

class Button{
private:
  OneButton button;
  int value;
public:
  explicit Button(uint8_t pin):button(pin) {
    button.attachClick([](void *scope) { ((Button *) scope)->Clicked();}, this);
    button.attachDoubleClick([](void *scope) { ((Button *) scope)->DoubleClicked();}, this);
    button.attachLongPressStart([](void *scope) { ((Button *) scope)->LongPressed();}, this);
  }

  void Clicked(){
    //Serial.println("Click then value++");
    tt = 1;
    //Serial.println(tt);
  }

  void DoubleClicked(){
    //Serial.println("DoubleClick");
    tt = 2;
    //Serial.println(tt);
  }

  void LongPressed(){
    //Serial.println("LongPress and the value is");
    tt = 3;
    //Serial.println(tt);
  }

  void handle(){
    button.tick();
  }
};
