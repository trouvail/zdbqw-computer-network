#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <thread>
#include <string>
#pragma warning(disable : 4996)
#pragma comment(lib, "ws2_32.lib")  //加载 ws2_32.dll

#define BUF_SIZE 100

DWORD WINAPI Send(LPVOID sockpara) { // 发送消息函数
	SOCKET * sock = (SOCKET*)sockpara;
	char bufSend[BUF_SIZE] = { 0 };
	while (1) {
		//printf("Input a string: ");
		std::cin >> bufSend;
		int t = send(*sock, bufSend, strlen(bufSend), 0);
		if (strcmp(bufSend, "bye") == 0)
		{
			SYSTEMTIME st = { 0 };
			GetLocalTime(&st); // 获取当前时间
			closesocket(*sock); // 关闭套接字
			std::cout << "=============================================================" << std::endl;
			std::cout << "你在" << st.wDay << "号" << st.wHour << "点" << st.wMinute << "分" << st.wSecond << "秒离开了与那个人聊天" << std::endl;
			std::cout << "=============================================================" << std::endl;
			return 0L;
		}
		if (t > 0) {
			SYSTEMTIME st = { 0 };
			GetLocalTime(&st);
			std::cout << "-------------------------------------------------------------" << std::endl;
			std::cout << "你在" << st.wDay << "号" << st.wHour << "点" << st.wMinute << "分" << st.wSecond << "秒发给了那个人一条消息\n";
			std::cout << "-------------------------------------------------------------" << std::endl;
		}
		memset(bufSend, 0, BUF_SIZE);
	}
}



DWORD WINAPI Recv(LPVOID sock_) { // 接收消息函数
	char bufRecv[BUF_SIZE] = { 0 };
	SOCKET *sock = (SOCKET*)sock_;
	while (1) {
		int t = recv(*sock, bufRecv, BUF_SIZE, 0);
		if (strcmp(bufRecv, "bye") == 0)
		{
			SYSTEMTIME st = { 0 };
			GetLocalTime(&st); // 获取当前时间
			closesocket(*sock);
			std::cout << "=============================================================" << std::endl;
			std::cout << "那个人在" << st.wDay << "号" << st.wHour << "点" << st.wMinute << "分" << st.wSecond << "秒离开了与你的聊天" << std::endl;
			std::cout << "=============================================================" << std::endl;
			return 0L;
		}
		if (t > 0) {
			SYSTEMTIME st = { 0 };
			GetLocalTime(&st); 
			std::cout << "-------------------------------------------------------------" << std::endl;
			std::cout << "那个人在" << st.wDay << "号" << st.wHour << "点" << st.wMinute << "分" << st.wSecond << "秒发过来一条消息:\n";
			printf("%s\n", bufRecv);
			std::cout << "-------------------------------------------------------------" << std::endl;
		}
		memset(bufRecv, 0, BUF_SIZE);
	}
}



int main() {
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0)
	{
		std::cout << "成功唤醒网络" << std::endl;
	}
	else {
		std::cout << "出现了一点问题导致唤醒网络失败，请重启后再试(>﹏<)" << std::endl;
		return 0;
	}

	//创建套接字
	SOCKET servSock = socket(AF_INET, SOCK_STREAM, 0);

	//绑定套接字
	sockaddr_in sockAddr;
	memset(&sockAddr, 0, sizeof(sockAddr));  //每个字节都用0填充
	sockAddr.sin_family = PF_INET;  //使用IPv4地址
	sockAddr.sin_addr.s_addr = inet_addr("127.0.0.1");  //具体的IP地址
	sockAddr.sin_port = htons(1234);  //端口
	bind(servSock, (SOCKADDR*)&sockAddr, sizeof(SOCKADDR));

	// 进入监听状态，监听远程连接是否到来
	if (listen(servSock, 20) == 0) {
		std::cout << "正在监听中，等待是会有结果的" << std::endl;
	}
	else {
		std::cout << "出现一些错误导致监听失败，请重启后再试(>﹏<)" << std::endl;
		return 0;
	}

	// 接收客户端请求
	SOCKADDR clntAddr;
	int nSize = sizeof(SOCKADDR);
	SOCKET clntSock = accept(servSock, (SOCKADDR*)&clntAddr, &nSize);
	if (clntSock > 0) {
		std::cout << "那个人终于出现了，现在可以开始你们的聊天" << std::endl;
	}

	// 为收发数据分别创建线程
	HANDLE hThread[2];
	hThread[0] = CreateThread(NULL, 0, Recv, (LPVOID)&clntSock, 0, NULL);
	hThread[1] = CreateThread(NULL, 0, Send, (LPVOID)&clntSock, 0, NULL);
	WaitForMultipleObjects(2, hThread, TRUE, INFINITE);
	CloseHandle(hThread[0]);
	CloseHandle(hThread[1]);
	
	// 关闭套接字
	closesocket(clntSock);

	// 关闭套接字
	closesocket(servSock);

	// 终止网络库DLL的使用
	WSACleanup();

	return 0;
}