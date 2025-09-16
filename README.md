
# C RAT with OS-Specific Prompts

A Remote Access Tool (RAT) written in C that provides OS-specific command prompts and supports all native operating system commands with enhanced file transfer capabilities.

## Table of Contents
- [Features](#features)
- [Installation](#installation)
- [Usage](#usage)
- [File Overview](#file-overview)
- [Examples](#examples)
- [Security Note](#security-note)
- [Contributing](#contributing)
- [License](#license)
- [Contact](#contact)

## Features
- **OS-Specific Prompts**: Displays proper command prompts for Linux (`[user@hostname dir]$`) and Windows (`C:\path\>`)
- **Native Command Support**: All standard OS commands work directly (ls, pwd, cd, ps, netstat, dir, type, etc.)
- **File Transfer**: Upload and download files between server and client
- **Cross-Platform**: Compatible with both Linux and Windows systems
- **Dynamic Directory Tracking**: Prompt updates automatically when changing directories
- **Seamless Integration**: Works like a native terminal session

## Installation
Clone the repository and compile the binaries:

```bash
git clone https://github.com/elxecutor/RAT.git
cd RAT
gcc -o server server.c
gcc -o client client.c
```

## Usage

### Starting the Server
1. Run the server on the controller machine:
   ```bash
   ./server
   ```

### Connecting the Client
2. Run the client on the target machine:
   ```bash
   ./client
   ```

### Basic Commands
3. Execute commands from the server interface:
   ```bash
   >> ls -la
   >> cd /tmp
   >> pwd
   >> whoami
   >> download /etc/passwd
   >> upload malware.exe
   >> exit
   ```

## File Overview

### Core Files
- `server.c` - Controller application that sends commands to clients
- `client.c` - Agent application that runs on target systems and executes commands
- `README.md` - Project documentation
- `.gitignore` - Git ignore rules for compiled binaries and temporary files

### Key Functions
- **OS Detection**: Automatically detects Linux vs Windows environment
- **Prompt Generation**: Creates appropriate command prompts for each OS
- **Command Execution**: Handles all native OS commands plus custom upload/download
- **File Transfer**: Secure file upload and download functionality

## Examples

### Example Server Session
```
======================================================
                    C RAT Server                     
======================================================
Commands:
======================================================
System:
  help                    show this help menu
  <any_command>           execute any OS command
  exit                    terminate session

Files:
  download <file>         download file from client
  upload <file>           upload file to client

Notes:
  All standard OS commands work (ls, pwd, cd, etc.)
  Windows commands work too (dir, type, etc.)
  The prompt shows the current OS and directory
======================================================
Waiting for client to connect...
[atsuomi@executor rat]$ 

>> ls
client    client.c    README.md    server    server.c
[atsuomi@executor rat]$ 

>> cd /tmp
[atsuomi@executor tmp]$ 

>> pwd
/tmp
[atsuomi@executor tmp]$ 

>> download /etc/passwd
Downloading file: /etc/passwd
File downloaded successfully as: ./passwd
File sent for download
[atsuomi@executor tmp]$ 

>> exit
Terminating connection...
```

### Key Changes Made
1. **OS Detection**: Automatically detects Linux vs Windows
2. **Dynamic Prompts**: Shows current user, hostname, and directory
3. **Simplified Commands**: Removed custom command handlers except upload/download
4. **Native Command Execution**: All OS commands work directly
5. **Better Integration**: Seamless command-line experience
6. **Fixed Command/Response Flow**: Every command gets a proper response
7. **Improved Error Handling**: Better handling of failed commands and connections
8. **Enhanced cd Command**: Proper directory change handling with prompt updates

## Security Note

**⚠️ IMPORTANT DISCLAIMER ⚠️**

This tool is designed for **educational purposes and authorized penetration testing only**. 

- Only use this tool on systems you own or have explicit written permission to test
- Unauthorized access to computer systems is illegal and unethical
- The authors are not responsible for any misuse of this software
- Always comply with local laws and regulations
- Use responsibly and ethically

## Contributing
We welcome contributions! Please see our [Contributing Guidelines](CONTRIBUTING.md) and [Code of Conduct](CODE_OF_CONDUCT.md) for details.

## License
This project is licensed under the [MIT License](LICENSE).

## Contact
For questions or support, please open an issue or contact the maintainer via [X](https://x.com/elxecutor/).
