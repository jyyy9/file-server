#include "client/console_ui.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>

namespace fileserver {
namespace client {

ConsoleUI::ConsoleUI(const std::string& host, int port)
    : host_(host), port_(port) {}

ConsoleUI::~ConsoleUI() = default;

void ConsoleUI::Run() {
    std::cout << "\n正在连接 " << host_ << ":" << port_ << " ..." << std::endl;
    if (!client_.Connect(host_, port_)) {
        std::cerr << "连接失败!" << std::endl;
        return;
    }
    std::cout << "已连接!" << std::endl;

    while (running_) {
        ShowMenu();
        std::string choice = ReadLine("请选择");
        if      (choice == "1") DoRegister();
        else if (choice == "2") DoLogin();
        else if (choice == "3") DoUpload();
        else if (choice == "4") DoDownload();
        else if (choice == "5") DoListFiles();
        else if (choice == "6") DoDeleteFile();
        else if (choice == "7") DoResumeUpload();
        else if (choice == "0") { std::cout << "再见!\n"; running_ = false; }
        else std::cout << "无效选择\n";
    }
}

void ConsoleUI::ShowMenu() {
    std::cout << "\n========================================\n"
              << "  FileServer 客户端\n"
              << "========================================\n"
              << "  1. 注册\n"
              << "  2. 登录\n"
              << "  3. 上传文件\n"
              << "  4. 下载文件\n"
              << "  5. 查看文件列表\n"
              << "  6. 删除文件\n"
              << "  7. 断点续传\n"
              << "  0. 退出\n"
              << "========================================\n";
    if (!client_.GetToken().empty())
        std::cout << "  已登录, token: "
                  << client_.GetToken().substr(0, 12) << "...\n\n";
}

void ConsoleUI::ShowProgress(const char* label, int64_t done, int64_t total) {
    if (total == 0) return;
    int pct = static_cast<int>(done * 100 / total);
    int bar_len = 30;
    int filled = bar_len * pct / 100;

    std::cout << "\r  " << label << ": [";
    for (int i = 0; i < bar_len; ++i)
        std::cout << (i < filled ? "█" : "░");
    std::cout << "] " << std::setw(3) << pct << "%  ("
              << (done >> 20) << "MB / " << (total >> 20) << "MB)"
              << std::flush;
}

// ── 注册 ─────────────────────────────────────────────────────────
void ConsoleUI::DoRegister() {
    std::string user = ReadLine("用户名");
    std::string pass = ReadPassword("密码");
    auto resp = client_.Register(user, pass);
    PrintResponse(resp, "注册成功");
}

// ── 登录 ─────────────────────────────────────────────────────────
void ConsoleUI::DoLogin() {
    std::string user = ReadLine("用户名");
    std::string pass = ReadPassword("密码");
    auto resp = client_.Login(user, pass);
    PrintResponse(resp, "登录成功");
}

// ── 上传 ─────────────────────────────────────────────────────────
void ConsoleUI::DoUpload() {
    if (!IsLoggedIn()) return;

    std::string path = ReadLine("文件路径");
    auto resp = client_.UploadFile(client_.GetToken(), path,
        [this](int64_t done, int64_t total) {
            ShowProgress("上传中", done, total);
        });
    std::cout << std::endl;
    PrintResponse(resp, "上传完成");
}

// ── 下载 ─────────────────────────────────────────────────────────
void ConsoleUI::DoDownload() {
    if (!IsLoggedIn()) return;

    int64_t file_id = ReadFileId();
    std::string save_path = ReadLine("保存到");

    auto resp = client_.DownloadFile(client_.GetToken(), file_id, save_path,
        [this](int64_t done, int64_t total) {
            ShowProgress("下载中", done, total);
        });
    std::cout << std::endl;
    PrintResponse(resp, "下载完成");
}

// ── 文件列表 ─────────────────────────────────────────────────────
void ConsoleUI::DoListFiles() {
    if (!IsLoggedIn()) return;

    auto resp = client_.ListFiles(client_.GetToken());
    if (resp.ok() && resp.data.contains("files")) {
        std::cout << "\n  ID  文件名                  大小       状态\n"
                  << "  -------------------------------------------------\n";
        for (auto& f : resp.data["files"]) {
            std::cout << "  " << std::setw(4) << f["id"]
                      << "  " << std::setw(20) << f["filename"].get<std::string>().substr(0,20)
                      << "  " << std::setw(8) << (f["filesize"].get<int64_t>() >> 20) << " MB"
                      << "  " << (f["status"].get<int>() == 1 ? "完成" : "上传中")
                      << std::endl;
        }
    }
    PrintResponse(resp);
}

// ── 删除 ─────────────────────────────────────────────────────────
void ConsoleUI::DoDeleteFile() {
    if (!IsLoggedIn()) return;
    int64_t file_id = ReadFileId();
    auto resp = client_.DeleteFile(client_.GetToken(), file_id);
    PrintResponse(resp, "删除成功");
}

// ── 断点续传 ─────────────────────────────────────────────────────
void ConsoleUI::DoResumeUpload() {
    if (!IsLoggedIn()) return;

    std::string path = ReadLine("文件路径");
    std::ifstream fs(path, std::ios::binary | std::ios::ate);
    if (!fs) {
        std::cout << "文件不存在: " << path << std::endl;
        return;
    }
    int64_t filesize = fs.tellg();
    fs.close();

    std::string filename = path;
    size_t pos = filename.find_last_of("/\\");
    if (pos != std::string::npos) filename = filename.substr(pos + 1);

    // 计算 MD5
    std::cout << "正在计算 MD5...\r" << std::flush;
    std::string md5 = FileClient::ComputeFileMD5(path);
    std::cout << "MD5: " << md5.substr(0,16) << "..." << std::endl;

    // upload_start — 自动检测断点
    auto start_resp = client_.UploadStart(client_.GetToken(), filename, filesize, md5);
    if (!start_resp.ok()) { PrintResponse(start_resp); return; }

    int64_t file_id = start_resp.data["file_id"];
    int64_t offset  = start_resp.data.value("offset", 0LL);

    if (offset > 0) {
        std::cout << "检测到未完成任务, 从 " << (offset >> 20)
                  << "MB 处继续上传" << std::endl;
    }

    // 继续上传
    std::ifstream ifs(path, std::ios::binary);
    ifs.seekg(offset);

    const int64_t kChunkSize = 4 * 1024 * 1024;
    std::string buffer;
    while (offset < filesize) {
        size_t size = std::min(kChunkSize, static_cast<int64_t>(filesize - offset));
        buffer.resize(size);
        ifs.read(&buffer[0], size);

        auto data_resp = client_.UploadData(client_.GetToken(), file_id, offset, buffer);
        if (!data_resp.ok()) { PrintResponse(data_resp); ifs.close(); return; }

        offset += size;
        ShowProgress("续传中", offset, filesize);
    }
    ifs.close();

    auto final_resp = client_.UploadFinalize(client_.GetToken(), file_id);
    std::cout << std::endl;
    PrintResponse(final_resp, "续传完成");
}

// ── 辅助 ─────────────────────────────────────────────────────────
std::string ConsoleUI::ReadLine(const std::string& prompt) {
    std::cout << "  " << prompt << ": ";
    std::string line;
    std::getline(std::cin, line);
    return line;
}

std::string ConsoleUI::ReadPassword(const std::string& prompt) {
    // Linux: 禁用回显
    system("stty -echo");
    std::string pass = ReadLine(prompt);
    system("stty echo");
    return pass;
}

int64_t ConsoleUI::ReadFileId() {
    std::string s = ReadLine("文件ID");
    try { return std::stoll(s); }
    catch (...) { return 0; }
}

bool ConsoleUI::IsLoggedIn() {
    if (client_.GetToken().empty()) {
        std::cout << "请先登录!\n";
        return false;
    }
    return true;
}

void ConsoleUI::PrintResponse(const ClientResponse& resp, const char* ok_msg) {
    if (resp.ok()) {
        std::cout << "  [OK] " << (ok_msg ? ok_msg : resp.msg) << std::endl;
        if (!resp.data.empty())
            std::cout << "  " << resp.data.dump() << std::endl;
    } else {
        std::cout << "  [失败] code=" << resp.code << " " << resp.msg << std::endl;
    }
}

}  // namespace client
}  // namespace fileserver
