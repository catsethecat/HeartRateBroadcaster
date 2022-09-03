#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <wincrypt.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <SetupAPI.h>
#include <bluetoothleapis.h>
#include <bthledef.h>

#pragma comment (lib,"Ws2_32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "bluetoothapis.lib")
#pragma comment (lib, "crypt32.lib")

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

int ipow(int base, int exponent) {
    if (!exponent)
        return 1;
    int res = base;
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
        total += (*digit - '0') * ipow(10, i);
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
        int b = num % ipow(10, i + 1);
        tmp[14 - i] = '0' + b / ipow(10, i);
        num -= b;
    }
    if (str)
        memcpy(str, tmp + 15 - i, i + 1);
    return i + negative;
}

int float_to_str(float num, char* str) {
    int res = int_to_str((int)num, str);
    if (num < 0)
        num *= -1;
    str[res] = '.';
    res++;
    res += int_to_str((int)((num-(int)num)*1000000), str + res);
    return res;
}

int printf(char* fmt, ...) {
    char msg[1024] = { 0 };
    int msgLen = 0;
    va_list ap;
    va_start(ap, fmt);
    for (char* c = fmt; *c; c++) {
        if (*c == '%') {
            c++;
            if (*c == 'i') {
                msgLen += int_to_str(va_arg(ap, int),msg+msgLen);
            }
            if (*c == 'f') {
                msgLen += float_to_str((float)va_arg(ap, double), msg + msgLen);
            }
            if (*c == 's') {
                char* str = va_arg(ap, char*);
                strcpy(msg + msgLen, str);
                msgLen += (int)strlen(str);
            }
        }
        else {
            msg[msgLen] = *c;
            msgLen++;
        }
    }
    va_end(ap);
    static HANDLE hStdOut = NULL;
    if (hStdOut == NULL)
        hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(hStdOut, msg, msgLen, NULL, NULL);
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------

void fatalError(char* msg) {
    //MessageBoxA(NULL, msg, "Fatal error", MB_ICONERROR);
    printf("Fatal error: %s\n", msg);
    Sleep(5000);
	ExitProcess(0);
}

#include "websocketserver.c"

void OnCharacteristicValueChanged(BTH_LE_GATT_EVENT_TYPE EventType, PVOID EventOutParameter, PVOID Context) {
    BLUETOOTH_GATT_VALUE_CHANGED_EVENT* event = (BLUETOOTH_GATT_VALUE_CHANGED_EVENT*)EventOutParameter;
    WebSocketBroadcast(event->CharacteristicValue->Data, (unsigned char)event->CharacteristicValue->DataSize);
}

#ifdef _DEBUG
int main(int _argc, char** _argv)
#else
int mainCRTStartup()
#endif
{
    printf("HeartRateBroadcaster v1.0.0 (c) 2022 Catse\n\n");

    char* cmdline = GetCommandLineA();
    int argc = 0;
    char argv[8][1024];
    for (char* c = cmdline, *a = argv[argc]; c == cmdline || *(c-1) != 0; c++) {
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

    unsigned short port = 2752;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-port") == 0 && argc > i + 1)
            port = str_getint(argv[i + 1]);
    }

    if (StartWebSocketServer(port) != 0)
        fatalError("Failed to start WebSocket server");

    // get handle to the first BLE heart rate monitor found
    GUID classGuid = *(GUID*)"\x0d\x18\x00\x00\x00\x00\x00\x10\x80\x00\x00\x80\x5f\x9b\x34\xfb"; //{0000180D-0000-1000-8000-00805F9B34FB}
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&classGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA interfaceData = { 0 };
    interfaceData.cbSize = sizeof(interfaceData);
    BOOL succ = SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &classGuid, 0, &interfaceData);
    if (!succ)
        fatalError("No devices found. Make sure your device is paired and shows up in Windows settings.");
    printf("Device found, connecting...\n");
    char buf[1024];
    PSP_DEVICE_INTERFACE_DETAIL_DATA_A pDetails = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)buf;
    pDetails->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
    succ = SetupDiGetDeviceInterfaceDetailA(hDevInfo, &interfaceData, pDetails, sizeof(buf), NULL, NULL);
    if (!succ)
        fatalError("failed to get device details");
    SetupDiDestroyDeviceInfoList(hDevInfo);
    HANDLE hDev = CreateFileA(pDetails->DevicePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDev == INVALID_HANDLE_VALUE)
        fatalError("failed to get device handle");

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
    res = BluetoothGATTGetCharacteristics(hDev, pHeartRateService, 16, characteristics, &numCharacteristics, BLUETOOTH_GATT_FLAG_NONE);
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
    res = BluetoothGATTGetDescriptors(hDev, pHRMeasurementCharacteristic, 16, descriptors, &numDescriptors, BLUETOOTH_GATT_FLAG_NONE);
    if (res != S_OK)
        fatalError("failed to get GATT descriptors");
    PBTH_LE_GATT_DESCRIPTOR pConfigDescriptor = 0;
    for (int i = 0; i < numDescriptors; i++)
        if (descriptors[i].DescriptorType == ClientCharacteristicConfiguration)
            pConfigDescriptor = &descriptors[i];
    if (!pConfigDescriptor)
        fatalError("no client characteristic configuration descriptor found");

    BTH_LE_GATT_DESCRIPTOR_VALUE descriptorValue;
    memset(&descriptorValue, 0, sizeof(descriptorValue));
    descriptorValue.DescriptorType = ClientCharacteristicConfiguration;
    descriptorValue.ClientCharacteristicConfiguration.IsSubscribeToNotification = TRUE;
    while (BluetoothGATTSetDescriptorValue(hDev, pConfigDescriptor, &descriptorValue, BLUETOOTH_GATT_FLAG_NONE) != S_OK) {
        printf("descriptor value write failed, retrying...\n");
        Sleep(3000);
    }

    BLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION eventParams = { 0 };
    eventParams.Characteristics[0] = *pHRMeasurementCharacteristic;
    eventParams.NumCharacteristics = 1;
    BLUETOOTH_GATT_EVENT_HANDLE hEvent;
    res = BluetoothGATTRegisterEvent(hDev, CharacteristicValueChangedEvent, &eventParams, (PFNBLUETOOTH_GATT_EVENT_CALLBACK)OnCharacteristicValueChanged, NULL, &hEvent, BLUETOOTH_GATT_FLAG_NONE);
    if (res != S_OK)
        fatalError("failed to register value change event");

    printf("Heart rate monitor connected\n");

    Sleep(INFINITE);
    return 0;
}


