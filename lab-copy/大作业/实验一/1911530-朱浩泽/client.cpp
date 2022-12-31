#include <iostream>
#include <WINSOCK2.h>
#include <time.h>
#include <fstream>
#pragma comment(lib, "ws2_32.lib")
using namespace std;



const int MAXSIZE = 1024; // 传输缓冲区最大长度
const unsigned char SYN = 0x1;  // SYN = 1 ACK = 0
const unsigned char ACK = 0x2; // SYN = 0, ACK = 1，FIN = 0
const unsigned char ACK_SYN = 0x3; // SYN = 1, ACK = 1
const unsigned char FIN = 0x4; // FIN = 1 ACK = 0
const unsigned char FIN_ACK = 0x5;
const unsigned char OVER = 0x7; // 结束标志
double MAX_TIME = 0.5 * CLOCKS_PER_SEC;



u_short cksum(u_short* mes, int size) {
    int count = (size + 1) / 2; // 因为前两个数据16位，+1后等价于4个16位的数据，而sizeof是按字节（8位）计算的
    u_short* buf = (u_short*)malloc(size + 1);
    memset(buf, 0, size + 1);
    memcpy(buf, mes, size); // 将传入的地址复制给buf
    u_long sum = 0;
    while (count--) {
        sum += *buf++;
        if (sum & 0xffff0000) { // 判断是否有溢出
            sum &= 0xffff;
            sum++; // 末位加一
        }
    }
    return ~(sum & 0xffff); // 16位取反，所以最后传入过去后取反本来应该是全1，这里就是全0即0x0
}

struct HEADER
{
    u_short sum = 0; // 校验和 16位
    u_short datasize = 0; // 所包含数据长度 16位
    unsigned char flag = 0; // 八位，使用低三位，排列是FIN ACK SYN 
    unsigned char SEQ = 0; // 八位，传输的序列号，0~255，超过后取模
    HEADER() {
        sum = 0;
        datasize = 0;
        flag = 0;
        SEQ = 0;
    }
};

int Connect(SOCKET& socketClient, SOCKADDR_IN &servAddr, int& servAddrlen) // 三次握手建立连接
{
    HEADER header;
    char* Buffer = new char[sizeof(header)];

    u_short sum;

    // 进行第一次握手
    header.flag = SYN;
    header.sum = 0; // 校验和置0
    u_short temp = cksum((u_short*)&header, sizeof(header));
    header.sum = temp; // 计算校验和
    memcpy(Buffer, &header, sizeof(header)); // 将首部放入缓冲区
    if (sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen) == -1) // 发送UDP头部信息
    {
        return -1;
    }
    clock_t start = clock(); // 记录发送第一次握手时间

    u_long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode); // 允许套接字的非阻塞模式

    // 进行第二次握手
    while (recvfrom(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, &servAddrlen) <= 0)
    {
        if (clock() - start > MAX_TIME) //超时，重新传输第一次握手
        {
            header.flag = SYN;
            header.sum = 0; // 校验和置0
            header.sum = cksum((u_short*)&header, sizeof(header)); // 计算校验和
            memcpy(Buffer, &header, sizeof(header)); // 将首部放入缓冲区
            sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);
            start = clock();
            cout << "由于第一次握手超时，正在重新传输" << endl;
        }
    }
    memcpy(&header, Buffer, sizeof(header)); // 将收到的值赋值给header
    if (header.flag == ACK && cksum((u_short*)&header, sizeof(header) == 0)) // 全1取反即为0
    {
        cout << "已成功进行第二次握手" << endl;
    }
    else
    {
        cout << "第二次握手失败，需要进行重启" << endl;
        return - 1;
    }

    // 进行第三次握手
    header.flag = ACK_SYN;
    header.sum = 0;
    header.sum = cksum((u_short*)&header, sizeof(header)); // 计算校验和
    if (sendto(socketClient, (char*) & header, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen) == -1)
    {
        return -1; // 判断客户端是否打开，-1为未开启发送失败
    }
    cout << "服务器成功连接！可以发送数据" << endl;
    return 1;
}

void send_package(SOCKET& socketClient, SOCKADDR_IN& servAddr, int& servAddrlen, char* message, int len, int &order) // 发送单个数据报
{
    HEADER header;
    char* buffer = new char[MAXSIZE + sizeof(header)]; // 数据长度加上头部长度
    header.datasize = len; // 数据段的长度
    header.SEQ = unsigned char(order); // 序列号
    memcpy(buffer, &header, sizeof(header));
    memcpy(buffer + sizeof(header), message, sizeof(header) + len); // 将数据拷贝到缓冲区
    u_short check = cksum((u_short *) buffer, sizeof(header) + len); // 计算校验和
    header.sum = check;
    memcpy(buffer, &header, sizeof(header)); // 更新新的校验和到缓冲区
    sendto(socketClient, buffer, len + sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen); // 发送数据
    cout << "Send message " << len << " bytes!" << " Flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << " SUM:" << int(header.sum) << endl;
    clock_t start = clock(); // 记录发送时间
    // 接收ack等信息
    while(true)
    {
        u_long mode = 1;
        ioctlsocket(socketClient, FIONBIO, &mode); // 设置非阻塞模式，防止接收信息阻塞
        while (recvfrom(socketClient, buffer, MAXSIZE, 0, (sockaddr*)&servAddr, &servAddrlen) <= 0)
        {
            if (clock() - start > MAX_TIME) // 重新发送数据
            {
                header.datasize = len;
                header.SEQ = u_char(order); // 序列号
                header.flag = u_char(0x0);
                memcpy(buffer, &header, sizeof(header));
                memcpy(buffer + sizeof(header), message, sizeof(header) + len);
                u_short check = cksum((u_short*)buffer, sizeof(header) + len); // 计算校验和
                header.sum = check;
                memcpy(buffer, &header, sizeof(header));
                sendto(socketClient, buffer, len + sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);//发送
                cout << "发送数据超时，重新发送数据 " << len << " bytes! Flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << endl;
                clock_t start = clock(); // 重新记录发送时间
            }
        }
        memcpy(&header, buffer, sizeof(header)); // 缓冲区接收到信息，读取
        u_short check = cksum((u_short*)&header, sizeof(header));
        if (header.SEQ == u_short(order) && header.flag == ACK)
        {
            cout << "发送的数据报已经得到确认！Flag:" << int(header.flag)<< " SEQ:" << int(header.SEQ) << endl;
            break;
        }
        else
        {
            continue;
        }
    }
    u_long mode = 0;
    ioctlsocket(socketClient, FIONBIO, &mode); // 改回阻塞模式
}

void send(SOCKET& socketClient, SOCKADDR_IN& servAddr, int& servAddrlen, char* message, int len)
{
    int packagenum = len / MAXSIZE + (len % MAXSIZE != 0); // 计算一共要发送几次数据报
    int seqnum = 0; // 记录已经到第几个数据报
    for (int i = 0; i < packagenum; i++)
    {
        // 倒数第二个参数是发送的长度
        send_package(socketClient, servAddr, servAddrlen, message + i * MAXSIZE, i == packagenum - 1? len - (packagenum - 1) * MAXSIZE : MAXSIZE, seqnum);
        seqnum++;
        if (seqnum > 255) // 循环计数
        {
            seqnum = seqnum - 256;
        }
    }

    // 发送结束信息
    HEADER header;
    char* Buffer = new char[sizeof(header)];
    header.flag = OVER;
    header.sum = 0;
    u_short temp = cksum((u_short*)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header)); // 将头部信息存到缓冲区
    sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen); // 发送结束信息
    cout << "结束会话" << endl;
    clock_t start = clock();
    while (true)
    {
        u_long mode = 1;
        ioctlsocket(socketClient, FIONBIO, &mode); // 改成非阻塞模式
        while (recvfrom(socketClient, Buffer, MAXSIZE, 0, (sockaddr*)&servAddr, &servAddrlen) <= 0)
        {
            if (clock() - start > MAX_TIME) // 超时将会重新发送
            {
                char* Buffer = new char[sizeof(header)];
                header.flag = OVER;
                header.sum = 0;
                u_short temp = cksum((u_short*)&header, sizeof(header));
                header.sum = temp;
                memcpy(Buffer, &header, sizeof(header));
                sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);
                cout << "发送超时，重新结束会话" << endl;
                start = clock();
            }
        }
        memcpy(&header, Buffer, sizeof(header)); // 缓冲区接收到信息，读取
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

    // 进行第一次挥手
    header.flag = FIN;
    header.sum = 0; // 校验和置0
    u_short temp = cksum((u_short*)&header, sizeof(header));
    header.sum = temp; // 更新校验和到首部
    memcpy(Buffer, &header, sizeof(header)); // 将首部放入缓冲区
    if (sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen) == -1) // 首部发送失败
    {
        return -1;
    }
    clock_t start = clock(); // 计时第一次挥手
    u_long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode); // 改为非阻塞模式

    // 进行第二次挥手
    while (recvfrom(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, &servAddrlen) <= 0)
    {
        if (clock() - start > MAX_TIME) // 如果超时重新第一次挥手
        {
            header.flag = FIN;
            header.sum = 0; // 校验和置0
            header.sum = cksum((u_short*)&header, sizeof(header)); // 更新校验和到首部
            memcpy(Buffer, &header, sizeof(header)); //将首部放入缓冲区
            sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);
            start = clock(); // 重新记录发送第一次挥手时间
            cout << "第一次挥手超时，正在重新挥手" << endl;
        }
    }
    memcpy(&header, Buffer, sizeof(header)); // 赋值收到的头部信息
    if (header.flag == ACK && cksum((u_short*)&header, sizeof(header) == 0))
    {
        cout << "已接收第二次挥手" << endl;
    }
    else
    {
        cout << "接收错误，退出程序" << endl;
        return -1;
    }

    // 进行第三次挥手
    header.flag = FIN_ACK;
    header.sum = 0;
    header.sum = cksum((u_short*)&header, sizeof(header)); // 计算校验和
    if (sendto(socketClient, (char*)&header, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen) == -1) // 首部发送失败
    {
        return -1;
    }
    start = clock(); // 计时第三次挥手

    // 进行第四次挥手
    while (recvfrom(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, &servAddrlen) <= 0)
    {
        if (clock() - start > MAX_TIME) // 如果超时重新第三次挥手
        {
            header.flag = FIN_ACK; // 如果不行可改成FIN
            header.sum = 0; // 校验和置0
            header.sum = cksum((u_short*)&header, sizeof(header)); // 计算校验和
            memcpy(Buffer, &header, sizeof(header)); // 将首部放入缓冲区
            sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr*)&servAddr, servAddrlen);
            start = clock();
            cout << "第三次挥手超时，正在重新挥手" << endl;
        }
    }
    cout << "四次挥手全部结束，退出程序" << endl;
    return 1;
}

int main()
{
    WSADATA wsadata; // 初始化网络库
    WSAStartup(MAKEWORD(2, 2), &wsadata); // 希望调用的最高版本2.2

    SOCKADDR_IN server_addr;
    SOCKET server;

    server_addr.sin_family = AF_INET; // 使用IPV4
    server_addr.sin_port = htons(2456); // 主机序转网络序，16位，绑定端口
    server_addr.sin_addr.s_addr = htonl(2130706433); // 主机序转网络序，32位，绑定ip地址

    server = socket(AF_INET, SOCK_DGRAM, 0);
    int len = sizeof(server_addr);

    // 建立连接
    if (Connect(server, server_addr, len) == -1)
    {
        return 0;
    }

    // 输入文件名称
    string filename;
    cout << "请输入你想存放的文件：" << endl;
    cin >> filename;
    ifstream fin(filename.c_str(), ifstream::binary); // 用二进制方式打开文件

    // 进行文件输入
    char* buffer = new char[10000000];
    int index = 0;
    unsigned char temp = fin.get();
    // 设置前缀，防止冲突
    buffer[index++] = 't';
    buffer[index++] = 'r';
    buffer[index++] = 'a';
    buffer[index++] = 'n';
    buffer[index++] = '_';
    while (fin)
    {
        buffer[index++] = temp;
        temp = fin.get();
    }
    fin.close(); // 关闭输入流

    // 发送文件
    send(server, server_addr, len, (char*)(filename.c_str()), filename.length()); // 发送文件名称
    clock_t start = clock();
    send(server, server_addr, len, buffer, index); // 发送文件内容
    clock_t end = clock();
    cout << "传输总时间为:" << (end - start) / CLOCKS_PER_SEC << "s" << endl;
    cout << "吞吐率为:" << ((float)index ) / ((end - start) /CLOCKS_PER_SEC) << "byte/s" << endl;
    disConnect(server, server_addr, len); // 断开连接
}