# include "filetr.h"


// 传输线程函数
void *transfer_file(void *args) {
    struct transfer_params *params = (struct transfer_params *)args;
    char file_buffer[8192];
    size_t bytes_read;

    if(params->file_command == CMD_RETREIVE) {
        if (params->transfer_type == 'A') {  // ASCII 传输
            while (fgets(file_buffer, sizeof(file_buffer), params->file) != NULL) {
                // 处理换行符，替换 '\n' 为 '\r\n'
                char *newline;
                while ((newline = strchr(file_buffer, '\n')) != NULL) {
                    *newline = '\0';  // 暂时移除 '\n'
                    if (send(params->data_socket, file_buffer, strlen(file_buffer), 0) < 0 ||
                        send(params->data_socket, "\r\n", 2, 0) < 0) {
                        perror("Error sending data in ASCII mode");
                        params->transfer_error = 1;
                        break;
                    }
                    *newline = '\n';  // 恢复 '\n'
                }
            }
        } else {  // 二进制传输
            while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), params->file)) > 0) {
                if (send(params->data_socket, file_buffer, bytes_read, 0) < 0) {
                    perror("Error sending data");
                    params->transfer_error = 1;
                    break;
                }
            }
        }
    }
    else if (params->file_command == CMD_STORE) {
        if (params->transfer_type == 'A') {  // ASCII 传输模式
            while ((bytes_read = recv(params->data_socket, file_buffer, sizeof(file_buffer), 0)) > 0) {
                for (ssize_t i = 0; i < bytes_read; i++) {
                    if (file_buffer[i] == '\r' && i + 1 < bytes_read && file_buffer[i + 1] == '\n') {
                        // 跳过 '\r', 只写入 '\n'
                        i++;
                        if (fputc('\n', params->file) == EOF) {
                            perror("Error writing to file in ASCII mode");
                            params->transfer_error = 1;
                            break;
                        }
                    } else {
                        if (fputc(file_buffer[i], params->file) == EOF) {
                            perror("Error writing to file in ASCII mode");
                            params->transfer_error = 1;
                            break;
                        }
                    }
                }
                if (params->transfer_error) {
                    break;
                }
            }
        } else {  // 二进制传输模式
            while ((bytes_read = recv(params->data_socket, file_buffer, sizeof(file_buffer), 0)) > 0) {
                if (fwrite(file_buffer, 1, bytes_read, params->file) < (size_t)bytes_read) {
                    perror("Error writing to file");
                    params->transfer_error = 1;
                    break;
                }
            }
        }
    }

    // 关闭文件和数据连接
    fclose(params->file);
    close(params->data_socket);

    // 根据传输错误情况发送响应
    if (params->transfer_error) {
        SEND_RESPONSE(params->client_socket, "426 Connection closed; transfer aborted.\r\n");
    } else {
        SEND_RESPONSE(params->client_socket, "226 Transfer complete.\r\n");
    }

    // 重置传输状态
    // 注意：如果 restart_position 是全局或会话变量，需要在主线程中重置
    // 在此示例中，我们假设它是全局变量，需要线程间同步

    free(params);
    pthread_exit(NULL);
}
