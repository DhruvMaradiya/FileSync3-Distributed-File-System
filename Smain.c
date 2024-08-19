// Include necessary header files for the program
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <errno.h>
#include <libgen.h>
#include <tar.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Define constants
#define MAX_BUFFER 1000024 // Maximum buffer size for data transfer
#define SPDF_PORT 4533  // Port number for the PDF server
#define STEXT_PORT 4532 // Port number for the text server
#define SMAIN_PORT 4530 // Port number for the main server
#define CHUNK_SIZE 8192 // Size of chunks for file transfer

// Function prototypes
void prcclient(int client_socket);
void handle_ufile(int client_socket, char *filename, char *path);
void handle_dfile(int client_socket, char *filepath);
void handle_rmfile(int client_socket, char *filepath);
int forward_to_stext(const char *filename, const char *path);
int forward_to_spdf(const char *filename, const char *path);
void request_and_forward_file(int client_socket, const char *file_path, const char *server_name, int server_port);
char *replace_smain_with_stext(const char *path);
char *replace_smain_with_spdf(const char *path);
void send_file(int client_socket, const char *filename);
int forward_delete_request(int client_socket, const char *filepath, int port);
void handle_display(int client_socket, char *pathname);
int get_files_from_stext(const char *pathname, char *txt_files);
int get_files_from_spdf(const char *pathname, char *pdf_files);
int receive_file(int client_socket, char *filename);
int create_directory(const char *path);
char *expand_path(const char *path);
void handle_dtar(int client_socket, char *file_extension);
void receive_and_forward_file(int from_socket, int to_socket, const char *filename);

// Main function
int main()
{
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(client_addr);
    // Create a socket for the server
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        perror("Error in socket creation");
        exit(1);
    }

    // Set up the server address structure
    server_addr.sin_family = AF_INET;         // Use IPv4 addresses
    server_addr.sin_port = htons(SMAIN_PORT); // Set the server port
    server_addr.sin_addr.s_addr = INADDR_ANY; // Allow connections from any IP address

    // Bind the socket to the server address
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Error in binding");
        exit(1);
    }
    // Listen for incoming connections
    if (listen(server_socket, 10) == 0)
    {
        printf("Smain server listening on port %d...\n", SMAIN_PORT);
    }
    else
    {
        perror("Error in listening");
        exit(1);
    }
    // Main server loop to accept and handle client connections
    while (1)
    {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_size);
        if (client_socket < 0)
        {
            perror("Error accepting connection");
            continue;
        }
        // Create a child process to handle the client
        pid_t pid = fork();
        if (pid == 0)
        {
            // Child process
            close(server_socket);
            prcclient(client_socket); // Process commands from the client
            exit(0);
        }
        else if (pid > 0)
        {
            // Parent process
            close(client_socket);
        }
        else
        {
            perror("Fork failed"); // Print error if fork fails
        }
    }

    close(server_socket); // Close the server socket when exiting
    return 0;
}
// Function to handle commands from a client
void prcclient(int client_socket)
{
    char buffer[MAX_BUFFER]; // Buffer for receiving data
    ssize_t bytes_received;  // Number of bytes received from the client

    while (1)
    {
        // Receive data from the client
        bytes_received = recv(client_socket, buffer, MAX_BUFFER - 1, 0);
        if (bytes_received <= 0)
        {
            if (bytes_received == 0)
            {
                printf("Client disconnected.\n");
            }
            else
            {
                perror("recv failed");
            }
            break;
        }

        buffer[bytes_received] = '\0'; // Null-terminate the received data

        // Parse the command from the received data
        char *command = strtok(buffer, " ");
        if (command == NULL)
        {
            send(client_socket, "Invalid command", 15, 0); // Send error message if command is invalid
            continue;
        }

        // Handle different commands
        if (strcmp(command, "ufile") == 0)
        {
            printf("handling ufile");
            char *filename = strtok(NULL, " ");
            char *path = strtok(NULL, "");
            if (filename != NULL && path != NULL)
            {
                handle_ufile(client_socket, filename, path); // Handle the upload file command
            }
            else
            {
                const char *error_msg = "Error: Invalid ufile command format. Usage: ufile <filename> <path>";
                send(client_socket, error_msg, strlen(error_msg), 0); // Send error message if command format is invalid
            }
        }
        else if (strcmp(command, "dfile") == 0)
        {
            char *filepath = strtok(NULL, "");
            handle_dfile(client_socket, filepath);
        }
        else if (strcmp(command, "rmfile") == 0)
        {
            char *filepath = strtok(NULL, "");
            handle_rmfile(client_socket, filepath); // Handle the remove file command
        }
        else if (strncmp(buffer, "dtar", 4) == 0)
        {
            char *file_extension = buffer + 5; // Skip "dtar "
            handle_dtar(client_socket, file_extension);
        }
        else if (strcmp(command, "display") == 0)
        {
            char *directory = strtok(NULL, "");
            handle_display(client_socket, directory); // Handle the display command
        }
        else
        {
            send(client_socket, "Unknown command", 15, 0);
        }

        printf("\n");
    }

    close(client_socket); // Close the client socket when done
}
// Function to handle the upload file command
void handle_ufile(int client_socket, char *filename, char *path)
{
    if (filename == NULL || path == NULL)
    {
        const char *error_msg = "Error: Invalid filename or path";
        send(client_socket, error_msg, strlen(error_msg), 0); // Send error message if filename or path is invalid
        return;
    }

    char *expanded_path = expand_path(path); // Expand the path to its full form
    if (expanded_path == NULL)
    {
        char error_msg[MAX_BUFFER];
        snprintf(error_msg, MAX_BUFFER, "Error: Unable to expand or create path %s", path);
        send(client_socket, error_msg, strlen(error_msg), 0); // Send error message if path expansion fails
        return;
    }

    char filepath[MAX_BUFFER] = {0};                                            // Buffer for storing the file path
    snprintf(filepath, sizeof(filepath) - 1, "%s/%s", expanded_path, filename); // Construct the full file path

    // Check if file already exists
    if (access(filepath, F_OK) != -1)
    {
        char error_msg[MAX_BUFFER];
        snprintf(error_msg, MAX_BUFFER, "Error: File %s already exists", filename);
        send(client_socket, error_msg, strlen(error_msg), 0); // Send error message if file already exists
        free(expanded_path);                                  // Free the expanded path memory
        return;
    }
    // Receive the file from the client
    int receive_result = receive_file(client_socket, filepath);
    if (receive_result != 0)
    {
        char error_msg[MAX_BUFFER];
        snprintf(error_msg, MAX_BUFFER, "Error storing file on Smain: %s", strerror(errno));
        send(client_socket, error_msg, strlen(error_msg), 0); // Send error message if file storage fails
                                                              // Free the expanded path memory
        free(expanded_path);
        return;
    }
    // Get the file extension
    char *file_extension = strrchr(filename, '.');
    if (file_extension == NULL)
    {
        const char *error_msg = "Error: File has no extension";
        send(client_socket, error_msg, strlen(error_msg), 0); // Send error message if file has no extension
        free(expanded_path);                                  // Free the expanded path memory
        remove(filepath);                                     // Remove the file if it has no extension
        return;
    }

    char success_msg[MAX_BUFFER];
    if (strcmp(file_extension, ".txt") == 0) // handle .txt
    {
        if (forward_to_stext(filepath, "~/stext") == 0)
        {
            snprintf(success_msg, MAX_BUFFER, "File %s stored successfully on Smain", filename);
            // Remove the file if it was successfully forwarded
            remove(filepath);
        }
        else
        {
            snprintf(success_msg, MAX_BUFFER, "File %s stored unsuccessfully on Smain ", filename);
        }
    }
    else if (strcmp(file_extension, ".c") == 0) // handle .c
    {
        snprintf(success_msg, MAX_BUFFER, "File %s stored successfully on Smain server", filename);
    }
    else if (strcmp(file_extension, ".pdf") == 0) // handle .pdf
    {
        if (forward_to_spdf(filepath, "~/spdf") == 0)
        {
            snprintf(success_msg, MAX_BUFFER, "File %s stored successfully on Smain", filename);
            // Remove the file if it was successfully forwarded
            remove(filepath);
        }
        else
        {
            snprintf(success_msg, MAX_BUFFER, "File %s stored unsuccessfully on Smain ", filename);
        }
    }
    else
    {
        snprintf(success_msg, MAX_BUFFER, "File %s stored successfully on Smain server", filename);
    }
    send(client_socket, success_msg, strlen(success_msg), 0); // Send success message to the client
    free(expanded_path);                                      // Free the expanded path memory
}

int receive_file(int client_socket, char *filename)
{
    // Buffer to receive the file size
    char size_str[32];
    ssize_t size_received = recv(client_socket, size_str, sizeof(size_str) - 1, 0); // Receive file size from client
    if (size_received <= 0)                                                         // Check if receiving size was unsuccessful
    {
        perror("Error receiving file size");
        return -1;
    }
    size_str[size_received] = '\0';                  // Null-terminate the size string
    size_t file_size = strtoull(size_str, NULL, 10); // Convert size string to integer

    // Send acknowledgment
    send(client_socket, "ACK", 3, 0);

    printf("Receiving file: %s\n", filename); // Log the filename being received

    // Open file for writing
    int file = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file < 0) // Check if file opening was unsuccessful
    {
        perror("Error opening file for writing");
        return -1;
    }

    char buffer[MAX_BUFFER];           // Buffer for receiving file data
    size_t total_received = 0;         // Track total bytes received
    while (total_received < file_size) // Continue until full file is received
    {
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0); // Receive file data
        if (bytes_received <= 0)                                                 // Check if receiving data was unsuccessful
        {
            if (bytes_received < 0)
            {
                perror("Error receiving file data");
            }
            break;
        }
        ssize_t bytes_written = write(file, buffer, bytes_received); // Write data to file
        if (bytes_written != bytes_received)                         // Check if all data was written
        {
            perror("Error writing to file");
            close(file);
            return -1;
        }
        total_received += bytes_received; // Update total bytes received
    }

    close(file);
    // Check if the entire file was received
    if (total_received == file_size)
    {
        printf("File received and saved: %s\n", filename);
        return 0;
    }
    else
    {
        // printf("Error: Incomplete file transfer. Received %zu/%zu bytes\n", total_received, file_size);
        return -1;
    }
}

int forward_to_stext(const char *filepath, const char *base_path)
{ // Create socket for Stext server
    int stext_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (stext_socket < 0)
    {
        perror("Error creating socket for Stext");
        return -1;
    }

    struct sockaddr_in stext_addr; // Server address structure
    stext_addr.sin_family = AF_INET;
    stext_addr.sin_port = htons(STEXT_PORT);
    stext_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(stext_socket, (struct sockaddr *)&stext_addr, sizeof(stext_addr)) < 0)
    {
        perror("Error connecting to Stext server");
        close(stext_socket);
        return -1;
    }

    // Remove filename from filepath
    char *dir_path = strdup(filepath); // Duplicate the path to modify it
    if (dir_path == NULL)
    {
        perror("Error duplicating filepath");
        close(stext_socket);
        return -1;
    }

    char *base_filename = basename(dir_path);        // Get the filename
    char *path_without_filename = dirname(dir_path); // Get directory path without filename

    // Construct the command to send to Stext
    char command[MAX_BUFFER];
    snprintf(command, sizeof(command), "store %s %s", base_filename, path_without_filename);

    // Send the command to Stext
    if (send(stext_socket, command, strlen(command), 0) < 0)
    {
        perror("Error sending command to Stext server");
        free(dir_path);
        close(stext_socket);
        return -1;
    }

    // Receive acknowledgment from Stext server
    char response[MAX_BUFFER];
    ssize_t bytes_received = recv(stext_socket, response, MAX_BUFFER - 1, 0);
    if (bytes_received > 0)
    {
        response[bytes_received] = '\0';
        printf("Stext server response: %s\n", response);
    }
    else
    {
        perror("Error receiving response from Stext server");
    }

    // Forward the file content to Stext
    int file = open(filepath, O_RDONLY);
    if (file < 0)
    {
        perror("Error opening file to forward to Stext server");
        free(dir_path);
        close(stext_socket);
        return -1;
    }

    ssize_t bytes_read;
    char buffer[MAX_BUFFER];
    while ((bytes_read = read(file, buffer, MAX_BUFFER)) > 0) // Read file content
    {
        if (send(stext_socket, buffer, bytes_read, 0) != bytes_read)
        {
            perror("Error forwarding file content to Stext server"); // Print error message
            close(file);
            free(dir_path);
            close(stext_socket);
            return -1;
        }
    }

    if (bytes_read < 0)
    {
        perror("Error reading file");
    }

    close(file);         // Close the file
    free(dir_path);      // Free allocated memory
    close(stext_socket); // Close the socket

    return 0;
}

int forward_to_spdf(const char *filepath, const char *base_path)
{
    // Create socket for Spdf server
    int spdf_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (spdf_socket < 0)
    {
        perror("Error creating socket for Spdf");
        return -1;
    }

    struct sockaddr_in spdf_addr;                       // Server address structure
    spdf_addr.sin_family = AF_INET;                     // IPv4
    spdf_addr.sin_port = htons(SPDF_PORT);              // Set port number
    spdf_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Set IP address to localhost

    if (connect(spdf_socket, (struct sockaddr *)&spdf_addr, sizeof(spdf_addr)) < 0)
    {
        perror("Error connecting to Spdf server");
        close(spdf_socket);
        return -1;
    }

    // Duplicate the path to modify it and replace "smain" with "spdf"
    char *dir_path = strdup(filepath);
    if (dir_path == NULL)
    {
        perror("Error duplicating filepath");
        close(spdf_socket);
        return -1;
    }
    char *base_filename = basename(dir_path);        // Get the filename
    char *path_without_filename = dirname(dir_path); // Get directory path without filename

    // Construct the command to send to spdf
    char command[MAX_BUFFER];
    snprintf(command, sizeof(command), "store %s %s", base_filename, path_without_filename);

    // Send the command to spdf
    if (send(spdf_socket, command, strlen(command), 0) < 0)
    {
        perror("Error sending command to Spdf server");
        free(dir_path);
        close(spdf_socket);
        return -1;
    }

    // Receive acknowledgment from Spdf server
    char response[MAX_BUFFER];
    ssize_t bytes_received = recv(spdf_socket, response, MAX_BUFFER - 1, 0);
    if (bytes_received > 0)
    {
        response[bytes_received] = '\0';
        printf("Spdf server response: %s\n", response);
    }
    else
    {
        perror("Error receiving response from Spdf server");
    }

    // Forward the file content to Spdf
    int file = open(filepath, O_RDONLY);
    if (file < 0)
    {
        perror("Error opening file to forward to Spdf server");
        free(dir_path);
        close(spdf_socket);
        return -1;
    }

    ssize_t bytes_read;
    char buffer[MAX_BUFFER];
    while ((bytes_read = read(file, buffer, MAX_BUFFER)) > 0) // Read file content
    {
        if (send(spdf_socket, buffer, bytes_read, 0) != bytes_read)
        {
            perror("Error forwarding file content to Spdf server");
            close(file);
            free(dir_path);
            close(spdf_socket);
            return -1;
        }
    }

    if (bytes_read < 0)
    {
        perror("Error reading file");
    }

    close(file);        // Close the file
    free(dir_path);     // Free allocated memory
    close(spdf_socket); // Close the socket

    return 0;
}

char *expand_path(const char *path)
{
    if (path == NULL) // Check if the provided path is NULL
    {
        fprintf(stderr, "Error: NULL path provided\n");
        return NULL;
    }
    const char *home;    // Pointer to store home directory path
    char *expanded_path; // Pointer to store the expanded path

    if (path[0] == '~' && path[1] == '/')
    {
        home = getenv("HOME"); // Get the value of the HOME environment variable
        if (home == NULL)      // Check if HOME is not set
        {
            struct passwd *pwd = getpwuid(getuid());
            if (pwd == NULL)
            {
                fprintf(stderr, "Error: Unable to determine home directory\n");
                return NULL;
            }
            home = pwd->pw_dir; // Use the home directory from user information
        }

        expanded_path = malloc(strlen(home) + strlen(path) + 1); // Allocate memory for expanded path (+1 for null-terminator)
        if (expanded_path == NULL)                               // Check if memory allocation failed
        {
            fprintf(stderr, "Error: Memory allocation failed\n");
            return NULL;
        }

        strcpy(expanded_path, home);     // Copy home directory path to expanded_path
        strcat(expanded_path, path + 1); // Skip the '~'
    }
    else
    {
        expanded_path = strdup(path); // Duplicate the path if it does not start with '~/'
    }

    // Ensure the directory exists
    if (create_directory(expanded_path) != 0)
    {
        free(expanded_path);
        return NULL;
    }

    return expanded_path;
}

int create_directory(const char *path)
{
    char tmp[256];  // Buffer to hold the path during directory creation
    char *p = NULL; // Pointer used to traverse the path
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path); // Copy the path to the buffer
    len = strlen(tmp);
    if (tmp[len - 1] == '/') // Check if the path ends with '/'
        tmp[len - 1] = 0;    // Remove the trailing '/' for directory creation

    // Traverse the path and create directories as needed
    for (p = tmp + 1; *p; p++)
    {
        if (*p == '/') // Check for directory separators
        {
            *p = '\0'; // Temporarily end the string for directory creation
            if (mkdir(tmp, S_IRWXU) != 0 && errno != EEXIST)
            {
                perror("mkdir failed");
                return -1;
            }
            *p = '/'; // Restore the directory separator
        }
    }
    // Create the final directory
    if (mkdir(tmp, S_IRWXU) != 0 && errno != EEXIST) // Create the final directory (if it doesn't already exist)
    {
        perror("mkdir failed");
        return -1;
    }
    return 0;
}

void send_file(int client_socket, const char *file_path)
{
    FILE *file = fopen(file_path, "rb"); // Open the file in binary read mode
    if (!file)                           // Check if file opening failed
    {
        perror("Failed to open file");
        send(client_socket, "Error: File not found.\n", 23, 0);
        return;
    }

    fseek(file, 0, SEEK_END);     // Move file pointer to the end of the file
    long file_size = ftell(file); // Get the size of the file
    fseek(file, 0, SEEK_SET);     // Move file pointer back to the beginning of the file

    char size_buffer[32];
    snprintf(size_buffer, sizeof(size_buffer), "%ld", file_size); // Convert file size to string
    send(client_socket, size_buffer, strlen(size_buffer), 0);

    printf("Sending file: %s, size: %ld bytes\n", file_path, file_size);

    char buffer[CHUNK_SIZE];                                          // Buffer to hold file data chunks
    size_t bytes_read;                                                // Variable to store the number of bytes read
    long total_sent = 0;                                              // Track the total number of bytes sent
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) // Read file data in chunks
    {
        ssize_t bytes_sent = send(client_socket, buffer, bytes_read, 0);
        if (bytes_sent < 0) // Check if sending data failed
        {
            perror("Error sending file data");
            break;
        }
        total_sent += bytes_sent; // Update total bytes sent
        // printf("Sent %zd bytes, total %ld/%ld\n", bytes_sent, total_sent, file_size);
    }

    fclose(file);

    if (total_sent == file_size) // Check if the entire file was sent
    {
        printf("File sent successfully: %s\n", file_path);
        send(client_socket, "File sent successfully.\n", 24, 0);
    }
    else
    {
        //   printf("Error: Incomplete file transfer. Sent %ld/%ld bytes\n", total_sent, file_size);
        send(client_socket, "Error: Incomplete file transfer.\n", 32, 0);
    }
}

void request_and_forward_file(int client_socket, const char *file_path, const char *server_name, int server_port)
{
    int server_socket = socket(AF_INET, SOCK_STREAM, 0); // Create a socket for the server connection
    struct sockaddr_in server_addr;                      // Structure to hold server address information

    server_addr.sin_family = AF_INET;                     // Set address family to IPv4
    server_addr.sin_port = htons(server_port);            // Set port number
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Set IP address to localhost (127.0.0.1)

    printf("Connecting to %s server on port %d\n", server_name, server_port);
    if (connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Failed to connect to server");
        send(client_socket, "Error: Unable to connect to server.\n", 36, 0); // Send error message to client
        close(server_socket);
        return;
    }

    char request[MAX_BUFFER];
    snprintf(request, sizeof(request), "get %s", file_path); // Create the request message to get the file
    printf("Sending request to %s: %s\n", server_name, request);
    send(server_socket, request, strlen(request), 0); // Send request to server

    // Forward the file size
    char size_buffer[32];
    ssize_t size_received = recv(server_socket, size_buffer, sizeof(size_buffer) - 1, 0);
    if (size_received <= 0) // Check if receiving file size failed
    {
        perror("Error receiving file size");
        close(server_socket);
        return;
    }
    size_buffer[size_received] = '\0';                  // Null-terminate the size string
    send(client_socket, size_buffer, size_received, 0); // Forward the file size to the client

    // Forward the file content
    char buffer[CHUNK_SIZE];
    ssize_t bytes_received;
    size_t total_sent = 0;
    size_t file_size = atoll(size_buffer);

    while (total_sent < file_size && (bytes_received = recv(server_socket, buffer, sizeof(buffer), 0)) > 0) // Receive file data in chunks
    {
        ssize_t bytes_sent = send(client_socket, buffer, bytes_received, 0);
        if (bytes_sent < 0)
        {
            perror("Error sending file data to client");
            break;
        }
        total_sent += bytes_sent; // Update total bytes sent

        // printf("Forwarded %zd bytes, total %zu/%zu\n", bytes_sent, total_sent, file_size);
    }

    if (total_sent == file_size) // Check if the entire file was forwarded
    {
        printf("File %s forwarded successfully.\n", file_path);
        char completion_msg[MAX_BUFFER]; // Buffer to hold completion message
        snprintf(completion_msg, sizeof(completion_msg), "File %s downloaded successfully.\n", file_path);
        send(client_socket, completion_msg, strlen(completion_msg), 0);
    }
    else
    {
        //  printf("Error: Incomplete file transfer. Forwarded %zu/%zu bytes\n", total_sent, file_size);
        send(client_socket, "Error: Incomplete file transfer.\n", 32, 0);
    }

    close(server_socket);
}

void handle_dfile(int client_socket, char *file_path)
{
    char buffer[MAX_BUFFER];
    // Get the file extension from the file path
    const char *file_ext = strrchr(file_path, '.');
    // Check if file extension is missing
    if (file_ext == NULL)
    {
        snprintf(buffer, sizeof(buffer), "Error: Invalid file path.\n");
        send(client_socket, buffer, strlen(buffer), 0);
        return;
    }

    // Remove any trailing newline characters
    char *newline = strchr(file_path, '\n');
    if (newline)
        *newline = '\0';

    // Expand the path
    char *expanded_path = expand_path(file_path);
    if (expanded_path == NULL)
    {
        snprintf(buffer, sizeof(buffer), "Error: Unable to expand file path.\n");
        send(client_socket, buffer, strlen(buffer), 0);
        return;
    }
    // Handle different file types based on extension
    if (strcmp(file_ext, ".c") == 0)
    {
        send_file(client_socket, expanded_path);
    }
    else if (strcmp(file_ext, ".pdf") == 0)
    {
        // Replace "smain" with "spdf" in the path and request the file
        char *spdf_path = replace_smain_with_spdf(expanded_path);
        request_and_forward_file(client_socket, spdf_path, "spdf", SPDF_PORT);
        free(spdf_path);
    }
    else if (strcmp(file_ext, ".txt") == 0)
    {
        // Replace "smain" with "stext" in the path and request the file

        char *stext_path = replace_smain_with_stext(expanded_path);
        request_and_forward_file(client_socket, stext_path, "stext", STEXT_PORT);
        free(stext_path);
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "Error: Unsupported file type.\n");
        send(client_socket, buffer, strlen(buffer), 0);
    }

    free(expanded_path); // Free the allocated memory
}
char *replace_smain_with_stext(const char *path)
{
    char *new_path = strdup(path); // Duplicate the path
    if (new_path == NULL)
    {
        return NULL; // Return NULL if memory allocation fails
    }
    // Replace "smain" with "stext" in the duplicated path
    char *smain_pos = strstr(new_path, "smain");
    if (smain_pos != NULL)
    {
        memcpy(smain_pos, "stext", 5);
    }

    return new_path;
}

char *replace_smain_with_spdf(const char *path)
{
    char *new_path = strdup(path); // Duplicate the path
    if (new_path == NULL)
    {
        return NULL; // Return NULL if memory allocation fails
    }
    // Replace "smain" with "spdf" in the duplicated path
    char *smain_pos = strstr(new_path, "smain");
    if (smain_pos != NULL)
    {
        memcpy(smain_pos, "spdf", 4);
        memmove(smain_pos + 4, smain_pos + 5, strlen(smain_pos + 5) + 1); // Adjust the rest of the path
    }

    return new_path; // Return the modified path
}

void handle_rmfile(int client_socket, char *filepath)
{
    char *file_ext = strrchr(filepath, '.'); // Get the file extension
    if (file_ext == NULL)
    {
        send(client_socket, "Error: Invalid file extension", 29, 0); // Send error message for missing file extension
        return;
    }

    // Expand the path
    char *expanded_path = expand_path(filepath);
    if (expanded_path == NULL)
    {
        send(client_socket, "Error: Unable to expand file path", 33, 0); // Send error message for path expansion failure
        return;
    }

    if (strcmp(file_ext, ".c") == 0)
    {
        // Handle .c file locally
        if (remove(expanded_path) == 0)
        {
            send(client_socket, "File deleted successfully\n", 25, 0); // Send success message
        }
        else
        {
            send(client_socket, "Error deleting file", 19, 0); // Send error message for deletion failure
        }
    }
    else if (strcmp(file_ext, ".txt") == 0)
    {
        // Forward request to Stext
        if (forward_delete_request(client_socket, expanded_path, STEXT_PORT) == 0)
        {
            send(client_socket, "File deletion request forwarded to Stext", 39, 0); // Send success message
        }
        else
        {
            send(client_socket, "Error forwarding delete request to Stext", 39, 0); // Send error message for forwarding failure
        }
    }
    else if (strcmp(file_ext, ".pdf") == 0)
    {
        // Forward request to Spdf
        if (forward_delete_request(client_socket, expanded_path, SPDF_PORT) == 0)
        {
            send(client_socket, "File deletion request forwarded to Spdf", 38, 0); // Send success message
        }
        else
        {
            send(client_socket, "Error forwarding delete request to Spdf", 38, 0); // Send error message for forwarding failure
        }
    }
    else
    {
        send(client_socket, "Error: Unsupported file type", 28, 0); // Send error message for unsupported file type
    }

    free(expanded_path); // Free the allocated memory
}

int forward_delete_request(int client_socket, const char *filepath, int port)
{
    // Create a socket for connecting to the server
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        perror("Socket creation failed");
        return -1;
    }
    // Set up server address structure
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to the server
    if (connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        close(server_socket);
        return -1;
    }
    // Prepare the delete command
    char command[MAX_BUFFER];
    snprintf(command, sizeof(command), "rmfile %s", filepath);
    // Send the delete command to the server
    if (send(server_socket, command, strlen(command), 0) < 0)
    {
        perror("Send failed");
        close(server_socket);
        return -1;
    }
    printf("Command sent to server: %s\n", command);
    // Receive and forward the server's response to the client
    char response[MAX_BUFFER];
    ssize_t bytes_received = recv(server_socket, response, sizeof(response) - 1, 0);
    if (bytes_received > 0)
    {
        response[bytes_received] = '\0';
        printf("Server response received: %s\n", response);
        send(client_socket, response, strlen(response), 0);
    }
    else if (bytes_received == 0)
    {
        printf("Connection closed by server\n");
        send(client_socket, "Error: No response from server", 30, 0);
    }
    else
    {
        perror("Error receiving response from server");
        send(client_socket, "Error: No response from server", 30, 0);
    }

    close(server_socket);
    return 0;
}

void receive_and_forward_file(int server_socket, int client_socket, const char *filename)
{
    FILE *file = fopen(filename, "wb"); // Open the file for writing
    if (!file)
    {
        perror("Error opening file for writing");
        return;
    }

    char buffer[MAX_BUFFER];
    ssize_t bytes_received;
    while ((bytes_received = recv(server_socket, buffer, sizeof(buffer), 0)) > 0)
    {
        fwrite(buffer, 1, bytes_received, file); // Write received data to the file
    }
    fclose(file); // Close the file
}

void handle_dtar(int client_socket, char *file_extension)
{
    fflush(stdout); // Flush stdout to ensure all output is written

    char tar_filename[20];
    char command[MAX_BUFFER];
    int server_port;
    // Determine the tar filename and server port based on file extension
    if (strcmp(file_extension, ".c") == 0)
    {
        snprintf(tar_filename, sizeof(tar_filename), "c.tar");
        snprintf(command, sizeof(command), "tar -cvf %s -C ~/smain $(find ~/smain -name '*.c')", tar_filename);
        // fflush(stdout);
        system(command); // Execute tar command to create a tarball of .c files
    }
    else if (strcmp(file_extension, ".pdf") == 0 || strcmp(file_extension, ".txt") == 0)
    {
        if (strcmp(file_extension, ".pdf") == 0)
        {
            snprintf(tar_filename, sizeof(tar_filename), "pdf.tar");
            server_port = SPDF_PORT;
        }
        else if (strcmp(file_extension, ".txt") == 0)
        {
            snprintf(tar_filename, sizeof(tar_filename), "txt.tar");
            server_port = STEXT_PORT;
        }
        // Create tarball file
        snprintf(command, sizeof(command), "dtar %s", file_extension);
        fflush(stdout);

        // Connect to the appropriate server
        int server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0)
        {
            perror("Error creating socket for server connection");
            send(client_socket, "Error: Unable to connect to server", 34, 0);
            return;
        }

        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            perror("Error connecting to server");
            send(client_socket, "Error: Unable to connect to server", 34, 0);
            close(server_socket);
            return;
        }

        // Send tar command to server
        if (send(server_socket, command, strlen(command), 0) < 0)
        {
            perror("Error sending command to server");
            close(server_socket);
            send(client_socket, "Error: Unable to send command to server", 38, 0);
            return;
        }

        // Receive tar file from server
        FILE *tar_file = fopen(tar_filename, "wb");
        if (tar_file == NULL)
        {
            perror("Error creating tar file");
            send(client_socket, "Error: Unable to create tar file", 32, 0);
            close(server_socket);
            return;
        }

        char buffer[MAX_BUFFER];
        ssize_t bytes_received, bytes_sent;
        size_t total_bytes_received = 0;
        size_t total_bytes_sent = 0;
        // Receive data from the Stext or Spdf server and write it to the tar file
        while ((bytes_received = recv(server_socket, buffer, sizeof(buffer), 0)) > 0)
        {
            fwrite(buffer, 1, bytes_received, tar_file);
            total_bytes_received += bytes_received;
              printf("Received %zd bytes from server, total: %zu\n", bytes_received, total_bytes_received);
            fflush(stdout);

            // Send the received data to the client
            bytes_sent = send(client_socket, buffer, bytes_received, 0);
            if (bytes_sent < 0)
            {
                perror("Error sending data to client");
                fclose(tar_file);
                close(server_socket);
                return;
            }
            total_bytes_sent += bytes_sent;
              printf("Sent %zd bytes to client, total: %zu\n", bytes_sent, total_bytes_sent);
            // fflush(stdout);
        }

           printf("Total bytes received from server: %zu\n", total_bytes_received);

        fflush(stdout);

        if (total_bytes_received == 0)
        {
            printf("Warning: No data received from server\n");
            fflush(stdout);
            send(client_socket, "Error: No data received from server", 35, 0);
            remove(tar_filename);
            return;
        }

        // Handle any error in receiving data from the server
        if (bytes_received < 0)
        {
            perror("Error receiving file data from server");
        }
        else
        {
            printf("File transfer complete. Total bytes received from server: %zu\n", total_bytes_received);
        }

        // Clean up: close the file and the server socket
        fclose(tar_file);
        close(server_socket);

        // Final message to client
        if (total_bytes_received > 0)
        {
            printf("Tar file successfully transferred to client. Total bytes sent: %zu\n", total_bytes_sent);
        }
        else
        {
            send(client_socket, "Error: No data received from server", 35, 0);
        }
    }
    else
    {
        send(client_socket, "Error: Invalid file extension for dtar", 37, 0);
        return;
    }
}

void handle_display(int client_socket, char *pathname)
{
    // Expand the given path to handle user directory shortcuts
    char *expanded_path = expand_path(pathname);
    if (expanded_path == NULL) // Check if path expansion was successful
    {
        send(client_socket, "Error: Invalid path", 20, 0);
        printf("Error: Invalid path\n");
        return;
    }

    // Get .c files
    char c_files[MAX_BUFFER] = "";
    DIR *dir = opendir(expanded_path);
    if (dir)
    {
        struct dirent *entry;
        // Iterate through directory entries
        while ((entry = readdir(dir)) != NULL)
        {
            if (entry->d_type == DT_REG && strstr(entry->d_name, ".c")) // Check if entry is a regular file with a .c extension
            {
                strcat(c_files, entry->d_name);
                strcat(c_files, "\n");
            }
        }
        closedir(dir); // Close the directory stream
    }
    else
    {
        printf("Error opening directory for .c files: %s\n", strerror(errno));
    }
    //  printf("C files found:\n%s", c_files);

    // Get .pdf files from spdf
    char pdf_files[MAX_BUFFER] = "";
    if (get_files_from_spdf(expanded_path, pdf_files) != 0)
    {
        printf("Error getting .pdf files from spdf\n");
    }
    //   printf("PDF files found:\n%s", pdf_files);

    // Get .txt files from stext
    char txt_files[MAX_BUFFER] = "";
    if (get_files_from_stext(expanded_path, txt_files) != 0)
    {
        printf("Error getting .txt files from stext\n");
    }
    //   printf("TXT files found:\n%s", txt_files);

    // Combine all files
    char all_files[MAX_BUFFER * 3];
    snprintf(all_files, sizeof(all_files), "%s%s%s", c_files, pdf_files, txt_files);
    printf("\nAll files to be sent to client:\n\n%s", all_files);

    if (send(client_socket, all_files, strlen(all_files), 0) < 0)
    {
        perror("Error sending file list to client");
    }
    else
    {
        //  printf("File list sent to client successfully\n");
    }
    free(expanded_path);
}

int get_files_from_stext(const char *pathname, char *txt_files)
{ // Create a socket for communication with the Stext server
    int stext_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (stext_socket < 0)
    {
        perror("Error creating socket for Stext");
        return -1;
    }
    // Set up server address structure
    struct sockaddr_in stext_addr;
    stext_addr.sin_family = AF_INET;
    stext_addr.sin_port = htons(STEXT_PORT);
    stext_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    // Connect to the Stext server
    if (connect(stext_socket, (struct sockaddr *)&stext_addr, sizeof(stext_addr)) < 0)
    {
        perror("Error connecting to Stext server");
        close(stext_socket);
        return -1;
    }
    // Replace "smain" with "stext" in the path
    char *stext_path = replace_smain_with_stext(pathname);
    char command[MAX_BUFFER];
    snprintf(command, sizeof(command), "list %s", stext_path);
    free(stext_path);

    //  printf("Sending command to stext: %s\n", command);
    // Send the command to the Stext server
    if (send(stext_socket, command, strlen(command), 0) < 0)
    {
        perror("Error sending command to Stext server");
        close(stext_socket);
        return -1;
    }
    // Receive the response from the Stext server
    ssize_t bytes_received = recv(stext_socket, txt_files, MAX_BUFFER - 1, 0);
    if (bytes_received > 0)
    {
        txt_files[bytes_received] = '\0';
        printf("Received .txt files:\n%s", txt_files);
    }
    else if (bytes_received == 0)
    {
        printf("Stext server closed the connection\n");
        strcpy(txt_files, "");
    }
    else
    {
        perror("Error receiving data from Stext server");
        strcpy(txt_files, "");
    }

    close(stext_socket);
    return 0;
}

int get_files_from_spdf(const char *pathname, char *pdf_files)
{ // Create a socket for communication with the Spdf server
    int spdf_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (spdf_socket < 0)
    {
        perror("Error creating socket for Spdf");
        return -1;
    }
    // Set up server address structure
    struct sockaddr_in spdf_addr;
    spdf_addr.sin_family = AF_INET;
    spdf_addr.sin_port = htons(SPDF_PORT);
    spdf_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    // Connect to the Spdf server
    if (connect(spdf_socket, (struct sockaddr *)&spdf_addr, sizeof(spdf_addr)) < 0)
    {
        perror("Error connecting to Spdf server");
        close(spdf_socket);
        return -1;
    }
    // Replace "smain" with "spdf" in the path
    char *spdf_path = replace_smain_with_spdf(pathname);
    char command[MAX_BUFFER];
    snprintf(command, sizeof(command), "list %s", spdf_path);
    free(spdf_path);

    // printf("Sending command : %s\n", command);
    // Send the command to the Spdf server
    if (send(spdf_socket, command, strlen(command), 0) < 0)
    {
        perror("Error sending command to Spdf server");
        close(spdf_socket);
        return -1;
    }
    // Receive the response from the Spdf server
    ssize_t bytes_received = recv(spdf_socket, pdf_files, MAX_BUFFER - 1, 0);
    if (bytes_received > 0)
    {
        pdf_files[bytes_received] = '\0'; // Null-terminate the received data
        printf("Received .pdf files:\n%s", pdf_files);
    }
    else
    {
        strcpy(pdf_files, ""); // Clear the buffer if no files are received
        printf("No .pdf files received from spdf\n");
    }

    close(spdf_socket);
    return 0;
}
