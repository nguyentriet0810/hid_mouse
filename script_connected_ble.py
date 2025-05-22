import asyncio
from bleak import BleakScanner, BleakClient

SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

def notification_handler(sender, data):
    print(f"Nhận từ ESP32: {data.decode('utf-8')}")

async def main():
    print("Đang tìm thiết bị ESP32...")
    devices = await BleakScanner.discover()
    target = None
    for d in devices:
        print(f"Thiết bị: {d.name} - {d.address}")
        if d.name and "ESP32-S3-BLE" in d.name:
            target = d
            break

    if target is None:
        print("Không tìm thấy thiết bị ESP32-S3-BLE")
        return

    print(f"Đã tìm thấy: {target.name} - {target.address}")
    await asyncio.sleep(1)  # <- Cho ESP32 có thời gian sẵn sàng trước khi connect
    async with BleakClient(target.address) as client:
        print("Đang kết nối...")
        await client.start_notify(CHAR_UUID, notification_handler)
        print("Đã kết nối! Chờ dữ liệu từ ESP32...\nNhấn Ctrl+C để thoát.")
        while True:
            await asyncio.sleep(1)

asyncio.run(main())