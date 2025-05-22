import sys, os, subprocess

class AutoOpenApp():
    def __init__(self):
        self.software_lst = self.read_txt_file("software.txt")
        self.status = None
        print(self.software_lst)

    def open(self, keyword):
        try:
            self.software_lst = self.read_txt_file("software.txt")
            app_path = self.software_lst.get(keyword)
            subprocess.Popen(app_path, shell=True)
            print(f"Opened {app_path} successfully.")
            self.status = True
        except Exception as e:
            print(f"Error opening {app_path}: {e}")
            self.status = False

    def read_txt_file(self, file_path): 
        try:
            with open(file_path, 'r', encoding='utf-8') as file:
                content = file.read()
                content_list = content.split('\n')  # Split the content based on newline characters
                # Remove any empty strings from the list
                content_list = [text.strip() for text in content_list if text.strip()]
                # Convert the content_list into a dictionary
                result_dict = {}
                for item in content_list:
                    # Extract word in bracket
                    word_in_bracket = item[item.find('(')+1:item.find(')')]
                    # Extract address
                    address = item[:item.find('(')-1]
                    # Add to dictionary
                    result_dict[word_in_bracket] = address
                return result_dict
        except FileNotFoundError:
            print(f"File '{file_path}' not found.")
            return {}
        except Exception as e:
            print(f"An error occurred: {e}")
            return {}

if __name__ == "__main__":
    auto = AutoOpenApp()
    auto.open("Mở Excel")