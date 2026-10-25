# ESP32-S3 BLE Air Mouse

Firmware C cho ESP32-S3-WROOM-1-N16R8, MPU6050/MPU6500 va ESP-IDF **5.3.1**.
Du an moi doc lap voi `../main_code/main_code.ino`.

## Cau truc

| Component | Trach nhiem | Phu thuoc phan cung |
|---|---|---|
| `mouse_types` | Kieu du lieu va dinh dang report | Khong |
| `board_hal` | GPIO, LED active-low, so do chan, I2C bus | ESP-IDF GPIO/I2C |
| `mpu6050` | Probe 0x68/0x69, detect MPU6050/MPU6500, cau hinh/read-back | ESP-IDF I2C moi |
| `mouse_motion` | Calibration, gyro bias, loc bo sung, deadzone, toc do, cuon | Khong |
| `mouse_input` | Chong doi nut, nhan/tha, hang doi, BOOT mode | Khong |
| `ble_mouse` | GAP, HIDS, DIS, BAS, pairing, encryption, notification | Bluedroid |
| `ota_policy` | Manifest, board identity, version and size checks | Khong |
| `ota_update` | Wi-Fi maintenance, HTTPS GitHub download, flash and rollback | ESP-IDF Wi-Fi/TLS/OTA |
| `main` | Khoi dong, lich 100 Hz, xu ly loi va dieu phoi | Cac component tren |

Khong dung Arduino, OneButton, BleMouse hay thu vien MPU6050 ben ngoai.
Core thuan C co the kiem thu tren Windows ma khong can board.

Tu **1.1.1**, driver phan biet WHO_AM_I `0x68` (MPU6050) va `0x70` (MPU6500).
Module GY-521 tren board da tra ve on dinh `0x70`, nen duoc cau hinh theo profile
MPU6500, gom bo loc accel rieng tai `ACCEL_CONFIG2`. ID khac van bi tu choi.
Xem [sensor diagnosis](docs/SENSOR_DIAGNOSTICS.md) ve log va ket qua test tren board.

## Chan theo schematic

| Tin hieu | GPIO |
|---|---:|
| MPU6050 SDA / SCL | 5 / 4 |
| MPU6050 INT | 6, du phong; hien doc polling 100 Hz |
| Nut trai / phai | 10 / 11 |
| BOOT, dung lam nut mode sau khoi dong | 0 |
| LED1: ket noi BLE | 12 |
| LED2: cam bien/calibration | 13 |
| LED3: che do cuon | 14 |
| LED4: hoat dong | 21 |
| USB D- / D+ | 19 / 20 |

Nut nhan va LED deu active-low. MPU6050 dung 3.3 V, GND chung.
Bus 100 kHz, bat pull-up noi de ho tro khoi dong; module can pull-up SDA/SCL
phu hop len **3.3 V**. Khong dua muc logic 5 V vao ESP32-S3.

## Cach su dung

1. Sau reset, giu cam bien yen o tu the cam chuot trong khoang 2 giay.
   Firmware tu thu lai calibration neu phat hien chuyen dong.
2. Pair thiet bi **ESP32 Air Mouse** trong Bluetooth cua may tinh.
3. Nghieng theo truc Y cua MPU de di chuyen ngang; nghieng theo truc X de di
   chuyen doc. Toc do tang theo goc vuot deadzone, ve tu the trung tam de dung.
4. Nut GPIO10 la chuot trai, GPIO11 la chuot phai. Nhan/tha truc tiep ho tro
   click, double-click theo he dieu hanh va giu de keo-tha.
5. Nhan ngan BOOT de lay lai tu the trung tam.
6. Giu BOOT tren 300 ms roi nghieng theo truc X de cuon doc. Tha BOOT de ve
   che do con tro. Hai nut chuot van hoat dong trong che do cuon.

Chi dung BOOT lam mode **sau khi firmware da khoi dong**. Giu BOOT trong luc
reset/power-on van dua ESP32-S3 vao che do nap firmware nhu phan cung goc.
Middle-click, Back/Forward va cuon ngang chua duoc gan thao tac vat ly.

LED1 nhap nhay khi advertising, sang lien tuc khi link da ma hoa va host
da bat notification. LED2 nhap nhay khi calibration, sang khi san sang,
tat khi khong tim thay/loi MPU. LED3 bao cuon; LED4 bao nut/chuyen dong.

## Build va nap

Mo truc tiep thu muc **firmware_idf** trong VS Code voi ESP-IDF extension.
Trong ESP-IDF PowerShell:

```powershell
cd D:\ESP32_mouse\firmware_idf
idf.py build
idf.py -p COM18 flash monitor
```

`COM18` la vi du tu settings cua du an cu; kiem tra cong thuc te truoc khi nap.
Co script cho bo ESP-IDF da cai tren may:

```powershell
.\tools\idf.ps1 -Action build
.\tools\idf.ps1 -Action menuconfig
.\tools\idf.ps1 -Action flash -Port COM18
.\tools\idf.ps1 -Action monitor -Port COM18
```

Trong `menuconfig > ESP Mouse` co ten BLE, deadzone, do nhay, toc do toi da,
do nhay cuon va dao chieu truc. Mac dinh flash 16 MB, console USB Serial/JTAG.
Target **N16R8**: flash 16 MiB, PSRAM Octal 8 MiB at 80 MHz. Hai slot OTA
6 MiB, rollback va kiem tra dung luong memory duoc bat. Xem [OTA guide](docs/OTA.md)
ve migration, Wi-Fi, gesture giu hai nut chuot khi reset va GitHub Release.

## Kiem thu va quy tac C

```powershell
.\tools\test-host.ps1 -Compiler C:\MinGW\bin\gcc.exe
python .\tests\mpu_driver\run.py --project . --compiler C:\MinGW\bin\gcc.exe
python .\tests\ble_ccc\run.py --project . --compiler C:\MinGW\bin\gcc.exe
```

Xem `docs/MISRA.md` ve pham vi, cac ngoai le va phan viec can cong cu MISRA.
Xem `docs/VALIDATION.md` ve ket qua build/test va checklist test tren board.
Compiler canh bao nghiem ngat duoc ap dung rieng cho source du an; SDK duoc
build theo flags cua Espressif. Khong co chung nhan MISRA hay Bluetooth SIG.

## Gioi han da biet

- Giao thuc HID report mode; khong cung cap boot mouse protocol cho BIOS.
- Da tai hien loi reconnect BLE tren dien thoai: CCC bi xoa sau disconnect.
  Ban 1.1.3 luu/restore CCC theo tung bond trong NVS. Xem
  [BLE diagnostics](docs/BLE_DIAGNOSTICS.md) ve bang chung va ket qua bench.
- Chuot theo goc nghieng, khong phai cam bien quang hoac chuot theo vi tri
  tuyet doi. Calibration/recenter va do nhay can tinh chinh theo cach lap MPU.
- Khong co do pin trong schematic. BAS hien gia tri co dinh **99%** theo
  firmware Arduino cu, doi duoc trong menuconfig; day **khong phai so do pin**.
- PnP vendor ID `0xFFFF` la placeholder phat trien; can ID duoc cap khi phat hanh.
- Pairing Just Works, bonding luu trong NVS; khong co man hinh/ban phim de xac
  nhan passkey. Chi gui report sau khi link ma hoa va host subscribe.

## Tai lieu tham chieu

- [ESP-IDF 5.3.1 I2C master](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/peripherals/i2c.html)
- ESP-IDF da cai: `examples/bluetooth/bluedroid/ble/gatt_server_service_table`.
- [MPU-6000/MPU-6050 register map, TDK](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map.pdf)
- [HOGP implementation conformance statement, Bluetooth SIG](https://files.bluetooth.com/wp-content/uploads/2024/10/HOGP.ICS.p8.pdf)
