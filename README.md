# 🚀 BLE HID Mouse 

## 📋 1. Overview

Developed a motion-controlled mouse using ESP32-S3 and MPU6050, leveraging BLE HID to emulate a standard mouse device. Real-time orientation data is captured from the motion sensor and transmitted over Bluetooth, allowing precise and responsive cursor control on a PC based solely on hand movements.

---

## ⚙️ 2. Technologies Used

- **ESP32-S3**: BLE-capable microcontroller used to process motion data and handle BLE HID communication, with built-in USB support for direct firmware uploading without the need for a USB-UART converter.

- **MPU6050**: Captures real-time orientation (gyro + accelerometer) data used to compute cursor movement.

- **BLE HID**: Utilized to emulate a Bluetooth mouse, enabling seamless OS-level cursor control without additional drivers.

---

## ✨ 3. Key Features

- **Real-Time Motion Tracking**: Translates gyroscopic data to 2D cursor movement with minimal latency.

- **BLE HID Mouse Emulation**: Appears as a native Bluetooth mouse to any host device (e.g., PC, laptop, tablet).

- **Compact & Wireless**: Entire system is embedded on a lightweight board, suitable for wearable or handheld use.

- **Powered by 3.7V Rechargeable Battery**: Operates using a single-cell Li-ion/LiPo battery for portable and untethered usage.

- **Integrated Charging Circuit**: Includes a built-in charging module (TP4056), allowing direct battery charging via USB port.

---

## 🛠️ 4. How to Use

### Step 1: Clone the Repository
Clone the repository to your local machine:
git clone https://github.com/nguyentriet0810/hid_mouse
This repository contains both the firmware and PCB design files.

### Step 2: Fabricate the PCB
Navigate to the hardware/ folder and use the provided Gerber files or Altium project to manufacture the 2-layer PCB.

### Step 3: Flash the Firmware
Open the firmware project in Arduino IDE.
Connect the ESP32-S3 to your PC via USB cable.
Press and hold the BOOT button, then press and release the RESET button. Finally, release the BOOT button to enter bootloader mode.
In Tools > Port, select the correct COM port for the ESP32-S3.
In Tools > CDC On Boot, set it to Enabled.
Click the Upload button in Arduino IDE to flash the firmware.

### Step 4: Connect to PC via BLE
Open Bluetooth in your PC or Laptop
Connect to "ESP32_Mouse" 

## 🎥 5. Demo

## 🧑‍💻 6. Author

**Nguyen Hoang Minh Triet** – Final Year Student at **HCM University of Technology**  
Email: [23trietminh23@gmail.com](mailto:23trietminh23@gmail.com)  
GitHub: [@nguyentriet0810](https://github.com/nguyentriet0810)  
YouTube: [Hoang Triet](https://www.youtube.com/@hoangtriet9999)

Feel free to open an issue or contact me for any questions or suggestions!




