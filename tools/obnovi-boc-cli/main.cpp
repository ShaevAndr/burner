#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace {

constexpr UINT WM_OPERATION_COMPLETE = WM_APP + 1;

enum ControlId {
    IdBuild = 1001, IdInstaller, IdStatus, IdLog
};

struct Options {
    std::wstring root;
    bool dryRun = false;
};

struct Gui {
    HWND window = nullptr;
    HWND build = nullptr;
    HWND installer = nullptr;
    HWND status = nullptr;
    HWND log = nullptr;
    HFONT font = nullptr;
    std::wstring projectRoot;
    bool busy = false;
};

struct Task {
    HWND window;
    std::vector<std::wstring> arguments;
    std::wstring root;
};

void writeLine(HANDLE stream, const std::wstring& text)
{
    const std::wstring line = text + L"\r\n";
    DWORD mode = 0;
    if (stream != INVALID_HANDLE_VALUE && stream && GetConsoleMode(stream, &mode)) {
        DWORD written = 0;
        WriteConsoleW(stream, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        return;
    }
    if (stream == INVALID_HANDLE_VALUE || !stream)
        return;
    const int size = WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return;
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
        &utf8[0], size, nullptr, nullptr);
    DWORD written = 0;
    WriteFile(stream, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

void print(const std::wstring& text) { writeLine(GetStdHandle(STD_OUTPUT_HANDLE), text); }
void error(const std::wstring& text) { writeLine(GetStdHandle(STD_ERROR_HANDLE), L"Ошибка: " + text); }

void help()
{
    print(L"обнови-БОЦ: перевыпуск приложений и установщиков");
    print(L"");
    print(L"Запустите obnovi-boc-cli.exe без параметров, чтобы открыть окно с двумя кнопками.");
    print(L"");
    print(L"Использование:");
    print(L"  obnovi-boc-cli build [--root ПУТЬ]");
    print(L"  obnovi-boc-cli installer [--root ПУТЬ]");
    print(L"");
    print(L"Команды:");
    print(L"  build       Чисто пересобрать обе редакции приложения.");
    print(L"  installer   Пересобрать приложения, установщики и portable ZIP.");
    print(L"");
    print(L"Параметры: --root ПУТЬ, --dry-run, -h, --help");
}

std::wstring fullPath(const std::wstring& path)
{
    const DWORD size = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!size) return {};
    std::vector<wchar_t> buffer(size);
    return GetFullPathNameW(path.c_str(), size, buffer.data(), nullptr) ? buffer.data() : L"";
}

std::wstring parentPath(std::wstring path)
{
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
    const size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return {};
    if (separator == 2 && path.size() >= 3 && path[1] == L':') return path.substr(0, 3);
    return path.substr(0, separator);
}

std::wstring join(const std::wstring& left, const std::wstring& right)
{
    if (left.empty()) return right;
    return left + ((left.back() == L'\\' || left.back() == L'/') ? L"" : L"\\") + right;
}

bool isFile(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool isRoot(const std::wstring& root)
{
    return isFile(join(root, L"scripts\\sync-firmware-config.ps1"))
        && isFile(join(root, L"scripts\\build-releases.ps1"))
        && isFile(join(root, L"scripts\\build-installers.ps1"));
}

std::wstring searchRoot(std::wstring start)
{
    start = fullPath(start);
    while (!start.empty()) {
        if (isRoot(start)) return start;
        const std::wstring parent = parentPath(start);
        if (parent.empty() || parent == start) break;
        start = parent;
    }
    return {};
}

std::wstring executableDirectory()
{
    std::vector<wchar_t> buffer(1024);
    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length) return {};
        if (length < buffer.size() - 1) return parentPath(std::wstring(buffer.data(), length));
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring currentDirectory()
{
    const DWORD size = GetCurrentDirectoryW(0, nullptr);
    if (!size) return {};
    std::vector<wchar_t> buffer(size);
    return GetCurrentDirectoryW(size, buffer.data()) ? buffer.data() : L"";
}

std::wstring quote(const std::wstring& argument)
{
    if (argument.empty()) return L"\"\"";
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) return argument;
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') { ++slashes; continue; }
        if (character == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(character);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

std::wstring commandLine(const std::vector<std::wstring>& arguments)
{
    std::wstring result;
    for (const std::wstring& argument : arguments) {
        if (!result.empty()) result.push_back(L' ');
        result += quote(argument);
    }
    return result;
}

std::vector<std::wstring> powershell(const std::wstring& command, const Options& options)
{
    std::vector<std::wstring> result = {
        L"powershell.exe", L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-File"
    };
    if (command == L"build") {
        result.push_back(join(options.root, L"scripts\\build-releases.ps1"));
        result.push_back(L"-Edition");
        result.push_back(L"all");
        result.push_back(L"-Clean");
    } else {
        result.push_back(join(options.root, L"scripts\\build-installers.ps1"));
        result.push_back(L"-Edition");
        result.push_back(L"all");
        result.push_back(L"-Version");
        result.push_back(L"1.0.0");
        result.push_back(L"-BuildApplication");
    }
    return result;
}

int run(const std::vector<std::wstring>& arguments, const std::wstring& root, bool dryRun)
{
    const std::wstring line = commandLine(arguments);
    if (dryRun) { print(L"Команда: " + line); return 0; }
    std::vector<wchar_t> mutableLine(line.begin(), line.end());
    mutableLine.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, TRUE, 0, nullptr,
            root.c_str(), &startup, &process)) {
        error(L"не удалось запустить PowerShell, код Windows " + std::to_wstring(GetLastError()));
        return 1;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}

bool valueAfter(const std::vector<std::wstring>& arguments, size_t* index,
    std::wstring* value, const std::wstring& option)
{
    if (*index + 1 >= arguments.size()) {
        error(L"после " + option + L" требуется значение");
        return false;
    }
    *value = arguments[++*index];
    return true;
}

void appendLog(Gui* gui, const std::wstring& text)
{
    const int length = GetWindowTextLengthW(gui->log);
    SendMessageW(gui->log, EM_SETSEL, length, length);
    const std::wstring line = text + L"\r\n";
    SendMessageW(gui->log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
    SendMessageW(gui->log, EM_SCROLLCARET, 0, 0);
}

std::wstring decode(const std::string& bytes)
{
    if (bytes.empty()) return {};
    UINT page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int length = MultiByteToWideChar(page, flags, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (length <= 0) {
        page = CP_OEMCP;
        flags = 0;
        length = MultiByteToWideChar(page, flags, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    }
    if (length <= 0) return L"Не удалось декодировать вывод процесса.";
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(page, flags, bytes.data(), static_cast<int>(bytes.size()), &result[0], length);
    return result;
}

int runCaptured(const std::vector<std::wstring>& arguments, const std::wstring& root,
    std::wstring* output)
{
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        *output = L"Не удалось создать канал вывода.";
        return 1;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const std::wstring line = commandLine(arguments);
    std::vector<wchar_t> mutableLine(line.begin(), line.end());
    mutableLine.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = nullInput == INVALID_HANDLE_VALUE ? nullptr : nullInput;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, root.c_str(), &startup, &process)) {
        const DWORD code = GetLastError();
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
        *output = L"Не удалось запустить PowerShell, код Windows " + std::to_wstring(code);
        return 1;
    }
    CloseHandle(writePipe);
    if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
    CloseHandle(process.hThread);
    std::string bytes;
    char buffer[4096];
    DWORD count = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &count, nullptr) && count)
        bytes.append(buffer, buffer + count);
    CloseHandle(readPipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    *output = decode(bytes);
    return static_cast<int>(exitCode);
}

DWORD WINAPI worker(LPVOID parameter)
{
    Task* task = static_cast<Task*>(parameter);
    std::wstring* output = new std::wstring;
    const int exitCode = runCaptured(task->arguments, task->root, output);
    PostMessageW(task->window, WM_OPERATION_COMPLETE, exitCode, reinterpret_cast<LPARAM>(output));
    delete task;
    return 0;
}

void setBusy(Gui* gui, bool busy)
{
    gui->busy = busy;
    const HWND controls[] = {gui->build, gui->installer};
    for (HWND control : controls) EnableWindow(control, !busy);
    SetWindowTextW(gui->status, busy ? L"Операция выполняется…" : L"Готово к работе");
}

void start(Gui* gui, const std::wstring& command)
{
    if (gui->busy) return;
    Options options;
    options.root = gui->projectRoot;
    if (!isRoot(options.root)) {
        MessageBoxW(gui->window,
            L"Не удалось автоматически найти корневую папку проекта.",
            L"Проект не найден", MB_OK | MB_ICONERROR);
        return;
    }
    std::vector<std::wstring> arguments = powershell(command, options);
    appendLog(gui, L"");
    appendLog(gui, L"> " + commandLine(arguments));
    setBusy(gui, true);
    Task* task = new Task{gui->window, arguments, options.root};
    HANDLE thread = CreateThread(nullptr, 0, worker, task, 0, nullptr);
    if (!thread) {
        delete task;
        setBusy(gui, false);
        appendLog(gui, L"Ошибка: не удалось запустить фоновую операцию.");
        return;
    }
    CloseHandle(thread);
}

HWND control(Gui* gui, const wchar_t* className, const wchar_t* text, DWORD style,
    int id, int x, int y, int width, int height)
{
    HWND result = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, gui->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(gui->font), TRUE);
    return result;
}

void layout(Gui* gui, int width, int height)
{
    const int margin = 18;
    const int gap = 14;
    const int buttonWidth = (width - margin * 2 - gap) / 2;
    MoveWindow(gui->build, margin, 18, buttonWidth, 64, TRUE);
    MoveWindow(gui->installer, margin + buttonWidth + gap, 18, buttonWidth, 64, TRUE);
    MoveWindow(gui->status, margin, 97, width - margin * 2, 22, TRUE);
    MoveWindow(gui->log, margin, 126, width - margin * 2, height - 126 - margin, TRUE);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    Gui* gui = reinterpret_cast<Gui*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        gui = static_cast<Gui*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        gui->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(gui));
    }
    switch (message) {
    case WM_CREATE: {
        gui->font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gui->build = control(gui, L"BUTTON", L"Перевыпустить собранное приложение",
            BS_PUSHBUTTON | WS_TABSTOP, IdBuild, 18, 18, 330, 64);
        gui->installer = control(gui, L"BUTTON", L"Перевыпустить установочные файлы",
            BS_PUSHBUTTON | WS_TABSTOP, IdInstaller, 362, 18, 330, 64);
        gui->status = control(gui, L"STATIC", L"Готово к работе", SS_LEFT, IdStatus, 18, 97, 674, 22);
        gui->log = control(gui, L"EDIT", L"Выберите одну из двух операций. Подробный вывод появится здесь.\r\n",
            WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            IdLog, 18, 126, 674, 316);
        SendMessageW(gui->log, EM_SETLIMITTEXT, 8 * 1024 * 1024, 0);
        gui->projectRoot = searchRoot(currentDirectory());
        if (gui->projectRoot.empty()) gui->projectRoot = searchRoot(executableDirectory());
        if (gui->projectRoot.empty()) {
            EnableWindow(gui->build, FALSE);
            EnableWindow(gui->installer, FALSE);
            SetWindowTextW(gui->status, L"Корневая папка проекта не найдена");
            appendLog(gui, L"Ошибка: поместите CLI внутрь проекта обнови-БОЦ.");
        } else {
            appendLog(gui, L"Проект: " + gui->projectRoot);
        }
        return 0;
    }
    case WM_SIZE:
        if (gui && wParam != SIZE_MINIMIZED) layout(gui, LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x = 620;
        limits->ptMinTrackSize.y = 420;
        return 0;
    }
    case WM_COMMAND:
        if (!gui) return 0;
        switch (LOWORD(wParam)) {
        case IdBuild: start(gui, L"build"); break;
        case IdInstaller: start(gui, L"installer"); break;
        }
        return 0;
    case WM_OPERATION_COMPLETE: {
        std::wstring* output = reinterpret_cast<std::wstring*>(lParam);
        if (output && !output->empty()) {
            const int end = GetWindowTextLengthW(gui->log);
            SendMessageW(gui->log, EM_SETSEL, end, end);
            SendMessageW(gui->log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(output->c_str()));
            appendLog(gui, L"");
        }
        delete output;
        setBusy(gui, false);
        if (static_cast<int>(wParam) == 0) {
            SetWindowTextW(gui->status, L"Операция успешно завершена");
            appendLog(gui, L"Готово: операция завершена успешно.");
            MessageBeep(MB_OK);
        } else {
            const std::wstring messageText = L"Операция завершилась с кодом " + std::to_wstring(wParam) + L".";
            SetWindowTextW(gui->status, messageText.c_str());
            appendLog(gui, L"Ошибка: " + messageText);
            MessageBoxW(window, messageText.c_str(), L"Ошибка операции", MB_OK | MB_ICONERROR);
        }
        return 0;
    }
    case WM_CLOSE:
        if (gui && gui->busy) {
            MessageBoxW(window, L"Дождитесь завершения текущей операции.", L"Операция выполняется",
                MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (gui && gui->font) DeleteObject(gui->font);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

int guiMain()
{
    FreeConsole();
    SetProcessDPIAware();
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"ObnoviBocManagerWindow";
    if (!RegisterClassExW(&windowClass)) return 2;
    Gui gui;
    HWND window = CreateWindowExW(0, windowClass.lpszClassName,
        L"обнови-БОЦ — перевыпуск", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 740, 500, nullptr, nullptr, instance, &gui);
    if (!window) return 2;
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}

} // namespace

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    int count = 0;
    wchar_t** raw = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!raw) { error(L"не удалось прочитать командную строку"); return 2; }
    std::vector<std::wstring> arguments;
    for (int index = 1; index < count; ++index) arguments.emplace_back(raw[index]);
    LocalFree(raw);
    if (arguments.empty()) return guiMain();
    if (arguments[0] == L"help" || arguments[0] == L"--help" || arguments[0] == L"-h") {
        help();
        return 0;
    }
    const std::wstring command = arguments[0];
    Options options;
    for (size_t index = 1; index < arguments.size(); ++index) {
        const std::wstring& argument = arguments[index];
        if (argument == L"--root") {
            if (!valueAfter(arguments, &index, &options.root, argument)) return 2;
        } else if (argument == L"--dry-run") options.dryRun = true;
        else if (argument == L"--help" || argument == L"-h") { help(); return 0; }
        else { error(L"неизвестный параметр '" + argument + L"' для команды '" + command + L"'"); return 2; }
    }
    if (command != L"build" && command != L"installer") {
        error(L"неизвестная команда '" + command + L"'");
        help();
        return 2;
    }
    if (!options.root.empty()) {
        options.root = fullPath(options.root);
        if (!isRoot(options.root)) { error(L"в указанном каталоге не найдены скрипты проекта: " + options.root); return 2; }
    } else {
        options.root = searchRoot(currentDirectory());
        if (options.root.empty()) options.root = searchRoot(executableDirectory());
        if (options.root.empty()) { error(L"не удалось найти корень проекта; используйте --root ПУТЬ"); return 2; }
    }
    const std::vector<std::wstring> commandArguments = powershell(command, options);
    print(L"Проект: " + options.root);
    print(L"Операция: " + command);
    const int exitCode = run(commandArguments, options.root, options.dryRun);
    if (!exitCode) print(L"Операция завершена успешно.");
    else error(L"операция завершилась с кодом " + std::to_wstring(exitCode));
    return exitCode;
}
