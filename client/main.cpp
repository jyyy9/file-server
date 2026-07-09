#include <iostream>
#include "client/console_ui.h"

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 8080;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::stoi(argv[2]);

    fileserver::client::ConsoleUI ui(host, port);
    ui.Run();
    return 0;
}
