import speech_recognition as sr
from autoOpenApp import AutoOpenApp

class Voice_Recognition():
    def __init__(self):
        self.r = sr.Recognizer()
        self.sample_rate = 48000
        self.chunk_size = 1024
        self.mic_list = sr.Microphone.list_microphone_names()
        self.device_id = 0
        self.r.pause_threshold = 0.5  # in seconds
        self.r.dynamic_energy_threshold = True

    def microphone_info(self):
        for i, microphone_name in enumerate(self.mic_list):
            print("Device ID:", i, " - ", microphone_name)

    def recognize(self):
        print("Say something")
        errorRecog = 0
        with sr.Microphone(device_index=self.device_id, 
                        sample_rate=self.sample_rate, 
                        chunk_size=self.chunk_size) as source:
            self.r.adjust_for_ambient_noise(source)
            audio = self.r.listen(source, timeout=None)
        try:
            # Use recognize_google instead of recognize_google_cloud
            reg_text = self.r.recognize_google(audio, language="vi-VN")
            #print(reg_text)
            return reg_text.title()
        except sr.UnknownValueError:
            print("Could not understand audio!")
        except sr.RequestError as e:
            print("Could not request results from Google service; {0}".format(e))

if __name__ == "__main__":
    vr = Voice_Recognition()
    app = AutoOpenApp()
    txt_reg = "None"
    # vr.microphone_info()
    txt_reg = vr.recognize()
    app.open(txt_reg)
    print(txt_reg)