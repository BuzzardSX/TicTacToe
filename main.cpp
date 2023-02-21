#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define SAMPLE_USE_WINAPI_ENTRYPOINT 1

#define BUGCHECK_WINAPI(condition)	\
	do						\
	{						\
		if (!(condition)) {	\
			printf("Expression %s failed on line %d, error %d\n", #condition, __LINE__, GetLastError());	\
			exit(1);		\
		}		\
	} while (0)

#define N (3)
#define CCROSS ((CHAR)0x55)
#define CNOUGHT ((CHAR)0xAA)

#define KEY_SHIFTED (0x8000)
#define KEY_TOGGLED (0x0001)

const wchar_t szWinClass[] = L"Win32TicTacToe";
const wchar_t szWinName[] = L"Крестики-нолики";
const wchar_t szPaintSemName[] = L"TicTacToePaintSem";
HWND hwnd;
HBRUSH hBrush;
HANDLE hCrossImage;
HANDLE hNoughtsImage;
CHAR cOurSym;
BOOL bAreWeServer;
BOOL bIsOurStep;
HANDLE hGameSem;
HANDLE hFieldMtx;
HANDLE hMapField;
PCHAR field;
UINT WM_TICTACTOEMESSAGE;
HANDLE hPaintSem;
HANDLE hPaintThd;

void DrawField(HWND hwnd);
CHAR CheckGameEnd(VOID);

DWORD WINAPI DrawThread(LPVOID param)
{
    CHAR existCombination = 0;
    while (TRUE)
    {
        WaitForSingleObject(hPaintSem, INFINITE);
        DrawField(hwnd);

        if (existCombination = CheckGameEnd())
        {
            const wchar_t* msg;

            if (existCombination == 1)
                msg = L"Tie!";
            else {
                msg = (existCombination == cOurSym) ? L"U won!" : L"U lose!";
            }

            MessageBox(hwnd, msg, L"Close", MB_OK);

            PostMessage(hwnd, WM_QUIT, 0, 0);
        }
    }

    return 0;
}

int InitField()
{
    hGameSem = CreateSemaphore(NULL, 1, 1, L"GameSemaphore");

    bAreWeServer = (GetLastError() != ERROR_ALREADY_EXISTS) ? TRUE : FALSE;
    bIsOurStep = bAreWeServer;

    WaitForSingleObject(hFieldMtx, INFINITE);

    hFieldMtx = CreateMutex(NULL, FALSE, L"MutexName");

    cOurSym = (bAreWeServer != FALSE) ? CCROSS : CNOUGHT;

    hPaintSem = CreateSemaphore(NULL, 1, 1, szPaintSemName);
    hPaintThd = CreateThread(NULL, 65536, &DrawThread, NULL, 0, NULL);

    hMapField = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, N * N * sizeof(CHAR), L"someName");

    field = (PCHAR)MapViewOfFile(hMapField, FILE_MAP_ALL_ACCESS, 0, 0, 0);

    BUGCHECK_WINAPI(field != 0 || field != NULL);

    return 0;
}

void FiniField()
{
    UnmapViewOfFile(field);

    CloseHandle(hMapField);

    ReleaseSemaphore(hGameSem, 1, NULL);
    CloseHandle(hGameSem);

    TerminateThread(hPaintThd, 0);

    CloseHandle(hPaintThd);
    CloseHandle(hPaintSem);
    CloseHandle(hFieldMtx);

    free(field);
}

void DrawField(HWND hwnd)
{
    RECT r;
    HDC hdc, hdcMem;
    PAINTSTRUCT ps;
    HPEN hPen, hPenOld;
    BITMAP bitmap;
    HGDIOBJ oldBitmap;
    HANDLE hNewBitmap;
    int i, j;
    CHAR t;

    if (GetClientRect(hwnd, &r))
    {
        printf("%d %d %d %d\n", r.left, r.top, r.right, r.bottom);

        hdc = BeginPaint(hwnd, &ps);

        FillRect(hdc, &r, (HBRUSH)(COLOR_WINDOW + 1));

        hPen = CreatePen(PS_SOLID, 5, RGB(0, 0, 0));
        hPenOld = (HPEN)SelectObject(hdc, hPen);

        for (i = 0; i < N - 1; i++)
        {
            MoveToEx(hdc, 0, r.bottom * (i + 1) / N, NULL);
            LineTo(hdc, r.right, r.bottom * (i + 1) / N);
            MoveToEx(hdc, r.right * (i + 1) / N, 0, NULL);
            LineTo(hdc, r.right * (i + 1) / N, r.bottom);
        }

        SelectObject(hdc, hPenOld);
        DeleteObject(hPen);

        hdcMem = CreateCompatibleDC(hdc);

        for (i = 0; i < N; i++)
            for (j = 0; j < N; j++)
            {
                WaitForSingleObject(hFieldMtx, INFINITE);
                t = field[i * N + j];
                ReleaseMutex(hFieldMtx);

                switch (t)
                {
                case CCROSS:
                    hNewBitmap = hCrossImage;
                    break;

                case CNOUGHT:
                    hNewBitmap = hNoughtsImage;
                    break;

                default:
                    continue;
                }

                oldBitmap = SelectObject(hdcMem, hNewBitmap);
                GetObject(hNewBitmap, sizeof(bitmap), &bitmap);

                BitBlt(
                    hdc,
                    r.right * (i * 2 + 1) / (N * 2) - bitmap.bmWidth / 2,
                    r.bottom * (j * 2 + 1) / (N * 2) - bitmap.bmHeight / 2,
                    bitmap.bmWidth,
                    bitmap.bmHeight,
                    hdcMem,
                    0,
                    0,
                    SRCCOPY
                );
                SelectObject(hdcMem, oldBitmap);
            }

        DeleteDC(hdc);
        EndPaint(hwnd, &ps);
    }
}

BOOL KeyPressEventHandled(WPARAM wParam)
{
    switch (wParam)
    {
        case 'Q':
            if (!(GetKeyState(VK_CONTROL) & KEY_SHIFTED)) break;
        case VK_ESCAPE:
            PostQuitMessage(WM_QUIT);
            return TRUE;
    }

    return FALSE;
}

BOOL MouseLBPressEventHandled(LPARAM lParam)
{
    POINTS p;
    RECT r;
    int x, y;
    char t;

    if (bIsOurStep == FALSE)
    {
        puts("Not your step!");
        return FALSE;
    }

    p = MAKEPOINTS(lParam);

    if (GetClientRect(hwnd, &r))
    {
        printf("%d %d %d %d\n", r.left, r.top, r.right, r.bottom);

        x = p.x * N / r.right;
        y = p.y * N / r.bottom;

        printf("%d:%d, %d:%d\n", p.x, p.y, x, y);

        WaitForSingleObject(hFieldMtx, INFINITE);
        t = field[x * N + y];
        ReleaseMutex(hFieldMtx);

        if (t == 0)
        {
            WaitForSingleObject(hFieldMtx, INFINITE);
            field[x * N + y] = cOurSym;
            ReleaseMutex(hFieldMtx);
            return TRUE;
        }
    }

    return FALSE;
}

CHAR CheckGameEnd(VOID)
{
    int i;
    CHAR result = 0;

    WaitForSingleObject(hFieldMtx, INFINITE);

#if (N == 3)
#define CHECK_RETURN_COMB(char1, char2, char3) \
  if (result = (((char1) & (char2)) & (char3))) goto exit

    CHECK_RETURN_COMB(field[0], field[1], field[2]);
    CHECK_RETURN_COMB(field[3], field[4], field[5]);
    CHECK_RETURN_COMB(field[6], field[7], field[8]);

    CHECK_RETURN_COMB(field[0], field[3], field[6]);
    CHECK_RETURN_COMB(field[1], field[4], field[7]);
    CHECK_RETURN_COMB(field[2], field[5], field[8]);

    CHECK_RETURN_COMB(field[0], field[4], field[8]);
    CHECK_RETURN_COMB(field[2], field[4], field[6]);

#undef CHECK_RETURN_COMB

#else
    int j;

    for (i = 0; i < N; i++)
    {
        result = 0xFF;
        for (j = 0; j < N; j++)
            result &= field[i * N + j];
        if (result)
            goto exit;
    }

    for (i = 0; i < N; i++)
    {
        result = 0xFF;
        for (j = 0; j < N; j++)
            result &= field[j * N + i];
        if (result)
            goto exit;
    }

    result = 0xFF;
    for (i = 0; i < N; i++)
        result &= field[i * N + i];
    if (result)
        goto exit;

    result = 0xFF;
    for (i = 0; i < N; i++)
        result &= field[i * N + (N - i - 1)];
    if (result)
        goto exit;
#endif

    result = 1;
    for (i = 0; i < N * N && result; i++)
        result &= field[i] != 0;
    if (result)
        goto exit;

exit:
    ReleaseMutex(hFieldMtx);
    return result;

}

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_TICTACTOEMESSAGE)
    {
        CHAR existCombination = 0;
        message = WM_PAINT;

        bIsOurStep = (BOOL)wParam != bAreWeServer;
        printf("bIsOurStep: %d\n", bIsOurStep);
    }

    switch (message)
    {
    case WM_PAINT:
        InvalidateRect(hwnd, NULL, FALSE);
        ReleaseSemaphore(hPaintSem, 1, NULL);
        return 0;

    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_DESTROY:
        PostMessage(hwnd, WM_QUIT, 0, 0);
        return 0;

    case WM_KEYUP:
        puts("Key up");
        if (KeyPressEventHandled(wParam))
            return 0;
        break;

    case WM_LBUTTONUP:
        puts("Left Button Up");
        if (MouseLBPressEventHandled(lParam))
        {
            SendMessage(HWND_BROADCAST, WM_TICTACTOEMESSAGE, bAreWeServer, 0);
            return 0;
        }
        break;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

#ifdef SAMPLE_USE_WINAPI_ENTRYPOINT
int WINAPI WinMain(HINSTANCE hThisInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpszArgument,
    int nCmdShow)
#else
int main(int argc, char** argv)
#endif /* SAMPLE_USE_WINAPI_ENTRYPOINT */
{
    BOOL bMessageOk;
    MSG messages;
    WNDCLASS wincl = { 0 };

#ifndef SAMPLE_USE_WINAPI_ENTRYPOINT
    int nCmdShow = SW_SHOW;
    HINSTANCE hThisInstance = GetModuleHandle(NULL);
#endif

    wincl.hInstance = hThisInstance;
    wincl.lpszClassName = szWinClass;
    wincl.lpfnWndProc = WindowProcedure;

    hBrush = CreateSolidBrush(RGB(255, 255, 255));
    wincl.hbrBackground = hBrush;

    if (!RegisterClass(&wincl))
        return 0;

    hwnd = CreateWindowEx(0, szWinClass, szWinName, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 800, HWND_DESKTOP, NULL, hThisInstance, NULL);

    hCrossImage = LoadImage(hThisInstance, L"C:\\Users\\dev_pavel\\source\\repos\\TicTacToe\\TicTacToe\\cross.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    hNoughtsImage = LoadImage(hThisInstance, L"C:\\Users\\dev_pavel\\source\\repos\\TicTacToe\\TicTacToe\\naught.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

    if (!hCrossImage || !hNoughtsImage) {
        puts("Images don't loaded!");
    }

    if (InitField())
    {
        puts("Field init failed!");
        return 0;
    }

    WM_TICTACTOEMESSAGE = RegisterWindowMessage(L"WM_TICTACTOEMESSAGE");

    ShowWindow(hwnd, nCmdShow);

    while ((bMessageOk = GetMessage(&messages, NULL, 0, 0)) != 0)
    {
        if (bMessageOk == -1)
        {
            puts("Suddenly, GetMessage failed! You can call GetLastError() to see what happend");
            break;
        }
        TranslateMessage(&messages);
        DispatchMessage(&messages);
    }

    DeleteObject(hCrossImage);
    DeleteObject(hNoughtsImage);
    DestroyWindow(hwnd);
    UnregisterClass(szWinClass, hThisInstance);
    DeleteObject(hBrush);
    FiniField();

    return (int)messages.wParam;
}
