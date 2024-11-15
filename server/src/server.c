# include "control.h"


int main(int argc, char** argv) {
    in_port_t server_port;
    char root_dir[256];
    int is_pasv_local = 1;

    server_port = DEFAULT_PORT;
    strcpy(root_dir, DEFAULT_ROOT);

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-port", 5) == 0 && i + 1 < argc) {
            server_port = atoi(argv[++i]);
        } else if (strncmp(argv[i], "-root", 5) == 0 && i + 1 < argc) {
            char fake_path[128];
            strncpy(fake_path, argv[++i], sizeof(fake_path) - 1);
            fake_path[sizeof(fake_path) - 1] = '\0';
            if (fake_path[0] != '/') {
                char cwd[128];
                if (getcwd(cwd, sizeof(cwd)) == NULL) {
                    perror("Error getting current working directory");
                    exit(EXIT_FAILURE);
                }
                snprintf(root_dir, sizeof(root_dir), "%s/%s", cwd, fake_path);
            }
            else {
                strncpy(root_dir, fake_path, sizeof(root_dir) - 1);
                root_dir[sizeof(root_dir) - 1] = '\0';
            }
        } else if (strncmp(argv[i], "-remote", 7) == 0) {
            // Set is_pasv_local to 0 if "-remote" argument is provided
            is_pasv_local = 0;
        } else {
            fprintf(stderr, "Usage: %s -port port_number -root /path/to/root [-remote]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (chdir(root_dir) != 0) {
        perror("Error changing root directory");
        exit(EXIT_FAILURE);
    }

    printf("Server will bind to port: %d\n", server_port);
    printf("Server root directory: %s\n", root_dir);

    if(control_init(server_port, root_dir, is_pasv_local)) {
        printf("Failed to launch!\n");
        return -1;
    }
    return 0;
}
