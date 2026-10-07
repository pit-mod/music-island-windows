#include "../src/Settings.h"
#include <iostream>
#include <stdexcept>

int main() {
    wchar_t directory[MAX_PATH]{},location[MAX_PATH]{};
    if(!GetTempPathW(MAX_PATH,directory)||!GetTempFileNameW(directory,L"MIS",0,location))return 1;
    unsigned checks=0;
    auto require=[&](bool passed,const char* message){++checks;if(!passed)throw std::runtime_error(message);};
    try {
        island::Settings defaults(location);
        require(!defaults.pin&&!defaults.reduced&&!defaults.hover&&!defaults.arrows&&defaults.scale==1&&defaults.monitorDevice.empty(),"new config must use the current defaults");
        // Existing releases wrote an ANSI INI file; keep those preferences.
        WritePrivateProfileStringW(L"island",L"arrows",L"1",location);
        WritePrivateProfileStringW(L"island",L"monitor",L"\\\\.\\DISPLAY3",location);
        island::Settings legacy(location);
        require(legacy.arrows&&legacy.monitorDevice==L"\\\\.\\DISPLAY3","existing config must survive the persistence upgrade");
        legacy.pin=legacy.reduced=legacy.hover=true;legacy.scale=1.2;legacy.monitorDevice=L"\\\\.\\DISPLAY7";
        require(legacy.save(),"updated preferences must be saved");
        island::Settings restored(location);
        require(restored.pin&&restored.reduced&&restored.hover&&restored.arrows&&std::abs(restored.scale-1.2)<.000001&&restored.monitorDevice==legacy.monitorDevice,"a new app instance must restore every saved option and monitor");
        restored.pin=restored.reduced=restored.hover=restored.arrows=false;restored.scale=.9;
        require(restored.save(),"a second settings change must save immediately");
        island::Settings changed(location);
        require(!changed.pin&&!changed.reduced&&!changed.hover&&!changed.arrows&&std::abs(changed.scale-.9)<.000001,"disabling options must persist as well as enabling them");
        HANDLE locked=CreateFileW(location,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(locked!=INVALID_HANDLE_VALUE,"persistence failure check must lock the previous config");
        changed.arrows=true;bool saved=changed.save();CloseHandle(locked);
        require(!saved,"a blocked replacement must report failure");
        island::Settings afterFailure(location);
        require(!afterFailure.arrows&&std::abs(afterFailure.scale-.9)<.000001,"a failed replacement must preserve the complete previous config");
        auto temporary=std::wstring(location)+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
        require(GetFileAttributesW(temporary.c_str())==INVALID_FILE_ATTRIBUTES,"failed saves must clean up their temporary file");
        for(const wchar_t* value:{L"nan",L"garbage",L"1.2garbage"}) {
            WritePrivateProfileStringW(L"island",L"scale",value,location);
            island::Settings invalid(location);
            require(invalid.scale==1,"invalid saved scale must fall back safely");
        }
        WritePrivateProfileStringW(L"island",L"scale",L"8",location);
        require(island::Settings(location).scale==1.4,"out-of-range scale must stay within the supported size");
        DeleteFileW(location);
        std::cout<<"PASS: "<<checks<<" config defaults, legacy migration, restart restoration, repeated changes, failed-save preservation and invalid-value checks\n";
        return 0;
    }catch(const std::exception& error){DeleteFileW(location);std::cerr<<error.what()<<'\n';return 1;}
}
