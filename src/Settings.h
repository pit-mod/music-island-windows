#pragma once
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace island {
struct Settings {
    std::wstring file,monitorDevice;
    bool pin=false,reduced=false,hover=false,arrows=false;
    double scale=1;

    Settings() {
        PWSTR directory=nullptr;
        if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&directory))) {
            auto folder=std::filesystem::path(directory)/L"MusicIslandWindows";
            CoTaskMemFree(directory);
            std::error_code error;
            std::filesystem::create_directories(folder,error);
            if(error)return;
            DWORD attributes=GetFileAttributesW(folder.c_str());
            if(attributes!=INVALID_FILE_ATTRIBUTES)SetFileAttributesW(folder.c_str(),attributes|FILE_ATTRIBUTE_HIDDEN);
            file=(folder/L"settings.ini").wstring();
            load();
        }
    }
    // An explicit location lets persistence checks use their own temporary files.
    explicit Settings(const std::filesystem::path& location):file(location.wstring()) {load();}

    void load() {
        if(file.empty())return;
        pin=GetPrivateProfileIntW(L"island",L"pin",0,file.c_str())!=0;
        reduced=GetPrivateProfileIntW(L"island",L"reduced",0,file.c_str())!=0;
        hover=GetPrivateProfileIntW(L"island",L"hover",0,file.c_str())!=0;
        arrows=GetPrivateProfileIntW(L"island",L"arrows",0,file.c_str())!=0;
        wchar_t monitorName[64]{};
        GetPrivateProfileStringW(L"island",L"monitor",L"",monitorName,64,file.c_str());
        monitorDevice=monitorName;
        wchar_t value[64]{},*end=nullptr;
        GetPrivateProfileStringW(L"island",L"scale",L"1",value,64,file.c_str());
        double parsed=wcstod(value,&end);
        scale=std::isfinite(parsed)&&end!=value&&*end==0?std::clamp(parsed,.75,1.4):1;
    }

    bool save() const {
        if(file.empty())return false;
        std::error_code error;
        auto folder=std::filesystem::path(file).parent_path();
        if(!folder.empty())std::filesystem::create_directories(folder,error);
        if(error)return false;
        // Replace the complete file only after it is written and flushed. A
        // failed save leaves the previous config intact instead of half updated.
        std::wstring text=L"\xFEFF[island]\r\n";
        auto flag=[&](const wchar_t* name,bool enabled){text+=name;text+=enabled?L"=1\r\n":L"=0\r\n";};
        flag(L"pin",pin);flag(L"reduced",reduced);flag(L"hover",hover);flag(L"arrows",arrows);
        text+=L"monitor="+monitorDevice+L"\r\nscale="+std::to_wstring(std::isfinite(scale)?std::clamp(scale,.75,1.4):1)+L"\r\n";
        auto temporary=file+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
        HANDLE handle=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE)return false;
        DWORD written=0,bytes=static_cast<DWORD>(text.size()*sizeof(wchar_t));
        bool success=WriteFile(handle,text.data(),bytes,&written,nullptr)&&written==bytes&&FlushFileBuffers(handle);
        CloseHandle(handle);
        if(success)success=MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
        if(!success)DeleteFileW(temporary.c_str());
        return success;
    }
};
}
