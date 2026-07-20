#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdio> 


#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib") 
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (SOCKET)(~0)
#define SOCKET_ERROR (-1)
#endif

const int PORT = 5201;            
const int BUFFER_SIZE = 65536;     // 64 KB
const int TEST_DURATION_SEC = 10;  


void initSockets() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Eroare: Initializarea Winsock a eșuat.\n";
        exit(1);
    }
#endif
}

void cleanupSockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void closeSocket(SOCKET s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}


void runServer() {
    initSockets();
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        std::cerr << "Eroare la crearea socket-ului.\n";
        return;
    }

    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "Eroare de tip Bind. Portul " << PORT << " este probabil ocupat.\n";
        closeSocket(server_fd);
        return;
    }

    if (listen(server_fd, 3) == SOCKET_ERROR) {
        std::cerr << "Eroare la Listen.\n";
        closeSocket(server_fd);
        return;
    }

    std::cout << "-----------------------------------------------------------\n";
    std::cout << "Serverul asculta conexiuni pe portul " << PORT << "\n";
    std::cout << "-----------------------------------------------------------\n";

    while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        SOCKET client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_socket == INVALID_SOCKET) {
            std::cerr << "Eroare la acceptarea conexiunii.\n";
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        std::cout << "Conexiune acceptata de la " << client_ip << ":" << ntohs(client_addr.sin_port) << "\n";

        std::vector<char> buffer(BUFFER_SIZE);
        uint64_t total_bytes = 0;
        uint64_t interval_bytes = 0;

        auto start_time = std::chrono::steady_clock::now();
        auto last_report_time = start_time;

        while (true) {
            int bytes_read = recv(client_socket, buffer.data(), BUFFER_SIZE, 0);
            if (bytes_read <= 0) break; 

            total_bytes += bytes_read;
            interval_bytes += bytes_read;

            auto current_time = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed = current_time - last_report_time;

            if (elapsed.count() >= 1.0) {
                double mbps = (interval_bytes * 8.0) / (1000000.0 * elapsed.count());
                std::cout << "[SERVER] Rata de transfer: " << mbps << " Mbits/sec\n";
                interval_bytes = 0;
                last_report_time = current_time;
            }
        }

        std::cout << "Conexiune inchisa.\n";
        closeSocket(client_socket);
        std::cout << "-----------------------------------------------------------\n";
    }

    closeSocket(server_fd);
    cleanupSockets();
}


void runClient(const std::string& server_ip) {
    initSockets();
    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "Eroare la crearea socket-ului.\n";
        return;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);

    std::cout << "Conectare la serverul " << server_ip << ", port " << PORT << "\n";

    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Conexiunea a eșuat. Asigura-te ca serverul este pornit.\n";
        closeSocket(client_socket);
        return;
    }

    std::cout << "-----------------------------------------------------------\n";

    
    std::vector<char> buffer(BUFFER_SIZE, 'A');
    uint64_t total_bytes = 0;
    uint64_t interval_bytes = 0;

    auto start_time = std::chrono::steady_clock::now();
    auto last_report_time = start_time;

    while (true) {
        auto current_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> total_elapsed = current_time - start_time;

        if (total_elapsed.count() >= TEST_DURATION_SEC) {
            break; // 10 secunde
        }

        int bytes_sent = send(client_socket, buffer.data(), BUFFER_SIZE, 0);
        if (bytes_sent == SOCKET_ERROR) {
            std::cerr << "Eroare la trimiterea datelor.\n";
            break;
        }

        total_bytes += bytes_sent;
        interval_bytes += bytes_sent;

        std::chrono::duration<double> interval_elapsed = current_time - last_report_time;
        if (interval_elapsed.count() >= 1.0) {
            double mbps = (interval_bytes * 8.0) / (1000000.0 * interval_elapsed.count());
            std::cout << "[CLIENT] Rata de transfer: " << mbps << " Mbits/sec\n";
            interval_bytes = 0;
            last_report_time = current_time;
        }
    }

    std::chrono::duration<double> final_elapsed = std::chrono::steady_clock::now() - start_time;
    double final_mbps = (total_bytes * 8.0) / (1000000.0 * final_elapsed.count());

    std::cout << "-----------------------------------------------------------\n";
    std::cout << "Test finalizat.\n";
    std::cout << "Total date transferate: " << total_bytes / (1024 * 1024) << " MBytes\n";
    std::cout << "Latime de banda (Bandwidth) medie: " << final_mbps << " Mbits/sec\n";

    closeSocket(client_socket);
    cleanupSockets();
}


void runDiskTest() {
    std::cout << "-----------------------------------------------------------\n";
    std::cout << "Incepere test Disk I/O (Scriere secventiala timp de " << TEST_DURATION_SEC << "s)...\n";
    std::cout << "-----------------------------------------------------------\n";

    const size_t DISK_BUFFER_SIZE = 1024 * 1024; 
    std::vector<char> buffer(DISK_BUFFER_SIZE, 'B'); 
    const std::string filename = "speedtest_dummy_file.tmp";

   
    FILE* file = fopen(filename.c_str(), "wb");
    if (!file) {
        std::cerr << "Eroare la crearea fisierului de test pe disk. Asigura-te ca ai permisiuni de scriere.\n";
        return;
    }

    uint64_t total_bytes = 0;
    uint64_t interval_bytes = 0;

    auto start_time = std::chrono::steady_clock::now();
    auto last_report_time = start_time;

    while (true) {
        auto current_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> total_elapsed = current_time - start_time;

        if (total_elapsed.count() >= TEST_DURATION_SEC) {
            break; 
        }

        
        size_t bytes_written = fwrite(buffer.data(), 1, DISK_BUFFER_SIZE, file);
        if (bytes_written != DISK_BUFFER_SIZE) {
            std::cerr << "Eroare la scrierea datelor pe disk.\n";
            break;
        }

        total_bytes += bytes_written;
        interval_bytes += bytes_written;

        
        std::chrono::duration<double> interval_elapsed = current_time - last_report_time;
        if (interval_elapsed.count() >= 1.0) {
            
            double mb_per_sec = (interval_bytes) / (1024.0 * 1024.0 * interval_elapsed.count());
            std::cout << "[DISK] Viteza scriere: " << mb_per_sec << " MB/s\n";
            interval_bytes = 0;
            last_report_time = current_time;
        }
    }

    fclose(file);

    std::chrono::duration<double> final_elapsed = std::chrono::steady_clock::now() - start_time;
    double final_mb_per_sec = (total_bytes) / (1024.0 * 1024.0 * final_elapsed.count());

    std::cout << "-----------------------------------------------------------\n";
    std::cout << "Test Disk finalizat.\n";
    std::cout << "Total date scrise pe disk: " << total_bytes / (1024 * 1024) << " MB\n";
    std::cout << "Viteza medie de Disk I/O: " << final_mb_per_sec << " MB/s\n";

    
    if (std::remove(filename.c_str()) != 0) {
        std::cerr << "Avertisment: Nu s-a putut sterge fisierul temporar " << filename << "\n";
    }
    else {
        std::cout << "Fisierul temporar a fost sters cu succes.\n";
    }
    std::cout << "-----------------------------------------------------------\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Utilizare incorecta.\n\n";
        std::cerr << "Comenzi disponibile:\n";
        std::cerr << "  Mod Server (Retea): " << argv[0] << " -s\n";
        std::cerr << "  Mod Client (Retea): " << argv[0] << " -c <IP_SERVER>\n";
        std::cerr << "  Mod Test Disk I/O:  " << argv[0] << " -d\n";
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "-s") {
        runServer();
    }
    else if (mode == "-c" && argc == 3) {
        std::string server_ip = argv[2];
        runClient(server_ip);
    }
    else if (mode == "-d") {
        runDiskTest();
    }
    else {
        std::cerr << "Argumente invalide. Vezi Commands.md .\n";
    }

    return 0;
}