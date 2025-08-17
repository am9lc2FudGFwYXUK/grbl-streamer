#include <iostream>
#include <fstream>
#include <string>
#include <unistd.h>    // for write, read, close, getopt
#include <getopt.h>    // for getopt_long
#include <fcntl.h>     // for open
#include <termios.h>   // for termios
#include <cstring>     // for memset
#include <chrono>      // for sleep
#include <thread>      // for sleep
#include <algorithm>   // for std::transform
#include <sys/select.h> // for select
#include <queue>       // for std::queue

// GRBL RX buffer size (effective available space is 127)
const int RX_BUFFER_SIZE = 127;

// Function to map integer baudrate to speed_t constant
speed_t get_baudrate(int baud) {
    switch (baud) {
        case 50: return B50;
        case 75: return B75;
        case 110: return B110;
        case 134: return B134;
        case 150: return B150;
        case 200: return B200;
        case 300: return B300;
        case 600: return B600;
        case 1200: return B1200;
        case 1800: return B1800;
        case 2400: return B2400;
        case 4800: return B4800;
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default:
            std::cerr << "Unsupported baudrate: " << baud << std::endl;
            exit(1);
    }
    return B0;  // Unreachable
}

// Function to read a line from the serial port (until \n) using polling with select
// For indefinite wait, timeout is NULL
std::string readSerialLine(int fd, bool verbose) {
    std::string line;
    while (true) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        // Indefinite timeout (wait forever)
        int retval = select(fd + 1, &rfds, nullptr, nullptr, nullptr);
        if (retval == -1) {
            perror("select");
            break;
        } else if (retval == 0) {
            // Should not happen with NULL timeout
            break;
        } else {
            if (FD_ISSET(fd, &rfds)) {
                char buf;
                int n = read(fd, &buf, 1);
                if (n > 0) {
                    line += buf;
                    if (buf == '\n') {
                        break;
                    }
                } else if (n == 0) {
                    // EOF
                    break;
                } else {
                    perror("read");
                    break;
                }
            }
        }
    }
    return line;
}

// Function to print help
void printHelp(const char* progName) {
    std::cout << "Usage: " << progName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -S, --serial <device>    Serial device (e.g., /dev/ttyUSB0)" << std::endl;
    std::cout << "  -f, --file <gcode>       G-code file to stream" << std::endl;
    std::cout << "  -b, --baud <rate>        Baudrate (default: 115200)" << std::endl;
    std::cout << "  -v, --verbose            Enable verbose output" << std::endl;
    std::cout << "  -h, --help               Display this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Example: " << progName << " -S /dev/ttyUSB0 -f example.gcode -b 115200 -v" << std::endl;
}

int main(int argc, char* argv[]) {
    const char* serial_device = nullptr;
    const char* gcode_file_path = nullptr;
    int baud_int = 115200;  // Default baudrate as int
    bool verbose = false;

    // Define long options
    static struct option long_options[] = {
        {"serial", required_argument, nullptr, 'S'},
        {"file", required_argument, nullptr, 'f'},
        {"baud", required_argument, nullptr, 'b'},
        {"verbose", no_argument, nullptr, 'v'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "S:f:b:vh", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'S':
                serial_device = optarg;
                break;
            case 'f':
                gcode_file_path = optarg;
                break;
            case 'b':
                baud_int = std::stoi(optarg);  // Convert string to int for baudrate
                break;
            case 'v':
                verbose = true;
                break;
            case 'h':
                printHelp(argv[0]);
                return 0;
            default:
                printHelp(argv[0]);
                return 1;
        }
    }

    // If no arguments provided, print help
    if (argc == 1) {
        printHelp(argv[0]);
        return 0;
    }

    // Check required arguments
    if (serial_device == nullptr || gcode_file_path == nullptr) {
        std::cerr << "Error: Serial device and G-code file are required." << std::endl;
        printHelp(argv[0]);
        return 1;
    }

    if (verbose) {
        std::cout << "Opening serial port: " << serial_device << std::endl;
    }
    // Open the serial port non-blocking
    int fd = open(serial_device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd == -1) {
        std::cerr << "Error opening serial port: " << serial_device << std::endl;
        return 1;
    }
    if (verbose) {
        std::cout << "Serial port opened successfully." << std::endl;
        std::cout << "Configuring serial port at baudrate: " << baud_int << std::endl;
    }

    // Get speed_t from int
    speed_t baudrate = get_baudrate(baud_int);

    // Configure the serial port
    struct termios options;
    memset(&options, 0, sizeof(options));
    tcgetattr(fd, &options);
    cfsetispeed(&options, baudrate);
    cfsetospeed(&options, baudrate);
    options.c_cflag = (CLOCAL | CREAD | CS8);  // 8N1, no parity, 1 stop bit
    options.c_iflag = IGNPAR;                  // Ignore parity errors
    options.c_oflag = 0;
    options.c_lflag = 0;                       // Non-canonical mode
    options.c_cc[VTIME] = 0;                   // No timeout
    options.c_cc[VMIN] = 1;                    // Read at least 1 char
    tcflush(fd, TCIFLUSH);
    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        std::cerr << "Error setting serial attributes." << std::endl;
        close(fd);
        return 1;
    }
    if (verbose) {
        std::cout << "Serial port configured successfully." << std::endl;
        std::cout << "Waking up GRBL..." << std::endl;
    }

    // Wake up GRBL
    write(fd, "\r\n\r\n", 4);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    tcflush(fd, TCIFLUSH);  // Flush any startup text
    if (verbose) {
        std::cout << "GRBL woken up." << std::endl;
    }

    // Read and echo any initial response after wakeup (with timeout for initial)
    {
        struct timeval tv_initial;
        tv_initial.tv_sec = 1;  // 1 second timeout for initial response
        tv_initial.tv_usec = 0;
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        int retval = select(fd + 1, &rfds, nullptr, nullptr, &tv_initial);
        if (retval > 0) {
            std::string initial_response = readSerialLine(fd, verbose);  // Temporary use with indefinite, but since data ready
            if (!initial_response.empty()) {
                std::cout << "Initial GRBL response: " << initial_response;
            }
        }
    }

    if (verbose) {
        std::cout << "Opening G-code file: " << gcode_file_path << std::endl;
    }
    // Open the G-code file
    std::ifstream gcode_file(gcode_file_path);
    if (!gcode_file.is_open()) {
        std::cerr << "Error opening G-code file: " << gcode_file_path << std::endl;
        close(fd);
        return 1;
    }
    if (verbose) {
        std::cout << "G-code file opened successfully." << std::endl;
    }

    int return_code = 0;
    int available = RX_BUFFER_SIZE;
    std::queue<size_t> pending_lengths;
    std::string line;

    while (true) {
        // Send as many lines as possible
        while (available > 0 && std::getline(gcode_file, line)) {
            // Skip empty lines
            if (line.empty()) continue;

            // Strip comments (lines starting with ';')
            size_t comment_pos = line.find(';');
            if (comment_pos != std::string::npos) {
                line = line.substr(0, comment_pos);
            }
            line.erase(line.find_last_not_of(" \t\r\n") + 1);  // Trim trailing whitespace
            if (line.empty()) continue;

            // Calculate length including \n
            size_t len = line.length() + 1;
            if (len > available) {
                // Cannot send yet, put back the line by seeking back
                gcode_file.seekg(-static_cast<std::streamoff>(line.length() + 1), std::ios::cur);
                break;
            }

            // Send the line
            std::string send_line = line + "\n";
            if (verbose) {
                std::cout << "Sending: " << line << " (len: " << len << ", available: " << available << ")" << std::endl;
            }
            if (write(fd, send_line.c_str(), send_line.size()) != static_cast<ssize_t>(send_line.size())) {
                std::cerr << "Error writing to serial port." << std::endl;
                return_code = 1;
                break;
            }

            available -= len;
            pending_lengths.push(len);
        }

        // If no more lines to send and no pending acknowledgments, done
        if (pending_lengths.empty()) {
            break;
        }

        // Wait for response indefinitely
        if (verbose) {
            std::cout << "Waiting for response... (pending: " << pending_lengths.size() << ", available: " << available << ")" << std::endl;
        }
        std::string response = readSerialLine(fd, verbose);
	if(verbose){
            std::cout << response;
	}

        // Trim whitespace
        size_t start = response.find_first_not_of(" \t\r\n");
        if (start != std::string::npos) {
            response = response.substr(start);
        }
        size_t end = response.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) {
            response = response.substr(0, end + 1);
        }

        // Convert to lowercase for case-insensitive comparison
        std::string lower_response = response;
        std::transform(lower_response.begin(), lower_response.end(), lower_response.begin(),
                       [](unsigned char c){ return std::tolower(c); });

        if (lower_response.find("ok") != std::string::npos) {
            if (!pending_lengths.empty()) {
                size_t len = pending_lengths.front();
                pending_lengths.pop();
                available += len;
                if (verbose) {
                    std::cout << "Received ok, freed " << len << " bytes (available now: " << available << ")" << std::endl;
                }
            }
        } else {
            std::cerr << "GRBL error detected: " << response << " Halting execution." << std::endl;
            // return_code = 1;
            break;
        }

        if (return_code != 0) break;
    }

    if (verbose) {
        if (return_code == 0) {
            std::cout << "Streaming completed successfully." << std::endl;
        } else {
            std::cout << "Streaming halted due to error." << std::endl;
        }
    }

    // Cleanup
    gcode_file.close();
    close(fd);

    return return_code;
}

