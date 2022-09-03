
HCRYPTPROV hCryptProv;
SOCKET webSocketClients[8];
int webSocketClientCount = 0;

int WebSocketBroadcast(char* data, unsigned char len) {
    unsigned char buf[2 + 255];
    buf[0] = 0x82;
    buf[1] = len;
    memcpy(buf + 2, data, len);
    int i = 0;
    while (i < webSocketClientCount) {
        if (send(webSocketClients[i], buf, 2 + len, 0) > 0) {
            i++;
        }
        else {
            webSocketClientCount--;
            webSocketClients[i] = webSocketClients[webSocketClientCount];
        }
    }
    return 0;
}

int WebSocketReceiveThread(void* arg) {
    SOCKET clientSocket = (SOCKET)(long long)arg;
    if (webSocketClientCount == 8)
        return 0;
    webSocketClients[webSocketClientCount] = clientSocket;
    webSocketClientCount++;
    char recvBuf[65536];
    char sendBuf[256];
    strcpy(sendBuf,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Connection: Upgrade\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Accept: ");
    int res = 0;
    while ((res = recv(clientSocket, recvBuf, 65536, 0)) > 0) {
        if (res > 65000) break;
        recvBuf[res] = 0;
        if (strcmp(recvBuf, "GET ") != 0) {
            char* key = strstr(recvBuf, "Sec-WebSocket-Key: ") + 19;
            if (key == (char*)19) break;
            char* keyEnd = strstr(key, "\r");
            if (!keyEnd || keyEnd - key > 32) break;
            strcpy(keyEnd, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
            HCRYPTHASH hHash = 0;
            CryptCreateHash(hCryptProv, CALG_SHA1, 0, 0, &hHash);
            CryptHashData(hHash, key, (DWORD)(keyEnd - key + 36), 0);
            char hashValue[20];
            DWORD hashValueLen = 20;
            CryptGetHashParam(hHash, HP_HASHVAL, hashValue, &hashValueLen, 0);
            CryptDestroyHash(hHash);
            DWORD base64OutputLen = 100;
            CryptBinaryToStringA(hashValue, hashValueLen, CRYPT_STRING_BASE64, sendBuf + 97, &base64OutputLen);
            strcpy(sendBuf + 97 + base64OutputLen, "\r\n");
            send(clientSocket, sendBuf, 97 + base64OutputLen + 2, 0);
        }
        else if ((unsigned char)recvBuf[0] == 0x89) {
            ((unsigned char*)recvBuf)[0] = 0x8A;
            send(clientSocket, recvBuf, res, 0);
        }
    }
    closesocket(clientSocket);
    return 0;
}

int WebSocketAcceptThread(void* arg) {
    SOCKET serverSocket = (SOCKET)(long long)arg;
    while (1) {
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket != -1)
            CloseHandle(CreateThread(0, 0, (LPTHREAD_START_ROUTINE)WebSocketReceiveThread, (LPVOID)(long long)clientSocket, 0, NULL));
    }
    return 0;
}

int StartWebSocketServer(unsigned short port) {
    if (!CryptAcquireContextA(&hCryptProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return -1;
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return -1;
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == -1)
        return -2;
    struct sockaddr_in fromAddr;
    fromAddr.sin_family = AF_INET;
    fromAddr.sin_port = htons(port);
    fromAddr.sin_addr.s_addr = INADDR_ANY;
    int addrLen = sizeof(struct sockaddr_in);
    if (bind(serverSocket, (struct sockaddr*)&fromAddr, addrLen) != 0)
        return -3;
    if (listen(serverSocket, 10) != 0)
        return -4;
    CloseHandle(CreateThread(0, 0, (LPTHREAD_START_ROUTINE)WebSocketAcceptThread, (LPVOID)(long long)serverSocket, 0, NULL));
    //printf("WebSocket server started on port %i\n", port);
    return 0;
}