import sys
from PyQt5.QtWidgets import QApplication, QMainWindow, QDialog, QFileDialog, QSystemTrayIcon, QMenu, QAction
from PyQt5.QtGui import QIcon
from PyQt5.QtCore import QTimer, Qt

from gui import Ui_MainWindow
from threading import Thread
import sys, os, time

from arduino import Arduino
from voice import Voice_Recognition
from autoOpenApp import AutoOpenApp

class MainWindow(QDialog):
    def __init__(self):
        super(MainWindow, self).__init__()
        self.main_win = QMainWindow()
        self.uic = Ui_MainWindow()
        self.uic.setupUi(self.main_win)
        self.arduino = Arduino()
        self.voice = Voice_Recognition()
        self.auto_open = AutoOpenApp()

        # Variables
        self.TXT_PATH = 'software.txt'
        self.file_path = None

        # Button
        self.uic.pushButton.clicked.connect(self.browse_func)
        self.uic.pushButton_2.clicked.connect(self.ok_func)
        self.uic.pushButton_3.clicked.connect(self.cancel_func)
        self.uic.pushButton_4.clicked.connect(self.voice_func)

        # Threading
        try:
            self.thread1 = Thread(target=self.arduino.arduino_connect)
            self.thread1.daemon = True
            self.thread1.start()
        except:
            print("Error can't solve! :>")

    # Functions
    def update_device_info(self):
        if self.arduino.status:
            self.uic.label_7.setText(self.arduino.driver())
            self.uic.label_9.setText(self.arduino.port())
            self.uic.label_10.setText("ATMega328P")
        else:
            self.uic.label_7.setText("N/A")
            self.uic.label_9.setText("N/A")
            self.uic.label_10.setText("N/A")

    def browse_func(self):
        print("open")
        self.announcement("", "#f0f0f0", "white")
        file_dialog = QFileDialog()
        self.file_path, _ = file_dialog.getOpenFileName(self, 'Open file', 'D:\\')
        self.uic.lineEdit.setText(self.file_path)
        self.uic.lineEdit_2.setFocus()

    def ok_func(self):
        print("ok")
        # Open a text file for reading
        keyword = None

        if self.file_path:
            print(f"Selected File: {self.file_path}")
            keyword = self.uic.lineEdit_2.text()
            print(keyword)

            if os.path.exists(self.file_path):
                if keyword: # Kiểm tra xem có từ gợi nhớ chưa
                    self.announcement("Cài đặt thành công!", "green", "white")
                    self.uic.lineEdit.clear()
                    self.uic.lineEdit_2.clear()
                    try:
                        with open(self.TXT_PATH, 'a', encoding='utf-8') as file:
                            # Write content to the file
                            file.write(f"{self.file_path} (Mở {keyword})\n") # Cú pháp: ../file address (keyword)
                    except Exception as e:
                        print(f"An error occurred: {e}")
                    self.file_path = None
                else:
                    self.announcement("Chưa nhập từ khoá!", "red", "white")
            else:
                self.announcement("File không tồn tại!", "red", "white")
        else:
            self.announcement("Hãy chọn 1 file!", "yellow", "black")
    
    def announcement(self, text, bg_color, text_color):
        self.uic.label_11.setText(text)
        self.uic.label_11.setStyleSheet(f"background-color: {bg_color}; color: {text_color}")
        self.uic.label_11.setFixedWidth(125)
        self.uic.label_11.setAlignment(Qt.AlignCenter)
    
    def cancel_func(self):
        print("cancel")
        self.uic.lineEdit.clear()
        self.uic.lineEdit_2.clear()
        self.uic.label_11.clear()
        self.announcement("", "#f0f0f0", "white")
    
    def voice_func(self):
        self.announcement("Hãy nói!", "yellow", "black")
        txt_reg = "None"
        txt_reg = self.voice.recognize()
        self.auto_open.open(txt_reg)
        self.uic.label_15.setText(txt_reg)
        if self.auto_open.status:
            self.announcement(f"Mở thành công!", "green", "white")
        else:
            self.announcement(f"Mở thất bại!", "red", "white")
        print(txt_reg)

    def show(self):
        self.main_win.show()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    main_win = MainWindow()
    main_win.show()
    sys.exit(app.exec())