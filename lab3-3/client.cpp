#include <iostream>
#include <WINSOCK2.h>
#include <time.h>
#include <fstream>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

const int MAXSIZE = 1024;//传输缓冲区最大长度
const unsigned char SYN = 0x1; //SYN = 1 ACK = 0
const unsigned char ACK = 0x2;//SYN = 0, ACK = 1，FIN = 0
const unsigned char ACK_SYN = 0x3;//SYN = 1, ACK = 1
const unsigned char FIN = 0x4;//FIN = 1 ACK = 0
const unsigned char FIN_ACK = 0x5;
const unsigned char OVER = 0x7;//结束标志
double MAX_TIME = 0.5 * CLOCKS_PER_SEC;
int windows = 2; //初始窗口大小
int state = 0; //0为慢启动阶段，1为拥塞避免阶段
int ssthresh = 12;//阈值
int sum = 0;


/*
1.把伪首部添加到UDP上；
2.计算初始时是需要将检验和字段添零的；
3.把所有位划分为16位（2字节）的字
4.把所有16位的字相加，如果遇到进位，则将高于16字节的进位部分的值加到最低位上，举例，0xBB5E+0xFCED=0x1 B84B，则将1放到最低位，得到结果是0xB84C
5.将所有字相加得到的结果应该为一个16位的数，将该数取反则可以得到检验和checksum。 */
u_short cksum(u_short* mes, int size) {
    int count = (size + 1) / 2;
    u_short* buf = (u_short*)malloc(size + 1);
    memset(buf, 0, size + 1);
    memcpy(buf, mes, size);
    u_long sum = 0;
    while (count--) {
        sum += *buf++;
        if (sum & 0xffff0000) {
            sum &= 0xffff;
            sum++;
        }
    }
    return ~(sum & 0xffff);
}

struct HEADER
{
    u_short sum = 0;//校验和 16位
    u_short datasize = 0;//所包含数据长度 16位
    unsigned char flag = 0;
    //八位，使用后四位，排列是FIN ACK SYN 
    unsigned char SEQ = 0;
    //八位，传输的序列号，0~255，超过后mod
    HEADER() {
        sum = 0;//校验和 16位
        datasize = 0;//所包含数据长度 16位
        flag = 0;
        //八位，使用后三位，排列是FIN ACK SYN 
        SEQ = 0;
    }
};

int Connect(SOCKET& socketClient, SOCKADDR_IN& servAddr, int& servAddrlen)//三次握手建立连接
{
    HEADER header;
    char* Buffer = new char[sizeof(header)];

    u_short sum;

    //进行第一次握手
    header.flag = SYN;
    header.sum = 0;//校验和置0
    u_short temp = cksum((u_short*)&header, sizeof(header));
    header.sum = temp;//计算校验和
    memcpy(Buffer, &header, sizeof(header));//将首部放入缓冲区
    if (sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen) == -1)
    {
        return -1;
    }
    clock_t start = clock(); //记录发送第一次握手时间

    u_long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);

    //接收第二次握手
    while (recvfrom(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, &servAddrlen) <= 0)
    {
        if (clock() - start > MAX_TIME)//超时，重新传输第一次握手
        {
            header.flag = SYN;
            header.sum = 0;//校验和置0
            header.sum = cksum((u_short*)&header, sizeof(header));//计算校验和
            memcpy(Buffer, &header, sizeof(header));//将首部放入缓冲区
            sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);
            start = clock();
            cout << "第一次握手超时，正在进行重传" << endl;
        }
    }


    //进行校验和检验
    memcpy(&header, Buffer, sizeof(header));
    if (header.flag == ACK && cksum((u_short*)&header, sizeof(header) == 0))
    {
        cout << "收到第二次握手信息" << endl;
    }
    else
    {
        cout << "连接发生错误，请重启客户端！" << endl;
        return -1;
    }

    //进行第三次握手
    header.flag = ACK_SYN;
    header.sum = 0;
    header.sum = cksum((u_short*)&header, sizeof(header));//计算校验和
    if (sendto(socketClient, (char*)&header, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen) == -1)
    {
        return -1;//判断客户端是否打开，-1为未开启发送失败
    }
    cout << "服务器成功连接！可以发送数据" << endl;
    return 1;
}


void send_package(SOCKET& socketClient, SOCKADDR_IN& servAddr, int& servAddrlen, char* message, int len, int order)
{
    HEADER header;
    char* buffer = new char[MAXSIZE + sizeof(header)];
    header.datasize = len;
    header.SEQ = unsigned char(order);//序列号
    memcpy(buffer, &header, sizeof(header));
    memcpy(buffer + sizeof(header), message, sizeof(header) + len);
    u_short check = cksum((u_short*)buffer, sizeof(header) + len);//计算校验和
    header.sum = check;
    memcpy(buffer, &header, sizeof(header));
    sendto(socketClient, buffer, len + sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);//发送
    cout << "Send message " << len << " bytes!" << " flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << " SUM:" << int(header.sum) << " windows:" << windows << endl;
}

void send(SOCKET& socketClient, SOCKADDR_IN& servAddr, int& servAddrlen, char* message, int len)
{
    HEADER header;
    char* Buffer = new char[sizeof(header)];
    // 发送的单个数据报的个数
    int packagenum = len / MAXSIZE + (len % MAXSIZE != 0);
    // 第一条未确认的报文
    int head = -1;
    // 最后一条还能发送的报文
    int EndOfBuf = 0;
    
    clock_t start;
    cout << "一共需要发送" << packagenum << "条数据报。" << endl;
    // 上一个数据报的序号
    int last_seq = 0;
    clock_t s = clock();
    while(head < packagenum - 1)
    {
        if (s - clock() > MAX_TIME)
        {
            if (state == 0)
            {
                windows++;
                // 如果超过阈值 则进入拥塞避免阶段
                if (windows > ssthresh)
                {
                    state = 1;
                }
                s = clock();
            }
        }
        if (EndOfBuf - head < windows && EndOfBuf != packagenum)
        {
            // 发送数据
            send_package(socketClient, servAddr, servAddrlen, message + EndOfBuf * MAXSIZE, EndOfBuf == packagenum - 1 ? len - (packagenum - 1) * MAXSIZE : MAXSIZE, EndOfBuf % 256);
            // 发送完记录时间
            start = clock();
            EndOfBuf++;
        }
        
        // 非阻塞模式
        u_long mode = 1;
        ioctlsocket(socketClient, FIONBIO, &mode);

        // 记录收到重复数据报的个数
        int recur = 0;
        if(recvfrom(socketClient, Buffer, MAXSIZE, 0, (sockaddr*)&servAddr, &servAddrlen))
        {
            // 获取接受到的信息
            memcpy(&header, Buffer, sizeof(header));
            u_short check = cksum((u_short*)&header, sizeof(header));
            if (int(check) != 0 || header.flag != ACK)
            {
                // 接收错误 重新发送
                EndOfBuf = head + 1;
                continue;
            }
            else
            {
                cout << last_seq << ' ' << int(header.SEQ);

                if (last_seq == int(header.SEQ))
                {
                    // 接到重复确认数据报
                    recur++;
                }
                else
                {
                    // 不重复的话就更改上一次的数据报记录
                    last_seq = int(header.SEQ);
                }

                if (state == 0)
                {
                    // 慢启动的增长模式
                    windows++;
                    if (windows >= ssthresh)
                    {
                        state = 1;
                    }
                }
                else if(state == 1)
                {
                    // 拥塞避免阶段的增长模式
                    windows += MAXSIZE / windows;
                }
                if (int(header.SEQ) >= head % 256)
                {
                    // 转到目前应该发送的数据报
                    head = head + int(header.SEQ) - head % 256;
                    if (EndOfBuf - int(header.SEQ) > 10)
                    {
                        recur++;
                    }
                    cout << "发送已经被确认。 Flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << " 目前剩余可发送长度为：" << (windows - EndOfBuf + head) << endl;
                }
                else if (head % 256 > 256 - windows - 1 && int(header.SEQ) < windows)
                {
                    head = head + 256 - head % 256 + int(header.SEQ);
                    cout << "发送已经被确认。 Flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << " 目前剩余可发送长度为：" << (windows - EndOfBuf + head) << endl;
                }
            }
            if (recur > 1)
            {
                // 进入快速恢复阶段
                recur = 0;
                ssthresh = (windows - 1) / 2;
                windows = ssthresh + 3;
                state = 1;
            }
        }
        else 
        {
            if(clock() - start > MAX_TIME) 
            {
                // 超时重传 并更改阈值和窗口大小
                ssthresh = windows / 2;
                windows = 2;
                state = 0;
                EndOfBuf = head + 1;
                cout << "开始重新传输未确认的数据。。。";
            }
        }

        // 改为阻塞模式
        mode = 0;
        ioctlsocket(socketClient, FIONBIO, &mode);
    }
    cout << head << endl;

    // 发送结束信息
    header.flag = OVER;
    header.sum = 0;
    u_short temp = cksum((u_short*)&header, sizeof(header));
    header.sum = temp;

    // 将头部信息存到缓冲区
    memcpy(Buffer, &header, sizeof(header)); 
    sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen); // 发送结束信息
    cout << "本条信息发送结束" << endl;
    start = clock();
    while (true)
    {
        u_long mode = 1;
        // 改成非阻塞模式
        ioctlsocket(socketClient, FIONBIO, &mode); 
        while (recvfrom(socketClient, Buffer, MAXSIZE, 0, (sockaddr*)&servAddr, &servAddrlen) <= 0)
        {
            // 超时将会重新发送
            if (clock() - start > MAX_TIME) 
            {
                char* Buffer = new char[sizeof(header)];
                header.flag = OVER;
                header.sum = 0;
                u_short temp = cksum((u_short*)&header, sizeof(header));
                header.sum = temp;
                memcpy(Buffer, &header, sizeof(header));
                sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);
                cout << "发送超时，结束会话" << endl;
                start = clock();
            }
        }

        // 缓冲区接收到信息，读取
        memcpy(&header, Buffer, sizeof(header)); 
        u_short check = cksum((u_short*)&header, sizeof(header));
        if (header.flag == OVER)
        {
            cout << "对方已成功接收文件" << endl;
            break;
        }
        else
        {
            continue;
        }
    }

    // 改回阻塞模式
    u_long mode = 0;
    ioctlsocket(socketClient, FIONBIO, &mode);
}



int disConnect(SOCKET& socketClient, SOCKADDR_IN& servAddr, int& servAddrlen)
{
    HEADER header;
    char* Buffer = new char[sizeof(header)];

    u_short sum;

    //进行第一次握手
    header.flag = FIN;
    header.sum = 0;//校验和置0
    u_short temp = cksum((u_short*)&header, sizeof(header));
    header.sum = temp;//计算校验和
    memcpy(Buffer, &header, sizeof(header));//将首部放入缓冲区
    if (sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen) == -1)
    {
        return -1;
    }
    clock_t start = clock(); //记录发送第一次挥手时间

    u_long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);

    //接收第二次挥手
    while (recvfrom(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, &servAddrlen) <= 0)
    {
        if (clock() - start > MAX_TIME)//超时，重新传输第一次挥手
        {
            header.flag = FIN;
            header.sum = 0;//校验和置0
            header.sum = cksum((u_short*)&header, sizeof(header));//计算校验和
            memcpy(Buffer, &header, sizeof(header));//将首部放入缓冲区
            sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);
            start = clock();
            cout << "第一次挥手超时，正在进行重传" << endl;
        }
    }


    //进行校验和检验
    memcpy(&header, Buffer, sizeof(header));
    if (header.flag == ACK && cksum((u_short*)&header, sizeof(header) == 0))
    {
        cout << "收到第二次挥手信息" << endl;
    }
    else
    {
        cout << "连接发生错误，程序直接退出！" << endl;
        return -1;
    }

    //进行第三次挥手
    header.flag = FIN_ACK;
    header.sum = 0;
    header.sum = cksum((u_short*)&header, sizeof(header));//计算校验和
    if (sendto(socketClient, (char*)&header, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen) == -1)
    {
        return -1;
    }

    start = clock();
    //接收第四次挥手
    while (recvfrom(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, &servAddrlen) <= 0)
    {
        if (clock() - start > MAX_TIME)//超时，重新传输第三次挥手
        {
            header.flag = FIN;
            header.sum = 0;//校验和置0
            header.sum = cksum((u_short*)&header, sizeof(header));//计算校验和
            memcpy(Buffer, &header, sizeof(header));//将首部放入缓冲区
            sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);
            start = clock();
            cout << "第四次握手超时，正在进行重传" << endl;
        }
    }
    cout << "四次挥手结束，连接断开！" << endl;
    return 1;
}


int main()
{
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);

    SOCKADDR_IN server_addr;
    SOCKET server;

    server_addr.sin_family = AF_INET;//使用IPV4
    server_addr.sin_port = htons(2456);
    server_addr.sin_addr.s_addr = htonl(2130706433);

    server = socket(AF_INET, SOCK_DGRAM, 0);
    int len = sizeof(server_addr);
    //建立连接
    if (Connect(server, server_addr, len) == -1)
    {
        return 0;
    }

    string filename;
    cout << "请输入文件名称" << endl;
    cin >> filename;
    ifstream fin(filename.c_str(), ifstream::binary);//以二进制方式打开文件
    char* buffer = new char[10000000];
    int index = 0;
    unsigned char temp = fin.get();
    while (fin)
    {
        buffer[index++] = temp;
        temp = fin.get();
    }
    fin.close();
    send(server, server_addr, len, (char*)(filename.c_str()), filename.length());
    clock_t start = clock();
    send(server, server_addr, len, buffer, index);
    clock_t end = clock();
    cout << "传输总时间为:" << (end - start) / CLOCKS_PER_SEC << "s" << endl;
    cout << "吞吐率为:" << ((float)index) / ((end - start) / CLOCKS_PER_SEC) << "byte/s" << endl;
    disConnect(server, server_addr, len);
}

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
