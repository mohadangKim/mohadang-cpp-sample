#define _WINSOCK_DEPRECATED_NO_WARNINGS // �ֽ� VC++ ������ �� ��� ����

#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>

#define SERVERPORT 9000
#define BUFSIZE    512

void PrintError(const char* msg) {
  std::cout << "Error : " << WSAGetLastError() << std::endl;
}

void PrintIpPort(const char* msg, const SOCKADDR_IN& clientaddr) {
  std::cout
    << "[IP = " << inet_ntoa(clientaddr.sin_addr)
    << ", Port = " << ntohs(clientaddr.sin_port)
    << "] "
    << msg
    << std::endl;
}

class SOCKETINFO {
public:
  OVERLAPPED _overlapped;
  SOCKET _sock;
  char _buf[BUFSIZE + 1];

private:
  int _recvbytes;
  int _sendbytes;
  WSABUF _wsabuf;

public:
  SOCKETINFO(SOCKET sock) :
    _sock(sock),
    _recvbytes(0),
    _sendbytes(0) { }
  void InitRecvBuf() {
    ZeroMemory(&_overlapped, sizeof(_overlapped));
    _recvbytes = 0;
    _wsabuf.buf = _buf;
    _wsabuf.len = BUFSIZE;
  }
  void UpdateSendBuf() {
    ZeroMemory(&_overlapped, sizeof(_overlapped));
    _wsabuf.buf = _buf + _sendbytes;
    _wsabuf.len = _recvbytes - _sendbytes;
  }
  void UpdateRecvBufStatus(DWORD transferred_size) {
    _recvbytes = transferred_size;
    _sendbytes = 0;
    // ���� ������ ���
    _buf[_recvbytes] = '\0';
  }
  void UpdateSendBufStatus(DWORD transferred_size) {
    _sendbytes += transferred_size;
  }
  bool IsRecv() {
    return _recvbytes == 0;
  }
  bool IsRemainSend() {
    return _recvbytes > _sendbytes;
  }
  void RecvData() {
    DWORD recvbytes = 0;
    DWORD flags = 0;
    int retval = WSARecv(
      _sock,
      &_wsabuf, 1, &recvbytes,
      &flags, &_overlapped, NULL);
    if (retval == SOCKET_ERROR) {
      if (WSAGetLastError() != ERROR_IO_PENDING) {
      }
    }
  }
  void SendData() {
    DWORD sendbytes;
    int retval = WSASend(_sock, &_wsabuf, 1,
      &sendbytes, 0, &_overlapped, NULL);
    if (retval == SOCKET_ERROR) {
      if (WSAGetLastError() != WSA_IO_PENDING) {
      }
    }
  }
};

DWORD WINAPI WorkerThread(LPVOID arg)
{
  int retval;
  HANDLE hcp = (HANDLE)arg;

  while (1) {
    // �񵿱� ����� �Ϸ� ��ٸ���
    DWORD transferred_size;
    SOCKET client_sock;
    SOCKETINFO* sock_info;
    retval = GetQueuedCompletionStatus(
      hcp, &transferred_size,
      (PULONG_PTR)&client_sock, (LPOVERLAPPED*)&sock_info,
      INFINITE);

    // Ŭ���̾�Ʈ ���� ���
    SOCKADDR_IN clientaddr;
    int addrlen = sizeof(clientaddr);
    getpeername(sock_info->_sock, (SOCKADDR*)&clientaddr, &addrlen);

    // �񵿱� ����� ��� Ȯ��
    if (retval == 0 || transferred_size == 0) {
      if (retval == 0) {
        DWORD temp1, temp2;
        WSAGetOverlappedResult(sock_info->_sock, &sock_info->_overlapped, &temp1, FALSE, &temp2);
        PrintError("WSAGetOverlappedResult()");
      }
      closesocket(sock_info->_sock);
      PrintIpPort("Client Session Closed", clientaddr);
      delete sock_info;
      continue;
    }

    // ������ ���۷� ����
    if (sock_info->IsRecv()) {
      sock_info->UpdateRecvBufStatus(transferred_size);
      PrintIpPort(sock_info->_buf, clientaddr);
    }
    else {
      sock_info->UpdateSendBufStatus(transferred_size);
    }

    if (sock_info->IsRemainSend()) {
      // ������ ������
      sock_info->UpdateSendBuf();
      sock_info->SendData();
    }
    else {
      sock_info->InitRecvBuf();
      sock_info->RecvData();
    }
  }

  return 0;
}

int main(int argc, const char* argv[]) {
  int retval;

  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    return 1;
  }

  // ����� �Ϸ� ��Ʈ ����
  HANDLE hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
  if (hcp == NULL) {
    return 1;
  }

  // CPU ���� Ȯ��
  SYSTEM_INFO si;
  GetSystemInfo(&si);

  // (CPU ���� * 2)���� �۾��� ������ ����
  HANDLE hThread;
  for (int i = 0; i < (int)si.dwNumberOfProcessors * 2; i++) {
    hThread = CreateThread(NULL, 0, WorkerThread, hcp, 0, NULL);
    if (hThread == NULL) {
      return 1;
    }
    CloseHandle(hThread);
  }

  // socket()
  SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_sock == INVALID_SOCKET) {
    PrintError("socket()");
    return 1;
  }

  // bind()
  SOCKADDR_IN serveraddr;
  ZeroMemory(&serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serveraddr.sin_port = htons(SERVERPORT);
  retval = bind(listen_sock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
  if (retval == SOCKET_ERROR) {
    PrintError("bind()");
    return 1;
  }

  // listen()
  retval = listen(listen_sock, SOMAXCONN);
  if (retval == SOCKET_ERROR) {
    PrintError("listen()");
    return 1;
  }

  while (true) {
    SOCKADDR_IN clientaddr;
    int addrlen = sizeof(clientaddr);
    SOCKET client_sock = accept(listen_sock, (SOCKADDR*)&clientaddr, &addrlen);
    if (client_sock == INVALID_SOCKET) {
      PrintError("accept()");
      break;
    }

    PrintIpPort("Client Session Connected", clientaddr);
    CreateIoCompletionPort((HANDLE)client_sock, hcp, client_sock, 0);
    SOCKETINFO* sock_info = new SOCKETINFO(client_sock);
    sock_info->InitRecvBuf();
    sock_info->RecvData();
  }

  WSACleanup();
  return 0;
}
