// RemoteDesktop.cpp : アプリケーションのエントリ ポイントを定義します。
//

#include "framework.h"
#include "RemoteDesktop.h"
#include "Network.h"
#include <shellapi.h>
#include <string>

#define MAX_LOADSTRING 100

// グローバル変数:
HINSTANCE hInst;                                // 現在のインターフェイス
WCHAR szTitle[MAX_LOADSTRING];                  // タイトル バーのテキスト
WCHAR szWindowClass[MAX_LOADSTRING];            // メイン ウィンドウ クラス名

HTTPServer g_HttpServer;

// このコード モジュールに含まれる関数の宣言を転送します:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    // Default configuration
    std::string port = "8090";
    float scale = 0.7f; 
    float quality = 0.7f;
    std::string password = "";
    bool useBmp = false;
    bool useGray = false;

    // Adjust default scale based on resolution
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    if (screenW <= 1920 && screenH <= 1080) {
        scale = 1.0f;
    }

    // Parse Command Line
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"-port") == 0 && i + 1 < argc) {
                std::wstring ws(argv[++i]);
                port = std::string(ws.begin(), ws.end());
            }
            else if (wcscmp(argv[i], L"-scale") == 0 && i + 1 < argc) {
                scale = (float)_wtof(argv[++i]);
                if (scale < 0.1f) scale = 0.1f;
                if (scale > 1.0f) scale = 1.0f;
            }
            else if (wcscmp(argv[i], L"-quality") == 0 && i + 1 < argc) {
                quality = (float)_wtof(argv[++i]);
                if (quality < 0.1f) quality = 0.1f;
                if (quality > 1.0f) quality = 1.0f;
            }
            else if (wcscmp(argv[i], L"-password") == 0 && i + 1 < argc) {
                std::wstring ws(argv[++i]);
                password = std::string(ws.begin(), ws.end());
            }
            else if (wcscmp(argv[i], L"-bmp") == 0) {
                useBmp = true;
            }
            else if (wcscmp(argv[i], L"-gray") == 0) {
                useGray = true;
            }
        }
        LocalFree(argv);
    }

    g_HttpServer.Configure(port, scale, quality, password, useBmp, useGray);

    // TODO: ここにコードを挿入してください。

    // グローバル文字列を初期化する
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_REMOTEDESKTOP, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // アプリケーション初期化の実行:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_REMOTEDESKTOP));


    MSG msg;

    // Start Server
    g_HttpServer.Start();

    // メイン メッセージ ループ:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    g_HttpServer.Stop();

    return (int) msg.wParam;
}



//
//  関数: MyRegisterClass()
//
//  目的: ウィンドウ クラスを登録します。
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_REMOTEDESKTOP));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_REMOTEDESKTOP);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   関数: InitInstance(HINSTANCE, int)
//
//   目的: インスタンス ハンドルを保存して、メイン ウィンドウを作成します
//
//   コメント:
//
//        この関数で、グローバル変数でインスタンス ハンドルを保存し、
//        メイン プログラム ウィンドウを作成および表示します。
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // グローバル変数にインスタンス ハンドルを格納する

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   SetTimer(hWnd, 1, 1000, NULL); // Update every 1 second

   return TRUE;
}

//
//  関数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目的: メイン ウィンドウのメッセージを処理します。
//
//  WM_COMMAND  - アプリケーション メニューの処理
//  WM_PAINT    - メイン ウィンドウを描画する
//  WM_DESTROY  - 中止メッセージを表示して戻る
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // 選択されたメニューの解析:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_TIMER:
        if (wParam == 1) {
            long long duration = g_HttpServer.GetLastCaptureDuration();
            std::string ip = g_HttpServer.GetLocalIPAddress();
            std::string port = g_HttpServer.GetPort();
            std::string clients = g_HttpServer.GetConnectedClientInfo();
            
            // Convert to wstring
            std::wstring wip(ip.begin(), ip.end());
            std::wstring wport(port.begin(), port.end());
            std::wstring wclients(clients.begin(), clients.end());

            WCHAR szNewTitle[MAX_LOADSTRING + 200];
            swprintf_s(szNewTitle, L"%s - IP: %s:%s - Client: %s - Capture: %lld ms", szTitle, wip.c_str(), wport.c_str(), wclients.c_str(), duration);
            SetWindowText(hWnd, szNewTitle);
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            
            std::wstring helpText = 
                L"起動オプション:\n"
                L"  -port <number>     : ポート番号 (デフォルト: 8090)\n"
                L"  -scale <0.1-1.0>   : 画面縮小率 (デフォルト: 自動または0.7)\n"
                L"  -quality <0.1-1.0> : JPEG品質 (デフォルト: 0.7)\n"
                L"  -password <pass>   : パスワード設定\n"
                L"  -bmp               : BMP形式を使用 (帯域幅を消費します)\n"
                L"  -gray              : グレースケールモード\n\n"
                L"使用例:\n"
                L"  RemoteDesktop.exe -port 8080 -scale 0.5";

            RECT rect;
            GetClientRect(hWnd, &rect);
            rect.left += 10;
            rect.top += 10;
            
            SetBkMode(hdc, TRANSPARENT);
            DrawTextW(hdc, helpText.c_str(), -1, &rect, DT_LEFT | DT_TOP);

            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        KillTimer(hWnd, 1);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// バージョン情報ボックスのメッセージ ハンドラーです。
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
