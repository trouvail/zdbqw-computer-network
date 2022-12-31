#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <time.h>
#include <WinSock2.h>
#include <Ws2tcpip.h>
#pragma comment(lib,"ws2_32.lib")

using namespace std;

#define MSS 6666
#define SYN 0x1
#define ACK 0x2
#define FIN 0x4
#define LAS 0x8
#define RST 0x10
#define OVERTIME (CLOCKS_PER_SEC / 2)
#define RETRYS 5
#define MAXTIME (CLOCKS_PER_SEC * 5)
// 期望获得的数据报
u_short exp_seq = 1;

// 用一字节的方式进行对齐
#pragma pack(push)
#pragma pack(1)
struct Header 
{
    u_short seq;
    u_short ack;
    // 标志位
    u_short flag;
    u_short chsum;
    u_short length;
    void set_header(u_short seq, u_short ack, u_short flag, u_short chsum, u_short length) 
    {
        this->seq = seq;
        this->ack = ack;
        this->flag = flag;
        this->chsum = chsum;
        this->length = length;
    }
};
#pragma pack(pop)

// 检测校验和 零为正确数据报
u_short chsum(char* msg, int length) 
{
    int size = length % 2 ? length + 1 : length;
    // 由于以字节为单位
    int count = size / 2;
    char* buf = new char[size];
    memset(buf, 0, size);
    memcpy(buf, msg, length);
    u_long sum = 0;
    u_short* buf_it = (u_short*)buf;
    while (count--) {
        sum += *buf_it++;
        if (sum & 0xffff0000) {
            sum &= 0xffff;
            sum++;
        }
    }
    delete[]buf;
    // 为零则代表校验和正确
    return ~(sum & 0xffff);
}

// 三次握手
int connect(SOCKET* server, SOCKADDR_IN* server_addr, char* data_buffer, int* len) 
{
    // 预定义各类数据
    int rt = RETRYS;
    int over_time = OVERTIME;
    Header header;
    char* se_buf = new char[sizeof(header)];
    char* re_buf = new char[sizeof(header) + MSS];
    SOCKADDR_IN c_ad;
    int c_ad_len = sizeof(SOCKADDR_IN);

    while (true) 
    {
        int mid = recvfrom(*server, re_buf, sizeof(header), 0, (sockaddr*)&c_ad, &c_ad_len);
        if (mid == SOCKET_ERROR) 
        {
            cout << strerror(errno) << endl;
            Sleep(2000);
            continue;
        }

        // 收到握手请求
        memcpy(&header, re_buf, sizeof(header));
        int re_chsum = chsum(re_buf, sizeof(header));

        if (re_chsum == 0 && (header.flag & SYN)) 
        {
            cout << "第一次握手成功" << endl;

            // 准备开始第二次握手
            header.set_header(0, 0, ACK + SYN, 0, 0);
            memcpy(se_buf, (char*)&header, sizeof(header));
            re_chsum = chsum(se_buf, sizeof(header));
            ((Header*)se_buf)->chsum = re_chsum;

            // 进行第二次握手
            int mid2 = sendto(*server, se_buf, sizeof(header), 0, (sockaddr*)&c_ad, sizeof(SOCKADDR_IN));
            if (mid2 == SOCKET_ERROR) 
            {
                delete[]se_buf;
                delete[]re_buf;

                // 直接返回
                return -1;
            }

            cout << "进行第二次握手" << endl;

            // 非阻塞模式
            u_long mode = 1;
            ioctlsocket(*server, FIONBIO, &mode);

            clock_t start = clock();
            // 等待接收第三次握手信息
            while (true) {
                int mid3;
                while ((mid3 = recvfrom(*server, re_buf, sizeof(header) + MSS, 0, (sockaddr*)&c_ad, &c_ad_len)) <= 0) 
                {
                    if (clock() - start > 1.2 * over_time) 
                    {
                        if (rt <= 0) 
                        {
                            cout << "握手失败" << endl;
                            delete[]se_buf;
                            delete[]re_buf;

                            // 改为阻塞模式
                            mode = 0;
                            ioctlsocket(*server, FIONBIO, &mode);

                            return -1;
                        }
                        int n = sendto(*server, se_buf, sizeof(header), 0, (sockaddr*)&c_ad, sizeof(SOCKADDR_IN));
                        cout << "重新进行第二次握手" << endl;

                        rt -= 1;
                        over_time += CLOCKS_PER_SEC;
                        start = clock();
                    }
                }
                memcpy(&header, re_buf, sizeof(header));
                re_chsum = chsum(re_buf, mid3);

                if (re_chsum == 0 && (header.flag & ACK)) 
                {
                    // 成功进行第三次握手
                    delete[]se_buf;
                    delete[]re_buf;

                    // 改为阻塞模式
                    mode = 0;
                    ioctlsocket(*server, FIONBIO, &mode);
                    cout << "第三次握手成功" << endl;
                    return 1;
                }
                else if (re_chsum == 0 && (header.flag & SYN)) 
                {
                    start = clock();
                    sendto(*server, se_buf, sizeof(header), 0, (sockaddr*)&c_ad, sizeof(SOCKADDR_IN));
                }
                else if (re_chsum == 0 && header.flag == 0) 
                {
                    // 接收到数据 返回RST
                    Header re;
                    re.set_header(0, 0, RST, 0, 0);
                    memcpy(se_buf, (char*)&re, sizeof(re));
                    re_chsum = chsum(se_buf, sizeof(re));
                    ((Header*)se_buf)->chsum = re_chsum;
                    int n = sendto(*server, se_buf, sizeof(re), 0, (sockaddr*)&c_ad, sizeof(SOCKADDR_IN));
                    cout << "握手失败" << endl;
                    delete[]se_buf;
                    delete[]re_buf;
                    mode = 0;
                    ioctlsocket(*server, FIONBIO, &mode);
                    return -1;
                }
                else 
                {
                    // 无论什么错 直接返回
                    delete[]se_buf;
                    delete[]re_buf;
                    mode = 0;
                    ioctlsocket(*server, FIONBIO, &mode);
                    return -1;
                }
            }
        }
    }
    return 1;
}

// 接收消息
void rec_mes(SOCKET* server, SOCKADDR_IN* server_addr, char* data_buffer, int* len) 
{
    bool rec_done = false;
    SOCKADDR_IN c_ad;
    int client_addr_length = sizeof(SOCKADDR_IN);
    Header header;

    // 设置存放缓冲区
    char* re_buf = new char[MSS + sizeof(header)];
    char* se_buf = new char[sizeof(header)];
    memset(re_buf, 0, MSS + sizeof(header));
    memset(se_buf, 0, sizeof(header));

    ((Header*)se_buf)->set_header(0, 0, ACK, 0, 0);
    ((Header*)se_buf)->chsum = chsum(se_buf, sizeof(Header));
    // 设置非阻塞模式
    u_long mode = 1;
    ioctlsocket(*server, FIONBIO, &mode);

    clock_t start;
    while (true) 
    {
        int mid;
        while ((mid = recvfrom(*server, re_buf, MSS + sizeof(header), 0, (sockaddr*)&c_ad, &client_addr_length)) <= 0) 
        {
            // 长时间没有接收到信息 文件已接收完毕 直接断开连接
            if (rec_done == true && clock() - start > MAXTIME) 
            {
                cout << "长时间未响应 断开连接";
                delete[]se_buf;
                delete[]re_buf;
                mode = 0;
                ioctlsocket(*server, FIONBIO, &mode);
                return;
            }
        }
        memcpy(&header, re_buf, sizeof(header));
        cout << "接收到数据报的长度为" << mid << "字节；其头部为: ";
        cout << "seq: " << header.seq << "；ack: " << header.ack << "；flag: " << header.flag << "；校验和: " << header.chsum << "；length: " << header.length << endl;
        // 计算校验和
        u_short chksum = chsum(re_buf, mid);
        if (chksum != 0) 
        {
            int n = sendto(*server, se_buf, sizeof(Header), 0, (sockaddr*)&c_ad, sizeof(SOCKADDR_IN));
            cout << "校验和出错" << endl;
            continue;
        }
        if (header.flag == FIN) 
        {
            Header re;
            re.set_header(0, (u_short)(header.seq + 1), ACK, 0, 0);
            memcpy(se_buf, (char*)&re, sizeof(re));
            chksum = chsum(se_buf, sizeof(re));
            ((Header*)se_buf)->chsum = chksum;
            int n = sendto(*server, se_buf, sizeof(re), 0, (sockaddr*)&c_ad, sizeof(SOCKADDR_IN));
            cout << "接收到FIN报文 结束传输" << endl;
            break;
        }
        else if (header.seq == exp_seq) 
        {
            // 接收到数据报 发回ACK
            Header re;
            re.set_header(0, exp_seq, ACK, 0, 0);
            memcpy(se_buf, (char*)&re, sizeof(re));
            chksum = chsum(se_buf, sizeof(re));
            ((Header*)se_buf)->chsum = chksum;
            int n = sendto(*server, se_buf, sizeof(re), 0, (sockaddr*)&c_ad, sizeof(SOCKADDR_IN));
            cout << "返回给客户端ACK报文:" << "seq: " << re.seq << "；ack: " << re.ack << "；flag: " << re.flag << "；校验和: " << chksum << "；length: " << re.length << endl;
            exp_seq += 1;
            memcpy(data_buffer + *len, re_buf + sizeof(header), header.length);
            *len += header.length;
            cout << "数据报成功获取" << endl;
            // 接收到结束报文
            if (header.flag & LAS) 
            {
                rec_done = true;
                start = clock();
                cout << "文件接收完毕 准备挥手" << endl;
            }
        }
        else 
        {
            int n = sendto(*server, se_buf, sizeof(Header), 0, (sockaddr*)&c_ad, sizeof(SOCKADDR_IN));
        }
    }
    delete[]se_buf;
    delete[]re_buf;
    mode = 0;
    ioctlsocket(*server, FIONBIO, &mode);
}

int main() {
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);
    // 创建数据报套接字
    SOCKET server = socket(AF_INET, SOCK_DGRAM, 0);
    SOCKADDR_IN server_addr;
    server_addr.sin_family = AF_INET;
    // server_addr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
    // vs2013版本以上使用新的函数转换IP地址
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr.s_addr);
    server_addr.sin_port = htons(90);
    if (bind(server, (sockaddr*)&server_addr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR) 
    {
        cout << "绑定端口失败，请重启程序" << endl;
        closesocket(server);
        WSACleanup();
        exit(-1);
    }
    else 
    {
        cout << "已成功绑定端口，等待客户端的连接。。。" << endl;
    }
    char* data = new char[20000000];
    int data_len = 0;
    if (connect(&server, &server_addr, data, &data_len) != -1) 
    {
        rec_mes(&server, &server_addr, data, &data_len);
        string file_name = "recv_";
        int i = 0;
        for (i = 0; i < data_len; i++) 
        {
            // cout << "测试i为几：" << i << endl;
            if (data[i] == 0) 
            {
                break;
            }
            file_name += data[i];
        }
        cout << "已接收完文件并将其存储到" + file_name << "中，其长度为: " << data_len - i - 1 << endl;
        ofstream file(file_name.c_str(), ofstream::binary);
        file.write(data + i + 1, data_len - i - 1);
        file.close();
        delete[]data;
    }
    else 
    {
        cout << "握手失败，请重启客户端和服务端" << endl;
    }
    // 关闭端口
    closesocket(server);
    WSACleanup();

    system("pause");
    return 0;
}