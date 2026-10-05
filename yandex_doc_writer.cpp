#include <windows.h>
#include <winhttp.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <shlobj.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

const std::wstring DEFAULT_DOC_URL = L"https://disk.yandex.ru/i/CVYHu7Bjcx6xOg";

// Структура вкладки браузера
struct BrowserTab {
    std::string id;
    std::string title;
    std::string type;
    std::string url;
    std::string wsUrl;
    std::string wsPath;
};

// Функция экранирования строки для JSON
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

// Простой парсер значения поля из JSON
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

// Выполнение HTTP GET запроса через WinHTTP
std::string httpGet(HINTERNET hConnect, const std::wstring& path) {
    std::string result;
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hReq) return result;

    if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        if (WinHttpReceiveResponse(hReq, NULL)) {
            DWORD bytesRead = 0;
            char buffer[4096];
            while (WinHttpReadData(hReq, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                result.append(buffer, bytesRead);
            }
        }
    }
    WinHttpCloseHandle(hReq);
    return result;
}

// Проверка доступности порта отладки
bool isDebuggerRunning(int port) {
    HINTERNET hSession = WinHttpOpen(L"Check/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    WinHttpSetTimeouts(hSession, 1000, 1000, 1000, 1000);
    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", (INTERNET_PORT)port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::string resp = httpGet(hConnect, L"/json/version");
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return !resp.empty();
}

// Запуск браузера (Chrome или Edge)
bool launchBrowser(const std::wstring& url) {
    std::wstring chromePath = L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
    std::wstring edgePath = L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";

    std::wstring browserExe = L"";
    if (GetFileAttributesW(chromePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        browserExe = chromePath;
    } else if (GetFileAttributesW(edgePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        browserExe = edgePath;
    } else {
        return false;
    }

    // Папка профиля пользователя для сохранения сессий и кук
    WCHAR localAppData[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData);
    std::wstring profileDir = std::wstring(localAppData) + L"\\YandexDocSession";
    CreateDirectoryW(profileDir.c_str(), NULL);

    std::wstring cmd = L"\"" + browserExe + L"\" --remote-debugging-port=9222 --user-data-dir=\"" + profileDir + L"\" \"" + url + L"\"";

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
}

// Отправка текстового сообщения в WebSocket
bool wsSendText(HINTERNET hWebSocket, const std::string& message) {
    DWORD dwError = WinHttpWebSocketSend(hWebSocket,
                                         WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                         (PVOID)message.data(),
                                         (DWORD)message.size());
    return (dwError == ERROR_SUCCESS);
}

// Клик мыши в редактор
void cdpClick(HINTERNET hWs, int& msgId, int x, int y) {
    std::string down = "{\"id\":" + std::to_string(++msgId) +
                       ",\"method\":\"Input.dispatchMouseEvent\",\"params\":{\"type\":\"mousePressed\",\"x\":" +
                       std::to_string(x) + ",\"y\":" + std::to_string(y) +
                       ",\"button\":\"left\",\"clickCount\":1}}";
    wsSendText(hWs, down);
    Sleep(60);

    std::string up = "{\"id\":" + std::to_string(++msgId) +
                     ",\"method\":\"Input.dispatchMouseEvent\",\"params\":{\"type\":\"mouseReleased\",\"x\":" +
                     std::to_string(x) + ",\"y\":" + std::to_string(y) +
                     ",\"button\":\"left\",\"clickCount\":1}}";
    wsSendText(hWs, up);
    Sleep(60);
}

// Вставка текста в активный элемент через CDP
void cdpInsertText(HINTERNET hWs, int& msgId, const std::string& text) {
    std::string msg = "{\"id\":" + std::to_string(++msgId) +
                      ",\"method\":\"Input.insertText\",\"params\":{\"text\":\"" +
                      escapeJson(text) + "\"}}";
    wsSendText(hWs, msg);
}

// Нажатие Enter
void cdpPressEnter(HINTERNET hWs, int& msgId) {
    std::string down = "{\"id\":" + std::to_string(++msgId) +
                       ",\"method\":\"Input.dispatchKeyEvent\",\"params\":{\"type\":\"rawKeyDown\",\"windowsVirtualKeyCode\":13,\"unmodifiedText\":\"\\r\",\"text\":\"\\r\",\"key\":\"Enter\",\"code\":\"Enter\"}}";
    wsSendText(hWs, down);

    std::string up = "{\"id\":" + std::to_string(++msgId) +
                     ",\"method\":\"Input.dispatchKeyEvent\",\"params\":{\"type\":\"keyUp\",\"windowsVirtualKeyCode\":13,\"key\":\"Enter\",\"code\":\"Enter\"}}";
    wsSendText(hWs, up);
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

        // Вырезаем путь из ws://127.0.0.1:9222/devtools/page/...
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
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::cout << "====================================================\n";
    std::cout << "  Yandex Documents CLI Writer (C++ / Chromium CDP)  \n";
    std::cout << "====================================================\n\n";

    std::wstring targetUrl = DEFAULT_DOC_URL;
    if (argc > 1) {
        std::string argStr = argv[1];
        targetUrl = std::wstring(argStr.begin(), argStr.end());
    }

    int port = 9222;

    // 1. Проверяем, запущен ли браузер с портом отладки
    if (!isDebuggerRunning(port)) {
        std::cout << "[Инфо] Браузер не запущен на порту " << port << ".\n";
        std::cout << "[Инфо] Запускаем браузер с документом...\n";

        if (!launchBrowser(targetUrl)) {
            std::cerr << "[Ошибка] Не удалось автоматически запустить Chrome или Edge.\n";
            std::cerr << "Запустите вручную: .\\start_browser.ps1\n";
            return 1;
        }

        std::cout << "[Инфо] Ожидаем загрузки браузера...\n";
        for (int i = 0; i < 15; ++i) {
            Sleep(1000);
            if (isDebuggerRunning(port)) {
                break;
            }
        }
    }

    if (!isDebuggerRunning(port)) {
        std::cerr << "[Ошибка] Браузер так и не ответил на порту " << port << ".\n";
        return 1;
    }

    HINTERNET hSession = WinHttpOpen(L"YandexDocCDP/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        std::cerr << "[Ошибка] WinHttpOpen вернул ошибку.\n";
        return 1;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", (INTERNET_PORT)port, 0);
    if (!hConnect) {
        std::cerr << "[Ошибка] Не удалось подключиться к 127.0.0.1:" << port << ".\n";
        WinHttpCloseHandle(hSession);
        return 1;
    }

    // 2. Получаем список вкладок
    std::string jsonResponse = httpGet(hConnect, L"/json");
    auto tabs = parseTabs(jsonResponse);

    if (tabs.empty()) {
        std::cerr << "[Ошибка] В браузере нет открытых вкладок.\n";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 1;
    }

    // 3. Ищем нужную вкладку (Яндекс Документы / Диск)
    BrowserTab selectedTab;
    bool found = false;

    // Сначала ищем страницу с yandex / disk / doc
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

    // Если не нашли по имени, берем любую страницу ("page")
    if (!found) {
        for (const auto& tab : tabs) {
            if (tab.type == "page" && tab.url.find("chrome://") == std::string::npos && tab.url.find("edge://") == std::string::npos) {
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

    // 4. Открываем WebSocket
    std::wstring pagePath(selectedTab.wsPath.begin(), selectedTab.wsPath.end());
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", pagePath.c_str(), NULL,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hReq) {
        std::cerr << "[Ошибка] Не удалось сформировать запрос WebSocket.\n";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 1;
    }

    BOOL optSet = WinHttpSetOption(hReq, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0);
    if (!optSet || !WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(hReq, NULL)) {
        std::cerr << "[Ошибка] Сбой апгрейда соединения до WebSocket.\n";
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 1;
    }

    HINTERNET hWebSocket = WinHttpWebSocketCompleteUpgrade(hReq, 0);
    WinHttpCloseHandle(hReq);

    if (!hWebSocket) {
        std::cerr << "[Ошибка] WebSocket не инициализирован.\n";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 1;
    }

    int msgId = 100;

    // Даем фокус редактору кликом в рабочую область
    std::cout << "[Инфо] Активируем рабочую область документа (клик фокуса)...\n";
    cdpClick(hWebSocket, msgId, 500, 350);

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
            cdpClick(hWebSocket, msgId, 500, 350);
            std::cout << "[OK] Фокус обновлен.\n";
            continue;
        } else if (line.empty()) {
            cdpPressEnter(hWebSocket, msgId);
            continue;
        }

        cdpInsertText(hWebSocket, msgId, line);
        cdpPressEnter(hWebSocket, msgId);

        std::cout << "[OK] Отправлено.\n";
    }

    WinHttpWebSocketClose(hWebSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
    WinHttpCloseHandle(hWebSocket);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    std::cout << "\nСессия завершена.\n";
    return 0;
}
