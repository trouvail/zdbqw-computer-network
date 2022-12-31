#pragma comment(lib, "Ws2_32.lib")
#include <iostream>
#include <winsock.h>
#include <fstream>
#include <time.h>
#include <string>
#include<queue>
using namespace std;

#define PORT 8888
const int MAXLEN = 509;
char buffer[200000000];
const unsigned char ACK = 0x01;
const unsigned char NAK = 0x02;
const unsigned char LAST = 0x08; 
const unsigned char NOTLAST = 0x18;
const unsigned char SHAKE_1 = 0x03;
const unsigned char SHAKE_2 = 0x04;
const unsigned char SHAKE_3 = 0x05;
const unsigned char WAVE_1 = 0x06;
const unsigned char WAVE_2 = 0x07;
const int MAX_WAIT_TIME = 50000;
bool flaglist[UCHAR_MAX + 1] ;
 struct flags
{
	int time;
	int order;
	 flags(int a, int b):time(a),order(b) {}
};
 int cwnd = 1;
 int temp=65;
 int ssthresh = 64;

 SOCKET client;

SOCKADDR_IN serverAddr, clientAddr;
//����У���
unsigned char checksum(char *package, int len) {
	if (len == 0) {
		return ~(0);
	}
	unsigned int sum = 0;
	int i = 0;
	while (len--) {
		sum += (unsigned char)package[i++];
		if (sum & 0xFF00) {
			sum &= 0x00FF;
			sum++;
		}
	}
	return ~(sum & 0x00FF);
}
void shake_hand() {
	while (true) {
		char shake_package[2];
		shake_package[1] = SHAKE_1;
		shake_package[0] = checksum(shake_package + 1, 1);
		sendto(client, shake_package, 2, 0, (sockaddr *)&serverAddr, sizeof(serverAddr));
		//��һ������
		int begin = clock();
		char recv[2];
		int len = sizeof(clientAddr);
		int fail = 0;
		while (recvfrom(client, recv, 2, 0, (sockaddr *)&serverAddr, &len) == SOCKET_ERROR) {
			if (clock() - begin > MAX_WAIT_TIME) {
				fail = 1;
				break;
			}
		}
		if (fail == 0 && checksum(recv, 2) == 0 && recv[1] == SHAKE_2)
		{
			//����������
			shake_package[1] = SHAKE_3;
			shake_package[0] = checksum(shake_package + 1, 1);
			sendto(client, shake_package, 2, 0, (sockaddr *)&serverAddr, sizeof(serverAddr));
			break;
		}
	}
}
void wave_hand() {
	while (true) {
		char tmp[2];
		tmp[1] = WAVE_1;
		tmp[0] = checksum(tmp + 1, 1);
		sendto(client, tmp, 2, 0, (sockaddr *)&serverAddr, sizeof(serverAddr));
		int begin = clock();
		char recv[2];
		int len = sizeof(clientAddr);
		int fail = 0;
		while (recvfrom(client, recv, 2, 0, (sockaddr *)&serverAddr, &len) == SOCKET_ERROR) {
			if (clock() - begin > MAX_WAIT_TIME) {
				fail = 1;
				break;
			}
		}
		if (fail == 0 && checksum(recv, 2) == 0 && recv[1] == WAVE_2)
		{
			break;
		}
	}
	cout << "wave hand" << endl;
}
bool send_p(char* pkt, int len, int serial_num, int last = 0) {
	static int count = 0;
	if (len > MAXLEN || (last == false && len != MAXLEN)) {
		return false;
	}
	char *real_package;
	int tmp_len;
	if (!last) {//make package
		real_package = new char[len + 3];
		real_package[1] = NOTLAST;
		real_package[2] = serial_num;
		for (int i = 3; i < len + 3; i++)
		{
			real_package[i] = pkt[i - 3];
		}
		real_package[0] = checksum(real_package + 1, len + 2);
		tmp_len = len + 3;
	}
	else {//���һ����

		real_package = new char[len + 4];
		real_package[1] = LAST;
		real_package[2] = serial_num;
		real_package[3] = len;
		for (int i = 4; i < len + 4; i++) {
			real_package[i] = pkt[i - 4];
		}
		real_package[0] = checksum(real_package + 1, len + 3);
		tmp_len = len + 4;
	}
 		sendto(client, real_package, tmp_len, 0, (sockaddr *)&serverAddr, sizeof(serverAddr));
		return true;
}
void send_m(char *message, int len) {
	 cwnd = 1;
	 temp = 65;
	 ssthresh = 64;
	queue<flags> mylist;
	int resend_times = 0;//�ط��ʹ���
	int base = 0;
	int send_num = 0;//�Ѿ����͵�����
	int next_package = base;
	int send_num_succ = 0;//�Ѿ��ɹ����͵�����
	int tot_package = len / MAXLEN + (len % MAXLEN != 0);
	int flag = 0;//�յ������ack��������Ϊ����ʱ���ش�
	int flag2 = 0;//�ش�״̬Ϊ0����������Ϊ1���ж��Ƿ������ٻָ�ģʽ�ı�־
	while (1) {
		if (send_num_succ == tot_package)
			break;
		while (mylist.size() <cwnd && send_num != tot_package) {
				send_p(message + send_num * MAXLEN,
					send_num == tot_package - 1 ? len - (tot_package - 1) * MAXLEN : MAXLEN,
					next_package % ((int)UCHAR_MAX + 1),
					send_num == tot_package - 1);
				mylist.push(flags(clock(), next_package % ((int)UCHAR_MAX + 1)));
				flaglist[next_package % ((int)UCHAR_MAX + 1)] = 1;
				next_package++;
				send_num++;
				cout << "�Ѿ�����" << send_num << "����" << endl;
		}

		char recv[3];
		int lentmp = sizeof(serverAddr);
		int acctimes = send_num - send_num_succ;
		if (recvfrom(client, recv, 3, 0, (sockaddr *)&serverAddr, &lentmp) != SOCKET_ERROR && checksum(recv, 3) == 0 &&
			recv[1] == ACK /*&& flaglist[(unsigned char)recv[2]]*/) {
			if (flag2 )flag++;
			//ȷ�Ͻ���
			if ((unsigned char)recv[2] ==unsigned char((send_num_succ ) % ((int)UCHAR_MAX + 1))) {
				if (cwnd <= ssthresh) {
					cwnd++;
				}
				else
				{
					temp++;
					if (temp%cwnd == 0&&cwnd< (int)UCHAR_MAX) {
						cwnd++;
					}
				}
				flaglist[mylist.front().order] = 0;
				send_num_succ++;
				base++;
				resend_times = 0;
				mylist.pop();
				flag = 0;
				flag2 = 1;
				cout << "�Ѿ�ȷ�Ϸ���" << send_num_succ << "����" << endl;
				continue;
			}
			//���ٻָ�
			if (flag == 3) {
					flag2 = 0;
					flag = 0;
					ssthresh = cwnd / 2;
					cwnd = cwnd / 2 + 3;
					temp = ssthresh + 1;
					next_package = base;
					resend_times++;
					send_num -= mylist.size();
					while (!mylist.empty()) 
					{ mylist.pop(); }
					cout << "��Ϊ���������ش���" << resend_times << "��" << "�ش���" << send_num << "����" << endl;
					continue;
				}
			//��ʱ�ش�
			if (clock() - mylist.front().time > MAX_WAIT_TIME) {
				flag2 = 0;
				flag = 0;
				ssthresh = cwnd / 2;
				cwnd = 1;
				temp = ssthresh + 1;
				next_package = base;
				resend_times++;
				send_num -= mylist.size();
				while (!mylist.empty())
				{
					mylist.pop();
				}
				cout << "��Ϊ��ʱ�����ش���" << resend_times << "��" << "�ش���" << send_num << "����" << endl;
			}

		}
		//�����ش�
		else  {
				flag2 = 0;
				flag = 0;
				ssthresh = cwnd/2;
				cwnd = 1;
				temp = ssthresh+1;
				next_package = base;
				resend_times++;
				send_num -= mylist.size();
				while (!mylist.empty()) {
					mylist.pop();
				}
				cout << "��Ϊ���������ش���" << resend_times << "��" << "�ش���" << send_num << "����" << endl;
			}

	}
}

int main() {
	WSADATA wsadata;
	if (WSAStartup(MAKEWORD(2, 2), &wsadata)) {
		printf("error!");
		return 0;
	}

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(10004);
	serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	client = socket(AF_INET, SOCK_DGRAM, 0);

	int time_out = 50;
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (char *)&time_out, sizeof(time_out));

	if (client == INVALID_SOCKET) {
		printf("socket of client invalid!");
		closesocket(client);
		return 0;
	}
	// ��������
	printf("connecting \n");
	shake_hand();
	printf("connect success!\n");
	//���ݴ���
		string filename;
		printf("Please input your filename:");
		cin >> filename;
		if (!strcmp("exit", filename.c_str())) {
			send_m((char*)filename.c_str(), filename.length());
			
		}
		send_m((char*)filename.c_str(), filename.length());
		// ʹ�ö����Ʒ�ʽ �򿪵�ǰĿ¼�µ��ļ�
		ifstream in(filename.c_str(), ifstream::binary);
		int len = 0;
		if (!in) {
			printf("can't open the file! Please retry\n");
			return 0;
		}
		// �ļ���ȡ��buffer
		BYTE t = in.get();
		while (in) {
			buffer[len++] = t;
			t = in.get();
		}
		in.close();
		printf("read over\n");
		send_m(buffer, len);
		printf("send over\n");
	wave_hand();
	closesocket(client);
	WSACleanup();
	printf("program will close after 3 seconds\n");
	Sleep(3000);
	return 0;
}