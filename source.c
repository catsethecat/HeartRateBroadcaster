#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <windowsx.h>
#include <wincrypt.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <SetupAPI.h>
#include <bluetoothleapis.h>
#include <bthledef.h>
#include <shellapi.h>
#include <Dbt.h>
#include <bluetoothapis.h>

#pragma comment (lib,"Ws2_32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "bluetoothapis.lib")
#pragma comment (lib, "crypt32.lib")

HANDLE hDev;
BTH_ADDR btaddr;
BLUETOOTH_GATT_EVENT_HANDLE hEvent;
unsigned short port = 2752;
char statusLine1[64];
char statusLine2[32];

HCRYPTPROV hCryptProv;
SOCKET webSocketClients[8];
int webSocketClientIndex = 0;
int webSocketClientCount = 0;

void fatalError(char* msg) {
    MessageBoxA(NULL, msg, "HeartRateBroadcaster", MB_ICONERROR);
    ExitProcess(0);
}

// ------------------------------------------------------- CRT stuff ---------------------------------------------------

int _fltused;

#pragma function(memset)
void* memset(void* ptr, int value, size_t num) {
    while (num--)
        ((unsigned char*)ptr)[num] = (unsigned char)value;
    return ptr;
}

#pragma function(memcpy)
void* memcpy(void* dst, void* src, size_t num) {
    size_t u64count = num / sizeof(unsigned long long);
    size_t remainder = num - u64count * sizeof(unsigned long long);
    unsigned long long* u64dst = dst;
    unsigned long long* u64src = src;
    unsigned char* remDst = (unsigned char*)dst + num - remainder;
    unsigned char* remSrc = (unsigned char*)src + num - remainder;
    for (unsigned int i = 0; i < u64count; i++)
        u64dst[i] = u64src[i];
    for (unsigned int i = 0; i < remainder; i++)
        remDst[i] = remSrc[i];
    return dst;
}

#pragma function(strlen)
size_t strlen(const char* str) {
    size_t len = 0;
    while (*(str++))
        len++;
    return len;
}

char* strstr(const char* str1, const char* str2) {
    char* c1 = (char*)str1, * c2 = (char*)str2;
    for (; *c1 && *c2; c2 = (*c1 == *c2 ? c2 + 1 : str2), c1++);
    return *c2 ? 0 : c1 - (c2 - str2);
}

#pragma function(memcmp)
int memcmp(const void* ptr1, const void* ptr2, size_t num) {
    int diff = 0;
    for (int i = 0; i < num && diff == 0; i++) {
        diff = ((unsigned char*)ptr1)[i] - ((unsigned char*)ptr2)[i];
    }
    return diff;
}

#pragma function(strcmp)
int strcmp(const char* str1, const char* str2) {
    int diff = 0;
    for (; !(diff = *str1 - *str2) && *str1 && *str2; str1++, str2++);
    return diff;
}

#pragma function(strcpy)
char* strcpy(char* dst, const char* src) {
    for (char* d = dst; *d = *src; src++, d++);
    return dst;
}

char* strncpy(char* dst, const char* src, size_t dstLen) {
    for (char* d = dst; *d = (*src*(dstLen>1)); src++, d++, dstLen--);
    return dst;
}

void* malloc(size_t size) {
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void free(void* ptr) {
    VirtualFree(ptr, 0, MEM_RELEASE);
}

unsigned long long ipow(int base, int exponent) {
    if (!exponent)
        return 1;
    unsigned long long res = base;
    while (exponent-- > 1)
        res *= base;
    return res;
}

int str_getint(char* str) {
    int sign = 1;
    if (*str == '-') {
        sign = -1;
        str++;
    }
    unsigned char* digit = str;
    for (; *digit >= '0' && *digit <= '9'; digit++);
    digit--;
    int total = 0;
    for (int i = 0; digit >= str; i++, digit--)
        total += (*digit - '0') * (int)ipow(10, i);
    return total * sign;
}

int int_to_str(int num, char* str) {
    if (num == 0) {
        str[0] = '0';
        str[1] = 0;
        return 1;
    }
    int negative = 0;
    if (num < 0) {
        *str = '-';
        num *= -1;
        str++;
        negative = 1;
    }
    char tmp[16] = { 0 };
    int i = 0;
    for (; num > 0; i++) {
        int b = num % (int)ipow(10, i + 1);
        tmp[14 - i] = '0' + b / (int)ipow(10, i);
        num -= b;
    }
    if (str)
        memcpy(str, tmp + 15 - i, i + 1);
    return i + negative;
}

// --------------------------------------------- WebSocket stuff    ----------------------------------------------------

int WebSocketBroadcast(char* data, unsigned char len) {
    unsigned char buf[2 + 255];
    buf[0] = 0x82;
    buf[1] = len;
    memcpy(buf + 2, data, len);
    int i = 0;
    while (i < webSocketClientIndex) {
        if (send(webSocketClients[i], buf, 2 + len, 0) > 0) {
            i++;
        }
        else {
            webSocketClientIndex--;
            webSocketClients[i] = webSocketClients[webSocketClientIndex];
        }
    }
    return 0;
}

int WebSocketReceiveThread(void* arg) {
    SOCKET clientSocket = (SOCKET)(long long)arg;
    if (webSocketClientCount == 8)
        return 0;
    webSocketClients[webSocketClientIndex] = clientSocket;
    webSocketClientIndex++, webSocketClientCount++;
    int_to_str(webSocketClientCount, strstr(statusLine2,",") + 11);
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
    webSocketClientCount--;
    int_to_str(webSocketClientCount, strstr(statusLine2, ",") + 11);
    return 0;
}

int WebSocketAcceptThread(void* arg) {
    SOCKET serverSocket = (SOCKET)(long long)arg;
    while (1) {
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket != -1)
            CloseHandle(CreateThread(0, 0, (LPTHREAD_START_ROUTINE)WebSocketReceiveThread,
                (LPVOID)(long long)clientSocket, 0, NULL));
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
    CloseHandle(CreateThread(0, 0, (LPTHREAD_START_ROUTINE)WebSocketAcceptThread, (LPVOID)(long long)serverSocket, 0,
        NULL));
    strcpy(statusLine2 + 6 + int_to_str(port, strcpy(statusLine2, "Port: ") + 6), ", Clients: 0");
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------

void OnCharacteristicValueChanged(BTH_LE_GATT_EVENT_TYPE EventType, PVOID EventOutParameter, PVOID Context) {
    BLUETOOTH_GATT_VALUE_CHANGED_EVENT* event = (BLUETOOTH_GATT_VALUE_CHANGED_EVENT*)EventOutParameter;
    WebSocketBroadcast(event->CharacteristicValue->Data, (unsigned char)event->CharacteristicValue->DataSize);
}

int ConnectHeartRateMonitor(void* arg) {
    strcpy(statusLine1, "Device: Connecting...");
    // get device path of the first BLE heart rate monitor known to the system {0000180D-0000-1000-8000-00805F9B34FB}
    GUID classGuid = { 0x0000180D, 0x0000, 0x1000, { 0x80, 0x00,  0x00,  0x80,  0x5f,  0x9b,  0x34,  0xfb } };
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&classGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA interfaceData = { 0 };
    interfaceData.cbSize = sizeof(interfaceData);
    if(!SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &classGuid, 0, &interfaceData))
        fatalError("No devices found. Make sure your device is paired and shows up in Windows settings.");
    char buf[1024];
    PSP_DEVICE_INTERFACE_DETAIL_DATA_A pDetails = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)buf;
    pDetails->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
    if(!SetupDiGetDeviceInterfaceDetailA(hDevInfo, &interfaceData, pDetails, sizeof(buf), NULL, NULL))
        fatalError("failed to get device details");
    //get bluetooth address ( PKEY_DeviceInterface_Bluetooth_DeviceAddress 2BD67D8B-8BEB-48D5-87E0-6CDA3428040A )
    //this is jank af there has to be a better way that doesnt involve strings
    DEVPROPKEY propKey = { { 0x2BD67D8B, 0x8BEB, 0x48D5, { 0x87, 0xe0,  0x6c,  0xda,  0x34,  0x28,  0x04,  0x0a } },
        1 };
    char propValue[35];
    DEVPROPTYPE propType;
    SP_DEVINFO_DATA infoData = { .cbSize = sizeof(SP_DEVINFO_DATA) };
    if (!SetupDiEnumDeviceInfo(hDevInfo, 0, &infoData))
        fatalError("EnumDevInfo");
    if (!SetupDiGetDevicePropertyW(hDevInfo, &infoData, &propKey, &propType, propValue, sizeof(propValue), NULL, 0))
        fatalError("GetDevProp");
    for (int i = 0; ; i++) {
        if (propValue[i+1] == 0 && propValue[i + 2] == 0) {
            for (char* c = propValue +i; c >= propValue; c -= 2) {
                btaddr += (*c < 'a' ? *c - '0' : *c - 'a' + 10) * ipow(16,(i - (int)(c- propValue))/2);
            }
            break;
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    // open device handle
    while ((hDev = CreateFileA(pDetails->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE)
        Sleep(3000);
    // get heart rate service
    BTH_LE_GATT_SERVICE services[16];
    unsigned short numServices = 0;
    HRESULT res = BluetoothGATTGetServices(hDev, 16, services, &numServices, BLUETOOTH_GATT_FLAG_NONE);
    if (res != S_OK)
        fatalError("failed to get GATT services");
    PBTH_LE_GATT_SERVICE pHeartRateService = 0;
    for (int i = 0; i < numServices; i++)
        if (services[i].ServiceUuid.Value.ShortUuid == 0x180d)
            pHeartRateService = &services[i];
    if (!pHeartRateService)
        fatalError("no heart rate service found");
    // get heart rate measurement characteristic
    BTH_LE_GATT_CHARACTERISTIC characteristics[16];
    unsigned short numCharacteristics = 0;
    res = BluetoothGATTGetCharacteristics(hDev, pHeartRateService, 16, characteristics, &numCharacteristics, 
        BLUETOOTH_GATT_FLAG_NONE);
    if (res != S_OK)
        fatalError("failed to get GATT characteristics");
    PBTH_LE_GATT_CHARACTERISTIC pHRMeasurementCharacteristic = 0;
    for (int i = 0; i < numCharacteristics; i++)
        if (characteristics[i].CharacteristicUuid.Value.ShortUuid == 0x2a37)
            pHRMeasurementCharacteristic = &characteristics[i];
    if (!pHRMeasurementCharacteristic)
        fatalError("no heart rate measurement characteristic found");
    // get descriptor for client characteristic configuration
    BTH_LE_GATT_DESCRIPTOR descriptors[16];
    unsigned short numDescriptors = 0;
    res = BluetoothGATTGetDescriptors(hDev, pHRMeasurementCharacteristic, 16, descriptors, &numDescriptors, 
        BLUETOOTH_GATT_FLAG_NONE);
    if (res != S_OK)
        fatalError("failed to get GATT descriptors");
    PBTH_LE_GATT_DESCRIPTOR pConfigDescriptor = 0;
    for (int i = 0; i < numDescriptors; i++)
        if (descriptors[i].DescriptorType == ClientCharacteristicConfiguration)
            pConfigDescriptor = &descriptors[i];
    if (!pConfigDescriptor)
        fatalError("no client characteristic configuration descriptor found");
    // write descriptor to subscribe to notifications
    BTH_LE_GATT_DESCRIPTOR_VALUE descriptorValue;
    memset(&descriptorValue, 0, sizeof(descriptorValue));
    descriptorValue.DescriptorType = ClientCharacteristicConfiguration;
    descriptorValue.ClientCharacteristicConfiguration.IsSubscribeToNotification = TRUE;
    while (BluetoothGATTSetDescriptorValue(hDev, pConfigDescriptor, &descriptorValue, BLUETOOTH_GATT_FLAG_NONE) != S_OK)
        Sleep(3000);
    // register value change event for heart rate measurement characteristic
    BLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION eventParams = { 0 };
    eventParams.Characteristics[0] = *pHRMeasurementCharacteristic;
    eventParams.NumCharacteristics = 1;
    res = BluetoothGATTRegisterEvent(hDev, CharacteristicValueChangedEvent, &eventParams, 
        (PFNBLUETOOTH_GATT_EVENT_CALLBACK)OnCharacteristicValueChanged, NULL, &hEvent, BLUETOOTH_GATT_FLAG_NONE);
    if (res != S_OK)
        fatalError("failed to register value change event");
    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg)
    {
    case 123: {
        if (LOWORD(lParam) == WM_CONTEXTMENU) {
            HMENU hMenu = CreatePopupMenu();
            InsertMenu(hMenu, 0, MF_STRING | MF_DISABLED, 0, statusLine1);
            InsertMenu(hMenu, 1, MF_STRING | MF_DISABLED, 0, statusLine2);
            InsertMenu(hMenu, 2, MF_STRING, 0, "Exit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, 0, GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam), 0, hwnd, NULL);
            return 0;
        }
        break;
    }
    case WM_DEVICECHANGE: {
        if (wParam == DBT_CUSTOMEVENT) {
            DEV_BROADCAST_HANDLE* hdr = (DEV_BROADCAST_HANDLE*)lParam;
            //GUID_BLUETOOTH_RADIO_IN_RANGE {EA3B5B82-26EE-450E-B0D8-D26FE30A3869}
            if (hdr->dbch_devicetype == DBT_DEVTYP_HANDLE && 
                memcmp(&hdr->dbch_eventguid, "\x82\x5b\x3b\xea\xee\x26\x0e\x45\xb0\xd8\xd2\x6f\xe3\x0a\x38\x69", 16) == 0) {
                BTH_RADIO_IN_RANGE* rir = (BTH_RADIO_IN_RANGE*)hdr->dbch_data;
                if (rir->deviceInfo.address == btaddr) {
                    ULONG diff = rir->deviceInfo.flags ^ rir->previousDeviceFlags;
                    if (diff & BDIF_LE_CONNECTED) {
                        if (rir->deviceInfo.flags & BDIF_LE_CONNECTED) {
                            strncpy(statusLine1, rir->deviceInfo.name, sizeof(statusLine1));
                        }
                        else if(hDev) {
                            BluetoothGATTUnregisterEvent(hEvent, BLUETOOTH_GATT_FLAG_NONE);
                            CloseHandle(hDev);
                            hEvent = 0, hDev = 0, btaddr = 0;
                            CloseHandle(
                                CreateThread(0, 0, (LPTHREAD_START_ROUTINE)ConnectHeartRateMonitor, 0, 0, NULL));
                        }
                    }
                }
            }
        }
        break;
    }
    case WM_COMMAND: {
        PostQuitMessage(0);
        break;
    }
    case WM_DESTROY: {
        PostQuitMessage(0);
        break;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

#ifdef _DEBUG
int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
#else
int WinMainCRTStartup()
#endif
{
    char* cmdLine = GetCommandLineA();
    int argc = 0;
    char argv[8][1024];
    for (char* c = cmdLine, *a = argv[argc]; c == cmdLine || *(c-1) != 0; c++) {
        *a = *c;
        if (*a == ' ' || *a == 0) {
            *a = 0;
            argc++;
            if (argc == 8)
                break;
            a = argv[argc];
        }
        else {
            a++;
        }
    }

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-port") == 0 && argc > i + 1)
            port = str_getint(argv[i + 1]);
    }

    if (FindWindowA("HeartRateBroadcaster", NULL) != NULL)
        fatalError("Already running");

    HINSTANCE hInstance = GetModuleHandle(0);
    WNDCLASS wc = { 0 };
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "HeartRateBroadcaster";
    RegisterClass(&wc);
    HWND hwnd = CreateWindowA("HeartRateBroadcaster", "HeartRateBroadcaster", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, 
        hInstance, NULL);
    if (hwnd == NULL)
        fatalError("failed to create window");

    NOTIFYICONDATAA iconData = { 0 };
    iconData.cbSize = sizeof(iconData);
    iconData.hWnd = hwnd;
    strcpy(iconData.szTip, "HeartRateBroadcaster");
    iconData.uFlags = NIF_TIP | NIF_ICON | NIF_MESSAGE | NIF_SHOWTIP;
    iconData.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    iconData.uCallbackMessage = 123;
    iconData.uID = 1;
    iconData.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconA(NIM_ADD, &iconData))
        fatalError("failed to create tray icon");
    Shell_NotifyIconA(NIM_SETVERSION, &iconData);

    BLUETOOTH_FIND_RADIO_PARAMS params;
    params.dwSize = sizeof(params);
    HANDLE hRadio = NULL;
    HBLUETOOTH_RADIO_FIND hFind = BluetoothFindFirstRadio(&params, &hRadio);
    //BOOL res = BluetoothFindNextRadio(hFind, &hRadio);
    DEV_BROADCAST_HANDLE filter = { 0 };
    filter.dbch_size = sizeof(filter);
    filter.dbch_devicetype = DBT_DEVTYP_HANDLE;
    filter.dbch_handle = hRadio;
    HDEVNOTIFY hNotify = RegisterDeviceNotificationA(hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

    if (StartWebSocketServer(port) != 0)
        fatalError("Failed to start WebSocket server");

    CloseHandle(CreateThread(0, 0, (LPTHREAD_START_ROUTINE)ConnectHeartRateMonitor, 0, 0, NULL));

    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0) != 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    ExitProcess(0);
    return 0;
}


