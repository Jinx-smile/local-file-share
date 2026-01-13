#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <string>
#include <windows.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>
#include <tuple>
#include <map>
#include <thread>
#include <atomic>

using namespace std;

#pragma comment(lib, "ws2_32.lib")

class FileServer {
private:
    SOCKET serverSocket;
    sockaddr_in serverAddr;
    atomic<bool> running;
    string serverDirectory;
    int port;
    string exePath;

public:
    FileServer(int p, const string& directory = "server_files") : running(true), serverDirectory(directory), port(p) {
        char exePathBuffer[MAX_PATH];
        GetModuleFileNameA(NULL, exePathBuffer, MAX_PATH);
        exePath = string(exePathBuffer);
        size_t pos = exePath.find_last_of("\\/");
        if (pos != string::npos) {
            exePath = exePath.substr(0, pos);
        }

        string fullServerPath = exePath + "\\" + serverDirectory;
        if (!CreateDirectoryA(fullServerPath.c_str(), NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                cerr << "Warning: Cannot create server directory: " << fullServerPath
                    << " (Error: " << error << ")" << endl;
            }
        }

        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            cerr << "WSAStartup failed: " << WSAGetLastError() << endl;
            return;
        }

        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket == INVALID_SOCKET) {
            cerr << "Socket creation failed: " << WSAGetLastError() << endl;
            WSACleanup();
            return;
        }

        int opt = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

        int sendBufSize = 65536;
        int recvBufSize = 65536;
        setsockopt(serverSocket, SOL_SOCKET, SO_SNDBUF, (char*)&sendBufSize, sizeof(sendBufSize));
        setsockopt(serverSocket, SOL_SOCKET, SO_RCVBUF, (char*)&recvBufSize, sizeof(recvBufSize));

        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(static_cast<u_short>(port));

        if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            cerr << "Bind failed on port " << port << ": " << WSAGetLastError() << endl;
            closesocket(serverSocket);
            WSACleanup();
            return;
        }

        if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
            cerr << "Listen failed: " << WSAGetLastError() << endl;
            closesocket(serverSocket);
            WSACleanup();
            return;
        }

        cout << "=========================================" << endl;
        cout << "  CLEAN File Server v3.0" << endl;
        cout << "=========================================" << endl;
        cout << "Server successfully started on port " << port << endl;
        cout << "Server directory: " << fullServerPath << endl;
        cout << "NO HEADERS in files - pure data only!" << endl;
        cout << "=========================================" << endl;

        showExistingFiles();
        createTestFileIfNeeded(fullServerPath);
    }

    void createTestFileIfNeeded(const string& serverPath) {
        string testFilePath = serverPath + "\\test.txt";
        ifstream testFile(testFilePath);
        if (!testFile) {
            ofstream newFile(testFilePath);
            if (newFile) {
                newFile << "This is a CLEAN test file.\n";
                newFile << "It contains NO headers or metadata.\n";
                newFile << "Just pure text content.\n";
                newFile << "When downloaded, you should NOT see 'SIZE:' anywhere.\n";
                newFile << "Server: " << getCurrentTime() << "\n";
                newFile.close();
                cout << "Created clean test file: test.txt" << endl;
            }
        }
        testFile.close();
    }

    string getCurrentTime() {
        auto now = chrono::system_clock::now();
        auto now_time = chrono::system_clock::to_time_t(now);
        char buffer[80];
        ctime_s(buffer, sizeof(buffer), &now_time);
        string timeStr(buffer);
        timeStr.pop_back();
        return timeStr;
    }

    void showExistingFiles() {
        string fullServerPath = exePath + "\\" + serverDirectory;
        cout << "\nExisting files in server directory:" << endl;
        cout << "-----------------------------------" << endl;

        string searchPath = fullServerPath + "\\*";
        WIN32_FIND_DATAA findFileData;
        HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findFileData);

        if (hFind == INVALID_HANDLE_VALUE) {
            cout << "No files found." << endl;
            cout << "Directory: " << fullServerPath << endl;
            return;
        }

        int fileCount = 0;
        long long totalSize = 0;

        do {
            if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                string filename = findFileData.cFileName;
                long long fileSize = (static_cast<long long>(findFileData.nFileSizeHigh) << 32) | findFileData.nFileSizeLow;

                cout << "  " << filename << " (" << formatFileSize(fileSize) << ")" << endl;
                fileCount++;
                totalSize += fileSize;
            }
        } while (FindNextFileA(hFind, &findFileData) != 0);

        FindClose(hFind);

        if (fileCount > 0) {
            cout << "-----------------------------------" << endl;
            cout << "Total: " << fileCount << " files, " << formatFileSize(totalSize) << endl;
        }
        cout << "=========================================" << endl;
    }

    void logMessage(const string& message) {
        auto now = chrono::system_clock::now();
        auto now_time = chrono::system_clock::to_time_t(now);
        auto now_ms = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()) % 1000;

        cout << "[" << put_time(localtime(&now_time), "%H:%M:%S")
            << "." << setfill('0') << setw(3) << now_ms.count()
            << "] " << message << endl;
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

    string readCommand(SOCKET clientSocket) {
        char buffer[1024];
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived;

        DWORD timeout = 100;
        setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            string command = buffer;

            size_t newlinePos = command.find('\n');
            if (newlinePos != string::npos) {
                command = command.substr(0, newlinePos);
            }

            if (!command.empty() && command.back() == '\r') {
                command.pop_back();
            }

            return command;
        }
        else if (bytesReceived == 0) {
            return "DISCONNECT";
        }

        return "";
    }

    void handleClient(SOCKET clientSocket, sockaddr_in clientAddr) {
        char ipstr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddr.sin_addr), ipstr, sizeof(ipstr));

        logMessage("Client connected from: " + string(ipstr));

        DWORD sendTimeout = 30000;
        DWORD recvTimeout = 30000;
        setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, (char*)&sendTimeout, sizeof(sendTimeout));
        setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeout, sizeof(recvTimeout));

        bool stayConnected = true;

        while (stayConnected && running) {
            string command = readCommand(clientSocket);

            if (!command.empty()) {
                if (command == "LIST") {
                    sendFileListAndClose(clientSocket);
                    stayConnected = false;
                }
                else if (command.find("GET ") == 0) {
                    // НОВАЯ команда - чистые данные без заголовков
                    string filename = command.substr(4);
                    sendFileClean(clientSocket, filename);
                    stayConnected = false;
                }
                else if (command.find("DOWNLOAD ") == 0) {
                    // СОВМЕСТИМОСТЬ - тоже чистые данные
                    string filename = command.substr(9);
                    sendFileClean(clientSocket, filename);
                    stayConnected = false;
                }
                else if (command.find("INFO ") == 0) {
                    // Получить информацию о файле (размер)
                    string filename = command.substr(5);
                    sendFileInfo(clientSocket, filename);
                    stayConnected = false;
                }
                else if (command.find("UPLOAD ") == 0) {
                    string filename = command.substr(7);
                    receiveFile(clientSocket, filename);
                    stayConnected = false;
                }
                else if (command == "PING" || command == "TEST") {
                    string response = "PONG\n";
                    send(clientSocket, response.c_str(), response.length(), 0);
                    stayConnected = false;
                }
                else if (command == "EXIT" || command == "QUIT" || command == "DISCONNECT") {
                    logMessage("Client requested disconnect");
                    stayConnected = false;
                    string response = "GOODBYE\n";
                    send(clientSocket, response.c_str(), response.length(), 0);
                }
                else {
                    string response = "ERROR: Unknown command\n";
                    send(clientSocket, response.c_str(), response.length(), 0);
                    stayConnected = false;
                }
            }
            else if (command == "DISCONNECT") {
                stayConnected = false;
            }
            else {
                Sleep(10);
            }
        }

        Sleep(50);
        closesocket(clientSocket);
        logMessage("Client disconnected: " + string(ipstr));
    }

    void sendFileListAndClose(SOCKET clientSocket) {
        string fullServerPath = exePath + "\\" + serverDirectory;

        stringstream fileList;
        fileList << "FILES ON SERVER\n";
        fileList << "===============\n\n";

        WIN32_FIND_DATAA findFileData;
        string searchPath = fullServerPath + "\\*";
        HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findFileData);

        if (hFind == INVALID_HANDLE_VALUE) {
            fileList << "No files available.\n";
            string response = fileList.str();
            send(clientSocket, response.c_str(), response.length(), 0);
            return;
        }

        int fileCount = 0;
        long long totalSize = 0;
        vector<pair<string, long long>> files;

        do {
            if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                string filename = findFileData.cFileName;
                long long fileSize = (static_cast<long long>(findFileData.nFileSizeHigh) << 32) | findFileData.nFileSizeLow;
                files.push_back(make_pair(filename, fileSize));
                fileCount++;
                totalSize += fileSize;
            }
        } while (FindNextFileA(hFind, &findFileData) != 0);

        FindClose(hFind);

        if (fileCount > 0) {
            sort(files.begin(), files.end());

            fileList << "Total: " << fileCount << " files (" << formatFileSize(totalSize) << ")\n";
            fileList << "Directory: " << fullServerPath << "\n";
            fileList << "------------------------------\n";

            for (int i = 0; i < files.size(); i++) {
                fileList << setw(2) << (i + 1) << ". "
                    << setw(25) << left << files[i].first
                    << " [" << setw(8) << right << formatFileSize(files[i].second) << "]\n";
            }
        }
        else {
            fileList << "No files available.\n";
        }

        fileList << "===============\n";

        string fileListStr = fileList.str();
        send(clientSocket, fileListStr.c_str(), fileListStr.length(), 0);

        logMessage("File list sent (" + to_string(fileListStr.length()) + " bytes)");
    }

    void sendFileInfo(SOCKET clientSocket, const string& filename) {
        string fullPath = exePath + "\\" + serverDirectory + "\\" + filename;

        ifstream file(fullPath, ios::binary | ios::ate);
        if (!file) {
            string error = "ERROR: File not found\n";
            send(clientSocket, error.c_str(), error.length(), 0);
            return;
        }

        streamsize fileSize = file.tellg();
        file.close();

        // Проверяем, существует ли файл для получения даты
        WIN32_FIND_DATAA findData;
        string findPath = exePath + "\\" + serverDirectory + "\\" + filename;
        HANDLE hFind = FindFirstFileA(findPath.c_str(), &findData);

        stringstream info;
        info << "FILE INFO: " << filename << "\n";
        info << "Size: " << formatFileSize(fileSize) << " (" << fileSize << " bytes)\n";

        if (hFind != INVALID_HANDLE_VALUE) {
            FILETIME ftCreate = findData.ftCreationTime;
            FILETIME ftAccess = findData.ftLastAccessTime;
            FILETIME ftWrite = findData.ftLastWriteTime;

            SYSTEMTIME stUTC, stLocal;
            FileTimeToSystemTime(&ftWrite, &stUTC);
            SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);

            info << "Modified: " << setfill('0')
                << setw(2) << stLocal.wDay << "."
                << setw(2) << stLocal.wMonth << "."
                << stLocal.wYear << " "
                << setw(2) << stLocal.wHour << ":"
                << setw(2) << stLocal.wMinute << ":"
                << setw(2) << stLocal.wSecond << "\n";

            FindClose(hFind);
        }

        info << "===============\n";

        string infoStr = info.str();
        send(clientSocket, infoStr.c_str(), infoStr.length(), 0);

        logMessage("Sent file info for: " + filename + " (" + to_string(fileSize) + " bytes)");
    }

    void sendFileClean(SOCKET clientSocket, const string& filename) {
        // ОТПРАВЛЯЕМ ТОЛЬКО ЧИСТЫЕ ДАННЫЕ ФАЙЛА - БЕЗ ЗАГОЛОВКОВ!
        string fullPath = exePath + "\\" + serverDirectory + "\\" + filename;

        logMessage("Sending CLEAN file: " + filename);

        ifstream file(fullPath, ios::binary | ios::ate);
        if (!file) {
            string error = "ERROR: File not found\n";
            send(clientSocket, error.c_str(), error.length(), 0);
            return;
        }

        streamsize fileSize = file.tellg();
        file.seekg(0, ios::beg);

        logMessage("File size: " + to_string(fileSize) + " bytes");

        // ВАЖНО: НЕ отправляем заголовок SIZE: !!!
        // Просто сразу начинаем отправлять данные файла

        // Отправляем файл
        const int BUFFER_SIZE = 65536;
        char buffer[BUFFER_SIZE];
        streamsize totalSent = 0;

        auto startTime = chrono::steady_clock::now();

        while (!file.eof()) {
            file.read(buffer, BUFFER_SIZE);
            streamsize bytesRead = file.gcount();
            if (bytesRead > 0) {
                int sent = send(clientSocket, buffer, bytesRead, 0);
                if (sent == SOCKET_ERROR) {
                    logMessage("Send error: " + to_string(WSAGetLastError()));
                    break;
                }
                totalSent += sent;

                // Логируем прогресс для больших файлов
                if (fileSize > 1024 * 1024) { // Для файлов > 1MB
                    int percent = static_cast<int>((totalSent * 100) / fileSize);
                    if (percent % 10 == 0) {
                        logMessage("Sending: " + to_string(percent) + "%");
                    }
                }
            }
        }

        file.close();

        auto endTime = chrono::steady_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(endTime - startTime);

        logMessage("File sent CLEAN: " + filename + " (" + to_string(totalSent) + " bytes in "
            + to_string(duration.count()) + " ms)");
    }

    void receiveFile(SOCKET clientSocket, const string& filename) {
        string fullPath = exePath + "\\" + serverDirectory + "\\" + filename;

        logMessage("Receiving file: " + filename);

        // Отправляем готовность
        string readyMsg = "READY\n";
        send(clientSocket, readyMsg.c_str(), readyMsg.length(), 0);

        ofstream file(fullPath, ios::binary);
        if (!file) {
            string error = "ERROR: Cannot create file\n";
            send(clientSocket, error.c_str(), error.length(), 0);
            return;
        }

        const int BUFFER_SIZE = 65536;
        char buffer[BUFFER_SIZE];
        int bytesReceived;
        long long totalBytes = 0;

        DWORD timeout = 30000;
        setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        auto startTime = chrono::steady_clock::now();

        while (true) {
            bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
            if (bytesReceived > 0) {
                file.write(buffer, bytesReceived);
                totalBytes += bytesReceived;
            }
            else if (bytesReceived == 0) {
                break;
            }
            else {
                int error = WSAGetLastError();
                if (error == WSAETIMEDOUT) {
                    break;
                }
                logMessage("Receive error: " + to_string(error));
                break;
            }
        }

        file.close();

        auto endTime = chrono::steady_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(endTime - startTime);

        string confirm = "UPLOAD_COMPLETE: " + to_string(totalBytes) + " bytes\n";
        send(clientSocket, confirm.c_str(), confirm.length(), 0);

        logMessage("File received: " + filename + " (" + to_string(totalBytes) + " bytes in "
            + to_string(duration.count()) + " ms)");
    }

    void start() {
        logMessage("Server is ready and waiting for connections...");

        while (running) {
            sockaddr_in clientAddr;
            int clientAddrSize = sizeof(clientAddr);

            SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientAddrSize);
            if (clientSocket != INVALID_SOCKET) {
                thread clientThread(&FileServer::handleClient, this, clientSocket, clientAddr);
                clientThread.detach();
            }
            else {
                Sleep(10);
            }
        }
    }

    void stop() {
        running = false;

        if (serverSocket != INVALID_SOCKET) {
            closesocket(serverSocket);
            serverSocket = INVALID_SOCKET;
        }

        WSACleanup();
        logMessage("Server stopped");
    }

    ~FileServer() {
        stop();
    }
};

int main() {
    int port = 8888;
    string directory = "server_files";

    cout << "=========================================" << endl;
    cout << "       CLEAN FILE SERVER v3.0" << endl;
    cout << "=========================================" << endl;


    cout << "Enter server port [8888]: ";
    string portInput;
    getline(cin, portInput);
    if (!portInput.empty()) {
        try {
            port = stoi(portInput);
        }
        catch (...) {
            cout << "Invalid port, using default 8888" << endl;
            port = 8888;
        }
    }

    FileServer server(port, directory);
    server.start();

    return 0;
}