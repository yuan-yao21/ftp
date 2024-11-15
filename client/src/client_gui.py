import random
import threading
from PyQt5 import QtWidgets, QtGui, QtCore
import sys
import os
import client
from client import parse_list_output
from PyQt5.QtCore import QThread, pyqtSignal


class FileTransferThread(QThread):
    progress = pyqtSignal(int)  # 用于传输进度的信号
    completed = pyqtSignal(bool, str)  # 用于通知传输完成的信号

    def __init__(self, ftp_client, filename, transfer_type, mode):
        super().__init__()
        self.ftp_client = ftp_client
        self.filename = filename
        self.transfer_type = transfer_type  # "retr" or "stor"
        self.mode = mode  # "pasv" or "port"
    
    def run(self):
        try:
            if self.transfer_type == 'retr':
                self._download_file()
            elif self.transfer_type == 'stor':
                self._upload_file()
            self.completed.emit(True, f'{self.transfer_type.capitalize()} {self.filename} successfully.')
        except Exception as e:
            self.completed.emit(False, f'Failed to {self.transfer_type} {self.filename}: {str(e)}')

    def _download_file(self):
        if self.mode == "pasv":
            self.ftp_client.passive_mode()
        elif self.mode == "port":
            ac_port = random.randint(1024, 65535)
            ip_address = '127,0,0,1'
            port_high = ac_port // 256
            port_low = ac_port % 256
            port_command = '{},{},{}'.format(ip_address, port_high, port_low)
            self.ftp_client.active_mode(port_command)

        # 执行下载
        self.ftp_client.retr(self.filename)

    def _upload_file(self):
        if self.mode == "pasv":
            self.ftp_client.passive_mode()
        elif self.mode == "port":
            ac_port = random.randint(1024, 65535)
            ip_address = '127,0,0,1'
            port_high = ac_port // 256
            port_low = ac_port % 256
            port_command = '{},{},{}'.format(ip_address, port_high, port_low)
            self.ftp_client.active_mode(port_command)

        # 执行上传
        self.ftp_client.stor(self.filename)


class FTPClientGUI(QtWidgets.QWidget):
    def __init__(self):
        super().__init__()
        self.ftp_client = None
        self.connected = False
        self.logged_in = False
        self.initUI()

    def initUI(self):
        # Set up the UI components
        self.setWindowTitle('Simple FTP Client GUI')
        self.setGeometry(100, 100, 1000, 800)

        # IP and Port input
        self.ip_label = QtWidgets.QLabel('IP Address:', self)
        self.ip_input = QtWidgets.QLineEdit(self)
        self.ip_input.setText('127.0.0.1')

        self.port_label = QtWidgets.QLabel('Port:', self)
        self.port_input = QtWidgets.QLineEdit(self)
        self.port_input.setText('21')

        self.connect_button = QtWidgets.QPushButton('Connect', self)
        self.connect_button.clicked.connect(self.connect_to_server)

        self.connection_status = QtWidgets.QLabel(self)
        self.connection_status.setFixedSize(20, 20)
        self.update_connection_status()

        # Username and Password input
        self.username_label = QtWidgets.QLabel('Username:', self)
        self.username_input = QtWidgets.QLineEdit(self)
        self.username_input.setText('anonymous')

        self.password_label = QtWidgets.QLabel('Password:', self)
        self.password_input = QtWidgets.QLineEdit(self)
        self.password_input.setEchoMode(QtWidgets.QLineEdit.Password)

        self.login_button = QtWidgets.QPushButton('Login', self)
        self.login_button.clicked.connect(self.login_to_server)

        self.login_status = QtWidgets.QLabel(self)
        self.login_status.setFixedSize(20, 20)
        self.update_login_status()

        # Data transfer mode selection
        self.mode_group = QtWidgets.QButtonGroup(self)
        self.active_mode_button = QtWidgets.QRadioButton('Active Mode', self)
        self.passive_mode_button = QtWidgets.QRadioButton('Passive Mode', self)
        self.passive_mode_button.setChecked(True)

        self.mode_group.addButton(self.active_mode_button)
        self.mode_group.addButton(self.passive_mode_button)

        # File operations
        self.retr_button = QtWidgets.QPushButton('Download File', self)
        self.retr_button.clicked.connect(self.retr_file)

        self.stor_button = QtWidgets.QPushButton('Upload File', self)
        self.stor_button.clicked.connect(self.stor_file)

        self.back_button = QtWidgets.QPushButton('Back to Parent Directory', self)
        self.back_button.clicked.connect(self.back_to_parent_directory)

        self.mkd_button = QtWidgets.QPushButton('Create Directory', self)
        self.mkd_button.clicked.connect(self.create_directory)

        self.quit_button = QtWidgets.QPushButton('QUIT', self)
        self.quit_button.clicked.connect(self.quit_client)

        self.syst_button = QtWidgets.QPushButton('SYST', self)
        self.syst_button.clicked.connect(self.get_system_info)

        self.type_button = QtWidgets.QPushButton('TYPE I', self)
        self.type_button.clicked.connect(self.setTypeI)

        # Current directory and file list display
        self.current_directory_label = QtWidgets.QLabel('Current Directory: /', self)
        self.file_list_widget = QtWidgets.QListWidget(self)
        self.file_list_widget.itemDoubleClicked.connect(self.change_directory_or_download)
        self.file_list_widget.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        self.file_list_widget.customContextMenuRequested.connect(self.show_context_menu)

        # Layout setup
        layout = QtWidgets.QGridLayout()
        layout.addWidget(self.ip_label, 0, 0)
        layout.addWidget(self.ip_input, 0, 1)
        layout.addWidget(self.port_label, 0, 2)
        layout.addWidget(self.port_input, 0, 3)
        layout.addWidget(self.connect_button, 0, 4)
        layout.addWidget(self.connection_status, 0, 5)

        layout.addWidget(self.username_label, 1, 0)
        layout.addWidget(self.username_input, 1, 1)
        layout.addWidget(self.password_label, 1, 2)
        layout.addWidget(self.password_input, 1, 3)
        layout.addWidget(self.login_button, 1, 4)
        layout.addWidget(self.login_status, 1, 5)

        layout.addWidget(self.active_mode_button, 2, 0)
        layout.addWidget(self.passive_mode_button, 2, 1)

        layout.addWidget(self.retr_button, 3, 0)
        layout.addWidget(self.stor_button, 3, 1)
        layout.addWidget(self.back_button, 4, 0)
        layout.addWidget(self.mkd_button, 4, 1)
        layout.addWidget(self.quit_button, 4, 4)
        layout.addWidget(self.syst_button, 3, 2)
        layout.addWidget(self.type_button, 4, 2)

        layout.addWidget(self.current_directory_label, 5, 0, 1, 5)
        layout.addWidget(self.file_list_widget, 6, 0, 1, 5)

        self.setLayout(layout)

    def connect_to_server(self):
        ip = self.ip_input.text()
        port = int(self.port_input.text())
        
        try:
            self.ftp_client = client.SimpleFTPClient(ip, port)
            self.ftp_client.connect()
            self.connected = True
            self.update_connection_status()
            QtWidgets.QMessageBox.information(self, 'Connection', 'Connected to server successfully.')
        except ValueError:
            QtWidgets.QMessageBox.critical(self, 'Connection Error', 'Invalid port number. Please enter a valid port.')
        except ConnectionRefusedError:
            QtWidgets.QMessageBox.critical(self, 'Connection Error', 'Failed to connect to the server. Connection was refused.')
        except TimeoutError:
            QtWidgets.QMessageBox.critical(self, 'Connection Error', 'Failed to connect to the server. Connection timed out.')
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, 'Connection Error', f'An error occurred: {str(e)}')
            self.connected = False
            self.update_connection_status()

    def login_to_server(self):
        if not self.ftp_client:
            QtWidgets.QMessageBox.warning(self, 'Warning', 'You must connect to the server first.')
            return

        username = self.username_input.text()
        password = self.password_input.text()
        self.ftp_client.set_username(username)
        self.ftp_client.set_password(password)
        self.logged_in = True
        self.update_login_status()
        QtWidgets.QMessageBox.information(self, 'Login', 'Logged in successfully.')
        self.update_current_directory()

    def get_system_info(self):
        if not self.ftp_client:
            QtWidgets.QMessageBox.warning(self, 'Warning', 'You must connect to the server first.')
            return

        try:
            syst_response = self.ftp_client.syst()
            QtWidgets.QMessageBox.information(self, 'System Info', f'System Type: {syst_response.strip()}')
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, 'Error', f'Failed to retrieve system info: {str(e)}')

    def setTypeI(self):
        if not self.ftp_client:
            QtWidgets.QMessageBox.warning(self, 'Warning', 'You must connect to the server first.')
            return

        try:
            type_response = self.ftp_client.set_type('I')
            QtWidgets.QMessageBox.information(self, 'Type I', f'Type set to I: {type_response.strip()}')
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, 'Error', f'Failed to set type to I: {str(e)}')

    def update_current_directory(self):
        # Update current working directory label and file list
        pwd_response = self.ftp_client.pwd()
        self.current_directory_label.setText(f'Current Directory: {pwd_response.strip()}')

        self.ftp_client.passive_mode()
        file_list_response = self.ftp_client.list()
        parsed_output = parse_list_output(file_list_response)

        self.file_list_widget.clear()
        for folder in parsed_output['directories']:
            item = QtWidgets.QListWidgetItem(folder['name'])
            item.setFont(QtGui.QFont('Arial', 10, QtGui.QFont.Bold))
            item.setData(QtCore.Qt.UserRole, 'folder')  # Mark item as folder
            self.file_list_widget.addItem(item)

        for file in parsed_output['files']:
            item = QtWidgets.QListWidgetItem(file['name'])
            item.setFont(QtGui.QFont('Arial', 10))
            item.setData(QtCore.Qt.UserRole, 'file')  # Mark item as file
            self.file_list_widget.addItem(item)

    def update_connection_status(self):
        if self.connected:
            self.connection_status.setStyleSheet("background-color: green; border-radius: 10px;")
        else:
            self.connection_status.setStyleSheet("background-color: red; border-radius: 10px;")

    def update_login_status(self):
        if self.logged_in:
            self.login_status.setStyleSheet("background-color: green; border-radius: 10px;")
        else:
            self.login_status.setStyleSheet("background-color: red; border-radius: 10px;")

    def change_directory_or_download(self, item):
        if item.data(QtCore.Qt.UserRole) == 'folder':
            folder_name = item.text()
            self.ftp_client.cwd(folder_name)
            self.update_current_directory()
        elif item.data(QtCore.Qt.UserRole) == 'file':
            file_name = item.text()
            reply = QtWidgets.QMessageBox.question(self, 'Download File', f'Do you want to download "{file_name}"?',
                                                   QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No, QtWidgets.QMessageBox.No)
            if reply == QtWidgets.QMessageBox.Yes:
                mode_tmp = "pasv" if self.passive_mode_button.isChecked() else "port"
                self.file_transfer_thread = FileTransferThread(self.ftp_client, file_name, 'retr', mode_tmp)
                self.file_transfer_thread.completed.connect(self.on_transfer_completed)
                self.file_transfer_thread.start()

    def back_to_parent_directory(self):
        if not self.ftp_client:
            QtWidgets.QMessageBox.warning(self, 'Warning', 'You must connect to the server first.')
            return
        self.ftp_client.cwd('..')
        self.update_current_directory()

    def create_directory(self):
        if not self.ftp_client:
            QtWidgets.QMessageBox.warning(self, 'Warning', 'You must connect to the server first.')
            return

        dir_name, ok = QtWidgets.QInputDialog.getText(self, 'Create Directory', 'Enter new directory name:')
        if ok and dir_name:
            self.ftp_client.mkd(dir_name)
            QtWidgets.QMessageBox.information(self, 'Create Directory', f'Directory "{dir_name}" created successfully.')
            self.update_current_directory()

    def show_context_menu(self, position):
        item = self.file_list_widget.itemAt(position)
        if item and item.data(QtCore.Qt.UserRole) == 'folder':
            menu = QtWidgets.QMenu()
            delete_action = menu.addAction('Delete Folder')
            action = menu.exec_(self.file_list_widget.viewport().mapToGlobal(position))
            if action == delete_action:
                folder_name = item.text()
                reply = QtWidgets.QMessageBox.question(self, 'Delete Folder', f'Are you sure you want to delete "{folder_name}"?',
                                                       QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No, QtWidgets.QMessageBox.No)
                if reply == QtWidgets.QMessageBox.Yes:
                    self.ftp_client.rmd(folder_name)
                    QtWidgets.QMessageBox.information(self, 'Delete Folder', f'Folder "{folder_name}" deleted successfully.')
                    self.update_current_directory()

    def quit_client(self):
        if self.ftp_client:
            self.ftp_client.close()
            QtWidgets.QMessageBox.information(self, 'Quit', 'Connection closed.')
            self.connected = False
            self.logged_in = False
            self.update_connection_status()
            self.update_login_status()

    def closeEvent(self, event):
        reply = QtWidgets.QMessageBox.question(self, 'Quit', 'Are you sure you want to quit?',
                                            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No, QtWidgets.QMessageBox.No)
        if reply == QtWidgets.QMessageBox.Yes:
            if self.ftp_client:
                self.ftp_client.close()
            event.accept()
        else:
            event.ignore()

    def retr_file(self):
        if not self.ftp_client:
            QtWidgets.QMessageBox.warning(self, 'Warning', 'You must connect to the server first.')
            return

        mode_tmp = "pasv" if self.passive_mode_button.isChecked() else "port"
        self.ftp_client.passive_mode()
        file_list_response = self.ftp_client.list()
        parsed_output = parse_list_output(file_list_response)
        file_list = [file['name'] for file in parsed_output['files']]

        file_name, ok = QtWidgets.QInputDialog.getItem(self, 'Select File', 'File to download:', file_list)
        if ok and file_name:
            self.file_transfer_thread = FileTransferThread(self.ftp_client, file_name, 'retr', mode_tmp)
            self.file_transfer_thread.completed.connect(self.on_transfer_completed)
            self.file_transfer_thread.start()

    def stor_file(self):
        if not self.ftp_client:
            QtWidgets.QMessageBox.warning(self, 'Warning', 'You must connect to the server first.')
            return

        mode_tmp = "pasv" if self.passive_mode_button.isChecked() else "port"
        options = QtWidgets.QFileDialog.Options()
        file_path, _ = QtWidgets.QFileDialog.getOpenFileName(self, 'Select File to Upload', '', 'All Files (*);;Text Files (*.txt)', options=options)
        if file_path:
            filename = os.path.basename(file_path)
            self.file_transfer_thread = FileTransferThread(self.ftp_client, filename, 'stor', mode_tmp)
            self.file_transfer_thread.completed.connect(self.on_transfer_completed)
            self.file_transfer_thread.start()

    def on_transfer_completed(self, success, message):
        if success:
            QtWidgets.QMessageBox.information(self, 'File Transfer', message)
        else:
            QtWidgets.QMessageBox.critical(self, 'File Transfer', message)


if __name__ == '__main__':
    app = QtWidgets.QApplication(sys.argv)
    window = FTPClientGUI()
    window.show()
    sys.exit(app.exec_())
