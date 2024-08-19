// Include necessary header files for the program
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>

#define MAX_BUFFER 1000024 // Maximum buffer size for I/O operations
#define SMAIN_PORT 4530 // Port number for server connection
#define CHUNK_SIZE 8192 // Size of data chunks to send or receive

// Function prototypes
void send_file(int socket, const char *filename);
void receive_file(int socket, const char *filename);
int validate_command(char *command, char *args);
void handle_display(int client_socket, const char *pathname);
void receive_tar_file(int socket, const char *filename);

// Signal handler for segmentation faults
void segfault_handler(int signal)
{
    fprintf(stderr, "Caught segmentation fault!\n");
    exit(1);
}

int main()
{
    signal(SIGSEGV, segfault_handler);
    int client_socket;
    struct sockaddr_in server_addr;
    char buffer[MAX_BUFFER];
    char *server_ip = "127.0.0.1";
    ssize_t bytes_sent, bytes_received;

    while (1)
    {
        // Create a new socket for each request
        client_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (client_socket < 0)
        {
            perror("Error creating socket");
            continue; // Try again for the next command
        }

        // Set up server address structure
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(SMAIN_PORT);
        server_addr.sin_addr.s_addr = inet_addr(server_ip);

        // Connect to the server
        if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            perror("Error connecting to server");
            close(client_socket);
            continue; // Try again for the next command
        }

        // Prompt user for input
        printf("client24s$ ");
        fflush(stdout);
        if (fgets(buffer, MAX_BUFFER, stdin) == NULL)
        {
            perror("Error reading input");
            break;
        }
        buffer[strcspn(buffer, "\n")] = 0; // Remove newline

        // Exit the loop if the command is "exit"
        if (strcmp(buffer, "exit") == 0)
        {
            break;
        }

        char command_copy[MAX_BUFFER];
        strcpy(command_copy, buffer);              // Copy the input buffer to command_copy
        char *command = strtok(command_copy, " "); // Extract command from input
        char *args = strtok(NULL, "");             // Extract arguments from input

        // Validate the command syntax
        if (!validate_command(command, args))
        {
            printf("Invalid command syntax\n");
            continue;
        }

        // Send command to the server
        bytes_sent = send(client_socket, buffer, strlen(buffer), 0);
        if (bytes_sent < 0)
        {
            perror("Error sending message");
            continue;
        }

        // Handle specific commands
        if (strcmp(command, "ufile") == 0)
        {
            char *filename = strtok(args, " ");
            if (access(filename, F_OK) == -1)
            {
                printf("Error: File %s does not exist\n", filename);
                continue;
            }
            send_file(client_socket, filename); // Send file to the server
        }
        else if (strcmp(command, "rmfile") == 0)
        {
            char response[MAX_BUFFER];
            int bytes_received = recv(client_socket, response, MAX_BUFFER - 1, 0);
            if (bytes_received > 0)
            {
                response[bytes_received] = '\0';
                printf("%s\n", response); // Print server response
            }
            else if (bytes_received == 0)
            {
                printf("Server closed the connection.\n");
            }
            else
            {
                perror("Error receiving server response");
            }
            continue; // Move to the next command
        }
        else if (strcmp(command, "dfile") == 0)
        {
            char *filename = strtok(args, " ");
            if (filename != NULL)
            {
                char *base_filename = strrchr(filename, '/');
                base_filename = (base_filename == NULL) ? filename : base_filename + 1;
                receive_file(client_socket, base_filename); // Receive file from the server
            }
            else
            {
                printf("Error: No filename specified for download.\n");
            }
            continue;
        }
        else if (strncmp(command, "dtar", 4) == 0)
        {
            if (args != NULL)
            {
                char dtar_command[MAX_BUFFER];
                snprintf(dtar_command, sizeof(dtar_command), "dtar %s", args);
                send(client_socket, dtar_command, strlen(dtar_command), 0);

                char tar_filename[20];
                snprintf(tar_filename, sizeof(tar_filename), "%s.tar", args + 1);
                receive_tar_file(client_socket, tar_filename); // Receive tar file from the server
            }
            else
            {
                printf("Error: No file extension specified for dtar.\n");
            }
        }
     else if (strncmp(buffer, "display", 7) == 0) {
    char *pathname = buffer + 8;  // Skip "display " (7 characters + 1 space)
    if (strlen(pathname) > 0) {
        handle_display(client_socket, pathname);
    } else {
        printf("Invalid display command format\n");
    }
   continue;  // Skip the general response handling
}
        else
        {
            printf("Unknown command\n");
            continue;
        }
        // continue;

        // Receive and print the server's response
        char response[MAX_BUFFER];
        int bytes_received = recv(client_socket, response, MAX_BUFFER - 1, 0);
        if (bytes_received > 0)
        {
            response[bytes_received] = '\0';
        }
        else if (bytes_received == 0)
        {
            printf("Server closed the connection.\n");
        }
        else
        {
            perror("Error receiving server response"); // Print error message if receiving fails
        }

        buffer[bytes_received] = '\0';
        // Close the socket
        close(client_socket);
        printf("\n");
    }

    return 0;
}
// Validate the command and arguments
int validate_command(char *command, char *args)
{
    if (strcmp(command, "ufile") == 0)
    {
        char *filename = strtok(args, " ");
        char *path = strtok(NULL, " ");
        return (filename != NULL && path != NULL && strstr(path, "~/smain") == path);
    }
    else if (strcmp(command, "rmfile") == 0)
    {
        return (args != NULL && strstr(args, "~/smain") == args &&
                (strstr(args, ".c") || strstr(args, ".txt") || strstr(args, ".pdf")));
    }
    else if (strcmp(command, "dfile") == 0)
    {
        return (args != NULL && strstr(args, "~/smain") == args &&
                (strstr(args, ".c") || strstr(args, ".txt") || strstr(args, ".pdf")));
    }
    else if (strcmp(command, "dtar") == 0)
    {
        return (args != NULL && (strcmp(args, ".c") == 0 || strcmp(args, ".txt") == 0 || strcmp(args, ".pdf") == 0));
    }
    else if (strcmp(command, "display") == 0)
    {
        return (args != NULL && strstr(args, "~/smain") == args);
    }
    return 0;
}
// Send a file to the server
void send_file(int client_socket, const char *file_path)
{
    FILE *file = fopen(file_path, "rb"); // Open file in binary read mode
    if (!file)
    {
        perror("Failed to open file");
        send(client_socket, "Error: File not found.\n", 23, 0);
        return;
    }

    fseek(file, 0, SEEK_END); // Move to the end of the file to get its size
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET); // Move back to the beginning of the file

    char size_buffer[32];
    snprintf(size_buffer, sizeof(size_buffer), "%ld", file_size);
    send(client_socket, size_buffer, strlen(size_buffer), 0); // Send file size to the server

    char buffer[CHUNK_SIZE];
    size_t bytes_read;
    long total_sent = 0;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        ssize_t bytes_sent = send(client_socket, buffer, bytes_read, 0);
        if (bytes_sent < 0)
        {
            perror("Error sending file data"); // Print error message if sending data fails
            break;
        }
        total_sent += bytes_sent;
    }

    fclose(file);

    if (total_sent == file_size)
    {
        printf("File sent successfully: %s\n", file_path);
        send(client_socket, "File sent successfully.\n", 24, 0);
    }
    else
    {
        printf("Error: Incomplete file transfer. Sent %ld/%ld bytes\n", total_sent, file_size);
        send(client_socket, "Error: Incomplete file transfer.\n", 32, 0);
    }
}
// Receive a file from the server
void receive_file(int socket, const char *filename)
{
    char size_str[32]; // Buffer to hold the size of the file as a string
                       // Receive the size of the file from the server
    ssize_t size_received = recv(socket, size_str, sizeof(size_str) - 1, 0);
    if (size_received <= 0)
    {
        fprintf(stderr, "Error receiving file size\n");
        return;
    }
    size_str[size_received] = '\0'; // Null-terminate the size string
    size_t file_size = strtoull(size_str, NULL, 10);

    // Send acknowledgment
    send(socket, "ACK", 3, 0);

    int file = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file < 0)
    {
        perror("Error opening file for writing");
        return;
    }
    // Buffer to hold data received from the server
    char buffer[MAX_BUFFER];
    size_t total_received = 0;
    // Loop until the entire file is received
    while (total_received < file_size)
    {
        // Determine the number of bytes to receive in this iteration
        ssize_t bytes_to_receive = (file_size - total_received < sizeof(buffer)) ? (file_size - total_received) : sizeof(buffer);
        // Receive data from the server
        ssize_t bytes_received = recv(socket, buffer, bytes_to_receive, 0);
        if (bytes_received <= 0)
        {
            if (bytes_received < 0)
            {
                perror("Error receiving file data");
            }
            break;
        }
        // Write the received data to the file
        ssize_t bytes_written = write(file, buffer, bytes_received);
        if (bytes_written != bytes_received) // Check if writing data to the file was successful
        {
            fprintf(stderr, "Error writing to file\n");
            break;
        }
        total_received += bytes_received; // Update the total received data count
    }

    close(file);
    // Check if the entire file was received
    if (total_received == file_size)
    {
        printf("File downloaded successfully: %s\n", filename);
    }
    else
    {
        printf("Error: Incomplete file transfer. Received %zu/%zu bytes\n", total_received, file_size);
        // Remove the incomplete file
        unlink(filename);
    }

    // Wait for server's completion message
    char server_response[MAX_BUFFER];
    ssize_t response_size = recv(socket, server_response, sizeof(server_response) - 1, 0);
    if (response_size > 0)
    {
        server_response[response_size] = '\0'; // Null-terminate the server response
    }
    else
    {
        printf("No response from server after file transfer.\n");
    }
}

void handle_display(int client_socket, const char *pathname)
{
    char command[MAX_BUFFER];
    snprintf(command, sizeof(command), "display %s", pathname);

    if (send(client_socket, command, strlen(command), 0) < 0)
    {
        perror("Error sending display command");
        return;
    }

    char response[MAX_BUFFER];
    ssize_t bytes_received = recv(client_socket, response, sizeof(response) - 1, 0);
    if (bytes_received > 0)
    {
        response[bytes_received] = '\0';
        printf("Files in %s:\n%s", pathname, response);
    }
    else
    {
        printf("Error receiving display response\n");
    }
}
// Receive a tar file from the server
void receive_tar_file(int server_socket, const char *filename)
{
    // After sending the "dtar .txt" command
    char size_buffer[32];
    ssize_t size_received = recv(server_socket, size_buffer, sizeof(size_buffer) - 1, 0);
    if (size_received <= 0)
    {
        perror("Error receiving file size");
        return;
    }
    size_buffer[size_received] = '\0';
    size_t expected_size = atoll(size_buffer);

    // Then proceed with receiving the file data

    FILE *file = fopen(filename, "wb"); // Open file in binary write mode
    if (file == NULL)
    {
        perror("Error opening file for writing");
        return;
    }
    // Buffer to hold data received from the server
    char buffer[MAX_BUFFER];
    size_t total_received = 0;
    // Loop until the entire file is received
    while (total_received < expected_size)
    {
        ssize_t bytes_received = recv(server_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) // Check if receiving data was successful
        {
            if (bytes_received == 0)
            {
                printf("Connection closed by server\n");
            }
            else
            {
                perror("recv failed");
            }
            break;
        }
        fwrite(buffer, 1, bytes_received, file); // Write received data to file
        total_received += bytes_received;
    }
    // Check if the entire file was received
    if (total_received == expected_size)
    {
        printf("Tar file downloaded successfully: %s\n", filename);
    }
    else
    {
        printf("Error: Incomplete file transfer. Received %zu/%zu bytes\n", total_received, expected_size);
    }
}
