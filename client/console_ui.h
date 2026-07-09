#ifndef FILESERVER_CLIENT_CONSOLE_UI_H_
#define FILESERVER_CLIENT_CONSOLE_UI_H_

#include <string>
#include <memory>
#include "client/file_client.h"

namespace fileserver {
namespace client {

// ── 终端交互界面 ─────────────────────────────────────────────────
// 纯命令行菜单，负责用户交互和显示，业务逻辑全部委托给 FileClient。
class ConsoleUI {
public:
    explicit ConsoleUI(const std::string& host, int port);
    ~ConsoleUI();

    // 进入主循环
    void Run();

private:
    // ── 菜单 ─────────────────────────────────────────────────────
    void ShowMenu();
    void ShowProgress(const char* label, int64_t done, int64_t total);

    // ── 操作 ─────────────────────────────────────────────────────
    void DoRegister();
    void DoLogin();
    void DoUpload();
    void DoDownload();
    void DoListFiles();
    void DoDeleteFile();
    void DoResumeUpload();

    // ── 辅助 ─────────────────────────────────────────────────────
    std::string ReadLine(const std::string& prompt);
    std::string ReadPassword(const std::string& prompt);
    int64_t ReadFileId();
    bool IsLoggedIn();
    void PrintResponse(const ClientResponse& resp, const char* ok_msg = nullptr);

    // ── 成员 ─────────────────────────────────────────────────────
    std::string host_;
    int port_;
    FileClient client_;
    bool running_{true};
};

}  // namespace client
}  // namespace fileserver

#endif  // FILESERVER_CLIENT_CONSOLE_UI_H_
