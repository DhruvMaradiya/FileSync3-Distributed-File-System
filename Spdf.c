// Include necessary header files for the program
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <libgen.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/types.h>

#define MAX_BUFFER 1000024
#define SPDF_PORT 4533
// Function prototypes
int create_directory(const char *path);
char *expand_path(const char *path);
char *replace_smain_with_spdf(const char *path);
void handle_rmfile(char *filepath, char *response);
void handle_list(int client_socket, char *pathname);
void handle_create_tar(int client_socket);

int main()
{
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(client_addr);
    char buffer[MAX_BUFFER];
    // Create a TCP socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        perror("Error in socket creation");
        exit(1);
    }
    // Configure server address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SPDF_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    // Bind the socket to the specified port and address
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Error in binding");
        exit(1);
    }
    // Listen for incoming connections
    if (listen(server_socket, 10) == 0)
    {
        printf("Spdf server listening on port %d...\n", SPDF_PORT);
    }
    else
    {
        perror("Error in listening");
        exit(1);
    }

    while (1)
    {
        // Accept incoming client connections
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_size);
        if (client_socket < 0)
        {
            perror("Error accepting connection");
            continue;
        }

        printf("\nAccepted connection from Smain\n");
        // Receive command from the client
        char command[MAX_BUFFER];
        ssize_t bytes_received = recv(client_socket, command, MAX_BUFFER - 1, 0);
        if (bytes_received <= 0)
        {
            perror("Error receiving command");
            close(client_socket);
            continue;
        }
        command[bytes_received] = '\0';

        // Parse command
        char *cmd = strtok(command, " ");
        char *filepath = strtok(NULL, ""); // Get the rest of the string as filepath

        if (cmd == NULL || filepath == NULL)
        {
            send(client_socket, "Invalid command", 15, 0);
            close(client_socket);
            continue;
        }
        // Handle different commands from the client
        if (strncmp(command, "list", 4) == 0)
        {
            char *pathname = command + 5; // Skip "list "
            handle_list(client_socket, pathname);
        }
        else if (strncmp(command, "rmfile", 6) == 0)
        {
            char response[MAX_BUFFER];
            handle_rmfile(filepath, response);
            printf("Response from handle_rmfile: %s\n", response);
            send(client_socket, response, strlen(response), 0);
        }
        else if (strcmp(command, "dtar") == 0)
        {
            handle_create_tar(client_socket);
        }
        else if (strcmp(cmd, "get") == 0)
        {

            // Expand path
            char *expanded_path = expand_path(filepath);
            if (expanded_path == NULL)
            {
                send(client_socket, "Error: Unable to expand path", 28, 0);
                close(client_socket);
                continue;
            }

            // Get file size
            struct stat file_stat;
            if (stat(expanded_path, &file_stat) < 0)
            {
                char error_msg[MAX_BUFFER];
                snprintf(error_msg, sizeof(error_msg), "Error: Unable to get file stats: %s", strerror(errno));
                send(client_socket, error_msg, strlen(error_msg), 0);
                printf("%s\n", error_msg);
                free(expanded_path);
                close(client_socket);
                continue;
            }

            size_t file_size = file_stat.st_size;
            char size_msg[32];
            snprintf(size_msg, sizeof(size_msg), "%zu", file_size);
            send(client_socket, size_msg, strlen(size_msg), 0);
            // printf("Sent file size: %s bytes\n", size_msg);

            // Send file contents
            int file = open(expanded_path, O_RDONLY);
            if (file < 0)
            {
                char error_msg[MAX_BUFFER];
                snprintf(error_msg, sizeof(error_msg), "Error: Unable to open file: %s", strerror(errno));
                send(client_socket, error_msg, strlen(error_msg), 0);
                printf("%s\n", error_msg);
                free(expanded_path);
                close(client_socket);
                continue;
            }

            char buffer[MAX_BUFFER];
            ssize_t bytes_read;
            size_t total_sent = 0;
            // Send file data in chunks
            while (total_sent < file_size)
            {
                bytes_read = read(file, buffer, sizeof(buffer));
                if (bytes_read <= 0)
                {
                    if (bytes_read < 0)
                    {
                        perror("Error reading file");
                    }
                    break;
                }
                size_t bytes_sent = 0;

                while (bytes_sent < bytes_read)
                {
                    ssize_t sent = send(client_socket, buffer + bytes_sent, bytes_read - bytes_sent, 0);
                    if (sent < 0)
                    {
                        perror("Error sending file data");
                        break;
                    }
                    bytes_sent += sent;
                }
                total_sent += bytes_sent;
                // printf("Sent %zd bytes, total %zu/%zu\n", bytes_sent, total_sent, file_size);
            }
            close(file);
            free(expanded_path);

            if (total_sent == file_size)
            {
                printf("File sent successfully: %s\n", filepath);
            }
            else
            {
                printf("Error: Incomplete file transfer for %s. Sent %zu/%zu bytes\n", filepath, total_sent, file_size);
            }
        }
        else if (strcmp(cmd, "store") == 0)
        {
            // Send acknowledgment
            if (send(client_socket, "ACK", 3, 0) != 3)
            {
                perror("Error sending ACK");
                close(client_socket);
                continue;
            }

            // Extract filename and directory path
            char *filename = strtok(filepath, " ");
            char *dirpath = strtok(NULL, "");

            if (filename == NULL || dirpath == NULL)
            {
                send(client_socket, "Error: Invalid filepath", 24, 0);
                close(client_socket);
                continue;
            }

            // Replace ~/smain with ~/spdf in the path
            char *spdf_path = replace_smain_with_spdf(dirpath);
            if (spdf_path == NULL)
            {
                send(client_socket, "Error: Unable to process path", 29, 0);
                close(client_socket);
                continue;
            }

            // Expand path
            char *expanded_path = expand_path(spdf_path);
            free(spdf_path);
            if (expanded_path == NULL)
            {
                send(client_socket, "Error: Unable to expand path", 28, 0);
                close(client_socket);
                continue;
            }

            // printf("Expanded path: %s\n", expanded_path);

            // Create directory if it doesn't exist
            if (create_directory(expanded_path) != 0)
            {
                send(client_socket, "Error: Unable to create directory", 33, 0);
                free(expanded_path);
                close(client_socket);
                continue;
            }

            // Construct filepath
            char store_filepath[MAX_BUFFER];
            snprintf(store_filepath, sizeof(store_filepath), "%s/%s", expanded_path, filename);

            printf("Storing PDF file: %s\n", store_filepath);

            // Open file for writing
            int file = open(store_filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (file < 0)
            {
                perror("Error creating file");
                send(client_socket, "Error creating file", 19, 0);
                free(expanded_path);
                close(client_socket);
                continue;
            }

            // Receive and write file content
            char buffer[MAX_BUFFER];
            ssize_t bytes_read;
            while ((bytes_read = recv(client_socket, buffer, MAX_BUFFER, 0)) > 0)
            {
                if (write(file, buffer, bytes_read) != bytes_read)
                {
                    perror("Error writing to file");
                    send(client_socket, "Error writing to file", 21, 0);
                    close(file);
                    free(expanded_path);
                    close(client_socket);
                    continue;
                }
            }

            close(file);
            send(client_socket, "File stored successfully", 24, 0);
            printf("PDF file stored successfully: %s\n", store_filepath);

            free(expanded_path);

            close(client_socket);
            printf("\n");
        }
    }
    // Close the server socket
    close(server_socket);
    return 0;
}

// Function to create directories recursively
int create_directory(const char *path)
{
    char tmp[256];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = 0;
            if (mkdir(tmp, S_IRWXU) != 0 && errno != EEXIST)
            {
                perror("mkdir failed");
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, S_IRWXU) != 0 && errno != EEXIST)
    {
        perror("mkdir failed");
        return -1;
    }
    return 0;
}

char *expand_path(const char *path)
{
    if (path == NULL)
    {
        fprintf(stderr, "Error: NULL path provided\n");
        return NULL;
    }

    const char *home = getenv("HOME");
    if (home == NULL)
    {
        struct passwd *pwd = getpwuid(getuid());
        if (pwd == NULL)
        {
            fprintf(stderr, "Error: Unable to determine home directory\n");
            return NULL;
        }
        home = pwd->pw_dir;
    }

    char *expanded_path = malloc(strlen(home) + strlen(path) + 2);
    if (expanded_path == NULL)
    {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return NULL;
    }

    if (strncmp(path, "~/", 2) == 0)
    {
        sprintf(expanded_path, "%s%s", home, path + 1);
    }
    else
    {
        sprintf(expanded_path, "%s", path);
    }

    return expanded_path;
}

char *replace_smain_with_spdf(const char *path)
{
    char *new_path = strdup(path);
    if (new_path == NULL)
    {
        return NULL;
    }

    char *smain_pos = strstr(new_path, "smain");
    if (smain_pos != NULL)
    {
        memcpy(smain_pos, "spdf", 4);
        memmove(smain_pos + 4, smain_pos + 5, strlen(smain_pos + 5) + 1);
    }

    return new_path;
}

void handle_rmfile(char *filepath, char *response)
{
    char *expanded_path = expand_path(filepath);
    if (expanded_path == NULL)
    {
        snprintf(response, MAX_BUFFER, "Error: Unable to expand file path");
        return;
    }

    // Replace ~/smain with ~/spdf in the path
    char *spdf_path = replace_smain_with_spdf(expanded_path);
    free(expanded_path);
    if (spdf_path == NULL)
    {
        snprintf(response, MAX_BUFFER, "Error: Unable to process path");
        return;
    }

    if (remove(spdf_path) == 0)
    {
        snprintf(response, MAX_BUFFER, "File deleted successfully: %s\n", filepath);
    }
    else
    {
        snprintf(response, MAX_BUFFER, "Error deleting file: %s (%s)", filepath, strerror(errno));
    }

    free(spdf_path);
}

void handle_list(int client_socket, char *pathname)
{
    char *expanded_path = expand_path(pathname);
    if (expanded_path == NULL)
    {
        send(client_socket, "Error: Invalid path", 20, 0);
        printf("Error: Invalid path\n");
        return;
    }
    // printf("Expanded path: %s\n", expanded_path);

    char files[MAX_BUFFER] = "";
    DIR *dir = opendir(expanded_path);
    if (dir)
    {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            if (entry->d_type == DT_REG && strstr(entry->d_name, ".pdf"))
            { // Use ".pdf" for spdf
                strcat(files, entry->d_name);
                strcat(files, "\n");
            }
        }
        closedir(dir);
    }
    else
    {
        printf("Error opening directory: %s\n", strerror(errno));
    }

    printf("Files found:\n%s", files);
    if (send(client_socket, files, strlen(files), 0) < 0)
    {
        perror("Error sending file list to smain");
    }
    else
    {
        printf("File list sent to smain successfully\n\n");
    }
    free(expanded_path);
}
void handle_create_tar(int client_socket)
{
    char command[MAX_BUFFER];
    snprintf(command, sizeof(command), "tar -cvf pdf.tar -C ~/spdf $(find ~/spdf -name '*.pdf')");
    system(command);

    FILE *tar_file = fopen("pdfs.tar", "rb");
    if (tar_file == NULL)
    {
        perror("Error opening tar file");
        send(client_socket, "Error: Unable to open tar file", 30, 0);
        return;
    }

    fseek(tar_file, 0, SEEK_END);
    long file_size = ftell(tar_file);
    fseek(tar_file, 0, SEEK_SET);

    // printf("Tar file size: %ld bytes\n", file_size);

    char buffer[MAX_BUFFER];
    size_t bytes_read;
    long total_bytes_sent = 0;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), tar_file)) > 0)
    {
        ssize_t bytes_sent = send(client_socket, buffer, bytes_read, 0);
        if (bytes_sent < 0)
        {
            perror("Error sending tar file to client");
            fclose(tar_file);
            remove("pdf.tar");
            return;
        }
        total_bytes_sent += bytes_sent;
        //  printf("Sent %zd bytes to client, total: %ld/%ld\n", bytes_sent, total_bytes_sent, file_size);

        if (bytes_sent != bytes_read)
        {
            printf("Warning: Only %zd of %zd bytes sent. Retrying...\n", bytes_sent, bytes_read);
            fseek(tar_file, total_bytes_sent, SEEK_SET); // Retry from last position
        }
    }

    fclose(tar_file);
    remove("pdf.tar"); // Remove the temporary tar file

    if (total_bytes_sent == file_size)
    {
        printf("Tar file sent to client successfully: pdf.tar\n");
    }
    else
    {
        printf("Error: Incomplete file transfer. Sent %ld/%ld bytes\n", total_bytes_sent, file_size);
    }
}
