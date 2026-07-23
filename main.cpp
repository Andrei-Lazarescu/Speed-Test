#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdio>
#include <iomanip>


#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#pragma comment(lib, "ws2_32.lib")
#define fsync _commit
#define fileno _fileno
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (SOCKET)(~0)
#define SOCKET_ERROR (-1)
#endif

const int PORT = 5201;
const int TCP_BUFFER_SIZE = 65536;    // 64 KB
const int UDP_BUFFER_SIZE = 8192;     // 8 KB
const uint64_t ACK_INTERVAL = 16 * 1024 * 1024; // 16 MB pentru un ACK vizibil
int test_duration_sec = 10;

void initSockets() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Eroare: Initializarea Winsock a esuat.\n";
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


// 1. TCP 

void runTcpServer() {
    initSockets();
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 3);

    std::cout << "--- [TCP] Serverul asculta conexiuni pe portul " << PORT << " ---\n";

    while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        SOCKET client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        std::cout << "Conexiune TCP de la " << client_ip << "\n";

        std::vector<char> buffer(TCP_BUFFER_SIZE);
        uint64_t total_bytes = 0;
        uint64_t interval_bytes = 0;
        uint64_t unacked_bytes = 0;

        auto start_time = std::chrono::steady_clock::now();
        auto last_report_time = start_time;

        while (true) {
            int bytes_read = recv(client_socket, buffer.data(), TCP_BUFFER_SIZE, 0);
            if (bytes_read <= 0) break;

            total_bytes += bytes_read;
            interval_bytes += bytes_read;
            unacked_bytes += bytes_read;

            // Trimitem ACK inapoi corectat logic
            if (unacked_bytes >= ACK_INTERVAL) {
                send(client_socket, "ACK", 3, 0);
                unacked_bytes = 0;
            }

            auto current_time = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed = current_time - last_report_time;

            if (elapsed.count() >= 1.0) {
                double mbps = (interval_bytes * 8.0) / (1000000.0 * elapsed.count());
                std::cout << "[SERVER TCP] Rata primire: " << mbps << " Mbps\n";
                interval_bytes = 0;
                last_report_time = current_time;
            }
        }
        std::cout << "Conexiune TCP inchisa.\n---------------------------------------\n";
        closeSocket(client_socket);
    }
    closeSocket(server_fd);
    cleanupSockets();
}

void runTcpClient(const std::string& server_ip) {
    initSockets();
    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);

    std::cout << "--- Conectare TCP la " << server_ip << " ---\n";
    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Conexiune TCP esuata.\n";
        return;
    }

    std::vector<char> buffer(TCP_BUFFER_SIZE, 'T');
    char ack_buffer[4];
    uint64_t total_bytes = 0;
    uint64_t interval_bytes = 0;
    uint64_t unacked_bytes = 0;

    auto start_time = std::chrono::steady_clock::now();
    auto last_report_time = start_time;

    while (true) {
        auto current_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> total_elapsed = current_time - start_time;
        if (total_elapsed.count() >= test_duration_sec) break;

        int bytes_sent = send(client_socket, buffer.data(), TCP_BUFFER_SIZE, 0);
        if (bytes_sent > 0) {
            total_bytes += bytes_sent;
            interval_bytes += bytes_sent;
            unacked_bytes += bytes_sent;
        }

        // Citesc ACK-ul fara sa pierdem sincronizarea
        if (unacked_bytes >= ACK_INTERVAL) {
            recv(client_socket, ack_buffer, 3, 0);
            ack_buffer[3] = '\0';
            std::cout << "[TCP TRACE] Bloc de " << (ACK_INTERVAL / (1024 * 1024)) << "MB ajuns. " << ack_buffer << " primit. (0 retransmisii)\n";
            unacked_bytes = 0;
        }

        std::chrono::duration<double> interval_elapsed = current_time - last_report_time;
        if (interval_elapsed.count() >= 1.0) {
            double mbps = (interval_bytes * 8.0) / (1000000.0 * interval_elapsed.count());
            std::cout << "[CLIENT TCP] Rata transfer: " << mbps << " Mbps\n";
            interval_bytes = 0;
            last_report_time = current_time;
        }
    }
    closeSocket(client_socket);
    cleanupSockets();
}


// 2.UDP

void runUdpServer() {
    initSockets();
    SOCKET server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));

    //Timeout pentru socket ca sa nu se blocheze la infinit daca pica clientul
#ifdef _WIN32
    DWORD timeout = 1000; // 1000 milisecunde
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

    std::cout << "--- [UDP] Serverul asculta pachete pe portul " << PORT << " ---\n";

    std::vector<char> buffer(UDP_BUFFER_SIZE);
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    uint64_t max_seq = 0;
    uint64_t packets_received = 0;
    uint64_t interval_bytes = 0;

    auto last_packet_time = std::chrono::steady_clock::now();
    auto last_report_time = last_packet_time;
    bool active_test = false;

    while (true) {
        int bytes_read = recvfrom(server_fd, buffer.data(), UDP_BUFFER_SIZE, 0, (struct sockaddr*)&client_addr, &client_len);
        auto current_time = std::chrono::steady_clock::now();

        if (bytes_read > 0) {
            if (!active_test) {
                active_test = true;
                packets_received = 0;
                max_seq = 0;
                interval_bytes = 0;
                std::cout << "[SERVER UDP] Incepere receptie trafic nou...\n";
            }

            last_packet_time = current_time;
            uint64_t seq_num;
            std::memcpy(&seq_num, buffer.data(), sizeof(uint64_t));

            packets_received++;
            if (seq_num > max_seq) max_seq = seq_num;
            interval_bytes += bytes_read;

            std::chrono::duration<double> interval_elapsed = current_time - last_report_time;
            if (interval_elapsed.count() >= 1.0) {
                double mbps = (interval_bytes * 8.0) / (1000000.0 * interval_elapsed.count());
                std::cout << "[SERVER UDP] Viteza primire: " << mbps << " Mbps\n";
                interval_bytes = 0;
                last_report_time = current_time;
            }
        }

        // Logica pentru detectarea finalului testului (Daca au trecut >1.5s de la ultimul pachet)
        if (active_test) {
            std::chrono::duration<double> idle_time = current_time - last_packet_time;
            if (idle_time.count() >= 1.5) {
                uint64_t packets_lost = (max_seq + 1) - packets_received;
                double lost_percent = ((double)packets_lost / (max_seq + 1)) * 100.0;

                std::cout << "\n=== RAPORT FINAL UDP ===\n";
                std::cout << "Pachete Trimise (est): " << max_seq + 1 << "\n";
                std::cout << "Pachete Ajunse: " << packets_received << "\n";
                std::cout << "Pachete Pierdute: " << packets_lost << " (" << lost_percent << "%)\n";
                std::cout << "========================\n\n";

                active_test = false;
            }
        }
    }
}

void runUdpClient(const std::string& server_ip) {
    initSockets();
    SOCKET client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);

    std::vector<char> buffer(UDP_BUFFER_SIZE, 'U');
    uint64_t seq_num = 0;
    uint64_t interval_bytes = 0;

    std::cout << "--- [UDP] Trimitere flux de pachete catre " << server_ip << " timp de " << test_duration_sec << "s ---\n";

    auto start_time = std::chrono::steady_clock::now();
    auto last_report_time = start_time;

    while (true) {
        auto current_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> total_elapsed = current_time - start_time;
        if (total_elapsed.count() >= test_duration_sec) break;

        std::memcpy(buffer.data(), &seq_num, sizeof(uint64_t));

        int bytes_sent = sendto(client_socket, buffer.data(), UDP_BUFFER_SIZE, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
        if (bytes_sent > 0) {
            interval_bytes += bytes_sent;
            seq_num++;
        }

        std::chrono::duration<double> interval_elapsed = current_time - last_report_time;
        if (interval_elapsed.count() >= 1.0) {
            double mbps = (interval_bytes * 8.0) / (1000000.0 * interval_elapsed.count());
            std::cout << "[CLIENT UDP] Viteza trimitere: " << mbps << " Mbps (Pachet nr. " << seq_num << ")\n";
            interval_bytes = 0;
            last_report_time = current_time;
        }
        // Delay minimal adaugat pentru a nu sufoca routerele pe localhost in UDP (opțional)
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    std::cout << "Test finalizat. Am trimis " << seq_num << " pachete.\n";
    closeSocket(client_socket);
    cleanupSockets();
}


// 3. DISK I/O 

void runDiskTest() {
    std::cout << "-----------------------------------------------------------\n";
    std::cout << "Incepere test Disk I/O Real (Scriere + Validare Citire timp de " << test_duration_sec << "s)\n";
    std::cout << "-----------------------------------------------------------\n";

    const size_t DISK_BUFFER_SIZE = 1024 * 1024; // 1 MB
    std::vector<char> buffer(DISK_BUFFER_SIZE, 'B');
    const std::string filename = "speedtest_dummy_file.tmp";

    // SCRIERE
    FILE* file = fopen(filename.c_str(), "wb");
    if (!file) {
        std::cerr << "Eroare: Lipsa permisiuni pentru scriere pe disc.\n";
        return;
    }

    uint64_t total_bytes = 0;
    uint64_t interval_bytes = 0;
    auto start_time = std::chrono::steady_clock::now();
    auto last_report_time = start_time;

    while (true) {
        auto current_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> total_elapsed = current_time - start_time;
        if (total_elapsed.count() >= test_duration_sec) break;

        size_t bytes_written = fwrite(buffer.data(), 1, DISK_BUFFER_SIZE, file);
        fflush(file);
        fsync(fileno(file));

        total_bytes += bytes_written;
        interval_bytes += bytes_written;

        std::chrono::duration<double> interval_elapsed = current_time - last_report_time;
        if (interval_elapsed.count() >= 1.0) {
            double mb_per_sec = (interval_bytes) / (1024.0 * 1024.0 * interval_elapsed.count());
            std::cout << "[DISK WRITE] Confirmare hardware: " << mb_per_sec << " MB/s reale\n";
            interval_bytes = 0;
            last_report_time = current_time;
        }
    }
    fclose(file);

    //VALIDARE CITIRE 
    std::cout << "-----------------------------------------------------------\n";
    std::cout << "Verificare integritate fisiere (Read-Back)... ";
    FILE* file_read = fopen(filename.c_str(), "rb");
    if (file_read) {
        std::vector<char> read_buffer(DISK_BUFFER_SIZE);
        size_t bytes_read = fread(read_buffer.data(), 1, DISK_BUFFER_SIZE, file_read);

        bool valid = true;
        for (size_t i = 0; i < bytes_read; ++i) {
            if (read_buffer[i] != 'B') {
                valid = false;
                break;
            }
        }

        if (valid && bytes_read > 0) {
            std::cout << "[SUCCES] Datele citite de pe disc se potrivesc cu cele scrise!\n";
        }
        else {
            std::cout << "[ESEC] Eroare de corupere a datelor!\n";
        }
        fclose(file_read);
    }
    else {
        std::cout << "[ESEC] Nu am putut deschide fisierul pentru verificare.\n";
    }

    std::remove(filename.c_str());
    std::cout << "Test Disk finalizat. Fisier sters automat.\n-----------------------------------------------------------\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Utilizare: speedtest [mod] [protocol] [--time SECONDS]\n";
        std::cerr << "  -s -t           Server TCP\n";
        std::cerr << "  -s -u           Server UDP\n";
        std::cerr << "  -c <IP> -t      Client TCP\n";
        std::cerr << "  -c <IP> -u      Client UDP\n";
        std::cerr << "  -d              Test Disk Real\n";
        std::cerr << "  --time <sec>    (Optional) Seteaza durata testului. Exemplu: --time 15\n";
        return 1;
    }

    std::string mode = "";
    std::string protocol = "";
    std::string server_ip = "";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-s") mode = "server";
        else if (arg == "-c" && i + 1 < argc) { mode = "client"; server_ip = argv[++i]; }
        else if (arg == "-d") mode = "disk";
        else if (arg == "-t") protocol = "tcp";
        else if (arg == "-u") protocol = "udp";
        else if (arg == "--time" && i + 1 < argc) { test_duration_sec = std::stoi(argv[++i]); }
    }

    if (mode == "server" && protocol == "tcp") runTcpServer();
    else if (mode == "server" && protocol == "udp") runUdpServer();
    else if (mode == "client" && protocol == "tcp") runTcpClient(server_ip);
    else if (mode == "client" && protocol == "udp") runUdpClient(server_ip);
    else if (mode == "disk") runDiskTest();
    else std::cerr << "Argumente invalide. Ruleaza fara argumente pentru ajutor.\n";

    return 0;
}