#include <iostream>
#include <string>
#include <vector>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <fstream>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <time.h>
#include <WinSock2.h>
#include <Ws2tcpip.h>
#pragma comment(lib,"ws2_32.lib")

using namespace std;

#define MSS 6666
#define WINDOWS 10
#define SYN 0x1
#define ACK 0x2
#define FIN 0x4
#define LAS 0x8
#define RST 0x10
#define INITSSTHRESH 8
#define SYNRETRY 5
#define CONRETRY 5
#define OVERTIME (CLOCKS_PER_SEC / 2)

// 按一字节对齐方式定义
#pragma pack(push)
#pragma pack(1)
struct Header 
{
    u_short seq;
    u_short ack;
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

#pragma pack(push)
#pragma pack(1)
struct Packet 
{ // packet结构体
    Header header_pa;
    char data[MSS];
};
#pragma pack(pop)

class timer 
{
private:
    mutex time_lock;
    bool valid;
    u_int over_time;
    clock_t start;
public:
    timer() : over_time(OVERTIME) {};
    void start_timer(u_int over_time) 
    {
        time_lock.lock();
        valid = true;
        this->over_time = over_time;
        start = clock();
        time_lock.unlock();
    };
    void start_timer() 
    {
        time_lock.lock();
        valid = true;
        start = clock();
        time_lock.unlock();
    }
    void stop_timer() 
    {
        time_lock.lock();
        valid = false;
        time_lock.unlock();
    }
    bool out_of_time() 
    {
        return valid && (clock() - start >= over_time);
    }
    float left_time() 
    {
        return (over_time - (clock() - start)) / 1000.0 <= 0 ? 0 : (over_time - (clock() - start)) / 1000.0;
    }
};

class zyl_reno 
{
private:
    u_int cwnd;
    u_int ssthresh;
    u_int dup_ack;
    u_int num_to_add_one;
    enum 
    {
        SLOW_START,
        CONGESTION_AVOID,
        QUICK_RECOVER
    } state;
public:
    zyl_reno() 
    {
        cwnd = 1;
        ssthresh = INITSSTHRESH;
        dup_ack = 0;
        state = SLOW_START;
    };
    u_int get_cwnd() 
    {
        return cwnd;
    }
    u_int get_dup_ack() 
    {
        return dup_ack;
    }
    void rec_new() {
        dup_ack = 0;
        switch (state) 
        {
            case SLOW_START: 
            {
                cwnd += 1;
                if (cwnd >= ssthresh) 
                {
                    num_to_add_one = cwnd;
                    state = CONGESTION_AVOID;
                }
            }break;
            case CONGESTION_AVOID: 
            {
                num_to_add_one -= 1;
                if (num_to_add_one <= 0) 
                {
                    cwnd += 1;
                    num_to_add_one = cwnd;
                }
            }break;
            case QUICK_RECOVER: 
            {
                cwnd = ssthresh;
                state = CONGESTION_AVOID;
                num_to_add_one = cwnd;
            }; break;
        }
    }
    void OutOfTime() 
    {
        dup_ack = 0;
        ssthresh = cwnd / 2;
        cwnd = 1;
        state = SLOW_START;
    }
    void DupAck() {
        dup_ack += 1;
        if (dup_ack < 3) 
        {
            return;
        }
        switch (state) 
        {
            case SLOW_START:
            case CONGESTION_AVOID: 
            {
                ssthresh = cwnd / 2;
                cwnd = ssthresh + 3;
                state = QUICK_RECOVER;
            }break;
            case QUICK_RECOVER: 
            {
                cwnd += 1;
            }break;
        }
    }
};

mutex buf_lock;
mutex cout_lock;
timer zyl_timer;
zyl_reno real_zyl_reno;
bool finish_send;

// go-back-n的数据报
vector<Packet*> GoBackN_buf;
u_short base = 1, nextseqnum = 1;

// 校验和
u_short chsum(char* msg, int length) 
{
    int size = length % 2 ? length + 1 : length;
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
    return ~(sum & 0xffff);
}

// 三次握手
int connect(SOCKET* server, SOCKADDR_IN* server_addr) 
{
    int num_of_retry = SYNRETRY;
    static int over_time = OVERTIME;
    Header header;
    char* se_buf = new char[sizeof(header)];
    char* re_buf = new char[sizeof(header)];
    // 进行第一次握手
    header.set_header(base, 0, SYN, 0, 0);
    memcpy(se_buf, (char*)&header, sizeof(header));
    int chksum = chsum(se_buf, sizeof(header));
    ((Header*)se_buf)->chsum = chksum;
    int mid = sendto(*server, se_buf, sizeof(header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
    if (mid == SOCKET_ERROR) 
    {
        delete[]se_buf;
        delete[]re_buf;
        return -1;
    }
    cout << "进行第一次握手" << endl;
    clock_t start = clock();
    // 非阻塞模式
    u_long mode = 1;
    ioctlsocket(*server, FIONBIO, &mode);
    SOCKADDR_IN s_ad;
    int s_ad_len = sizeof(SOCKADDR_IN);
    // 等待服务器第二次握手
    while (true) 
    {
        while (recvfrom(*server, re_buf, sizeof(header), 0, (sockaddr*)&s_ad, &s_ad_len) <= 0) 
        {
            if (clock() - start > 1.2 * over_time) 
            {
                if (num_of_retry <= 0) 
                {
                    cout << "超过重传次数 退出";
                    // 设为阻塞模式
                    mode = 0;
                    ioctlsocket(*server, FIONBIO, &mode);
                    delete[]se_buf;
                    delete[]re_buf;
                    return -1;
                }
                sendto(*server, se_buf, sizeof(header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
                start = clock();
                cout << "重新进行第一次握手" << endl;
                over_time += CLOCKS_PER_SEC;
                num_of_retry -= 1;
            }
        }
        memcpy(&header, re_buf, sizeof(header));
        chksum = chsum(re_buf, sizeof(header));
        if ((header.flag & ACK) && (header.flag & SYN) && chksum == 0) 
        {
            cout << "第二次握手成功" << endl;

            // 进行第三次握手
            header.set_header(0, 0, ACK, 0, 0);
            memcpy(se_buf, (char*)&header, sizeof(header));
            chksum = chsum(se_buf, sizeof(header));
            ((Header*)se_buf)->chsum = chksum;
            sendto(*server, se_buf, sizeof(header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
            cout << "进行第三次握手" << endl;

            // 结束
            mode = 0;
            ioctlsocket(*server, FIONBIO, &mode);
            delete[]se_buf;
            delete[]re_buf;
            return 1;
        }
    }
    mode = 0;
    ioctlsocket(*server, FIONBIO, &mode);
    delete[]se_buf;
    delete[]re_buf;
    return 1;
}

// 接收线程
void rec_thread(SOCKET* server, SOCKADDR_IN* server_addr) 
{
    // 开启非阻塞模式
    u_long mode = 1;
    ioctlsocket(*server, FIONBIO, &mode);
    char* re_buf = new char[sizeof(Header)];
    Header* header;
    SOCKADDR_IN s_ad;
    int s_ad_len = sizeof(SOCKADDR_IN);
    while (true) 
    {
        if (finish_send) 
        {
            // 改为阻塞模式
            mode = 0;
            ioctlsocket(*server, FIONBIO, &mode);
            delete[]re_buf;
            return;
        }
        while (recvfrom(*server, re_buf, sizeof(Header), 0, (sockaddr*)&s_ad, &s_ad_len) <= -1) 
        {
            if (finish_send) {
                // 改为阻塞模式
                mode = 0;
                ioctlsocket(*server, FIONBIO, &mode);
                delete[]re_buf;
                return;
            }
            if (zyl_timer.out_of_time()) 
            {
                // 超时处理
                real_zyl_reno.OutOfTime();
                for (auto packet : GoBackN_buf) 
                {
                    sendto(*server, (char*)packet, sizeof(Header) + packet->header_pa.length, 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
                    cout_lock.lock();
                    cout << "数据报重传 其首部为: seq:" << packet->header_pa.seq << "；ack:" << packet->header_pa.ack << "；flag:" << packet->header_pa.flag << "；校验和:" << packet->header_pa.chsum << "；len:" << packet->header_pa.length << endl;
                    cout_lock.unlock();
                }
                // 重新计时
                zyl_timer.start_timer();
            }
        }
        header = (Header*)re_buf;
        int chksum = chsum(re_buf, sizeof(Header));
        if (chksum != 0) 
        {
            continue;
        }
        if (header->flag == RST) 
        {
            cout << "连接异常 退出程序" << endl;
            exit(-1);
        }
        else if (header->flag == ACK) 
        {
            if (header->ack >= base) 
            {
                // 处理新数据报
                real_zyl_reno.rec_new();
            }
            else {
                // 处理重复
                real_zyl_reno.DupAck();
                if (real_zyl_reno.get_dup_ack() == 3) 
                {
                    Packet* packet = GoBackN_buf[0];
                    sendto(*server, (char*)packet, sizeof(Header) + packet->header_pa.length, 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
                    // 保证输出时的正确性
                    cout_lock.lock();
                    cout << "已有三个重复ACK 进行快速重传 其首部为: seq:" << packet->header_pa.seq << "；ack:" << packet->header_pa.ack << "；flag:" << packet->header_pa.flag << "；校验和:" << packet->header_pa.chsum << "；len:" << packet->header_pa.length << endl;
                    cout_lock.unlock();
                }
            }
            // 定义接收到的数据
            int re_ack_num = header->ack + 1 - base;
            for (int i = 0; i < re_ack_num; i++) 
            {
                // 防止go-back-n的缓冲区出错
                buf_lock.lock();
                if (GoBackN_buf.size() <= 0) 
                {
                    break;
                }
                delete GoBackN_buf[0];
                // 更改缓冲区
                GoBackN_buf.erase(GoBackN_buf.begin());
                buf_lock.unlock();
            }
            base = header->ack + 1;
            cout_lock.lock();
            cout << "成功接收一个数据包 其首部为: seq:" << header->seq << "；ack:" << header->ack << "；flag:" << header->flag << "；校验和:" << header->chsum << "；len:" << header->length << "；拥塞窗口大小为:" << real_zyl_reno.get_cwnd() << endl;
            cout_lock.unlock();
        }
        if (base != nextseqnum) 
        {
            zyl_timer.start_timer();
        }
        else 
        {
            zyl_timer.stop_timer();
        }
    }
}

// 发送数据报
void send_one(SOCKET* server, SOCKADDR_IN* server_addr, char* msg, int len, bool last = false) 
{
    assert(len <= MSS);
    // 检查是否可以发送
    while ((u_short)(nextseqnum - base) >= WINDOWS || (u_short)(nextseqnum - base) >= real_zyl_reno.get_cwnd()) 
    {
        continue;
    }
    u_int zyl_win = real_zyl_reno.get_cwnd();
    Packet* zyl_pa = new Packet;
    zyl_pa->header_pa.set_header(nextseqnum, 0, last ? LAS : 0, 0, len);
    memcpy(zyl_pa->data, msg, len);
    u_short chSum = chsum((char*)zyl_pa, sizeof(Header) + len);
    zyl_pa->header_pa.chsum = chSum;

    buf_lock.lock();
    GoBackN_buf.push_back(zyl_pa);
    buf_lock.unlock();
    sendto(*server, (char*)zyl_pa, len + sizeof(Header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));

    cout_lock.lock();
    if (zyl_win <= WINDOWS)
    {
        cout << "发送一个数据包 其首部为: seq:" << zyl_pa->header_pa.seq << ";ack:" << zyl_pa->header_pa.ack << ";flag:" << zyl_pa->header_pa.flag << ";校验和:" << zyl_pa->header_pa.chsum << "；len:" << zyl_pa->header_pa.length << ";剩余发送窗口大小为:" << zyl_win - (nextseqnum - base) - 1 << endl;
    }
    else
    {
        cout << "发送一个数据包 其首部为: seq:" << zyl_pa->header_pa.seq << ";ack:" << zyl_pa->header_pa.ack << ";flag:" << zyl_pa->header_pa.flag << ";校验和:" << zyl_pa->header_pa.chsum << "；len:" << zyl_pa->header_pa.length << ";剩余发送窗口大小为:" << WINDOWS - (nextseqnum - base) - 1 << endl;
    }
    cout_lock.unlock();

    if (base == nextseqnum) 
    {
        zyl_timer.start_timer();
    }
    nextseqnum += 1;
}

// 发送文件
void send_file(string file_name, SOCKET* server, SOCKADDR_IN* server_addr) 
{
    ifstream file(file_name.c_str(), ifstream::binary);

    // 预定义总长度
    int all_file_len = 0;
    file.seekg(0, file.end);
    all_file_len = file.tellg();
    file.seekg(0, file.beg);

    // 将其放到缓冲区
    int buf_len = file_name.length() + all_file_len + 1;
    char* zyl_buf = new char[buf_len];
    memset(zyl_buf, 0, sizeof(char) * buf_len);
    memcpy(zyl_buf, file_name.c_str(), file_name.length());

    // 分割文件名和文件内容
    zyl_buf[file_name.length()] = 0; 
    file.read(zyl_buf + file_name.length() + 1, all_file_len);
    file.close();
    cout << "正在发送" + file_name << "文件 其大小为: " << all_file_len << "字节" << endl;
    clock_t start = clock();

    // 先开始接收
    thread receive_t(rec_thread, server, server_addr);

    // 开始发送
    for (int offset = 0; offset < buf_len; offset += MSS) 
    {
        send_one(server, server_addr, zyl_buf + offset, buf_len - offset >= MSS ? MSS : buf_len - offset, buf_len - offset <= MSS ? true : false);
    }
    // 缓冲区还有数据就不退出
    while (GoBackN_buf.size() != 0) 
    {
        continue;
    }
    finish_send = true;

    // 回收线程资源
    receive_t.join();
    clock_t end = clock();
    cout << "已成功发完" + file_name + "文件！" << endl;
    // 测试为毛为零
    cout << "start:" << start << ";end:" << end << endl;
    cout << "用时: " << double(end - start) / double(CLOCKS_PER_SEC) << "s" << endl;
    cout << "吞吐率: " << double(buf_len) / (double(end - start) / double(CLOCKS_PER_SEC)) << "byte/s" << endl;
    delete[]zyl_buf;
}

// 挥手
void disconnect(SOCKET* server, SOCKADDR_IN* server_addr) 
{
    int time_of_retry = CONRETRY;
    static int over_time = OVERTIME;
    int seq = nextseqnum;
    Header header;
    char* se_buf = new char[sizeof(header)];
    char* re_buf = new char[sizeof(header)];
    header.set_header(seq, 0, FIN, 0, 0);
    memcpy(se_buf, (char*)&header, sizeof(header));
    int chSum = chsum(se_buf, sizeof(header));
    ((Header*)se_buf)->chsum = chSum;
    // 进行挥手
    sendto(*server, se_buf, sizeof(header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
    //cout << "开始挥手 seq为" << seq << endl;
    cout << "进行挥手" << endl;

    SOCKADDR_IN s_ad;
    int s_ad_len = sizeof(SOCKADDR_IN);
    u_long mode = 1;
    ioctlsocket(*server, FIONBIO, &mode);

    clock_t start = clock();
    while (true) 
    {
        while (recvfrom(*server, re_buf, sizeof(header), 0, (sockaddr*)&s_ad, &s_ad_len) <= 0) 
        {
            // 超时
            if (clock() - start > 1.2 * over_time) 
            {
                // 重传超过次数 直接返回
                if (time_of_retry <= 0) 
                {
                    mode = 0;
                    ioctlsocket(*server, FIONBIO, &mode);
                    delete[]se_buf;
                    delete[]re_buf;
                    cout << "超过重传次数 退出程序" << endl;
                    return;
                }
                sendto(*server, se_buf, sizeof(header), 0, (sockaddr*)server_addr, sizeof(SOCKADDR_IN));
                time_of_retry -= 1;
                over_time += CLOCKS_PER_SEC;
                start = clock();
                cout << "超时 重新进行挥手" << endl;
            }
        }
        memcpy(&header, re_buf, sizeof(header));
        chSum = chsum(re_buf, sizeof(header));
        if (chSum == 0 && header.flag == ACK && header.ack == (u_short)(seq + 1)) 
        {
            cout << "挥手成功" << endl;
            break;
        }
        else 
        {
            continue;
        }
    }
    mode = 0;
    ioctlsocket(*server, FIONBIO, &mode);
    delete[]se_buf;
    delete[]re_buf;
    return;
}

int main() 
{
    srand(time(NULL));
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);
    // 套接字
    SOCKET server = socket(AF_INET, SOCK_DGRAM, 0);
    SOCKADDR_IN server_addr;
    server_addr.sin_family = AF_INET;
    // server_addr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
    // vs2013版本以上使用新的函数转换IP地址
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr.s_addr);
    server_addr.sin_port = htons(92);
    
    // 握手
    if (connect(&server, &server_addr) == -1) 
    {
        cout << "握手失败" << endl;
        closesocket(server);
        WSACleanup();
        exit(-1);
    }
    else 
    {
        cout << "三次握手成功" << endl;
    }

    // 获得文件名
    cout << "请输入你想要发送的文件名称: ";
    string file_name;
    cin >> file_name;

    // 发送文件
    send_file(file_name, &server, &server_addr);

    disconnect(&server, &server_addr);
    closesocket(server);
    WSACleanup();

    system("pause");
    return 0;
}