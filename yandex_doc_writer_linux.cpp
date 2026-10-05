#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/stat.h>

const std::string DEFAULT_DOC_URL = "https://disk.yandex.ru/i/CVYHu7Bjcx6xOg";

// Структура вкладки браузера
struct BrowserTab {
    std::string id;
    std::string title;
    std::string type;
    std::string url;
    std::string wsUrl;
    std::string wsPath;
};

// Экранирование строки для JSON
std::string escapeJson(const std::string& input) {
    std::ostringstream ss;
    for (char c : input) {
        switch (c) {
            case '"':  ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b";  break;
            case '\f': ss << "\\f";  break;
            case '\n': ss << "\\n";  break;
            case '\r': ss << "\\r";  break;
            case '\t': ss << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    ss << buf;
                } else {
                    ss << c;
                }
        }
    }
    return ss.str();
}

// Извлечение поля из простого JSON
std::string extractJsonField(const std::string& json, const std::string& field) {
    std::string key = "\"" + field + "\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";

    pos = json.find('"', pos);
    if (pos == std::string::npos) return "";

    size_t endPos = pos + 1;
    while (endPos < json.length()) {
        if (json[endPos] == '"' && json[endPos - 1] != '\\') {
            break;
        }
        endPos++;
    }
    if (endPos > json.length()) return "";
    return json.substr(pos + 1, endPos - pos - 1);
}

// TCP соединение
int connectTcp(const std::string& host, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

// HTTP GET запрос
std::string httpGet(const std::string& host, int port, const std::string& path) {
    int sock = connectTcp(host, port);
    if (sock < 0) return "";

    std::string req = "GET " + path + " HTTP/1.1\r\n"
                      "Host: " + host + ":" + std::to_string(port) + "\r\n"
                      "Connection: close\r\n\r\n";
    send(sock, req.data(), req.size(), 0);

    std::string response;
    char buffer[4096];
    ssize_t bytes;
    while ((bytes = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        response.append(buffer, bytes);
    }
    close(sock);

    size_t bodyPos = response.find("\r\n\r\n");
    if (bodyPos != std::string::npos) {
        return response.substr(bodyPos + 4);
    }
    return response;
}

// Проверка доступности порта отладки
bool isDebuggerRunning(int port) {
    int sock = connectTcp("127.0.0.1", port);
    if (sock < 0) return false;
    close(sock);
    return true;
}

// Запуск браузера в Linux (Chrome, Chromium, Edge, Yandex)
bool launchBrowserLinux(const std::string& url) {
    std::vector<std::string> candidates = {
        "/usr/bin/google-chrome",
        "/usr/bin/google-chrome-stable",
        "/usr/bin/chromium",
        "/usr/bin/chromium-browser",
        "/usr/bin/microsoft-edge",
        "/usr/bin/yandex-browser"
    };

    std::string browserPath;
    for (const auto& path : candidates) {
        if (access(path.c_str(), X_OK) == 0) {
            browserPath = path;
            break;
        }
    }

    if (browserPath.empty()) {
        const char* names[] = {"google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "microsoft-edge", "yandex-browser"};
        for (const char* name : names) {
            std::string cmd = std::string("which ") + name + " > /dev/null 2>&1";
            if (system(cmd.c_str()) == 0) {
                browserPath = name;
                break;
            }
        }
    }

    if (browserPath.empty()) return false;

    const char* home = getenv("HOME");
    std::string profileDir = home ? (std::string(home) + "/.config/yandex_doc_session") : "/tmp/yandex_doc_session";

    std::string runCmd = browserPath + " --remote-debugging-port=9222 --user-data-dir=\"" + profileDir + "\" \"" + url + "\" > /dev/null 2>&1 &";
    return (system(runCmd.c_str()) == 0);
}

// Подключение к WebSocket
int wsConnect(const std::string& host, int port, const std::string& path) {
    int sock = connectTcp(host, port);
    if (sock < 0) return -1;

    std::string req =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + ":" + std::to_string(port) + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";

    if (send(sock, req.data(), req.size(), 0) < 0) {
        close(sock);
        return -1;
    }

    char buffer[2048];
    ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        close(sock);
        return -1;
    }
    buffer[bytes] = '\0';
    if (std::strstr(buffer, "101 Switching Protocols") == nullptr) {
        close(sock);
        return -1;
    }
    return sock;
}

// Отправка текстового фрейма в WebSocket (с маскированием по RFC 6455)
bool wsSendText(int sock, const std::string& message) {
    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN + Text opcode

    size_t len = message.size();
    if (len <= 125) {
        frame.push_back(0x80 | (uint8_t)len);
    } else if (len <= 65535) {
        frame.push_back(0x80 | 126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back((len >> (i * 8)) & 0xFF);
        }
    }

    // Ключ маски (RFC 6455 требует маскирование со стороны клиента)
    uint8_t mask[4] = { 0x1A, 0x2B, 0x3C, 0x4D };
    frame.insert(frame.end(), mask, mask + 4);

    for (size_t i = 0; i < len; ++i) {
        frame.push_back((uint8_t)message[i] ^ mask[i % 4]);
    }

    ssize_t sent = send(sock, frame.data(), frame.size(), 0);
    return sent == (ssize_t)frame.size();
}

// Клик мыши в редактор
void cdpClick(int sock, int& msgId, int x, int y) {
    std::string down = "{\"id\":" + std::to_string(++msgId) +
                       ",\"method\":\"Input.dispatchMouseEvent\",\"params\":{\"type\":\"mousePressed\",\"x\":" +
                       std::to_string(x) + ",\"y\":" + std::to_string(y) +
                       ",\"button\":\"left\",\"clickCount\":1}}";
    wsSendText(sock, down);
    usleep(60000);

    std::string up = "{\"id\":" + std::to_string(++msgId) +
                     ",\"method\":\"Input.dispatchMouseEvent\",\"params\":{\"type\":\"mouseReleased\",\"x\":" +
                     std::to_string(x) + ",\"y\":" + std::to_string(y) +
                     ",\"button\":\"left\",\"clickCount\":1}}";
    wsSendText(sock, up);
    usleep(60000);
}

// Вставка текста в активный элемент через CDP
void cdpInsertText(int sock, int& msgId, const std::string& text) {
    std::string msg = "{\"id\":" + std::to_string(++msgId) +
                      ",\"method\":\"Input.insertText\",\"params\":{\"text\":\"" +
                      escapeJson(text) + "\"}}";
    wsSendText(sock, msg);
}

// Нажатие Enter
void cdpPressEnter(int sock, int& msgId) {
    std::string down = "{\"id\":" + std::to_string(++msgId) +
                       ",\"method\":\"Input.dispatchKeyEvent\",\"params\":{\"type\":\"rawKeyDown\",\"windowsVirtualKeyCode\":13,\"unmodifiedText\":\"\\r\",\"text\":\"\\r\",\"key\":\"Enter\",\"code\":\"Enter\"}}";
    wsSendText(sock, down);

    std::string up = "{\"id\":" + std::to_string(++msgId) +
                     ",\"method\":\"Input.dispatchKeyEvent\",\"params\":{\"type\":\"keyUp\",\"windowsVirtualKeyCode\":13,\"key\":\"Enter\",\"code\":\"Enter\"}}";
    wsSendText(sock, up);
}

// Разбор списка вкладок из /json
std::vector<BrowserTab> parseTabs(const std::string& json) {
    std::vector<BrowserTab> tabs;
    size_t start = 0;
    while ((start = json.find('{', start)) != std::string::npos) {
        size_t end = json.find('}', start);
        if (end == std::string::npos) break;

        std::string obj = json.substr(start, end - start + 1);
        BrowserTab tab;
        tab.id = extractJsonField(obj, "id");
        tab.title = extractJsonField(obj, "title");
        tab.type = extractJsonField(obj, "type");
        tab.url = extractJsonField(obj, "url");
        tab.wsUrl = extractJsonField(obj, "webSocketDebuggerUrl");

        size_t pathPos = tab.wsUrl.find("/devtools/page/");
        if (pathPos != std::string::npos) {
            tab.wsPath = tab.wsUrl.substr(pathPos);
        }

        if (!tab.wsPath.empty()) {
            tabs.push_back(tab);
        }
        start = end + 1;
    }
    return tabs;
}

int main(int argc, char* argv[]) {
    std::cout << "====================================================\n";
    std::cout << "  Yandex Documents CLI Writer (Linux / Ubuntu C++)  \n";
    std::cout << "====================================================\n\n";

    std::string targetUrl = DEFAULT_DOC_URL;
    if (argc > 1) {
        targetUrl = argv[1];
    }

    int port = 9222;
    if (argc > 2) {
        port = std::stoi(argv[2]);
    }

    // 1. Проверяем браузер
    if (!isDebuggerRunning(port)) {
        std::cout << "[Инфо] Браузер не запущен на порту " << port << ".\n";
        std::cout << "[Инфо] Запускаем браузер с документом...\n";

        if (!launchBrowserLinux(targetUrl)) {
            std::cerr << "[Ошибка] Браузер не найден в системе.\n";
            std::cerr << "Запустите вручную:\n";
            std::cerr << "  google-chrome --remote-debugging-port=9222 \"" << targetUrl << "\"\n";
            return 1;
        }

        std::cout << "[Инфо] Ожидаем загрузки браузера...\n";
        for (int i = 0; i < 15; ++i) {
            sleep(1);
            if (isDebuggerRunning(port)) {
                break;
            }
        }
    }

    if (!isDebuggerRunning(port)) {
        std::cerr << "[Ошибка] Браузер так и не открыл порт отладки " << port << ".\n";
        return 1;
    }

    // 2. Получаем вкладки
    std::string jsonResponse = httpGet("127.0.0.1", port, "/json");
    auto tabs = parseTabs(jsonResponse);

    if (tabs.empty()) {
        std::cerr << "[Ошибка] В браузере нет открытых вкладок.\n";
        return 1;
    }

    // 3. Выбираем вкладку
    BrowserTab selectedTab;
    bool found = false;

    for (const auto& tab : tabs) {
        if (tab.type == "page") {
            if (tab.url.find("yandex") != std::string::npos ||
                tab.url.find("disk") != std::string::npos ||
                tab.title.find("Яндекс") != std::string::npos) {
                selectedTab = tab;
                found = true;
                break;
            }
        }
    }

    if (!found) {
        for (const auto& tab : tabs) {
            if (tab.type == "page" && tab.url.find("chrome://") == std::string::npos) {
                selectedTab = tab;
                found = true;
                break;
            }
        }
    }

    if (!found) {
        selectedTab = tabs[0];
    }

    std::cout << "[Успех] Подключено к вкладке:\n";
    std::cout << "        Заголовок: " << selectedTab.title << "\n";
    std::cout << "        URL:       " << selectedTab.url << "\n\n";

    // 4. Подключаем WebSocket
    int sock = wsConnect("127.0.0.1", port, selectedTab.wsPath);
    if (sock < 0) {
        std::cerr << "[Ошибка] Не удалось установить WebSocket соединение.\n";
        return 1;
    }

    int msgId = 100;

    // Фокусируем рабочую область
    std::cout << "[Инфо] Активируем рабочую область документа (клик фокуса)...\n";
    cdpClick(sock, msgId, 500, 350);

    std::cout << "----------------------------------------------------\n";
    std::cout << "Введите строку для отправки в документ.\n";
    std::cout << "Команды: ':click' - повторный фокус кликом, ':exit' - выход.\n";
    std::cout << "----------------------------------------------------\n\n";

    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;

        if (line == ":exit" || line == "exit") {
            break;
        } else if (line == ":click") {
            cdpClick(sock, msgId, 500, 350);
            std::cout << "[OK] Фокус обновлен.\n";
            continue;
        } else if (line.empty()) {
            cdpPressEnter(sock, msgId);
            continue;
        }

        cdpInsertText(sock, msgId, line);
        cdpPressEnter(sock, msgId);

        std::cout << "[OK] Отправлено.\n";
    }

    close(sock);
    std::cout << "\nСессия завершена.\n";
    return 0;
}
