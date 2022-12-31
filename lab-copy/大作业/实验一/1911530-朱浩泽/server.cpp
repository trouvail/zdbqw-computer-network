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

int Connect(SOCKET& sockServ, SOCKADDR_IN& ClientAddr, int& ClientAddrLen) // 三次握手建立连接
{
    HEADER header;
    char* Buffer = new char[sizeof(header)];

    // 进行第一次握手
    while (true)
    {
        if (recvfrom(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, &ClientAddrLen) == -1) 
        {
            return -1;
        }
        memcpy(&header, Buffer, sizeof(header)); // 将首部放入缓冲区
        if (header.flag == SYN && cksum((u_short*)&header, sizeof(header)) == 0) // 接收数据首部信息无误
        {
            cout << "已成功进行第一次握手" << endl;
            break;
        }
    }

    // 进行第二次握手
    header.flag = ACK;
    header.sum = 0;
    u_short temp = cksum((u_short*)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header)); // 将首部放入缓冲区
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1) // 发送UDP头部信息
    {
        return -1;
    }
    clock_t start = clock(); // 记录发送第二次握手时间

    // 进行第三次握手
    while (recvfrom(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, &ClientAddrLen) <= 0)
    {
        if (clock() - start > MAX_TIME) //超时，重新传输第二次握手
        {
            header.flag = ACK;
            header.sum = 0;
            u_short temp = cksum((u_short*)&header, sizeof(header)); // 计算校验和
            header.flag = temp;
            memcpy(Buffer, &header, sizeof(header)); // 将首部放入缓冲区
            if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1)
            {
                return -1;
            }
            cout << "由于第二次握手超时，正在重新传输" << endl;
        }
    }
    HEADER temp1;
    memcpy(&temp1, Buffer, sizeof(header)); // 将收到的值赋值给temp1
    if (temp1.flag == ACK_SYN && cksum((u_short*)&temp1, sizeof(temp1) == 0))
    {
        cout << "服务器成功连接！可以接收数据" << endl;
    }
    else
    {
        cout << "连接失败，需要重启客户端" << endl;
        return -1;
    }
    return 1;
}

int RecvMessage(SOCKET& sockServ, SOCKADDR_IN& ClientAddr, int& ClientAddrLen, char *message)
{
    long int all_length = 0; // 接收到的数据总长度
    HEADER header;
    char* Buffer = new char[MAXSIZE + sizeof(header)];
    int seq = 0; // 接收到的数据次序

    while (true)
    {
        int length = recvfrom(sockServ, Buffer, sizeof(header) + MAXSIZE, 0, (sockaddr*)&ClientAddr, &ClientAddrLen); // 接收到的报文长度
        memcpy(&header, Buffer, sizeof(header)); // 将接收到的数据赋值给首部
        // 判断是否接收到结束标志
        if (header.flag == OVER && cksum((u_short*)&header, sizeof(header)) == 0)
        {
            cout << "已接收完全部文件" << endl;
            break;
        }
        if (header.flag == unsigned char(0) && cksum((u_short*)Buffer, length - sizeof(header)) == 0)
        {
            // 判断是否接收的是另外的包
            if (seq != int(header.SEQ))
            {
                // 说明出现问题，返回ACK
                header.flag = ACK;
                header.datasize = 0;
                header.SEQ = (unsigned char)seq;
                header.sum = 0;
                u_short temp = cksum((u_short*)&header, sizeof(header));
                header.sum = temp;
                memcpy(Buffer, &header, sizeof(header)); // 将首部信息赋值给Buffer
                // 重新发送此数据报的ACK
                sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen);
                cout << "发送到客户端 ACK:" << (int)header.SEQ << " SEQ:" << (int)header.SEQ << endl;
                continue; // 丢弃该数据包
            }
            seq = int(header.SEQ);
            if (seq > 255) // 循环次序计数
            {
                seq = seq - 256;
            }
            // 取出buffer中的内容
            cout << "收到数据报 " << length - sizeof(header) << " bytes!Flag:" << int(header.flag) << " SEQ : " << int(header.SEQ) << " SUM:" << int(header.sum) << endl;
            char* temp = new char[length - sizeof(header)];
            memcpy(temp, Buffer + sizeof(header), length - sizeof(header)); // 将所有的信息存入Buffer
            memcpy(message + all_length, temp, length - sizeof(header));
            all_length = all_length + int(header.datasize);

            // 返回肯定信息
            header.flag = ACK;
            header.datasize = 0;
            header.SEQ = (unsigned char)seq;
            header.sum = 0;
            u_short temp1 = cksum((u_short*)&header, sizeof(header));
            header.sum = temp1;
            memcpy(Buffer, &header, sizeof(header)); // 重新将首部的值赋给Buffer
            // 重新发送此数据报的ACK
            sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen);
            cout << "Send to Clinet ACK:" << (int)header.SEQ << " SEQ:" << (int)header.SEQ << endl;
            seq++;
            if (seq > 255)
            {
                seq = seq - 256;
            }
        }
    }
    // 发送结束信息
    header.flag = OVER;
    header.sum = 0;
    u_short temp = cksum((u_short*)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1)
    {
        return -1;
    }
    return all_length; // 返回所有数据的长度
}

int disConnect(SOCKET& sockServ, SOCKADDR_IN& ClientAddr, int& ClientAddrLen)
{
    HEADER header;
    char* Buffer = new char [sizeof(header)];
    
    // 进行第一次挥手
    while (true) 
    {
        int length = recvfrom(sockServ, Buffer, sizeof(header) + MAXSIZE, 0, (sockaddr*)&ClientAddr, &ClientAddrLen); // 接收数据报长度
        memcpy(&header, Buffer, sizeof(header));
        if (header.flag == FIN && cksum((u_short*)&header, sizeof(header)) == 0)
        {
            cout << "已接收第一次挥手" << endl;
            break;
        }
    }

    // 进行第二次挥手
    header.flag = ACK;
    header.sum = 0; // 校验和置0
    u_short temp = cksum((u_short*)&header, sizeof(header));
    header.sum = temp; // 更新校验和到首部
    memcpy(Buffer, &header, sizeof(header)); // 将首部放入缓冲区
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1) // 首部发送失败
    {
        return -1;
    }
    clock_t start = clock();// 计时第二次挥手

    // 进行第三次挥手
    while (recvfrom(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, &ClientAddrLen) <= 0)
    {
        if (clock() - start > MAX_TIME) // 如果超时重新第二次挥手
        {
            header.flag = ACK;
            header.sum = 0; // 校验和置0
            u_short temp = cksum((u_short*)&header, sizeof(header));
            header.sum = temp; // 更新校验和到首部
            memcpy(Buffer, &header, sizeof(header)); //将首部放入缓冲区
            if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1)
            {
                return -1;
            }
            start = clock(); // 重新记录发送第二次挥手时间
            cout << "第二次挥手超时，正在重新挥手" << endl;
        }
    }
    HEADER temp1;
    memcpy(&temp1, Buffer, sizeof(header));
    if (temp1.flag == FIN_ACK && cksum((u_short*)&temp1, sizeof(temp1) == 0))
    {
        cout << "已接收第三次挥手" << endl;
    }
    else
    {
        cout << "接收错误，退出程序" << endl;
        return -1;
    }

    // 进行第四次挥手
    header.flag = FIN_ACK;
    header.sum = 0;
    temp = cksum((u_short*)&header, sizeof(header));
    header.sum = temp; // 更新校验和
    memcpy(Buffer, &header, sizeof(header)); // 将首部放入缓冲区
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr*)&ClientAddr, ClientAddrLen) == -1)
    {
        cout << "发送错误，退出程序" << endl;
        return -1;
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
    bind(server, (SOCKADDR*)&server_addr, sizeof(server_addr)); // 绑定套接字，进入监听状态
    cout << "开始监听，等待客户端上线" << endl;
    int len = sizeof(server_addr);
    // 建立连接
    Connect(server, server_addr, len);
    char* name = new char[20];
    char* data = new char[100000000];
    int namelen =  RecvMessage(server, server_addr, len, name);
    int datalen = RecvMessage(server, server_addr, len, data);
    string a;
    for (int i = 0; i < namelen; i++) // 将接收到的文件的名字信息赋值给a
    {
        a = a + name[i];
    }
    disConnect(server, server_addr, len); // 先断开连接
    ofstream fout(a.c_str(), ofstream::binary); // 用二进制方式打开文件
    for (int i = 0; i < datalen; i++) 
    {
        fout << data[i];
    }
    fout.close();
    cout << "文件已转载到本地" << endl;
}