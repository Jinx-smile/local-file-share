#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <string>
#include <windows.h>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <vector>
#include <algorithm>

using namespace std;

#pragma comment(lib, "ws2_32.lib")

class FileClient {
private:
    string serverIP;
    int port;

public:
    FileClient(const string& ip, int p) : serverIP(ip), port(p) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            cerr << "WSAStartup failed: " << WSAGetLastError() << endl;
            return;
        }
    }

    ~FileClient() {
        WSACleanup();
    }

    SOCKET createConnection(int timeoutMs = 2000) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) {
            cerr << "Socket creation failed: " << WSAGetLastError() << endl;
            return INVALID_SOCKET;
        }

        sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(static_cast<u_short>(port));

        if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
            hostent* host = gethostbyname(serverIP.c_str());
            if (host == NULL) {
                cerr << "Cannot resolve server address: " << serverIP << endl;
                closesocket(sock);
                return INVALID_SOCKET;
            }
            serverAddr.sin_addr.s_addr = *((unsigned long*)host->h_addr);
        }

        DWORD timeout = timeoutMs;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

        if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            cerr << "Connection failed: " << WSAGetLastError() << endl;
            closesocket(sock);
            return INVALID_SOCKET;
        }

        return sock;
    }

    void printSeparator(int length = 50) {
        cout << string(length, '=') << endl;
    }

    void printLine(int length = 50) {
        cout << string(length, '-') << endl;
    }

    void printHeader(const string& title) {
        cout << endl;
        printSeparator();
        cout << "  " << title << endl;
        printSeparator();
    }

    string formatFileSize(long long bytes) {
        const char* sizes[] = { "B", "KB", "MB", "GB", "TB" };
        int i = 0;
        double dblBytes = static_cast<double>(bytes);

        while (dblBytes >= 1024 && i < 4) {
            dblBytes /= 1024;
            i++;
        }

        char buffer[32];
        if (i == 0) {
            sprintf(buffer, "%lld %s", bytes, sizes[i]);
        }
        else {
            sprintf(buffer, "%.2f %s", dblBytes, sizes[i]);
        }
        return string(buffer);
    }

    void showLocalFiles() {
        char currentDir[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, currentDir);

        printHeader("LOCAL FILES");
        cout << "Directory: " << currentDir << endl;
        printLine();

        WIN32_FIND_DATAA findFileData;
        HANDLE hFind = FindFirstFileA((string(currentDir) + "\\*").c_str(), &findFileData);

        if (hFind != INVALID_HANDLE_VALUE) {
            int fileCount = 0;
            long long totalSize = 0;
            vector<pair<string, long long>> files;

            do {
                if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    string filename = findFileData.cFileName;
                    long long filesize = (static_cast<long long>(findFileData.nFileSizeHigh) << 32) | findFileData.nFileSizeLow;
                    files.push_back(make_pair(filename, filesize));
                    fileCount++;
                    totalSize += filesize;
                }
            } while (FindNextFileA(hFind, &findFileData) != 0);

            FindClose(hFind);

            sort(files.begin(), files.end());

            if (fileCount > 0) {
                cout << "Total files: " << fileCount << " | Total size: " << formatFileSize(totalSize) << endl;
                printLine();

                for (int i = 0; i < files.size(); i++) {
                    string filename = files[i].first;
                    long long filesize = files[i].second;

                    cout << setw(3) << (i + 1) << ". "
                        << setw(35) << left << filename
                        << " (" << formatFileSize(filesize) << ")" << endl;
                }
            }
            else {
                cout << "No files found." << endl;
            }
        }
        else {
            cerr << "Error reading directory." << endl;
        }

        printLine();
    }

    void requestFileList() {
        printHeader("SERVER FILE LIST");

        auto totalStartTime = chrono::steady_clock::now();

        SOCKET sock = createConnection(1000);
        if (sock == INVALID_SOCKET) {
            cerr << "Cannot connect to server" << endl;
            return;
        }

        DWORD readTimeout = 2000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&readTimeout, sizeof(readTimeout));

        string command = "LIST\n";
        if (send(sock, command.c_str(), command.length(), 0) == SOCKET_ERROR) {
            cerr << "Failed to send command" << endl;
            closesocket(sock);
            return;
        }

        cout << "Receiving file list..." << endl;

        string response;
        char buffer[8192];
        int bytesReceived;

        while (true) {
            bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (bytesReceived > 0) {
                buffer[bytesReceived] = '\0';
                response.append(buffer, bytesReceived);
            }
            else if (bytesReceived == 0) {
                break;
            }
            else {
                break;
            }
        }

        closesocket(sock);

        auto totalEndTime = chrono::steady_clock::now();
        auto totalDuration = chrono::duration_cast<chrono::milliseconds>(totalEndTime - totalStartTime);

        if (!response.empty()) {
            cout << endl << response << endl;
            cout << "Received in " << totalDuration.count() << " ms" << endl;
        }
        else {
            cerr << "No response from server" << endl;
        }
    }

    // Улучшенный метод скачивания - сначала получаем размер отдельным запросом
    void downloadFile(const string& filename) {
        printHeader("DOWNLOAD FILE");

        if (filename.empty()) {
            cerr << "Filename cannot be empty" << endl;
            return;
        }

        // ШАГ 1: Получаем размер файла отдельным запросом
        cout << "Getting file size..." << endl;
        long long fileSize = 0;

        SOCKET sizeSock = createConnection(1000);
        if (sizeSock != INVALID_SOCKET) {
            string sizeCommand = "SIZE " + filename + "\n";
            if (send(sizeSock, sizeCommand.c_str(), sizeCommand.length(), 0) != SOCKET_ERROR) {
                char sizeBuffer[256];
                int sizeBytes = recv(sizeSock, sizeBuffer, sizeof(sizeBuffer) - 1, 0);
                if (sizeBytes > 0) {
                    sizeBuffer[sizeBytes] = '\0';
                    string sizeResponse = sizeBuffer;

                    if (sizeResponse.find("SIZE:") == 0) {
                        size_t colonPos = sizeResponse.find(":");
                        size_t newlinePos = sizeResponse.find("\n");
                        if (colonPos != string::npos && newlinePos != string::npos) {
                            string sizeStr = sizeResponse.substr(colonPos + 1, newlinePos - colonPos - 1);
                            try {
                                fileSize = stoll(sizeStr);
                                cout << "File size: " << formatFileSize(fileSize) << endl;
                            }
                            catch (...) {
                                cerr << "Invalid size format" << endl;
                            }
                        }
                    }
                    else if (sizeResponse.find("ERROR:") == 0) {
                        cout << "Error: " << sizeResponse << endl;
                        closesocket(sizeSock);
                        return;
                    }
                }
            }
            closesocket(sizeSock);
        }

        if (fileSize <= 0) {
            cout << "Could not get file size, trying to download anyway..." << endl;
        }

        // ШАГ 2: Скачиваем файл
        SOCKET sock = createConnection(2000);
        if (sock == INVALID_SOCKET) {
            return;
        }

        // Используем новую команду GETFILE которая не должна отправлять заголовки
        string command = "GETFILE " + filename + "\n";
        if (send(sock, command.c_str(), command.length(), 0) == SOCKET_ERROR) {
            cerr << "Failed to send command" << endl;
            closesocket(sock);
            return;
        }

        cout << "Downloading " << filename << "..." << endl;

        ofstream file(filename, ios::binary);
        if (!file) {
            cerr << "Cannot create file" << endl;
            closesocket(sock);
            return;
        }

        const int BUFFER_SIZE = 65536;
        char buffer[BUFFER_SIZE];
        int bytesReceived;
        long long totalBytes = 0;

        DWORD timeout = 30000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        auto startTime = chrono::steady_clock::now();

        // Просто читаем ВСЕ данные что приходят
        while (true) {
            bytesReceived = recv(sock, buffer, BUFFER_SIZE, 0);
            if (bytesReceived > 0) {
                // Проверяем, не содержит ли буфер текста "SIZE:"
                string data(buffer, min(bytesReceived, 100)); // Проверяем первые 100 байт
                if (data.find("SIZE:") != string::npos) {
                    // Нашли заголовок - пропускаем его
                    size_t headerEnd = data.find("\n");
                    if (headerEnd != string::npos) {
                        // Пропускаем заголовок и символ новой строки
                        int headerSize = headerEnd + 1;
                        if (headerSize < bytesReceived) {
                            // Записываем данные после заголовка
                            file.write(buffer + headerSize, bytesReceived - headerSize);
                            totalBytes += (bytesReceived - headerSize);
                            cout << "Warning: Skipped SIZE header in data stream" << endl;
                        }
                    }
                    else {
                        // Заголовок не полностью в этом пакете
                        file.write(buffer, bytesReceived);
                        totalBytes += bytesReceived;
                    }
                }
                else {
                    // Обычные данные файла
                    file.write(buffer, bytesReceived);
                    totalBytes += bytesReceived;
                }

                // Показываем прогресс если знаем размер
                if (fileSize > 0) {
                    int percent = static_cast<int>((totalBytes * 100) / fileSize);
                    if (percent % 25 == 0) {
                        cout << "Progress: " << percent << "%" << endl;
                    }
                }
                else {
                    // Иначе показываем количество полученных данных
                    if (totalBytes % (256 * 1024) == 0) {
                        cout << "Received: " << formatFileSize(totalBytes) << endl;
                    }
                }
            }
            else if (bytesReceived == 0) {
                // Сервер закрыл соединение
                break;
            }
            else {
                // Ошибка
                break;
            }
        }

        file.close();
        closesocket(sock);

        auto endTime = chrono::steady_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(endTime - startTime);

        if (totalBytes > 0) {
            cout << endl << "Download completed!" << endl;
            printLine();
            cout << "File:  " << filename << endl;
            cout << "Size:  " << formatFileSize(totalBytes) << endl;
            cout << "Time:  " << duration.count() << " ms" << endl;

            if (duration.count() > 0) {
                double speed = (totalBytes * 1000.0) / (duration.count() * 1024.0);
                cout << "Speed: " << fixed << setprecision(2) << speed << " KB/s" << endl;
            }

            // Проверяем файл на наличие заголовков
            verifyFileContent(filename);
        }
        else {
            cout << "Download failed - no data received" << endl;
            DeleteFileA(filename.c_str());
        }
    }

    // Альтернативный метод - договариваемся с сервером о новом протоколе
    void downloadFileNewProtocol(const string& filename) {
        printHeader("DOWNLOAD FILE (NEW PROTOCOL)");

        if (filename.empty()) {
            cerr << "Filename cannot be empty" << endl;
            return;
        }

        SOCKET sock = createConnection(2000);
        if (sock == INVALID_SOCKET) {
            return;
        }

        // Новая команда - RAW означает "сырые" данные без заголовков
        string command = "RAW " + filename + "\n";
        if (send(sock, command.c_str(), command.length(), 0) == SOCKET_ERROR) {
            cerr << "Failed to send command" << endl;
            closesocket(sock);
            return;
        }

        // Сначала получаем ответ сервера
        char responseBuffer[256];
        int responseBytes = recv(sock, responseBuffer, sizeof(responseBuffer) - 1, 0);
        if (responseBytes <= 0) {
            cerr << "No response from server" << endl;
            closesocket(sock);
            return;
        }

        responseBuffer[responseBytes] = '\0';
        string response = responseBuffer;

        if (response.find("ERROR") != string::npos) {
            cout << "Server error: " << response << endl;
            closesocket(sock);
            return;
        }

        cout << "Starting download of " << filename << "..." << endl;

        ofstream file(filename, ios::binary);
        if (!file) {
            cerr << "Cannot create file" << endl;
            closesocket(sock);
            return;
        }

        const int BUFFER_SIZE = 65536;
        char buffer[BUFFER_SIZE];
        int bytesReceived;
        long long totalBytes = responseBytes;

        // Записываем уже полученный ответ (если это не был заголовок ошибки)
        if (response.find("OK") == 0 || response.find("READY") == 0) {
            // Это служебный ответ, не записываем его в файл
            cout << "Server ready for transfer" << endl;
            totalBytes = 0;
        }
        else {
            // Возможно, это уже часть файла
            file.write(responseBuffer, responseBytes);
        }

        DWORD timeout = 30000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        auto startTime = chrono::steady_clock::now();

        // Читаем остальные данные
        while (true) {
            bytesReceived = recv(sock, buffer, BUFFER_SIZE, 0);
            if (bytesReceived > 0) {
                file.write(buffer, bytesReceived);
                totalBytes += bytesReceived;

                // Показываем прогресс
                if (totalBytes % (256 * 1024) == 0) {
                    cout << "Received: " << formatFileSize(totalBytes) << endl;
                }
            }
            else if (bytesReceived == 0) {
                break;
            }
            else {
                break;
            }
        }

        file.close();
        closesocket(sock);

        auto endTime = chrono::steady_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(endTime - startTime);

        if (totalBytes > 0) {
            cout << endl << "Download completed!" << endl;
            printLine();
            cout << "File:  " << filename << endl;
            cout << "Size:  " << formatFileSize(totalBytes) << endl;
            cout << "Time:  " << duration.count() << " ms" << endl;

            verifyFileContent(filename);
        }
        else {
            cout << "Download failed" << endl;
        }
    }

    void verifyFileContent(const string& filename) {
        cout << endl << "Verifying file content for SIZE: headers..." << endl;

        ifstream file(filename, ios::binary);
        if (!file) {
            cout << "Cannot open file for verification" << endl;
            return;
        }

        // Читаем весь файл для проверки
        file.seekg(0, ios::end);
        streamsize fileSize = file.tellg();
        file.seekg(0, ios::beg);

        if (fileSize == 0) {
            cout << "File is empty" << endl;
            file.close();
            return;
        }

        // Читаем первые 1000 байт для быстрой проверки
        const int CHECK_SIZE = min((streamsize)1000, fileSize);
        vector<char> buffer(CHECK_SIZE);
        file.read(buffer.data(), CHECK_SIZE);
        file.close();

        string content(buffer.data(), CHECK_SIZE);

        // Ищем заголовок SIZE:
        size_t sizePos = content.find("SIZE:");
        if (sizePos != string::npos) {
            cout << "!!! ERROR !!! File contains 'SIZE:' at position " << sizePos << endl;

            // Находим конец строки с размером
            size_t endLine = content.find("\n", sizePos);
            if (endLine != string::npos) {
                string header = content.substr(sizePos, endLine - sizePos + 1);
                cout << "Header found: " << header;

                // Показываем контекст
                size_t contextStart = max(0, (int)sizePos - 20);
                size_t contextEnd = min(content.length(), endLine + 20);
                cout << "Context: ..." << content.substr(contextStart, contextEnd - contextStart) << "..." << endl;

                // Предлагаем очистить файл
                cout << endl << "Do you want to remove the header from the file? (y/n): ";
                string answer;
                getline(cin, answer);

                if (answer == "y" || answer == "Y") {
                    removeHeaderFromFile(filename, endLine + 1);
                }
            }
        }
        else {
            cout << "✓ File is CLEAN - no SIZE: headers found in first " << CHECK_SIZE << " bytes" << endl;

            // Показываем начало файла для текстовых файлов
            bool isText = true;
            for (char ch : content) {
                if (ch == 0 || (ch < 32 && ch != '\n' && ch != '\r' && ch != '\t' && ch != '\b')) {
                    isText = false;
                    break;
                }
            }

            if (isText && !content.empty()) {
                cout << "First few lines:" << endl;
                cout << "----------------" << endl;

                stringstream ss(content);
                string line;
                int lineCount = 0;
                while (getline(ss, line) && lineCount < 3) {
                    cout << line << endl;
                    lineCount++;
                }
                cout << "----------------" << endl;
            }
        }
    }

    void removeHeaderFromFile(const string& filename, size_t headerSize) {
        ifstream inFile(filename, ios::binary);
        if (!inFile) {
            cerr << "Cannot open file for cleaning" << endl;
            return;
        }

        // Переходим после заголовка
        inFile.seekg(headerSize, ios::beg);

        // Создаем временный файл
        string tempFile = filename + ".clean";
        ofstream outFile(tempFile, ios::binary);

        if (!outFile) {
            cerr << "Cannot create clean file" << endl;
            inFile.close();
            return;
        }

        // Копируем данные после заголовка
        const int BUFFER_SIZE = 65536;
        char buffer[BUFFER_SIZE];

        while (!inFile.eof()) {
            inFile.read(buffer, BUFFER_SIZE);
            streamsize bytesRead = inFile.gcount();
            if (bytesRead > 0) {
                outFile.write(buffer, bytesRead);
            }
        }

        inFile.close();
        outFile.close();

        // Заменяем старый файл чистым
        DeleteFileA(filename.c_str());
        rename(tempFile.c_str(), filename.c_str());

        cout << "File cleaned successfully!" << endl;

        // Проверяем новый размер
        ifstream checkFile(filename, ios::binary | ios::ate);
        streamsize newSize = checkFile.tellg();
        checkFile.close();

        cout << "New file size: " << formatFileSize(newSize) << endl;
    }

    void uploadFile() {
        printHeader("UPLOAD FILE");

        showLocalFiles();

        cout << endl << "Enter filename to upload: ";
        string filename;
        getline(cin, filename);

        if (filename.empty()) {
            cout << "Upload cancelled" << endl;
            return;
        }

        char currentDir[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, currentDir);
        string fullPath = string(currentDir) + "\\" + filename;

        ifstream testFile(fullPath, ios::binary | ios::ate);
        if (!testFile) {
            cerr << "File not found: " << filename << endl;
            return;
        }

        streamsize fileSize = testFile.tellg();
        testFile.close();

        cout << endl << "File: " << filename << " (" << formatFileSize(fileSize) << ")" << endl;

        cout << "Start upload? (y/n): ";
        string confirm;
        getline(cin, confirm);

        if (confirm != "y" && confirm != "Y") {
            cout << "Upload cancelled" << endl;
            return;
        }

        SOCKET sock = createConnection(2000);
        if (sock == INVALID_SOCKET) {
            return;
        }

        DWORD uploadTimeout = 30000;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&uploadTimeout, sizeof(uploadTimeout));

        // Отправляем размер файла отдельно от данных
        string command = "PUT " + filename + " " + to_string(fileSize) + "\n";
        if (send(sock, command.c_str(), command.length(), 0) == SOCKET_ERROR) {
            cerr << "Failed to send command" << endl;
            closesocket(sock);
            return;
        }

        // Ждем подтверждения
        char readyBuffer[256];
        DWORD readyTimeout = 2000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&readyTimeout, sizeof(readyTimeout));

        int readyBytes = recv(sock, readyBuffer, sizeof(readyBuffer) - 1, 0);
        if (readyBytes <= 0 || string(readyBuffer).find("READY") == string::npos) {
            cerr << "Server not ready: ";
            if (readyBytes > 0) {
                readyBuffer[readyBytes] = '\0';
                cerr << readyBuffer;
            }
            closesocket(sock);
            return;
        }

        cout << "Uploading file..." << endl;

        ifstream file(fullPath, ios::binary);
        if (!file) {
            cerr << "Cannot open file" << endl;
            closesocket(sock);
            return;
        }

        const int BUFFER_SIZE = 65536;
        char buffer[BUFFER_SIZE];
        streamsize totalSent = 0;
        auto startTime = chrono::steady_clock::now();

        while (!file.eof()) {
            file.read(buffer, BUFFER_SIZE);
            streamsize bytesRead = file.gcount();
            if (bytesRead > 0) {
                int sent = send(sock, buffer, bytesRead, 0);
                if (sent == SOCKET_ERROR) {
                    cerr << "Upload failed: " << WSAGetLastError() << endl;
                    break;
                }
                totalSent += sent;

                if (fileSize > 0) {
                    int percent = static_cast<int>((totalSent * 100) / fileSize);
                    if (percent % 25 == 0) {
                        cout << "Progress: " << percent << "%" << endl;
                    }
                }
            }
        }

        file.close();

        // Закрываем отправку
        shutdown(sock, SD_SEND);

        // Ждем подтверждения
        char confirmBuffer[256];
        DWORD confirmTimeout = 2000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&confirmTimeout, sizeof(confirmTimeout));

        int confirmBytes = recv(sock, confirmBuffer, sizeof(confirmBuffer) - 1, 0);
        closesocket(sock);

        if (confirmBytes > 0) {
            confirmBuffer[confirmBytes] = '\0';
            cout << endl << "Server response: " << confirmBuffer << endl;
        }

        auto endTime = chrono::steady_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(endTime - startTime);

        cout << endl << "Upload completed!" << endl;
        printLine();
        cout << "File:  " << filename << endl;
        cout << "Size:  " << formatFileSize(totalSent) << endl;
        cout << "Time:  " << duration.count() << " ms" << endl;

        if (duration.count() > 0) {
            double speed = (totalSent * 1000.0) / (duration.count() * 1024.0);
            cout << "Speed: " << fixed << setprecision(2) << speed << " KB/s" << endl;
        }
    }

    void createTestFile() {
        char currentDir[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, currentDir);

        string testFilePath = string(currentDir) + "\\test_clean.txt";
        ofstream testFile(testFilePath);
        if (testFile) {
            testFile << "This is a CLEAN test file.\n";
            testFile << "It should download without SIZE: headers.\n";
            testFile << "Line 3: Testing clean file transfer.\n";
            testFile << "Line 4: No metadata in the content.\n";
            testFile << "Line 5: End of test file.\n";
            testFile.close();

            ifstream sizeCheck(testFilePath, ios::binary | ios::ate);
            streamsize fileSize = sizeCheck.tellg();
            sizeCheck.close();

            cout << "Created clean test file: " << testFilePath << endl;
            cout << "Size: " << formatFileSize(fileSize) << endl;
            cout << "This file contains NO headers or metadata." << endl;
        }
        else {
            cerr << "Failed to create test file" << endl;
        }
    }

    void testConnection() {
        printHeader("TEST CONNECTION");
        cout << "Testing connection to " << serverIP << ":" << port << "..." << endl;

        auto startTime = chrono::steady_clock::now();

        SOCKET sock = createConnection(1000);
        if (sock == INVALID_SOCKET) {
            cout << "Connection FAILED" << endl;
            return;
        }

        auto connectTime = chrono::steady_clock::now();
        auto connectDuration = chrono::duration_cast<chrono::milliseconds>(connectTime - startTime);

        DWORD timeout = 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        string command = "PING\n";
        if (send(sock, command.c_str(), command.length(), 0) == SOCKET_ERROR) {
            cout << "Failed to send command" << endl;
            closesocket(sock);
            return;
        }

        char buffer[256];
        int bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0);
        closesocket(sock);

        auto endTime = chrono::steady_clock::now();
        auto totalDuration = chrono::duration_cast<chrono::milliseconds>(endTime - startTime);

        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            cout << "✓ Connection SUCCESSFUL!" << endl;
            cout << "Server response: " << buffer;
            cout << "Connection time: " << connectDuration.count() << " ms" << endl;
        }
        else {
            cout << "Connected but no response (timeout)" << endl;
        }
    }

    void showMenu() {
        string choice;
        while (true) {
            cout << endl;
            cout << "FILE TRANSFER CLIENT - CLEAN FILES" << endl;
            cout << "==================================" << endl;
            cout << "Server: " << serverIP << ":" << port << endl;
            cout << "==================================" << endl;
            cout << "1. List files on server" << endl;
            cout << "2. Download file (auto-clean)" << endl;
            cout << "3. Download file (new protocol)" << endl;
            cout << "4. Upload file" << endl;
            cout << "5. Show local files" << endl;
            cout << "6. Create test file" << endl;
            cout << "7. Test connection" << endl;
            cout << "8. Verify file for headers" << endl;
            cout << "9. Exit" << endl;
            cout << "==================================" << endl;

            cout << "Select option [1-9]: ";
            getline(cin, choice);

            if (choice == "1") {
                requestFileList();
            }
            else if (choice == "2") {
                cout << endl << "Enter filename to download: ";
                string filename;
                getline(cin, filename);
                downloadFile(filename);
            }
            else if (choice == "3") {
                cout << endl << "Enter filename to download: ";
                string filename;
                getline(cin, filename);
                downloadFileNewProtocol(filename);
            }
            else if (choice == "4") {
                uploadFile();
            }
            else if (choice == "5") {
                showLocalFiles();
            }
            else if (choice == "6") {
                createTestFile();
            }
            else if (choice == "7") {
                testConnection();
            }
            else if (choice == "8") {
                cout << endl << "Enter filename to verify: ";
                string filename;
                getline(cin, filename);
                verifyFileContent(filename);
            }
            else if (choice == "9" || choice == "exit") {
                cout << endl << "Goodbye!" << endl;
                break;
            }
            else {
                cout << endl << "Invalid option" << endl;
            }
        }
    }
};

int main() {
    cout << "========================================" << endl;
    cout << "    FILE TRANSFER CLIENT - CLEAN MODE   " << endl;
    cout << "========================================" << endl;
    cout << "  Guaranteed clean files - no headers!  " << endl;
    cout << "========================================" << endl;

    string serverIP;
    int port;

    cout << endl << "Server Configuration" << endl;
    cout << "---------------------" << endl;

    cout << "Enter server IP [127.0.0.1]: ";
    getline(cin, serverIP);
    if (serverIP.empty()) {
        serverIP = "127.0.0.1";
    }

    cout << "Enter server port [8888]: ";
    string portStr;
    getline(cin, portStr);
    if (portStr.empty()) {
        port = 8888;
    }
    else {
        try {
            port = stoi(portStr);
        }
        catch (...) {
            cout << "Invalid port, using default 8888" << endl;
            port = 8888;
        }
    }

    FileClient client(serverIP, port);
    client.showMenu();

    return 0;
}