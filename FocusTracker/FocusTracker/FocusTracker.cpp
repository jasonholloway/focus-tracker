#include "pch.h"

#define WM_PROCESS_JOBS (WM_USER + 0x0001)

using namespace std;
using namespace boost::lockfree;
using namespace boost::asio::ip;
using boost::asio::ip::udp;

class LogBuf : public std::stringbuf {
public:
	udp::socket* _socket;
	udp::endpoint _endpoint;

	LogBuf(udp::socket* socket, udp::endpoint endpoint) 
		: _socket(socket), _endpoint(endpoint) {
	}

	virtual int sync() {
		auto string = this->str();
		return _socket->send_to(boost::asio::buffer(string), _endpoint);
	}
};

struct Focus {
public:
	DWORD time;
	HWND hwnd;
};

struct SessionChange {
public:
	DWORD time;
	UINT sessionId;
	DWORD event;
};

void WINAPI OnFocus(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
void OnSessionEvent(MSG *msg);
HWND CreateMessageWindow();
void CleanUp();
BOOL WINAPI CloseHandler(DWORD);
LONG WINAPI ExceptionFilter(LPEXCEPTION_POINTERS);
std::function<void(Focus)> dispatchFocus(std::ostream* sink);


HWINEVENTHOOK _focusHook = 0;

auto _qFocuses = new spsc_queue<Focus, capacity<1000>>();

char szName[512];
char szPath[512];

HWND _hwnd;
DWORD _sessionId;

int main() {
	SetUnhandledExceptionFilter(ExceptionFilter);
	SetConsoleCtrlHandler(CloseHandler, TRUE);

	boost::asio::io_context io;
	udp::socket s(io, udp::endpoint(udp::v4(), 0));
	
	udp::resolver resolver(io);
	auto endpoints = resolver.resolve(udp::v4(), "localhost", "32323");

	LogBuf logBuf(&s, endpoints.begin()->endpoint());
	ostream sink(&logBuf);

	sink << "WOW!" << endl;
	sink.flush();

	_hwnd = CreateMessageWindow();
	_sessionId = WTSGetActiveConsoleSessionId();

	_focusHook = SetWinEventHook(
		EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, 
		NULL,
		OnFocus,
		0, 0, 
		WINEVENT_SKIPOWNPROCESS);

	WTSRegisterSessionNotification(_hwnd, 0);

	cout << "Tracking changes to window focus..." << endl;

	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		switch (msg.message) {

		case WM_PROCESS_JOBS:
			_qFocuses->consume_all(dispatchFocus(&sink));
			break;
					   
		case WM_WTSSESSION_CHANGE:
			OnSessionEvent(&msg);
			break;

		default: DispatchMessage(&msg);
		}
	}

	CleanUp();
}

function<void(Focus)> dispatchFocus(ostream *out) {
	return [=] (Focus job) {
		if (IsWindow(job.hwnd)) {
			szName[0] = 0;
			GetWindowTextA(job.hwnd, szName, ARRAYSIZE(szName));

			DWORD idProc = 0;
			szPath[0] = 0;
			GetWindowThreadProcessId(job.hwnd, &idProc);
			auto hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, idProc);
			GetModuleFileNameExA(hProc, NULL, szPath, ARRAYSIZE(szPath));
			CloseHandle(hProc);

			*out << job.time << " |#| " << szPath << " |#| " << szName << endl;
		}
	};
}

void WINAPI OnFocus(HWINEVENTHOOK hWinEventHook, DWORD dwEvent, HWND hwndFocus, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
	switch (dwEvent) {
		case EVENT_SYSTEM_FOREGROUND:
			Focus job;
			job.time = dwmsEventTime;
			job.hwnd = hwndFocus;
			_qFocuses->push(job);

			PostMessage(_hwnd, WM_PROCESS_JOBS, NULL, NULL);
			break;
	}
}

const char* showSessionEvent(DWORD event) {
	switch (event) {
	case WTS_SESSION_LOCK:
		return "LOCK";
	case WTS_SESSION_UNLOCK:
		return "UNLOCK";
	case WTS_SESSION_LOGON:
		return "LOGON";
	case WTS_SESSION_LOGOFF:
		return "LOGOFF";
	default:
		return "OTHER";
	}
}

void OnSessionEvent(MSG *msg) {
	SessionChange change;
	change.time = msg->time;
	change.event = msg->wParam;
	change.sessionId = msg->lParam;

	cout << change.time << " |#| SESSION " << change.sessionId << " " << showSessionEvent(change.event) << endl;
}

HWND CreateMessageWindow() {
	auto hInstance = GetModuleHandle(NULL);

	WNDCLASSEX wndClass = {};
	wndClass.cbSize = sizeof(WNDCLASSEX);
	wndClass.lpfnWndProc = DefWindowProc;
	wndClass.hInstance = hInstance;
	wndClass.lpszClassName = TEXT("FocusTracker");
	RegisterClassEx(&wndClass);

	return CreateWindow(TEXT("FocusTracker"), TEXT("FocusTracker"), 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
}

void CleanUp() {
	if (_focusHook != 0) {
		UnhookWinEvent(_focusHook);
		_focusHook = 0;
	}

	delete _qFocuses;
}

BOOL WINAPI CloseHandler(DWORD dwCtrlType) {
	switch (dwCtrlType) {
		case CTRL_SHUTDOWN_EVENT:
			return FALSE;
		case CTRL_LOGOFF_EVENT:
			return FALSE;
		default:
			PostMessage(_hwnd, WM_QUIT, NULL, NULL);
			return TRUE;
	}
}

LONG WINAPI ExceptionFilter(LPEXCEPTION_POINTERS ExceptionInfo) {
	PostMessage(_hwnd, WM_QUIT, NULL, NULL);
	return 0;
}

