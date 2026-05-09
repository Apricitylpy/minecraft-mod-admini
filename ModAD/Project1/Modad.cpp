#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <windows.h>
#include <wininet.h>

#pragma comment(lib, "wininet.lib")

using namespace std;

// 下载文件
bool DownloadFile(const string& url, const string& savePath)
{
    HINTERNET hInternet = InternetOpenA(
        "MinecraftModDownloader",
        INTERNET_OPEN_TYPE_DIRECT,
        NULL,
        NULL,
        0
    );

    if (!hInternet)
    {
        cout << "无法初始化网络\n";
        return false;
    }

    HINTERNET hFile = InternetOpenUrlA(
        hInternet,
        url.c_str(),
        NULL,
        0,
        INTERNET_FLAG_RELOAD,
        0
    );

    if (!hFile)
    {
        cout << "下载失败:\n" << url << endl;

        InternetCloseHandle(hInternet);
        return false;
    }

    ofstream out(savePath, ios::binary);

    if (!out.is_open())
    {
        cout << "无法创建文件:\n" << savePath << endl;

        InternetCloseHandle(hFile);
        InternetCloseHandle(hInternet);

        return false;
    }

    char buffer[4096];
    DWORD bytesRead = 0;

    while (InternetReadFile(hFile, buffer, sizeof(buffer), &bytesRead) && bytesRead)
    {
        out.write(buffer, bytesRead);
    }

    out.close();

    InternetCloseHandle(hFile);
    InternetCloseHandle(hInternet);

    return true;
}

// 从 URL 获取文件名
string GetFileName(const string& url)
{
    size_t pos = url.find_last_of('/');

    if (pos == string::npos)
        return "unknown.jar";

    return url.substr(pos + 1);
}

int main()
{
    // UTF-8 控制台
    SetConsoleOutputCP(CP_UTF8);

    cout << "============================\n";
    cout << " Minecraft 模组下载器\n";
    cout << "============================\n";

    // 创建 mods 文件夹
    CreateDirectoryA("mods", NULL);

    // 打开 JSON
    ifstream file("mods.json");

    if (!file.is_open())
    {
        cout << "无法读取 mods.json\n";
        cout << "请确认文件位于 exe 同目录\n";

        system("pause");
        return 0;
    }

    // 读取全部内容
    string content(
        (istreambuf_iterator<char>(file)),
        istreambuf_iterator<char>()
    );

    file.close();

    vector<string> urls;

    size_t pos = 0;

    // 查找 downloads
    while (true)
    {
        pos = content.find("\"downloads\"", pos);

        if (pos == string::npos)
            break;

        // 找 [
        pos = content.find("[", pos);

        if (pos == string::npos)
            break;

        // 找第一个 "
        size_t start = content.find("\"", pos);

        if (start == string::npos)
            break;

        start++;

        // 找结束 "
        size_t end = content.find("\"", start);

        if (end == string::npos)
            break;

        string url = content.substr(start, end - start);

        urls.push_back(url);

        pos = end;
    }

    if (urls.empty())
    {
        cout << "未找到模组下载链接\n";

        system("pause");
        return 0;
    }

    cout << "找到 " << urls.size() << " 个模组\n";

    // 开始下载
    for (size_t i = 0; i < urls.size(); i++)
    {
        string url = urls[i];

        string filename = GetFileName(url);

        string savePath = "mods/" + filename;

        cout << "[" << (i + 1) << "/" << urls.size() << "] ";
        cout << filename << endl;

        bool ok = DownloadFile(url, savePath);

        if (ok)
        {
            cout << "下载完成\n";
        }
        else
        {
            cout << "下载失败\n";
        }
    }

    cout << "全部下载完成！\n";

    system("pause");

    return 0;
}