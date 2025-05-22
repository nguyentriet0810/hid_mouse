import serial, serial.tools.list_ports
import time, winsound, threading
import win32.lib.win32con as win32con

from pywin32_system32 import win32api

class Arduino():
    def __init__(self):
        self.PORT_DRIVER = ["Arduino Uno", "USB-SERIAL CH340", "USB Serial Device"]
        #self.PORT_DRIVER = ["Standard Serial over Bluetooth link"]
        self.flag = True
        self.arduino_port = None
        self.arduino_driver = None
        self.serialCom = None
        self.status = False
        self.data = ""
        self.alive = False
        self.thread_lock = threading.Lock()  # Lock for thread safety
    
    def arduino_connect(self):
        ports = list(serial.tools.list_ports.comports())
        self.arduino_port = None
        
        for port in ports:
            for driver in self.PORT_DRIVER:
                if driver in port.description:
                    self.arduino_port = port.device
                    self.arduino_driver = port.description
                    #print(self.arduino_driver)
                    break

        if self.arduino_port:
            if self.flag:
                if self.arduino_port != None:
                    try:
                        self.serialCom = serial.Serial(self.arduino_port, 115200, timeout=0.1)
                        print("Connected to", self.arduino_port)
                        if self.alive == False:
                            self.start_thread()
                            self.alive = True
                    except Exception as e:
                        print("Failed to initialize COM port:", e)
                # Run subprocess
                self.beep()
                self.flag = False
                self.status = True
        else:
            if not self.flag:
                print("Arduino not found!")
                self.flag = True
                self.status = False

    def driver(self):
        if self.arduino_driver:
            driver = self.arduino_driver.split("(")[0].strip()
            return driver
        else:
            return None
    
    def port(self):
        return self.arduino_port
    
    def read_serial(self):
        xcor, ycor, click = 960, 0, 0
        x, y = 0, 0 
        while self.serialCom.is_open:
            try:
                data = self.serialCom.readline().decode('utf-8').strip()
                try:
                    x, y, click = data.split("/")
                    x, y, click = int(x), int(y), int(click)
                    if x == 14 and y == 16:
                        self.mouse_cursor(xcor, ycor, click)
                    else:

                        if x > 14:
                            xcor = xcor - 1
                            if xcor < 0: 
                                xcor = 0
                        elif x < 14:
                            xcor = xcor + 1
                            if xcor > 1920:
                                xcor = 1920
                        if y < 16:
                            ycor = ycor - 1
                            if ycor < 0:
                                ycor = 0
                        elif y > 16:
                            ycor = ycor + 1
                            if ycor > 1080:
                                ycor = 1080
                        self.mouse_cursor(xcor, ycor, click)
                except Exception as e:
                    print("Error parsing data:", e)
                print(data, xcor, ycor, click)
            except UnicodeDecodeError:
                pass

    def start_thread(self):
        with self.thread_lock:
            if not self.alive:
                thread = threading.Thread(target=self.read_serial)
                thread.start()
                self.alive = True

    def mouse_cursor(self, xcor, ycor, click):
        if click == 0:
            win32api.SetCursorPos((xcor, ycor))
        elif click == 1:
            win32api.SetCursorPos((xcor, ycor))
            win32api.mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, xcor, ycor, 0, 0)
            time.sleep(0.05)  # Small delay between mouse down and up events
            win32api.mouse_event(win32con.MOUSEEVENTF_LEFTUP, xcor, ycor, 0, 0)
            print("Left click")
            time.sleep(2)
        elif click == 2:
            win32api.SetCursorPos((xcor, ycor))
            win32api.mouse_event(win32con.MOUSEEVENTF_RIGHTDOWN, xcor, ycor, 0, 0)
            time.sleep(0.05)
            win32api.mouse_event(win32con.MOUSEEVENTF_RIGHTUP, xcor, ycor, 0, 0)
            print("Right click")
            time.sleep(2)
        elif click == 3:
            
            print("Voice recognition")

    def beep(self):
        time.sleep(1)
        winsound.Beep(1000, 200)

if __name__ == "__main__":
    arduino = Arduino()
    try:
        while True:
            arduino.arduino_connect()
    except KeyboardInterrupt:
        print("Exiting...")
        if arduino.serialCom:
            arduino.serialCom.close()