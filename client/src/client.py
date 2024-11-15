#! /usr/bin/python3
import re
import socket
import os
import argparse
from urllib import request

BUFFER_SIZE = 8192

DATA_MODE_UNDETERMINED = 0
DATA_MODE_ACTIVE = 1
DATA_MODE_PASSIVE = 2

class SimpleFTPClient:
    def __init__(self, server, port=21):
        self.server = server
        self.port = port
        self.sock = None
        self.data_sock = None
        self.data_listen_sock = None
        self.mode = DATA_MODE_UNDETERMINED
        self.pasv_ip = None
        self.pasv_port = None

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.server, self.port))
        print(self._get_response())

    def send_command(self, cmd):
        self.sock.sendall(f"{cmd}\r\n".encode())
        return self._get_response()

    def _get_response(self):
        response = ''
        while True:
            data = self.sock.recv(BUFFER_SIZE).decode()
            response += data
            if '\r\n' in data or not data:
                break
        return response

    def set_username(self, username):
        print(self.send_command(f"USER {username}"))

    def set_password(self, password):
        print(self.send_command(f"PASS {password}"))

    def syst(self):
        response = self.send_command("SYST")
        print(response)
        return response[4:].strip()

    def set_type(self, mode='I'):
        response = self.send_command(f"TYPE {mode}")
        print(response)
        return response[4:].strip()

    def passive_mode(self):
        response = self.send_command("PASV")
        print(response)
        if not response.startswith('227'):
            print("Failed to enter passive mode.")
            return False

        start = response.find('(') + 1
        end = response.find(')')
        if start == 0 or end == -1:
            print("Failed to parse PASV response.")
            return False

        pasv_info = response[start:end].split(',')
        if len(pasv_info) != 6:
            print("Invalid PASV response.")
            return False

        try:
            ip = '.'.join(pasv_info[:4])
            port = (int(pasv_info[4]) << 8) + int(pasv_info[5])
            self.mode = DATA_MODE_PASSIVE
            self.pasv_ip = ip
            self.pasv_port = port
            return True
        except ValueError:
            print("Failed to parse PASV port.")
            return False

    def active_mode(self, port_command):
        try:
            ip_port_parts = port_command.split(',')
            if len(ip_port_parts) != 6:
                print("Invalid PORT command format.")
                return False

            ip = '.'.join(ip_port_parts[:4])
            p1, p2 = int(ip_port_parts[4]), int(ip_port_parts[5])
            port = (p1 * 256) + p2

            self.data_listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.data_listen_sock.bind((ip, port))
            self.data_listen_sock.listen(10)

            response = self.send_command(f"PORT {port_command}")
            print(response)

            if not response.startswith('200'):
                print(f"PORT command failed: {response}")
                return False

            self.mode = DATA_MODE_ACTIVE
            return True

        except Exception as e:
            print(f"Failed to enter active mode - {e}")
            if self.data_listen_sock:
                self.data_listen_sock.close()
                self.data_listen_sock = None
            return False

    def retr(self, filename):
        if self.mode == DATA_MODE_UNDETERMINED:
            print("Data mode not set.")
            return

        # # 检查本地是否已存在文件并获取其大小
        # local_file_size = 0
        # if os.path.exists(filename):
        #     local_file_size = os.path.getsize(filename)
        #     # 发送 REST 命令
        #     if not self.rest(local_file_size):
        #         print("Failed to set REST position.")
        #         return

        response = self.send_command(f"RETR {filename}")
        print(response)
        if not response.startswith('150') and not response.startswith('125'):
            print("Server did not accept RETR command.")
            return

        # 建立数据连接
        if self.mode == DATA_MODE_PASSIVE:
            self.data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.data_sock.connect((self.pasv_ip, self.pasv_port))
        elif self.mode == DATA_MODE_ACTIVE:
            self.data_sock, addr = self.data_listen_sock.accept()
            self.data_listen_sock.close()
            self.data_listen_sock = None

        try:
            # 以(追加ab, 暂时删除)写入模式打开文件
            with open(filename, 'wb') as f:
                while True:
                    data = self.data_sock.recv(BUFFER_SIZE)
                    if not data:
                        break
                    f.write(data)
            print("File downloaded successfully.")
        except Exception as e:
            print(f"Error during file download - {e}")
        finally:
            self.data_sock.close()
            self.data_sock = None
            self.mode = DATA_MODE_UNDETERMINED

        response = self._get_response()
        print(response)

    def stor(self, filename):
        if self.mode == DATA_MODE_UNDETERMINED:
            print("Data mode not set.")
            return

        # # 获取服务器上已存在文件的大小
        # server_file_size = self.size(filename)
        # if server_file_size is None:
        #     # 无法获取大小，可能文件不存在，从头开始上传
        #     server_file_size = 0
        # else:
        #     # 发送 REST 命令
        #     if not self.rest(server_file_size):
        #         print("Failed to set REST position.")
        #         return

        response = self.send_command(f"STOR {filename}")
        print(response)
        if not response.startswith('150') and not response.startswith('125'):
            print("Server did not accept STOR command.")
            return

        # 建立数据连接
        if self.mode == DATA_MODE_PASSIVE:
            self.data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.data_sock.connect((self.pasv_ip, self.pasv_port))
        elif self.mode == DATA_MODE_ACTIVE:
            self.data_sock, addr = self.data_listen_sock.accept()
            self.data_listen_sock.close()
            self.data_listen_sock = None

        try:
            # 打开本地文件，（并将文件指针移动到指定偏移量，暂时删除这行代码)，从头开始发送数据
            with open(filename, 'rb') as f:
                # f.seek(server_file_size)
                while True:
                    data = f.read(BUFFER_SIZE)
                    if not data:
                        break
                    self.data_sock.sendall(data)
            print("File uploaded successfully.")
        except Exception as e:
            print(f"Error during file upload - {e}")
        finally:
            self.data_sock.close()
            self.data_sock = None
            self.mode = DATA_MODE_UNDETERMINED

        response = self._get_response()
        print(response)

    def size(self, filename):
        response = self.send_command(f"SIZE {filename}")
        if response.startswith('213'):
            try:
                size = int(response[4:].strip())
                return size
            except ValueError:
                print("Failed to parse SIZE response.")
                return None
        elif response.startswith('550'):
            return None
        else:
            print("Server did not accept SIZE command.")
            return None

    def cwd(self, directory):
        print(self.send_command(f"CWD {directory}"))

    def pwd(self):
        response = self.send_command("PWD")
        print(response)
        directory = response.split('"')[1]
        return directory

    def mkd(self, directory):
        print(self.send_command(f"MKD {directory}"))

    def rmd(self, directory):
        print(self.send_command(f"RMD {directory}"))

    def list(self):
        if self.mode == DATA_MODE_UNDETERMINED:
            print("Data mode not set.")
            return None

        response = self.send_command("LIST")
        print(response)
        if not response.startswith('150') and not response.startswith('125'):
            print("Server did not accept LIST command.")
            return None
        
        # Establish data connection
        if self.mode == DATA_MODE_PASSIVE:
            self.data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.data_sock.connect((self.pasv_ip, self.pasv_port))
        elif self.mode == DATA_MODE_ACTIVE:
            self.data_sock, addr = self.data_listen_sock.accept()
            self.data_listen_sock.close()
            self.data_listen_sock = None

        file_info = ''
        try:
            while True:
                data = self.data_sock.recv(BUFFER_SIZE).decode()
                if not data:
                    break
                file_info += data
                print(data, end='')
            print()
        except Exception as e:
            print(f"Error during listing - {e}")
        finally:
            self.data_sock.close()
            self.data_sock = None
            self.mode = DATA_MODE_UNDETERMINED

        # Receive final server response
        response = self._get_response()
        print(response)

        return file_info

    def rest(self, offset):
        response = self.send_command(f"REST {offset}")
        print(response)
        if not response.startswith('350'):
            print("Server did not accept REST command.")
            return False
        return True

    def close(self):
        print(self.send_command("QUIT"))
        if self.sock:
            self.sock.close()
            self.sock = None
        if self.data_sock:
            self.data_sock.close()
            self.data_sock = None
        if self.data_listen_sock:
            self.data_listen_sock.close()
            self.data_listen_sock = None

def run_client(ip, port):
    ftp = SimpleFTPClient(ip, port)
    ftp.connect()

    while True:
        command = input("").strip()
        if command.lower().startswith("user"):
            try:
                _, username = command.split(maxsplit=1)
                ftp.set_username(username)
            except ValueError:
                print("Usage: USER <username>")
        elif command.lower().startswith("pass"):
            try:
                _, password = command.split(maxsplit=1)
                ftp.set_password(password)
            except ValueError:
                print("Usage: PASS <password>")
        elif command.lower() == "syst":
            ftp.syst()
        elif command.lower().startswith("type"):
            try:
                _, mode = command.split(maxsplit=1)
                ftp.set_type(mode)
            except ValueError:
                print("Usage: TYPE <mode>")
        elif command.lower().startswith("port"):
            try:
                _, port_command = command.split(maxsplit=1)
                ftp.active_mode(port_command)
            except ValueError:
                print("Usage: PORT x,x,x,x,p1,p2")
        elif command.lower() == "pasv":
            ftp.passive_mode()
        elif command.lower().startswith("retr"):
            try:
                _, filename = command.split(maxsplit=1)
                ftp.retr(filename)
            except ValueError:
                print("Usage: RETR <filename>")
        elif command.lower().startswith("stor"):
            try:
                _, filename = command.split(maxsplit=1)
                ftp.stor(filename)
            except ValueError:
                print("Usage: STOR <filename>")
        elif command.lower() == "list":
            ftp.list()
        elif command.lower().startswith("cwd"):
            try:
                _, directory = command.split(maxsplit=1)
                ftp.cwd(directory)
            except ValueError:
                print("Usage: CWD <directory>")
        elif command.lower() == "pwd":
            ftp.pwd()
        elif command.lower().startswith("mkd"):
            try:
                _, directory = command.split(maxsplit=1)
                ftp.mkd(directory)
            except ValueError:
                print("Usage: MKD <directory>")
        elif command.lower().startswith("rmd"):
            try:
                _, directory = command.split(maxsplit=1)
                ftp.rmd(directory)
            except ValueError:
                print("Usage: RMD <directory>")
        elif command.lower().startswith("rest"):
            try:
                _, offset = command.split(maxsplit=1)
                ftp.rest(offset)
            except ValueError:
                print("Usage: REST <offset>")
        elif command.lower().startswith("size"):
            try:
                _, filename = command.split(maxsplit=1)
                size = ftp.size(filename)
                if size is not None:
                    print(f"Size of {filename}: {size} bytes")
            except ValueError:
                print("Usage: SIZE <filename>")
        elif command.lower() == "quit":
            ftp.close()
            break
        elif command.lower() == "abor":
            ftp.close()
            break
        else:
            print("Unknown command.")


def parse_list_output(list_output):
    files = []
    directories = []

    # 按行分割输出
    lines = list_output.strip().split("\n")

    # 遍历每一行
    for line in lines:
        # 使用正则表达式匹配文件/文件夹信息
        match = re.match(r"([drwx\-]+)\s+\d+\s+(\S+)\s+(\S+)\s+(\d+)\s+(\S+\s+\d+\s+\d+:\d+)\s+(.+)", line)
        if match:
            permissions = match.group(1)
            owner = match.group(2)
            group = match.group(3)
            size = int(match.group(4))
            date = match.group(5)
            name = match.group(6)
            
            # 根据第一个字符判断是文件夹还是文件
            if permissions.startswith('d'):
                directories.append({
                    'name': name,
                    'permissions': permissions,
                    'owner': owner,
                    'group': group,
                    'size': size,
                    'date': date
                })
            else:
                files.append({
                    'name': name,
                    'permissions': permissions,
                    'owner': owner,
                    'group': group,
                    'size': size,
                    'date': date
                })
    
    return {
        'files': files,
        'directories': directories
    }


def get_external_ip():
    """
    获取客户端的外部 IP 地址
    """
    try:
        with request.urlopen('https://myip.ipip.net', timeout=5) as response:
            response_text = response.read().decode('utf-8')
            print(response_text)
            ip_pattern = r'(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})'
            match = re.search(ip_pattern, response_text)
            if match:
                external_ip = match.group(1)
                return external_ip
    except Exception as e:
        print(f"Unable to determine external IP address - {e}")
    return None


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Simple FTP Client")
    parser.add_argument("-ip", type=str, default="127.0.0.1", help="IP address of the FTP server")
    parser.add_argument("-port", type=int, default=21, help="Port number of the FTP server")
    parser.add_argument("-remote", action="store_true", help="Show external IP address of the client")

    args = parser.parse_args()

    if args.remote:
        print(f"External IP: {get_external_ip()}")

    run_client(args.ip, args.port)
