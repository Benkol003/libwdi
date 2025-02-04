#pragma once

#include <windows.h>
#include <cfgmgr32.h>
#include <SetupAPI.h>

#include <string>
#include <memory>
#include <vector>



static void DevInfoSetDeleter(HDEVINFO h) {
    if (h != INVALID_HANDLE_VALUE && h != 0) SetupDiDestroyDeviceInfoList(h);
}

typedef std::unique_ptr<std::remove_pointer_t<HDEVINFO>, decltype(&DevInfoSetDeleter)> HDEVINFO_RAII;

bool SetupDiGetDevicePropertyStringList(HDEVINFO devInfoSet, PSP_DEVINFO_DATA devInfo, const DEVPROPKEY* propKey, std::vector<std::wstring>& value) {
    DWORD err;
    DEVPROPTYPE propType;
    DWORD propBufSz = 0;

    //friendly name

    if (!SetupDiGetDevicePropertyW(devInfoSet, devInfo, propKey, &propType, NULL, 0, &propBufSz, 0)) {
        err = GetLastError();
        if (err != ERROR_INSUFFICIENT_BUFFER) {
            return false;
        }
    }
    std::unique_ptr<BYTE> propBuf(new BYTE[propBufSz]);

    if (!SetupDiGetDevicePropertyW(devInfoSet, devInfo, propKey, &propType, propBuf.get(), propBufSz, &propBufSz, 0)) {
        return false;
    }
    else {
        if (propType != DEVPROP_TYPE_STRING_LIST) {
            return false;
        }
        else {
            value.clear();
            for (size_t i = 0; i < propBufSz; ) {
                value.emplace_back((wchar_t*)(propBuf.get() + i));
                //bytes in string + 1 (null char) +1 next string
                i+= sizeof(wchar_t) * wcsnlen_s((wchar_t*)(propBuf.get() + i), propBufSz)+2;

            }
            return true;
        }
    }
}


bool SetupDiGetDevicePropertyString(HDEVINFO devInfoSet, PSP_DEVINFO_DATA devInfo, const DEVPROPKEY* propKey, std::wstring& value) {
    DWORD err;
    DEVPROPTYPE propType;
    DWORD propBufSz = 0;

    //friendly name

    if (!SetupDiGetDevicePropertyW(devInfoSet, devInfo, propKey, &propType, NULL, 0, &propBufSz, 0)) {
        err = GetLastError();
        if (err != ERROR_INSUFFICIENT_BUFFER) {
            return false;
        }
    }
    std::unique_ptr<BYTE> propBuf(new BYTE[propBufSz]);

    if (!SetupDiGetDevicePropertyW(devInfoSet, devInfo, propKey, &propType, propBuf.get(), propBufSz, &propBufSz, 0)) {
        return false;
    }
    else {
        if (propType != DEVPROP_TYPE_STRING) {
            return false;
        }
        else {
            value = std::wstring((wchar_t*)propBuf.get(), wcsnlen_s((wchar_t*)propBuf.get(),propBufSz));
        }
    }
    return true;
}

bool SetupDiGetDevicePropertyGUID(HDEVINFO devInfoSet, PSP_DEVINFO_DATA devInfo, const DEVPROPKEY* propKey, GUID &value) {
    DWORD err;
    DEVPROPTYPE propType;
    DWORD propBufSz = 0;

    //friendly name

    if (!SetupDiGetDevicePropertyW(devInfoSet, devInfo, propKey, &propType, NULL, 0, &propBufSz, 0)) {
        err = GetLastError();
        if (err != ERROR_INSUFFICIENT_BUFFER) {
            return false;
        }
    }
    std::unique_ptr<BYTE> propBuf(new BYTE[propBufSz]);

    if (!SetupDiGetDevicePropertyW(devInfoSet, devInfo, propKey, &propType, propBuf.get(), propBufSz, &propBufSz, 0)) {
        return false;
    }
    else {
        if (propType != DEVPROP_TYPE_GUID) {
            return false;
        }
        else {
            memcpy(&value, propBuf.get(), sizeof(GUID));
        }
    }
    return true;
}


bool CM_Get_DevNode_Property_String(DEVINST devInst, const DEVPROPKEY* propKey, std::wstring& value) {
    DWORD err;
    ULONG bufSz = 0; DEVPROPTYPE propType;
    if (CR_BUFFER_SMALL != (err = CM_Get_DevNode_PropertyW(devInst, propKey, &propType, NULL, &bufSz, 0))) {
        return false;
    }
    std::unique_ptr<BYTE> propBuf(new BYTE[bufSz]);
    if (CR_SUCCESS != (err = CM_Get_DevNode_PropertyW(devInst, propKey, &propType, propBuf.get(), &bufSz, 0))) {
        return false;
    }
    if (propType != DEVPROP_TYPE_STRING) {
        return false;
    }
    value = std::wstring((wchar_t*)propBuf.get(), wcsnlen_s((wchar_t*)propBuf.get(),bufSz));
    return true;
}

bool CM_Get_DevNode_Property_Binary(DEVINST devInst, const DEVPROPKEY* propKey, std::vector<BYTE>& value) {
    DWORD err;
    ULONG bufSz = 0; DEVPROPTYPE propType;
    if (CR_BUFFER_SMALL != (err = CM_Get_DevNode_PropertyW(devInst, propKey, &propType, NULL, &bufSz, 0))) {
        return false;
    }
    std::unique_ptr<BYTE> propBuf(new BYTE[bufSz]);
    if (CR_SUCCESS != (err = CM_Get_DevNode_PropertyW(devInst, propKey, &propType, propBuf.get(), &bufSz, 0))) {
        return false;
    }
    if (propType != DEVPROP_TYPE_BINARY) {
        return false;
    }

    value = std::vector<BYTE>(propBuf.get(), propBuf.get() + (bufSz));
    return true;
}
