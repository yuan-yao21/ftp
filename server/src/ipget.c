# include "ipget.h"


int get_server_ip(char* ip_buffer, size_t ip_buffer_len, int is_pasv_local) {
    // 确保 ip_buffer 非空并且长度足够
    if (ip_buffer == NULL || ip_buffer_len < INET_ADDRSTRLEN) {
        return -1; // 参数错误
    }

    // 初始化 ip_buffer
    ip_buffer[0] = '\0';

    // 如果是本地模式，直接返回 127.0.0.1
    if (is_pasv_local) {
        strncpy(ip_buffer, "127.0.0.1", ip_buffer_len - 1);
        ip_buffer[ip_buffer_len - 1] = '\0'; // 确保字符串以 '\0' 结尾
        return 0;
    } else {
        // 使用 curl 获取公网 IP 地址
        FILE* fp;
        char command[128] = "curl -s http://ifconfig.me";  // 替换为 http 或其他更简单的 IP 服务
        char response[1024];
        regex_t regex;
        regmatch_t matches[1];
        const char* ip_pattern = "([0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3})";

        // 执行命令并打开读取流
        fp = popen(command, "r");
        if (fp == NULL) {
            return -1; // 命令执行失败
        }

        // 读取命令输出
        if (fgets(response, sizeof(response), fp) != NULL) {
            printf("response: %s\n", response);  // 打印完整的响应内容
            // 编译正则表达式
            if (regcomp(&regex, ip_pattern, REG_EXTENDED) != 0) {
                pclose(fp);
                return -1; // 正则表达式编译失败
            }

            // 使用正则表达式匹配 IP 地址
            if (regexec(&regex, response, 1, matches, 0) == 0) {
                // 提取匹配的 IP 地址
                size_t match_length = matches[0].rm_eo - matches[0].rm_so;
                if (match_length < ip_buffer_len) {
                    strncpy(ip_buffer, response + matches[0].rm_so, match_length);
                    ip_buffer[match_length] = '\0'; // 确保字符串以 '\0' 结尾
                    regfree(&regex);
                    pclose(fp);
                    return 0;
                }
            }

            regfree(&regex);
        }

        pclose(fp);
        return -1; // 获取 IP 失败
    }
}
