# include "control.h"
# include "ipget.h"
# include "filetr.h"


char root_dir[256];             // 服务器根目录
int is_pasv_local = 1;          // 是否为PASV模式下的本地连接


// 处理客户端的请求
void handle_client(int client_socket) {
    struct sockaddr_in data_addr;   // 客户端提供的地址（PORT模式）
    int data_socket = -1;           // 数据连接socket
    int passive_socket = -1;        // 被动模式下的监听socket

    char buffer[BUFFER_SIZE];
    int read_size;
    int logged_in = 0;                          // 用于记录客户端是否已登录
    char user[256];                             // 存储用户名
    int port_mode = 0;                          // 标记是否为PORT模式
    int pasv_mode = 0;                          // 标记是否为PASV模式
    char transfer_type = 'I';                   // 默认传输类型为二进制模式
    char pasv_server_ip[100] = "127.0.0.1";     // PASV模式下的服务器IP
    int pasv_server_port;                       // PASV模式下的服务器端口
    long long int restart_position = 0;         // 断点续传的位置

    // 发送欢迎消息
    SEND_RESPONSE(client_socket, "220 Anonymous FTP server ready.\r\n");

    while ((read_size = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[read_size] = '\0';
        
        // 打印接收到的命令
        printf("Received: %s", buffer);

        // 处理 QUIT 和 ABOR 命令，放在前面（因为这两个命令不需要登录）
        if (strncmp(buffer, "QUIT", 4) == 0 || strncmp(buffer, "ABOR", 4) == 0) {
            SEND_RESPONSE(client_socket, "221 Goodbye\r\n");
            if (data_socket != -1) {
                close(data_socket);  // 关闭数据连接
            }
            close(client_socket);    // 关闭控制连接
            break;                   // 退出客户端处理
        }

        // 处理 USER 命令
        if (strncmp(buffer, "USER", 4) == 0) {
            sscanf(buffer, "USER %s", user);
            if (strcmp(user, "anonymous") == 0) {
                SEND_RESPONSE(client_socket, "331 Anonymous login ok, send your email address as password\r\n");

                // 从客户端读取一行数据（等待PASS命令）
                memset(buffer, 0, sizeof(buffer));
                int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
                if (bytes_received <= 0) {
                    // 错误或客户端关闭连接
                    close(client_socket);
                    return;
                }
                buffer[bytes_received] = '\0';

                printf("Received: %s", buffer);
                
                // 解析命令并验证是否是PASS命令
                char pass_cmd[5], password[100];
                sscanf(buffer, "%s %s", pass_cmd, password);
                if (strcmp(pass_cmd, "PASS") == 0) {
                    SEND_RESPONSE(client_socket, "230 Guest login ok, access restrictions apply.\r\n");
                    logged_in = 1;  // 设置登录状态
                } else {
                    SEND_RESPONSE(client_socket, "530 Login required\r\n");
                }

                memset(buffer, 0, sizeof(buffer));
            } else {
                SEND_RESPONSE(client_socket, "530 Only anonymous login is allowed\r\n");
            }
            continue;
        }
        if (strncmp(buffer, "PASS", 4) == 0) {
            SEND_RESPONSE(client_socket, "503 Login with USER first.\r\n");
            continue;
        }

        // 判断是否已登录，后面的命令需要登录
        if (!logged_in) {
            SEND_RESPONSE(client_socket, "530 Login required\r\n");
            continue;
        }

        // 处理 PORT 命令
        if (strncmp(buffer, "PORT", 4) == 0) {
            int h1, h2, h3, h4, p1, p2;
            if (sscanf(buffer, "PORT %d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
                SEND_RESPONSE(client_socket, "501 Syntax error in parameters or arguments.\r\n");
                continue;
            }

            // 验证 IP 和端口范围
            if ((h1 | h2 | h3 | h4 | p1 | p2) < 0 || h1 > 255 || h2 > 255 || h3 > 255 || h4 > 255 || p1 > 255 || p2 > 255) {
                SEND_RESPONSE(client_socket, "501 Invalid IP address or port numbers.\r\n");
                continue;
            }

            char ip_str[INET_ADDRSTRLEN];
            snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", h1, h2, h3, h4);

            memset(&data_addr, 0, sizeof(data_addr));
            data_addr.sin_family = AF_INET;
            data_addr.sin_port = htons(p1 * 256 + p2);

            if (inet_pton(AF_INET, ip_str, &data_addr.sin_addr) <= 0) {
                SEND_RESPONSE(client_socket, "501 Invalid IP address.\r\n");
                continue;
            }

            // 验证客户端 IP 地址与控制连接的 IP 是否一致
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            getpeername(client_socket, (struct sockaddr*)&client_addr, &client_len);
            if (client_addr.sin_addr.s_addr != data_addr.sin_addr.s_addr) {
                SEND_RESPONSE(client_socket, "533 IP addresses do not match.\r\n");
                continue;
            }

            port_mode = 1; // 启用 PORT 模式
            pasv_mode = 0;
            SEND_RESPONSE(client_socket, "200 PORT command successful.\r\n");
            continue;
        }
        // 处理 PASV 命令
        else if (strncmp(buffer, "PASV", 4) == 0) {
            // 获取服务器 IP 地址
            if (get_server_ip(pasv_server_ip, sizeof(pasv_server_ip), is_pasv_local) != 0) {
                SEND_RESPONSE(client_socket, "500 Failed to get server IP address.\r\n");
                continue;
            }

            // 选择随机端口（49152–65535 动态/私有端口范围）
            int max_attempts = 10;
            for (int i = 0; i < max_attempts; ++i) {
                pasv_server_port = 49152 + rand() % (65535 - 49152 + 1);

                // 设置地址
                struct sockaddr_in pasv_addr;
                memset(&pasv_addr, 0, sizeof(pasv_addr));
                pasv_addr.sin_family = AF_INET;
                pasv_addr.sin_port = htons(pasv_server_port);
                if (inet_pton(AF_INET, pasv_server_ip, &pasv_addr.sin_addr) <= 0) {
                    SEND_RESPONSE(client_socket, "500 Invalid server IP address.\r\n");
                    break;
                }

                // 创建 PASV 监听 socket
                if ((passive_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
                    perror("Error creating PASV socket");
                    continue;
                }

                // 设置 SO_REUSEADDR
                int opt = 1;
                setsockopt(passive_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

                // 绑定端口
                if (bind(passive_socket, (struct sockaddr*)&pasv_addr, sizeof(pasv_addr)) == 0) {
                    break; // 绑定成功
                } else {
                    close(passive_socket);
                    passive_socket = -1;
                }
            }

            if (passive_socket < 0) {
                SEND_RESPONSE(client_socket, "421 Failed to enter passive mode.\r\n");
                continue;
            }

            // 开始监听
            if (listen(passive_socket, 10) < 0) {
                perror("Error listening on PASV socket");
                close(passive_socket);
                passive_socket = -1;
                SEND_RESPONSE(client_socket, "421 Failed to enter passive mode.\r\n");
                continue;
            }

            // 构建响应
            int h1, h2, h3, h4, p1, p2;
            sscanf(pasv_server_ip, "%d.%d.%d.%d", &h1, &h2, &h3, &h4);
            p1 = pasv_server_port / 256;
            p2 = pasv_server_port % 256;
            char pasv_response[128];
            snprintf(pasv_response, sizeof(pasv_response), "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d).\r\n", h1, h2, h3, h4, p1, p2);

            port_mode = 0;
            pasv_mode = 1; // 启用 PASV 模式
            SEND_RESPONSE(client_socket, pasv_response);
            continue;
        }
        // 处理 RETR 命令 (下载文件)
        else if (strncmp(buffer, "RETR", 4) == 0) {
            if (!port_mode && !pasv_mode) {
                SEND_RESPONSE(client_socket, "425 Use PORT or PASV first.\r\n");
                continue;
            }

            char filename[256];
            char *arg = buffer + 4;
            while (*arg == ' ') arg++;
            if (sscanf(arg, "%255s", filename) != 1) {
                SEND_RESPONSE(client_socket, "501 Syntax error in parameters or arguments.\r\n");
                continue;
            }

            // 构建文件的完整路径
            char temp_path[PATH_MAX];
            char cwd[256];
            if (getcwd(cwd, sizeof(cwd)) == NULL) {
                SEND_RESPONSE(client_socket, "550 Failed to get current directory.\r\n");
                continue;
            }
            snprintf(temp_path, sizeof(temp_path), "%s/%s", cwd, filename);

            // 解析实际路径
            char resolved_path[PATH_MAX];
            if (realpath(temp_path, resolved_path) == NULL) {
                SEND_RESPONSE(client_socket, "550 File not found.\r\n");
                continue;
            }

            // 确保文件在 root_dir 下
            char resolved_root[PATH_MAX];
            if (realpath(root_dir, resolved_root) == NULL) {
                SEND_RESPONSE(client_socket, "550 Server error.\r\n");
                continue;
            }
            if (strncmp(resolved_path, resolved_root, strlen(resolved_root)) != 0) {
                SEND_RESPONSE(client_socket, "550 Access denied.\r\n");
                continue;
            }

            FILE *file;
            if (transfer_type == 'A') {  // ASCII 模式
                file = fopen(resolved_path, "r");
            } else {  // 二进制模式
                file = fopen(resolved_path, "rb");
            }
            if (file == NULL) {
                SEND_RESPONSE(client_socket, "550 File not found or permission denied.\r\n");
                continue;
            }

            // 移动文件指针到指定的偏移量
            if (restart_position > 0) {
                if (fseeko(file, restart_position, SEEK_SET) != 0) {
                    SEND_RESPONSE(client_socket, "550 Failed to set file position.\r\n");
                    fclose(file);
                    continue;
                }
            }

            // 数据连接处理
            if (port_mode) {
                data_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (data_socket < 0) {
                    perror("Error creating data transfer socket");
                    fclose(file);
                    continue;
                }
                if (connect(data_socket, (struct sockaddr*)&data_addr, sizeof(data_addr)) < 0) {
                    perror("Error connecting to data transfer socket");
                    close(data_socket);
                    fclose(file);
                    continue;
                }
                /**
                我原本的处理是在打开data_socket后发送150标记，但是这样会导致我的客户端在发出 RETR 命令之后就会一直等待150标记，从而导致在 PASV 模式下，server 会阻塞在
                data_socket = accept(passive_socket, NULL, NULL) 从而致使在 PASV 模式下无法运行 RETR, LIST, STOR 等命令，所以我将发送标记的位置针对 PORT 和 PASV 模式下做了不同的区分。
                我在助教提供的 autograde_client 中发现了这个问题，我注意到 PORT 模式下发送的是 125 标记，而 PASV 模式下发送的是 150 标记，所以我在这里做了区分。
                后面的 LIST 和 STOR 命令也做了相同的处理。
                */
                SEND_RESPONSE(client_socket, "125 Data connection already open. Transfer starting.\r\n");
            } else if (pasv_mode) {
                SEND_RESPONSE(client_socket, "150 File status okay. About to open data connection.\r\n");
                data_socket = accept(passive_socket, NULL, NULL);
                if (data_socket < 0) {
                    perror("Error accepting data connection");
                    fclose(file);
                    close(passive_socket);
                    passive_socket = -1;
                    continue;
                }
                close(passive_socket);
                passive_socket = -1;
            }

            // 准备传输线程参数
            struct transfer_params *params = malloc(sizeof(struct transfer_params));
            params->data_socket = data_socket;
            params->client_socket = client_socket;
            params->file = file;
            params->transfer_type = transfer_type;
            params->transfer_error = 0;
            params->file_command = CMD_RETREIVE;

            // 创建传输线程
            pthread_t transfer_thread;
            if (pthread_create(&transfer_thread, NULL, transfer_file, (void *)params) != 0) {
                perror("Error creating transfer thread");
                SEND_RESPONSE(client_socket, "550 Failed to start data transfer.\r\n");
                fclose(file);
                close(data_socket);
                data_socket = -1;
                free(params);
                continue;
            }

            // 分离线程（不需要主线程等待它结束）
            pthread_detach(transfer_thread);

            port_mode = pasv_mode = 0;
            restart_position = 0;
            continue;
        }
        // 处理 STOR 命令 (上传文件)
        else if (strncmp(buffer, "STOR", 4) == 0) {
            if (!port_mode && !pasv_mode) {
                SEND_RESPONSE(client_socket, "425 Use PORT or PASV first.\r\n");
                continue;
            }

            char filename[256];
            char *arg = buffer + 4;
            while (*arg == ' ') arg++;
            if (sscanf(arg, "%255s", filename) != 1) {
                SEND_RESPONSE(client_socket, "501 Syntax error in parameters or arguments.\r\n");
                continue;
            }

            // 构建文件的完整路径
            char temp_path[PATH_MAX];
            char cwd[256];
            if (getcwd(cwd, sizeof(cwd)) == NULL) {
                SEND_RESPONSE(client_socket, "550 Failed to get current directory.\r\n");
                continue;
            }
            snprintf(temp_path, sizeof(temp_path), "%s/%s", cwd, filename);

            // 确保目标路径在 root_dir 下
            char resolved_root[PATH_MAX];
            if (realpath(root_dir, resolved_root) == NULL) {
                SEND_RESPONSE(client_socket, "550 Server error.\r\n");
                continue;
            }

            // 由于文件可能不存在，无法使用 realpath，手动检查路径前缀
            if (strncmp(temp_path, resolved_root, strlen(resolved_root)) != 0) {
                SEND_RESPONSE(client_socket, "550 Access denied.\r\n");
                continue;
            }

            FILE *file;
            if (transfer_type == 'A') {  // ASCII 模式
                file = fopen(temp_path, "w");
            } else {  // 二进制模式
                file = fopen(temp_path, "wb");
            }
            if (file == NULL) {
                SEND_RESPONSE(client_socket, "550 Permission denied.\r\n");
                continue;
            }

            // 移动文件指针到指定的偏移量
            if (restart_position > 0) {
                if (fseeko(file, restart_position, SEEK_SET) != 0) {
                    SEND_RESPONSE(client_socket, "550 Failed to set file position.\r\n");
                    fclose(file);
                    continue;
                }
            }

            // 数据连接处理
            if (port_mode) {
                data_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (data_socket < 0) {
                    perror("Error creating data transfer socket");
                    fclose(file);
                    continue;
                }
                if (connect(data_socket, (struct sockaddr*)&data_addr, sizeof(data_addr)) < 0) {
                    perror("Error connecting to data transfer socket");
                    close(data_socket);
                    fclose(file);
                    continue;
                }
                SEND_RESPONSE(client_socket, "125 Data connection already open. Transfer starting.\r\n");
            } else if (pasv_mode) {
                SEND_RESPONSE(client_socket, "150 File status okay. About to open data connection.\r\n");
                data_socket = accept(passive_socket, NULL, NULL);
                if (data_socket < 0) {
                    perror("Error accepting data connection");
                    fclose(file);
                    close(passive_socket);
                    passive_socket = -1;
                    continue;
                }
                close(passive_socket);
                passive_socket = -1;
            }

            // 准备传输线程参数
            struct transfer_params *params = malloc(sizeof(struct transfer_params));
            params->data_socket = data_socket;
            params->client_socket = client_socket;
            params->file = file;
            params->transfer_type = transfer_type;
            params->transfer_error = 0;
            params->file_command = CMD_STORE;

            // 创建传输线程
            pthread_t transfer_thread;
            if (pthread_create(&transfer_thread, NULL, transfer_file, (void *)params) != 0) {
                perror("Error creating transfer thread");
                SEND_RESPONSE(client_socket, "550 Failed to start data transfer.\r\n");
                fclose(file);
                close(data_socket);
                data_socket = -1;
                free(params);
                continue;
            }

            // 分离线程（不需要主线程等待它结束）
            pthread_detach(transfer_thread);

            port_mode = pasv_mode = 0;
            restart_position = 0;
            continue;
        }
        // 处理 LIST 命令
        else if (strncmp(buffer, "LIST", 4) == 0) {
            if (!port_mode && !pasv_mode) {
                SEND_RESPONSE(client_socket, "425 Use PORT or PASV first.\r\n");
                continue;
            }

            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)) == NULL) {
                SEND_RESPONSE(client_socket, "550 Failed to get current directory.\r\n");
                continue;
            }

            // 确保当前目录在 root_dir 下
            char resolved_root[PATH_MAX];
            if (realpath(root_dir, resolved_root) == NULL) {
                SEND_RESPONSE(client_socket, "550 Server error.\r\n");
                continue;
            }
            if (strncmp(cwd, resolved_root, strlen(resolved_root)) != 0) {
                SEND_RESPONSE(client_socket, "550 Access denied.\r\n");
                continue;
            }

            // 数据连接处理
            if (port_mode) {
                data_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (data_socket < 0) {
                    perror("Error creating data transfer socket");
                    continue;
                }
                if (connect(data_socket, (struct sockaddr*)&data_addr, sizeof(data_addr)) < 0) {
                    perror("Error connecting to data transfer socket");
                    close(data_socket);
                    continue;
                }
                SEND_RESPONSE(client_socket, "125 Data connection already open. Transfer starting.\r\n");
            } else if (pasv_mode) {
                SEND_RESPONSE(client_socket, "150 File status okay. About to open data connection.\r\n");
                data_socket = accept(passive_socket, NULL, NULL);
                if (data_socket < 0) {
                    perror("Error accepting data connection");
                    close(passive_socket);
                    passive_socket = -1;
                    continue;
                }
                close(passive_socket);
                passive_socket = -1;
            }

            // 使用 safer 的方式执行 ls 命令
            int pipe_fd[2];
            if (pipe(pipe_fd) == -1) {
                perror("Error creating pipe");
                close(data_socket);
                continue;
            }

            pid_t pid = fork();
            if (pid == -1) {
                perror("Error forking process");
                close(pipe_fd[0]);
                close(pipe_fd[1]);
                close(data_socket);
                continue;
            } else if (pid == 0) {
                // 子进程
                dup2(pipe_fd[1], STDOUT_FILENO);
                close(pipe_fd[0]);
                close(pipe_fd[1]);
                execlp("ls", "ls", "-l", cwd, NULL);
                exit(EXIT_FAILURE);
            } else {
                // 父进程
                close(pipe_fd[1]);

                // 从管道读取 ls 输出并发送给客户端
                char buffer[BUFFER_SIZE];
                ssize_t bytes_read;
                int transfer_error = 0;
                while ((bytes_read = read(pipe_fd[0], buffer, sizeof(buffer))) > 0) {
                    if (send(data_socket, buffer, bytes_read, 0) < 0) {
                        perror("Error sending data");
                        transfer_error = 1;
                        break;
                    }
                }
                close(pipe_fd[0]);
                close(data_socket);
                data_socket = -1;

                if (transfer_error) {
                    SEND_RESPONSE(client_socket, "426 Connection closed; transfer aborted.\r\n");
                } else {
                    SEND_RESPONSE(client_socket, "226 Transfer complete.\r\n");
                }
            }

            port_mode = pasv_mode = 0;
            continue;
        }
        // 处理 SYST 命令
        else if (strncmp(buffer, "SYST", 4) == 0) {
            SEND_RESPONSE(client_socket, "215 UNIX Type: L8\r\n");
            continue;
        }
        // 处理 TYPE 命令
        else if (strncmp(buffer, "TYPE", 4) == 0) {
            if (strncmp(buffer + 5, "I", 1) == 0) {
                SEND_RESPONSE(client_socket, "200 Type set to I.\r\n");
                transfer_type = 'I';
            } else if (strncmp(buffer + 5, "A", 1) == 0) {
                // 设置为ASCII模式
                SEND_RESPONSE(client_socket, "200 Type set to A.\r\n");
                transfer_type = 'A';
            } else {
                SEND_RESPONSE(client_socket, "504 Command not implemented for that parameter.\r\n");
            }
            continue;
        }
        // 处理 CWD 命令
        else if (strncmp(buffer, "CWD", 3) == 0) {
            // 从命令中提取目录路径
            char new_directory[256];
            // 跳过命令和空格
            char *arg = buffer + 3;
            while (*arg == ' ') arg++;
            // 移除末尾的回车和换行符
            size_t len = strlen(arg);
            while (len > 0 && (arg[len - 1] == '\r' || arg[len - 1] == '\n')) {
                arg[--len] = '\0';
            }
            strncpy(new_directory, arg, sizeof(new_directory) - 1);
            new_directory[sizeof(new_directory) - 1] = '\0';

            // 检查是否提供了目录参数
            if (strlen(new_directory) == 0) {
                SEND_RESPONSE(client_socket, "550 No directory specified.\r\n");
                continue;
            }

            // 构建目标路径
            char temp_path[PATH_MAX];
            if (new_directory[0] == '/') {
                // 绝对路径，相对于 root_dir
                snprintf(temp_path, sizeof(temp_path), "%s%s", root_dir, new_directory);
            } else {
                // 相对路径，基于当前工作目录
                char cwd[256];
                if (getcwd(cwd, sizeof(cwd)) == NULL) {
                    SEND_RESPONSE(client_socket, "550 Failed to get current directory.\r\n");
                    continue;
                }
                snprintf(temp_path, sizeof(temp_path), "%s/%s", cwd, new_directory);
            }

            // 解析实际路径
            char resolved_path[PATH_MAX];
            if (realpath(temp_path, resolved_path) == NULL) {
                SEND_RESPONSE(client_socket, "550 Failed to resolve path.\r\n");
                continue;
            }

            // 确保目标路径在 root_dir 下
            char resolved_root[PATH_MAX];
            if (realpath(root_dir, resolved_root) == NULL) {
                SEND_RESPONSE(client_socket, "550 Server error.\r\n");
                continue;
            }
            if (strncmp(resolved_path, resolved_root, strlen(resolved_root)) != 0) {
                SEND_RESPONSE(client_socket, "550 Access denied.\r\n");
                continue;
            }

            // 尝试切换到目标目录
            if (chdir(resolved_path) == 0) {
                SEND_RESPONSE(client_socket, "250 Directory successfully changed.\r\n");
            } else {
                SEND_RESPONSE(client_socket, "550 Failed to change directory.\r\n");
            }
            continue;
        }
        // 处理 PWD 命令
        else if (strncmp(buffer, "PWD", 3) == 0) {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)) == NULL) {
                SEND_RESPONSE(client_socket, "550 Failed to get current directory.\r\n");
                continue;
            }

            // 确保当前目录在 root_dir 下
            char resolved_root[PATH_MAX];
            if (realpath(root_dir, resolved_root) == NULL) {
                SEND_RESPONSE(client_socket, "550 Server error.\r\n");
                continue;
            }
            if (strncmp(cwd, resolved_root, strlen(resolved_root)) != 0) {
                SEND_RESPONSE(client_socket, "550 Access denied.\r\n");
                continue;
            }

            // 计算相对路径
            const char *relative_path = cwd + strlen(resolved_root);
            if (strlen(relative_path) == 0) {
                relative_path = "/"; // 根目录
            }

            char response[1024];
            snprintf(response, sizeof(response), "257 \"%s\" is the current directory.\r\n", relative_path);
            SEND_RESPONSE(client_socket, response);
            continue;
        }
        // 处理 MKD 命令
        else if (strncmp(buffer, "MKD", 3) == 0) {
            // 从命令中提取目录名称
            char directory[256];
            char *arg = buffer + 3;
            while (*arg == ' ') arg++;
            size_t len = strlen(arg);
            while (len > 0 && (arg[len - 1] == '\r' || arg[len - 1] == '\n')) {
                arg[--len] = '\0';
            }
            strncpy(directory, arg, sizeof(directory) - 1);
            directory[sizeof(directory) - 1] = '\0';

            if (strlen(directory) == 0) {
                SEND_RESPONSE(client_socket, "550 No directory specified.\r\n");
                continue;
            }

            // 构建新目录的完整路径
            char temp_path[PATH_MAX];
            if (directory[0] == '/') {
                snprintf(temp_path, sizeof(temp_path), "%s%s", root_dir, directory);
            } else {
                char cwd[256];
                if (getcwd(cwd, sizeof(cwd)) == NULL) {
                    SEND_RESPONSE(client_socket, "550 Failed to get current directory.\r\n");
                    continue;
                }
                snprintf(temp_path, sizeof(temp_path), "%s/%s", cwd, directory);
            }

            // 确保新目录将位于 root_dir 下
            char resolved_path[PATH_MAX];
            if (realpath(temp_path, resolved_path) == NULL) {
                // 如果目录不存在，realpath 将失败，因此我们只解析其父目录
                char parent_dir[PATH_MAX];
                strncpy(parent_dir, temp_path, sizeof(parent_dir) - 1);
                parent_dir[sizeof(parent_dir) - 1] = '\0';
                char *last_slash = strrchr(parent_dir, '/');
                if (last_slash != NULL) {
                    *last_slash = '\0';
                }
                if (realpath(parent_dir, resolved_path) == NULL) {
                    SEND_RESPONSE(client_socket, "550 Failed to resolve path.\r\n");
                    continue;
                }
                // 将新目录名附加到已解析的父目录
                strncat(resolved_path, "/", sizeof(resolved_path) - strlen(resolved_path) - 1);
                strncat(resolved_path, last_slash + 1, sizeof(resolved_path) - strlen(resolved_path) - 1);
            }

            // 检查路径是否在 root_dir 下
            char resolved_root[PATH_MAX];
            if (realpath(root_dir, resolved_root) == NULL) {
                SEND_RESPONSE(client_socket, "550 Server error.\r\n");
                continue;
            }
            if (strncmp(resolved_path, resolved_root, strlen(resolved_root)) != 0) {
                SEND_RESPONSE(client_socket, "550 Access denied.\r\n");
                continue;
            }

            // 尝试创建目录
            if (mkdir(temp_path, 0777) == 0) {
                char response[1024];
                snprintf(response, sizeof(response), "257 \"%s\" directory created.\r\n", directory);
                SEND_RESPONSE(client_socket, response);
            } else {
                SEND_RESPONSE(client_socket, "550 Failed to create directory.\r\n");
            }
            continue;
        }
        // 处理 RMD 命令
        else if (strncmp(buffer, "RMD", 3) == 0) {
            // 从命令中提取目录名称
            char directory[256];
            char *arg = buffer + 3;
            while (*arg == ' ') arg++;
            size_t len = strlen(arg);
            while (len > 0 && (arg[len - 1] == '\r' || arg[len - 1] == '\n')) {
                arg[--len] = '\0';
            }
            strncpy(directory, arg, sizeof(directory) - 1);
            directory[sizeof(directory) - 1] = '\0';

            if (strlen(directory) == 0) {
                SEND_RESPONSE(client_socket, "550 No directory specified.\r\n");
                continue;
            }

            // 构建要删除目录的完整路径
            char temp_path[PATH_MAX];
            if (directory[0] == '/') {
                snprintf(temp_path, sizeof(temp_path), "%s%s", root_dir, directory);
            } else {
                char cwd[256];
                if (getcwd(cwd, sizeof(cwd)) == NULL) {
                    SEND_RESPONSE(client_socket, "550 Failed to get current directory.\r\n");
                    continue;
                }
                snprintf(temp_path, sizeof(temp_path), "%s/%s", cwd, directory);
            }

            // 解析实际路径
            char resolved_path[PATH_MAX];
            if (realpath(temp_path, resolved_path) == NULL) {
                SEND_RESPONSE(client_socket, "550 Failed to resolve path.\r\n");
                continue;
            }

            // 确保要删除的目录在 root_dir 下
            char resolved_root[PATH_MAX];
            if (realpath(root_dir, resolved_root) == NULL) {
                SEND_RESPONSE(client_socket, "550 Server error.\r\n");
                continue;
            }
            if (strncmp(resolved_path, resolved_root, strlen(resolved_root)) != 0) {
                SEND_RESPONSE(client_socket, "550 Access denied.\r\n");
                continue;
            }

            // 尝试删除目录
            if (rmdir(resolved_path) == 0) {
                char response[1024];
                snprintf(response, sizeof(response), "250 Directory \"%s\" removed.\r\n", directory);
                SEND_RESPONSE(client_socket, response);
            } else {
                SEND_RESPONSE(client_socket, "550 Failed to remove directory.\r\n");
            }
            continue;
        }
        // 处理 REST 命令，用于断点续传
        else if (strncmp(buffer, "REST", 4) == 0) {
            char *arg = buffer + 4;
            while (*arg == ' ') arg++;
            if (sscanf(arg, "%lld", &restart_position) != 1 || restart_position < 0) {
                SEND_RESPONSE(client_socket, "501 Syntax error in parameters or arguments.\r\n");
                continue;
            }
            SEND_RESPONSE(client_socket, "350 Restart position accepted.\r\n");
            continue;
        }
        // 处理 SIZE 命令，客户端通过此命令获取服务器上的文件大小，为了 STOR 的断点续传
        else if (strncmp(buffer, "SIZE", 4) == 0) {
            char filename[256];
            char *arg = buffer + 4;
            while (*arg == ' ') arg++;
            if (sscanf(arg, "%255s", filename) != 1) {
                SEND_RESPONSE(client_socket, "501 Syntax error in parameters or arguments.\r\n");
                continue;
            }

            // 构建文件的完整路径
            char temp_path[PATH_MAX];
            char cwd[256];
            if (getcwd(cwd, sizeof(cwd)) == NULL) {
                SEND_RESPONSE(client_socket, "550 Failed to get current directory.\r\n");
                continue;
            }
            snprintf(temp_path, sizeof(temp_path), "%s/%s", cwd, filename);

            // 解析实际路径
            char resolved_path[PATH_MAX];
            if (realpath(temp_path, resolved_path) == NULL) {
                SEND_RESPONSE(client_socket, "550 File not found.\r\n");
                continue;
            }

            // 确保文件在 root_dir 下
            char resolved_root[PATH_MAX];
            if (realpath(root_dir, resolved_root) == NULL) {
                SEND_RESPONSE(client_socket, "550 Server error.\r\n");
                continue;
            }
            if (strncmp(resolved_path, resolved_root, strlen(resolved_root)) != 0) {
                SEND_RESPONSE(client_socket, "550 Access denied.\r\n");
                continue;
            }

            // 获取文件大小
            struct stat st;
            if (stat(resolved_path, &st) != 0) {
                SEND_RESPONSE(client_socket, "550 File not found or permission denied.\r\n");
                continue;
            }

            // 发送文件大小给客户端
            char response[256];
            snprintf(response, sizeof(response), "213 %lld\r\n", (long long)st.st_size);
            SEND_RESPONSE(client_socket, response);
            continue;
        }
        // 处理未实现的命令
        else {
            SEND_RESPONSE(client_socket, "502 Command not implemented\r\n");
            continue;
        }
    }
}


int control_init(in_port_t _port, const char* _root_dir, int _is_pasv_local) {

    char test_server_ip[100];
    if (_is_pasv_local == 0 ) {
        get_server_ip(test_server_ip, sizeof(test_server_ip), 0);
        printf("Server IP: %s\n", test_server_ip);
    }

    strncpy(root_dir, _root_dir, sizeof(root_dir) - 1);
    root_dir[sizeof(root_dir) - 1] = '\0';
    is_pasv_local = _is_pasv_local;

    int listenfd, connfd;
    struct sockaddr_in addr;

    srand(time(NULL));

    if ((listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        printf("Error socket(): %s(%d)\n", strerror(errno), errno);
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listenfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        printf("Error bind(): %s(%d)\n", strerror(errno), errno);
        return 1;
    }

    if (listen(listenfd, 10) == -1) {
        printf("Error listen(): %s(%d)\n", strerror(errno), errno);
        return 1;
    }

    printf("FTP server started on port %d\n", _port);

    // 循环等待客户端连接
    while (1) {
        connfd = accept(listenfd, NULL, NULL);
        if (connfd < 0) {
            perror("Accept failed");
            continue;
        }

        printf("Client connected\n");

        // 创建子进程处理客户端请求
        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            close(connfd);
            continue;
        } else if (pid == 0) {
            // 子进程中处理客户端
            close(listenfd);
            handle_client(connfd);
            close(connfd);
            exit(0);
        } else {
            // 父进程中，关闭客户端的套接字
            close(connfd);
        }
    }

    close(listenfd);
    return 0;
}
