
# FileSync3: Distributed File System

FileSync3 is a distributed file system that enables seamless file management between a client and three servers (Smain, Spdf, and Stext). The system supports multiple client connections, allowing users to upload, download, and manage files across different servers. While clients interact only with the Smain server, Smain handles the distribution of files to the appropriate servers in the background.

## Features
- **Upload Files:** Clients can upload `.c`, `.pdf`, and `.txt` files to the Smain server.
- **Download Files:** Clients can download files from Smain.
- **Remove Files:** Clients can delete files stored on Smain and retrieve them to their local directory.
- **Create Tar Files:** Clients can create a tar archive of specific file types and download it from Smain.
- **Path Display:** Clients can display the full path of a file.

## Server Distribution
- `.c` files are stored locally on Smain.
- `.pdf` files are transferred to the Spdf server.
- `.txt` files are transferred to the Stext server.

All file transfers to Spdf and Stext are handled by Smain, and clients are unaware of the presence of these servers.

## Installation and Setup
1. **Clone the Repository:**
    ```bash
    git clone https://github.com/dhruvmaradiya/FileSync3-Distributed-File-System.git
    cd FileSync3-Distributed-File-System
    ```

2. **Compile the Servers and Client:**
    ```bash
    gcc -o smain Smain.c
    gcc -o spdf Spdf.c
    gcc -o stext Stext.c
    gcc -o client client.c
    ```

3. **Start the Servers:**
    - Start Smain server:
      ```bash
      ./smain
      ```
    - Start Spdf server:
      ```bash
      ./spdf
      ```
    - Start Stext server:
      ```bash
      ./stext
      ```

4. **Run the Client:**
    ```bash
    ./client
    ```

## Usage
- **Upload a File:**
    ```bash
    ufile sample.txt /destination/path/
    ufile sample.c /destination/path/
    ufile sample.pdf /destination/path/
    ```
- **Download a File:**
    ```bash
    dfile sample.txt
    ```
- **Remove a File:**
    ```bash
    rmfile sample.txt
    ```
- **Create and Download a Tar File:**
    ```bash
    dtar .c
    dtar .pdf
    dtar .txt
    ```
- **Display Path**
    ```bash
    display pathname
    ```

## Sample Files
For testing, you can use the following sample files:
- `sample.txt`
- `sample.c`
- `sample.pdf`

These files can be uploaded, downloaded, and managed using the client commands.

## Project Structure
- `smain.c` - Handles client connections and manages the distribution of files.
- `spdf.c` - Manages the storage of PDF files.
- `stext.c` - Manages the storage of text files.
- `client.c` - Client program to interact with the Smain server.

## License
This project is licensed under the MIT License.
