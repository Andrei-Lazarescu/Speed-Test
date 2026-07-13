# Specificații Proiect: Aplicație Speed Test & Disk I/O (Clone iPerf3 / FIO)

## 1. Descriere Proiect
Acest proiect presupune dezvoltarea unei aplicații de tip utilitar (CLI - Command Line Interface) capabilă să măsoare performanța rețelei între două stații (similara cu funcționalitățile de bază ale **iperf3**) și, în mod suplimentar, să evalueze viteza de citire/scriere a mediilor de stocare (similară cu funcționalitățile de bază ale **fio**).

Aplicația va fi configurabilă și va suporta arhitectura de tip **Client-Server** pentru testele de rețea, respectiv execuție locală pentru testele de Disk I/O.

---

## 2. Cerințe Funcționale (Requirements)

### 2.1. Scenarii de Rețea (Network Testing - iperf3 style)
Aplicația trebuie să ruleze în două moduri: **Server** (ascultă conexiuni) și **Client** (inițiază testul și trimite/cere date).
*   **Suport Protocoale:**
    *   **TCP:** Măsurarea lățimii de bandă (throughput), jitterului de transfer și pierderilor de pachete la nivel de aplicație.
    *   **UDP:** Testarea performanței la rate fixe de biți (bandwidth throttling), monitorizarea pachetelor pierdute (packet loss) și a variației întârzierilor (jitter).
*   **Parametri Configurabili din CLI:**
    *   `-s, --server`: Pornirea aplicației în mod Server.
    *   `-c, --client <IP>`: Pornirea aplicației în mod Client, conectându-se la IP-ul specificat.
    *   `-p, --port <PORT>`: Specificarea portului utilizat (default: 5201).
    *   `-t, --time <SECONDS>`: Durata testului în secunde (default: 10s).
    *   `-i, --interval <SECONDS>`: Intervalul de timp la care se afișează statistici intermediare (eg. la fiecare 1 secundă).
    *   `-P, --parallel <THREADS>`: Numărul de conexiuni paralele (multi-threading / conexiuni simultane).
    *   `-R, --reverse`: Modul invers – Serverul trimite date și Clientul primește.
    *   `-u, --udp`: Utilizarea protocolului UDP în locul TCP.
    *   `-b, --bandwidth <BITRATE>`: Limitarea vitezei de trimitere pentru UDP (ex: 10M, 1G).

### 2.2. Scenarii de Stocare (Disk I/O Testing - fio style)
Modul de testare a discului va rula local și va evalua performanța sistemului de fișiere.
*   **Tipuri de Acces (I/O Engines):**
    *   **Sequential Read/Write:** Citire și scriere secvențială a datelor.
    *   **Random Read/Write:** Citire și scriere aleatorie (relevantă pentru IOPS - Input/Output Operations Per Second).
*   **Parametri Configurabili din CLI:**
    *   `--disk-test`: Activarea modulului de testare Disk I/O.
    *   `--io-type <read|write|randread|randwrite>`: Tipul de test dorit.
    *   `--bs <BLOCK_SIZE>`: Dimensiunea blocului de date (ex: 4k, 64k, 1M).
    *   `--size <FILE_SIZE>`: Dimensiunea fișierului de test creat (ex: 1G, 5G) pentru a preveni cache-ul din RAM.
    *   `--direct <0|1>`: Opțiune pentru utilizarea Direct I/O (ocolirea cache-ului OS-ului, echivalent cu `O_DIRECT`).

---

## 3. Limbaj de Programare și Tehnologii Recomandate

Pentru acest proiect, limbajul de programare ales este **C++ (standardul C++17 sau C++20)**. Acesta este limbajul în care au fost dezvoltate și utilitarele originale (`iperf3` în C, `fio` în C), oferind fundamentele necesare pentru un tool de sistem.

### De ce C++?
1.  **Performanță Brută și Control Hardware:** C++ oferă "zero-cost abstractions" și acces direct la memoria sistemului. Lipsa unui Garbage Collector înseamnă că nu vor exista întreruperi neașteptate (jitter) care să viciereze rezultatele testelor de rețea.
2.  **Acces Nativ la API-urile Sistemului de Operare:** Interacțiunea cu socket-urile de rețea (POSIX Sockets) și manipularea de nivel jos a fișierelor (apeluri de sistem precum `open`, `fcntl`, flag-ul `O_DIRECT`) se fac nativ, fără straturi intermediare de interpretare.
3.  **Multi-threading Nativ:** Prin `std::thread` (sau `std::jthread` în C++20) și primitive de sincronizare (`std::mutex`, `std::atomic`), se poate construi o arhitectură concurentă extrem de rapidă pentru scenariile cu conexiuni paralele (`-P`).
4.  **Ecosistem de Librării (Opțional):** Pentru a evita scrierea masivă de cod duplicat pentru suport multi-platformă, se poate utiliza **Boost.Asio** pentru networking asincron de înaltă performanță.

---

## 4. Dificultăți și Provocări Tehnice (Development Roadblocks)

Alegerea C++ aduce performanță maximă, dar crește complexitatea dezvoltării. Iată obstacolele majore:

### 4.1. Portabilitatea Cross-Platform (Linux vs. Windows)
*   **Provocare:** API-urile de rețea și fișiere diferă masiv între sisteme. În Linux se folosesc socket-uri POSIX și flag-ul `O_DIRECT`, în timp ce pe Windows se folosesc Winsock (`WSAStartup`) și `FILE_FLAG_NO_BUFFERING`.
*   **Soluție:** Utilizarea directivelor de preprocesor (`#ifdef _WIN32`, `#ifdef __linux__`) pentru a separa logic ramurile de cod specifice fiecărui OS, sau abstractizarea acestora printr-o bibliotecă precum Boost.Asio.

### 4.2. Managementul Memoriei (Memory Leaks & Buffer Overflows)
*   **Provocare:** Alocarea manuală a bufferelor pentru transmiterea de date în rețea sau citirea/scrierea pe disc poate duce ușor la scurgeri de memorie (memory leaks) sau coruperea memoriei (segmentation faults).
*   **Soluție:** Utilizarea idiomului **RAII (Resource Acquisition Is Initialization)** și a pointerilor inteligenți (`std::unique_ptr`, `std::shared_ptr`). Evitarea alocărilor dinamice (`new`/`delete` manual) în buclele critice de performanță (hot paths).

### 4.3. Precizia Timerelor pentru UDP Throttling (`--bandwidth`)
*   **Provocare:** Limitarea ratei de transfer pentru UDP necesită timere de extrem de înaltă rezoluție. Funcțiile standard de `sleep` ale sistemului de operare adesea nu sunt suficient de precise (au o marjă de câteva milisecunde), ceea ce duce la fluctuații (bursts) de trafic.
*   **Soluție:** Utilizarea `std::chrono::high_resolution_clock` pentru măsurarea timpului și implementarea unui mecanism de tip "busy-wait" hibrid sau a unui algoritm *Token Bucket* pentru a trimite pachetele la intervale precise de microsecunde.

### 4.4. Lock Contention și Agregarea Datelor în Modul Paralel
*   **Provocare:** Când se rulează mai multe thread-uri (`-P`), centralizarea datelor (ex: câți octeți au fost transferați) la fiecare secundă (`-i 1`) poate bloca firele de execuție dacă se folosesc mutex-uri greoaie, scăzând throughput-ul total.
*   **Soluție:** Utilizarea contoarelor atomice (`std::atomic<uint64_t>`) pentru agregarea metricilor partajate, eliminând astfel necesitatea blocajelor de tip lock/mutex în calea critică de transfer a datelor.
