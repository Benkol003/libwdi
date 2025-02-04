
//setupDi gives too many COM ports (possibly friendly names)
//CM_Get_Device.. errrors out

//  Get-WmiObject -Namespace ROOT\WMI MSSerial_PortName works for FTDI.
// Win32_SerialPort does not see the FTDI devices for some reasons
//try CM_Get_DevNode_PropertyW or SetupDiGetDevicePropertyW


#define WINVER _WIN32_WINNT_WIN10
#define _WIN32_WINNT _WIN32_WINNT_WIN10

#define INSTALLER_NAME L"winusb-installer.exe" //should be in same directory
#define PROCESS_TIMEOUT 120000 //takes 45 seconds for me

#pragma comment(lib,"setupapi.lib")
#pragma comment(lib,"cfgmgr32.lib")
#pragma comment(lib,"Newdev.lib")
#pragma comment(lib,"Shell32.lib")

#include <windows.h>
#include <initguid.h>
#include <devpropdef.h>
#include <devpkey.h>
#include <ntddser.h>
#include <winreg.h>
#include <newdev.h>
#include <shlobj.h>

#include <cfgmgr32.h>
#include <SetupAPI.h>

#include <vector>
#include <iostream>
#include <memory>

#include "setupDI_CM_wrapper.hpp"

//from winusb.inf.in in libwdi
#define DRIVER_PROVIDER L"libwdi"
#define INF_SECTION L"USB_Install"
constexpr GUID DRIVER_GUID { 0x88bae032,
    0x5a81,
    0x49f0,
    {0xbc,0x3d,
0xa4,0xff,0x13,0x82,0x16,0xd6}
};

/*
TODO:
check if location path specifies an interface ID. then specify vid,pid,iid and if composite device to libwdi.
check driver is signed (DNF_AUTHENTICODE_SIGNED )

looks like libwdi still adds the driver to all matching device -> multiple wdi_install_driver calls. redundant as after one install driver is compatible with all devices with matching hardware ID's, and 
causes the install to be a lot longer (so go fix this!)

NOTES:
checking if driver already installed allows to easily roll back driver. windows only keep one backup of prev driver, which will be overwritten if we install twice i.e. no backup.
*/



DWORD enumerateCOMPorts() {
    DWORD err;
    // GUID_DEVINTERFACE_SERENUM_BUS_ENUMERATOR; //also try GUID_DEVINTERFACE_COMPORT
    HDEVINFO_RAII devInfoSet(SetupDiGetClassDevsW(&GUID_DEVINTERFACE_COMPORT, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT),
        DevInfoSetDeleter);
    if (devInfoSet.get() == INVALID_HANDLE_VALUE) return GetLastError();
    //could possibly restrict enumeration to FTDI setup class guid, whatever that means
    DWORD diIdx = 0;
    std::unique_ptr<SP_DEVINFO_DATA> devInfo(new SP_DEVINFO_DATA);
    devInfo->cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    while (true) {
        if (!SetupDiEnumDeviceInfo(devInfoSet.get(), diIdx, devInfo.get())) {
                err = GetLastError(); if (err == ERROR_NO_MORE_ITEMS) {
                std::cout << "ran out of items\n" << std::endl;
                break;
            }
            else {
                err = GetLastError();
                return err;
            }
        }
        ++diIdx;
        printf("\n----device----\n");


        std::wstring devName; if (!SetupDiGetDevicePropertyString(devInfoSet.get(), devInfo.get(), &DEVPKEY_Device_FriendlyName, devName)) {
            std::cout<<"failed to get device friendly name"<<std::endl;
        }
        else {
            std::wcout<<L"friendly name: "<<devName<<std::endl;
        }

        //pci bus location info

        //&DEVPKEY_Device_LocationInfo
        //believe DEVPKEY_Device_PhysicalDeviceLocation is the binary data equivalent
        DEVINST parentDev; 
        if (CR_SUCCESS != CM_Get_Parent(&parentDev, devInfo->DevInst, NULL)) {
            std::cout << "failed to get parent devnode" << std::endl;
            return -1;
        }

        std::wstring devLoc;
        if (!CM_Get_DevNode_Property_String(parentDev, &DEVPKEY_Device_LocationInfo, devLoc)) {
            std::cout << "failed to get device location info" << std::endl;
        }
        else {
            std::wcout << L"device location: " << devLoc << std::endl;
        }

        std::vector<BYTE> devLocBinary;
        if (!CM_Get_DevNode_Property_Binary(parentDev, &DEVPKEY_Device_PhysicalDeviceLocation, devLocBinary)) {
            std::cout << "failed to get physical device location" << std::endl;
        }
        else {
            printf(" physical device location: 0x%x\n", devLocBinary[0]);            
        }
        

    }
    return 0;
}


//removes all drivers that match DRIVER_GUID (this may also )
DWORD cleanDeviceDrivers(HDEVINFO devInfoSet, PSP_DEVINFO_DATA devInfo) {
    std::unique_ptr<SP_DRVINFO_DATA_W> driverInfo(new SP_DRVINFO_DATA_W); driverInfo->cbSize = sizeof(SP_DRVINFO_DATA_W);
    std::unique_ptr<SP_DRVINFO_DETAIL_DATA_W> driverDetailInfo(new SP_DRVINFO_DETAIL_DATA_W); driverDetailInfo->cbSize = sizeof(SP_DRVINFO_DETAIL_DATA_W);

    DWORD idx = 0, duplicates = 0;
    DWORD err;
    if (!SetupDiBuildDriverInfoList(devInfoSet, devInfo, SPDIT_COMPATDRIVER)) {
        err = GetLastError();
        printf("failed to build driver info for device: %i\n", err);
        return err;
    }
    
    while (true) {
        if (!SetupDiEnumDriverInfoW(devInfoSet, devInfo, SPDIT_COMPATDRIVER, idx, driverInfo.get())) {
            err = GetLastError();
            if (err == ERROR_NO_MORE_ITEMS) {
                break;
            }
            else {
                printf("failed to enumerate device drivers: %i\n", err);
                return err;
            }
        }

        //get the inf class guid, then compare to check for removal
        DWORD szreq;

        //sizeof(SP_DRVINFO_DETAIL_DATA_W) is not allowed. just say that ->HardwareID has size 0 i.e. -1
        if(!SetupDiGetDriverInfoDetailW(devInfoSet, devInfo, driverInfo.get(), driverDetailInfo.get(), sizeof(SP_DRVINFO_DETAIL_DATA_W)-1, &szreq)) {
            err = GetLastError();
            if (err != ERROR_INSUFFICIENT_BUFFER) {
                printf("failed to get detailed device info.\n");
                return err;
            }
        }

        GUID guid_i; wchar_t className_i[MAX_CLASS_NAME_LEN];  DWORD nameSz;
        if (!SetupDiGetINFClassW(driverDetailInfo->InfFileName, &guid_i, className_i, MAX_CLASS_NAME_LEN, &nameSz)) {
            err = GetLastError(); wprintf(L"failed to get class GUID for driver inf %s\n", driverDetailInfo->InfFileName); return err;
        }

        if (IsEqualGUID(guid_i,DRIVER_GUID)==TRUE) {
            ++duplicates;
            //data we need will be filled in, not using the VLA for hardware id's id the drvinfo_detail struct
            BOOL reboot;
            //should i use
            if(!DiUninstallDriverW(NULL, driverDetailInfo->InfFileName, NULL, &reboot)) {
                err = GetLastError();
                printf("failed to uninstall driver.\n");
                return err;
            }
            if (reboot == TRUE) printf("reboot required.\n");
        }
        
        ++idx;
    }
    printf("drivers cleaned: %i\n", duplicates);
    return ERROR_SUCCESS;
}


//if no win32 error then returns ERROR_SUCCESS, even if no driver found.
DWORD selectLatestDriver(HDEVINFO devInfoSet, PSP_DEVINFO_DATA devInfo,     bool& foundDriver, SP_DRVINFO_DATA_W& latestDriver) {
    DWORD err; 

    if (!SetupDiBuildDriverInfoList(devInfoSet, devInfo, SPDIT_COMPATDRIVER)) {
        err = GetLastError();
        printf("failed to build driver info for device: %i\n", err);
        return err;
    }

    std::unique_ptr<SP_DRVINFO_DATA_W> driverInfo(new SP_DRVINFO_DATA_W); driverInfo->cbSize = sizeof(SP_DRVINFO_DATA_W);
    DWORD idx = 0;
    size_t foundDrivers = 0;
    ZeroMemory(&latestDriver, sizeof(SP_DRVINFO_DATA_W));
    latestDriver.cbSize = sizeof(SP_DRVINFO_DATA_W);

    while (true) {
        if (!SetupDiEnumDriverInfoW(devInfoSet, devInfo, SPDIT_COMPATDRIVER, idx, driverInfo.get())) {
            err = GetLastError();
            if (err == ERROR_NO_MORE_ITEMS) {
                break;
            }
            else {
                printf("failed to enumerate device drivers: %i\n", err);
                return err;
            }
        }


        if (wcscmp(driverInfo->ProviderName, DRIVER_PROVIDER) == 0) {

            SYSTEMTIME created;
            FileTimeToSystemTime(&driverInfo->DriverDate, &created);
            //libwdi drivers have dates but time is at midnight / 0...

            if (foundDrivers == 0) {
                memcpy(&latestDriver, driverInfo.get(), sizeof(SP_DRVINFO_DATA_W));
            }
            else {
                if (1 == CompareFileTime(&driverInfo->DriverDate, &latestDriver.DriverDate)) {
                    memcpy(&latestDriver, driverInfo.get(), sizeof(SP_DRVINFO_DATA_W));
                }
            }
            ++foundDrivers;
        }
        ++idx;
    }

    wprintf(L"found %i drivers matching provider '" DRIVER_PROVIDER L"'\n", foundDrivers);

    foundDriver = foundDrivers!=0;
    return ERROR_SUCCESS; //even if we dont find a driver at least we know there wasnt a win32 error
}

DWORD installDriverToStore(HDEVINFO devInfoSet, PSP_DEVINFO_DATA devInfo) {

    std::unique_ptr<wchar_t> path(new wchar_t[512]);
    DWORD pathSz = GetModuleFileNameW(NULL, path.get(), 512);
    if (pathSz == 512) { printf("failed to get path to this executable.\n"); return ERROR_INSUFFICIENT_BUFFER; }
    if (pathSz == 0) { printf("failed to get path to this executable.\n"); return GetLastError(); }

    //get exe dir
    for (int i = pathSz - 2; i >= 0; --i) {
        if (path.get()[i] == '/' || path.get()[i] == '\\') {
            path.get()[i + 1] = '\0'; break;
        }
    }
    std::wstring installerPath(path.get()); installerPath += INSTALLER_NAME;

    PROCESS_INFORMATION pi;
    STARTUPINFOW si;

    std::unique_ptr<wchar_t> cmdLine(new wchar_t[512]); cmdLine.get()[0] = '\0'; wcscat_s(cmdLine.get(), 512, installerPath.c_str());
    wcscat_s(cmdLine.get(), 512, L" -l 1");

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;

    if (0 == CreateProcessW(installerPath.c_str(), cmdLine.get(), NULL, NULL, TRUE, NULL, NULL, path.get(), &si, &pi)) {
        printf("failed to start installer process\n");
        return GetLastError();
    }

    DWORD err = WaitForSingleObject(pi.hProcess, PROCESS_TIMEOUT);
    if (err != WAIT_OBJECT_0) {
        if (err == WAIT_TIMEOUT) {
            printf("timed out waiting for installer to finish\n"); return ERROR_TIMEOUT;
        }
        if (err == WAIT_ABANDONED) {
            printf("wait abandoned (see WaitForSingleObject docs)\n"); return ERROR_ABANDONED_WAIT_0;
        }
        else {
            printf("error waiting for installer process to finish.\n"); return GetLastError();
        }
    }

   
    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    if (exitCode != 0) {
        printf("installer process failed with non-zero exit code: %i\n", exitCode);
        return ERROR_PROCESS_ABORTED;
    }
    else {
        printf("installer exited successfully.\n");
    }
    

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return ERROR_SUCCESS;
}

DWORD isDriverAlreadyInstalled(HDEVINFO devInfoSet, PSP_DEVINFO_DATA devInfo,bool& isInstalled) {

    DWORD err;

    if (!SetupDiBuildDriverInfoList(devInfoSet, devInfo, SPDIT_COMPATDRIVER)) {
        err = GetLastError();
        printf("failed to build driver info for device: %i\n", err);
        return err;
    }

    bool hasDriver = false;
    std::unique_ptr<SP_DRVINFO_DATA_W> installedDriver(new SP_DRVINFO_DATA_W); installedDriver->cbSize = sizeof(SP_DRVINFO_DATA_W);

    ////////// check if already installed /////////////
    printf("Current installed driver info:\n");
    std::wstring dps;
    if (SetupDiGetDevicePropertyString(devInfoSet, devInfo, &DEVPKEY_Device_Driver, dps)) wprintf(L"    GUID: %s\n", dps.c_str());
    if (SetupDiGetDevicePropertyString(devInfoSet, devInfo, &DEVPKEY_Device_DriverDesc, dps)) wprintf(L"    Description: %s\n", dps.c_str());
    if (SetupDiGetDevicePropertyString(devInfoSet, devInfo, &DEVPKEY_Device_DriverInfPath, dps)) wprintf(L"    INF Path: %s\n", dps.c_str());
    if (SetupDiGetDevicePropertyString(devInfoSet, devInfo, &DEVPKEY_Device_DriverInfSection, dps)) wprintf(L"    INF Section: %s\n", dps.c_str());
    if (SetupDiGetDevicePropertyString(devInfoSet, devInfo, &DEVPKEY_Device_DriverInfSectionExt, dps)) wprintf(L"    Extended INF Section: %s\n", dps.c_str());
    if (SetupDiGetDevicePropertyString(devInfoSet, devInfo, &DEVPKEY_Device_DriverProvider, dps)) wprintf(L"    Provider: %s\n", dps.c_str());
    if (SetupDiGetDevicePropertyString(devInfoSet, devInfo, &DEVPKEY_Device_FriendlyName, dps)) wprintf(L"    Friendly Name: %s\n", dps.c_str());

    GUID driverGUID;
    if (!SetupDiGetDevicePropertyGUID(devInfoSet, devInfo, &DEVPKEY_Device_ClassGuid, driverGUID)) {
        printf("failed to get GUID for installed driver\n");
        return ERROR_NOT_FOUND;
    }
    else {
        if (IsEqualGUID(driverGUID, DRIVER_GUID)) {
            printf("driver already installed.\n");
            isInstalled = true;
        }
        else {
            isInstalled = false;
        }
        return ERROR_SUCCESS;
    }
}

DWORD installDriverForDevice(HDEVINFO devInfoSet, PSP_DEVINFO_DATA devInfo,SP_DRVINFO_DATA_W& latestDriver){
    DWORD idx = 0;
    DWORD err;

    if (!SetupDiBuildDriverInfoList(devInfoSet, devInfo, SPDIT_COMPATDRIVER)) {
        err = GetLastError();
        printf("failed to build driver info for device: %i\n", err);
        return err;
    }

    ///////////////////////////////////////////////
    printf("will install the following driver for device:\n");
    wprintf(L"    description: %s\n", latestDriver.Description);
    wprintf(L"    manufacturer name: %s\n", latestDriver.MfgName);
    wprintf(L"    provider name: %s\n", latestDriver.ProviderName);
    SYSTEMTIME created;
    FileTimeToSystemTime(&latestDriver.DriverDate, &created);
    wprintf(L"    " DRIVER_PROVIDER " driver date : %i/%i/%i\n", created.wDay, created.wMonth, created.wYear);
    wprintf(L"    " DRIVER_PROVIDER " driver version: %u\n", latestDriver.DriverVersion);

    
    if (!IsUserAnAdmin()) {
        printf("not running as admin, cant install driver.\n");
        return ERROR_ACCESS_DENIED;
    }
    

    //install for device
    BOOL reboot;
    if (!DiInstallDevice(NULL, devInfoSet, devInfo, &latestDriver, NULL, &reboot)) {
        err = GetLastError();
        printf("failed to install driver: %i\n", err);
        return err;
    }
    printf("driver successfully installed\n");
    if (reboot == TRUE) printf("reboot required\n");
    return ERROR_SUCCESS;
}

DWORD findDevNode(const wchar_t* devLocationPath,HDEVINFO_RAII& rDevInfoSet, SP_DEVINFO_DATA& rDevInfo) {
    DWORD err;

    //&GUID_DEVINTERFACE_USB_DEVICE doesnt actually list the FTDI 'USB Serial Converter x' devices
    HDEVINFO_RAII devInfoSet(SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT)
    ,DevInfoSetDeleter);

    if (devInfoSet.get() == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD diIdx = 0;
    SP_DEVINFO_DATA devInfo;  devInfo.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    bool found = false;
    while (true) {
        if (!SetupDiEnumDeviceInfo(devInfoSet.get(), diIdx, &devInfo)) {
            err = GetLastError(); if (err == ERROR_NO_MORE_ITEMS) {
                wprintf(L"failed to find device with location '%s'\n",devLocationPath);
                return ERROR_NOT_FOUND;
            }
            else {
                err = GetLastError();
                return err;
            }
        }
        ++diIdx;

        std::vector<std::wstring> locationPaths;
        if (!SetupDiGetDevicePropertyStringList(devInfoSet.get(), &devInfo, &DEVPKEY_Device_LocationPaths, locationPaths)) {
            //std::cout << "  failed to get location paths\n" << std::endl;
        } else {

            if (locationPaths.end()!=std::find(locationPaths.begin(), locationPaths.end(), devLocationPath)) {
                printf("matched location path to device:\n"); found = true;

                printf("    location paths:\n");
                for (auto& wsi : locationPaths) {
                    wprintf(L"        %s\n", wsi.c_str());
                }

                /////////
                std::wstring devDesc; if (!SetupDiGetDevicePropertyString(devInfoSet.get(),  &devInfo, &DEVPKEY_Device_DeviceDesc, devDesc)) {
                    std::cout << "  failed to get description" << std::endl;
                }
                else {
                    std::wcout << L"  description: " << devDesc << std::endl;
                }

                std::wstring devMan; if (!SetupDiGetDevicePropertyString(devInfoSet.get(), &devInfo, &DEVPKEY_Device_Manufacturer, devMan)) {
                    std::cout << "  failed to get manufacturer" << std::endl;
                }
                else {
                    std::wcout << L"  manufacturer: " << devMan << std::endl;
                }
                /////////

                rDevInfoSet.swap(devInfoSet); rDevInfo = devInfo;
             
                break;
            }
        }
    }
    return ERROR_SUCCESS;
}

//dont use CM_.. / cfgmgr32 api as theres less information to get and doesnt seem to work

void handle_win32err(DWORD err) {
    if (err != ERROR_SUCCESS) {
        char errmsg[1024];
        const char* args[1] = { "%s" };
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ARGUMENT_ARRAY, NULL, err, 0, errmsg, 1024, (va_list*)args);
        printf("win32 error: %s", errmsg);
        exit(err);
    }
    return;
}

int wmain(int argc, wchar_t* argv[]){
    bool rollback = false, clean = false;

    if (argc < 2) {
        printf("please provide the device location path of the device"); return 0;
    }
    for (int i = 0; i < argc; ++i) {
        if (_wcsicmp(L"-h", argv[i])==0 || _wcsicmp(L"--help", argv[i])==0) {
            printf("Usage: ..exe [options] device_location_path\n");
            printf("Options:\n");
            printf("-h, --help: display this information\n");
            printf("-r: rollback driver for this device\n");
            printf("-c: clean device-associated driver duplicates of the driver to be installed\n");
            //printf("-t: test run - install driver then rollback"); //TODO try reinstall best driver
            //wprintf(L"-p [name]: driver provider name to use, default:" DRIVER_PROVIDER);

            return 0;
        }
        if (_wcsicmp(L"-r", argv[i]) == 0) rollback = true;
        if (_wcsicmp(L"-c", argv[i]) == 0) clean = true;

    }

    const wchar_t* devLocationPath = argv[1];
    HDEVINFO_RAII devInfoSet(nullptr, DevInfoSetDeleter); SP_DEVINFO_DATA devInfo;

    handle_win32err(findDevNode(argv[1], devInfoSet, devInfo));

    if (clean) {
        handle_win32err(cleanDeviceDrivers(devInfoSet.get(), &devInfo));
        return 0;
        //device information set is now out of date
        //handle_win32err(findDevNode(argv[1], devInfoSet, devInfo));
        
    }

    if (rollback) {
        if (!IsUserAnAdmin()) {
            printf("not running as admin, cant install driver.\n");
            return ERROR_ACCESS_DENIED;
        }
        BOOL reboot;
        if (!DiRollbackDriver(devInfoSet.get(), &devInfo, NULL, ROLLBACK_FLAG_NO_UI, &reboot)) {
            printf("failed to rollback driver.\n");
            DWORD err = GetLastError();
            if (err == ERROR_NO_MORE_ITEMS) printf("no previous driver to roll back to.\n");
            return err;
        }
        printf("rolled back driver.\n");
        if (reboot == TRUE) printf("reboot required\n");
        return ERROR_SUCCESS;
    }
    else {
        bool alreadyInst;
        handle_win32err(isDriverAlreadyInstalled(devInfoSet.get(), &devInfo, alreadyInst));
        if (alreadyInst) return 0;
        bool foundDriver; SP_DRVINFO_DATA_W latestDrv;
        handle_win32err(selectLatestDriver(devInfoSet.get(), &devInfo, foundDriver, latestDrv));
        if (!foundDriver) {
            handle_win32err(installDriverToStore(devInfoSet.get(), &devInfo));
            //need to rebuild devInfoSet
            handle_win32err(findDevNode(argv[1], devInfoSet, devInfo));
        }
        
        //try again
        handle_win32err(selectLatestDriver(devInfoSet.get(), &devInfo, foundDriver, latestDrv));
        if (!foundDriver) {
            printf("failed to select driver for device even after installing to driver store.\n");
            return -1;
        }
        else {
            handle_win32err(installDriverForDevice(devInfoSet.get(), &devInfo, latestDrv));
        }
        printf("successfully installed driver for device.\n");

    }
    return 0;
}
