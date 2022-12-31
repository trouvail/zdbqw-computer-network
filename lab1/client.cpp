#include <stdio.h>
#include <WinSock2.h>
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
		// 发送数据
		int t = send(*sock, bufSend, strlen(bufSend), 0);
		// 发送bye代表结束聊天
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
	SOCKET *sock = (SOCKET*)sock_; // 创建套接字
	while (1) {
		int t = recv(*sock, bufRecv, BUF_SIZE, 0); // 接收数据
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
		memset(bufRecv, 0, BUF_SIZE); // 将存放数据的数组初始化为0
	}
}



int main() {
	//初始化网络库DLL
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) // 调用者希望的最高版本——2.2版和可用socket详细信息
	{
		std::cout << "成功唤醒网络" << std::endl;
	}
	else {
		std::cout << "出现了一点问题导致唤醒网络失败，请重启后再试(>﹏<)" << std::endl;
		return 0;
	}
	sockaddr_in sockAddr;
	memset(&sockAddr, 0, sizeof(sockAddr));  //每个字节都用0填充
	sockAddr.sin_family = PF_INET;
	sockAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	sockAddr.sin_port = htons(1234);
	SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);

	if (connect(sock, (SOCKADDR*)&sockAddr, sizeof(SOCKADDR)) == 0)
	{
		std::cout << "那个人终于等到你了，现在可以开始你们的聊天了" << std::endl;
	}
	else {
		std::cout << "那个人还没有出现，请重启后再试(>﹏<)" << std::endl;
		return 0;
	}
	
	HANDLE hThread[2]; // 为收发数据分别创建线程
	hThread[0] = CreateThread(NULL, 0, Recv, (LPVOID)&sock, 0, NULL); // 创建接收线程
	hThread[1] = CreateThread(NULL, 0, Send, (LPVOID)&sock, 0, NULL); // 创建发送线程
	WaitForMultipleObjects(2, hThread, TRUE, INFINITE);
	CloseHandle(hThread[0]);
	CloseHandle(hThread[1]);
	closesocket(sock); 
	WSACleanup(); 
	return 0;
}