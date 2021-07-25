#include<stdio.h>
#include<stdlib.h>
#include<sys/time.h>
#include<winsock2.h>
#include<windows.h>
#include<string.h>

#pragma comment(lib,"ws2_32.lib");//把ws2_32.lib这个库加入到工程文件中

#define packet_size 1024//规定了UDP每次传递的数据包的大小

#define GET_TIME(now) { \
   struct timeval t; \
   gettimeofday(&t, NULL); \
   now = t.tv_sec + t.tv_usec/1000000.0; \
}
//最后一行将us转换为s，统一单位
//用于计算程序运行时间，也即文件传输时间

int main()
{
    WSADATA data;//存放windows socket初始化信息,初始化网络环境
    int state=WSAStartup(MAKEWORD(2,2),&data);//使用2.2版本的Socket
    if(state!=0){
        printf("初始化失败！\n");
        return 0;
        //若初始化失败则打印信息并终止程序
    }
    double start,end;//用于存储文件传输的开始时间和结束时间
    fd_set rfd;//主要用于select函数（select函数的用处会在后面提及）
    //是一组文件描述字(fd)的集合
    SOCKET sock=socket(AF_INET, SOCK_DGRAM,IPPROTO_UDP);//生成套接字，sock用于发送仅包含确认信息ack的数据包
    if(sock==INVALID_SOCKET){
        printf("套接字生成失败!\n\n");
        return 0;
        //生成套接字失败则打印信息并终止程序
    }
    //struct sockaddr_in:用来处理网络通信的地址，并把port和addr分开储存在两个变量中
    //用来处理网络通信的地址
    struct sockaddr_in server_send;
    struct sockaddr_in server_receive;
    server_receive.sin_family=AF_INET;
    server_receive.sin_port=htons(10000);//设置端口为10000（服务器上设置端口10000开放）
    server_receive.sin_addr.s_addr=INADDR_ANY;//监听本机的所有IP
    //绑定INADDR_ANY，使得只需管理一个套接字，不管数据是从哪个IP过来的，只要是绑定的端口号过来的数据，都可以接收到。
    //我使用的服务器有2个IP：10.0.8.8和49.232.4.77
    if(bind(sock,(LPSOCKADDR)&server_receive,sizeof(server_receive))==SOCKET_ERROR) {
        printf("套接字绑定端口失败！\n");
        return 0;
        //将套接字与指定端口进行绑定，失败则打印信息并终止程序
    }
    long long ack=0;//服务器端返回的确认信息ack
    char packet[packet_size];//用于存储收到的每个数据包
    char id[4];//存储数据包的前四位，即序号
    FILE* fp = fopen("receive7.png","wb");//这里不太智能，需要提前输入准备接收的文件的后缀名
    //并设置为以二进制形式写入文件
    long long current_id=0;//保存当前数据包的序号
    int temp=sizeof(server_receive);
    while(true){//该循环内用于循环确认是否收到数据包和是否需要重传确认信息ack
        long long receive_code=recvfrom(sock,packet,sizeof(packet),0,(SOCKADDR *)&server_send,&temp);
        if(receive_code==4){//若等于4则代表收到了最后一个特有的数据包：只含有4字节ack信息的数据包
        	break;//收到该数据包后即可跳出接收数据包的循环
		}
        if(receive_code==SOCKET_ERROR){
            printf("recvfrom() failed!Error code:%d\n",WSAGetLastError());
            break;
            //若接收数据包失败则打印出错误代码并跳出循环
        }
        memcpy(&current_id,packet,4);//将数据包中的前4位信息复制到current_id的地址中
        //相当于使得current_id的值修改为该地址指向的整数
        if(current_id<ack){//若小于则证明存在数据包丢失或ack丢失的情况发生
            memcpy(id,&current_id,4);//将current_id的地址以4字节形式赋值到id数组中
            printf("客户端可能未收到服务器端返回的第%d号包的确认ACK，即将重传该包的确认ACK.....\n",current_id);
            //打印提示信息
            sendto(sock,id,sizeof(id),0,(SOCKADDR *)&server_send,sizeof(server_send));//重新发送确认ack
            printf("已重传！\n");
        }
        else{//证明current_id==ack，即上一个ack已被客户端接收到
            printf("服务器端已收到第%d号包！\n",current_id);
            fwrite(packet+4,sizeof(byte),receive_code-4,fp);//将数据包中除了代表序号的前4位信息以外的所有内容以字节为单位写入到fp指向的文件中
            memcpy(id,&ack,4);//将ack的地址以4字节形式赋值到id数组中
            sendto(sock,id,4,0,(SOCKADDR *)&server_send, sizeof(server_send));//发送确认ack
            ack++;//ack+1,用于和下一个数据包的序号进行比较和发送下一个数据包的确认信息
        }
    }
    fclose(fp);//关闭文件
    closesocket(sock);//关闭套接字
    WSACleanup();//清理网络环境,释放socket所占的资源
}
