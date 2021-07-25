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

//该函数用于计算文件大小
long long file_Size(char* filename){
    FILE* fp=fopen(filename,"rb");//rb：以二进制形式读取文件
    //若采取r的话会导致txt以外形式的文件读取出现错误和误差
    if(!fp){//若打开文件失败
        return -1;//设置文件大小为-1
    }
    fseek(fp,0,SEEK_END);//将文件指针定位到文件末尾
    long long size=ftell(fp);//ftell用于得到文件位置指针当前位置相对于文件首的偏移字节数
    //这里相当于得到文件首和文件末尾之间的字节数，即文件大小
    fclose(fp);//关闭文件
    return size;//返回文件大小
}

int main()
{
    WSADATA data;//存放windows socket初始化信息,初始化网络环境
    int state=WSAStartup(MAKEWORD(2,2),&data);//使用2.2版本的Socket
    if(state!=0){
        printf("初始化失败!\n\n");
        return 0;
        //若初始化失败则打印信息并终止程序
    }

    SOCKET sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);//生成套接字，sock用于发送文件切割后的每个数据包
    //AF_INET:用于socket创建通信连接的类型，这里就是ipv4地址类型的通信连接可用
    //SOCK_DGRAM:传输形式为数据包形式
    //设置协议为UDP协议
    if(sock==INVALID_SOCKET){
        printf("套接字生成失败!\n\n");
        return 0;
        //生成套接字失败则打印信息并终止程序
    }
    SOCKET sock_end=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);//sock_end用于发送含有终止信息的数据包
    if(sock_end==INVALID_SOCKET){
        printf("套接字生成失败!\n\n");
        return 0;
        //生成套接字失败则打印信息并终止程序
    }
    //struct sockaddr_in:用来处理网络通信的地址，并把port和addr分开储存在两个变量中
    //用来处理网络通信的地址
    struct sockaddr_in client_send;
    struct sockaddr_in client_receive;
    client_send.sin_family=AF_INET;
    client_send.sin_port=htons(10000);//设置端口为10000（服务器上设置端口10000开放）
    client_send.sin_addr.s_addr=inet_addr("49.232.4.77");//服务器IP地址
    //client_send.sin_addr.s_addr=inet_addr("172.19.13.34");//服务器IP地址
    

    struct timeval time_limit;//用于判定超时
    time_limit.tv_sec=1;
    time_limit.tv_usec=200;
    //这里可修改超时的判定时间标准

    fd_set rfd;//主要用于select函数（select函数的用处会在后面提及）
    //是一组文件描述字(fd)的集合

    char file_name[100];//存储文件名
    printf("请输入需要传输的文件名称：\n");
    scanf("%s",file_name);
    long long file_size=file_Size(file_name);//得到文件大小
    long long send_packet_size=0;
    char packet[packet_size];//用于存储每个被切割后的数据包内容
    char id[4];//该数组内容会被复制到packet的前4位，作为每个数据包的序号。
    //它是保证数据包按顺序发送的唯一标准
    long long cnt=0;//用于计算重新发包的次数
    long long total=0;//计算总发包次数
    long long ack=0;//服务器端返回的确认信息ack
    long long packet_id=0;//包的序号
    long long time_limit_result;//保存是否超时的判定结果
    long long current_id;//保存当前数据包的序号

    FILE* fp=fopen(file_name,"rb");//打开文件，并以二进制形式读取
    double start,end;//用于存储文件传输的开始时间和结束时间

    GET_TIME(start);//获取文件开始传输的时间
    while(file_size>0){//只要大于0就说明文件还没传输完毕
        memcpy(packet,&ack,4);//将ack的值（int型）以地址形式存储到packet数组的前4位中
        //相当于在每个数据包的头部加上一个序号，只不过该序号不是int型，是4位地址型（char）
        if(file_size>packet_size-4){//除了前四位，每个数据包的剩下部分均用于存储信息
            send_packet_size=packet_size;//超出则强行切割至packet_size-4,剩余内容等待下次循环被继续切割
        }
        else{
            send_packet_size=file_size;//小于则直接全部存入数组即可
        }
        fread(packet+4,send_packet_size,1,fp);//从packet+4的位置开始，按照send_packet_size的大小
        //读出fp指向的文件内容并存入packet数组
        long long send_code=sendto(sock,packet,sizeof(packet),0,(SOCKADDR *)&client_send,sizeof(client_send));
        //发送packet数组（设置缓冲区为sizeof(packet)，这里为1024）
        if(send_code==SOCKET_ERROR){
            printf("第%d号包发送失败!\n\n",ack);
            return 0;
            //发送失败则打印信息并终止程序
        }
        total++;//发送文件总次数+1
        FD_ZERO(&rfd);//将指定的文件描述符集清空
        while(true){//该while循环涉及到添加的功能：超时重传和停等协议
        	long long retransmission_code;
            FD_SET(sock,&rfd);//用于在文件描述符集合中增加一个新的文件描述符
            time_limit_result=select(0,&rfd,NULL,NULL,&time_limit);
            //用于判断在time_limit时间内是否有收到文件这一事件发生
            if(time_limit_result==0){//若为0则代表没有事件发生，即超时
                printf("当前发送的第%d号包未收到ACK确认回复，已超时，即将重传......\n",ack);
                FD_ZERO(&rfd);//将指定的文件描述符集清空
                retransmission_code=sendto(sock,packet,sizeof(packet),0,(SOCKADDR *)&client_send,sizeof(client_send));
                //重新发送当前数据包
                if(retransmission_code==SOCKET_ERROR){
                    printf("第%d号包重传失败！\n\n",ack);
                    return 0;
                    //发送失败则打印信息并终止程序
                }
                printf("第%d号包已重传！\n",ack);
                //反之则发送成功
                total++;//发包总次数+1
                cnt++;//重新发包次数+1
            }
            else if(time_limit_result!=SOCKET_ERROR){//代表没有超时且收到了服务器端的数据包
            	int temp=sizeof(client_receive);
                long long receive_code=recvfrom(sock,id,sizeof(id),0,(SOCKADDR *)&client_receive,&temp);
                //收取服务器端发来的数据包
                memcpy(&current_id,id,4);//因为服务器端的数据包内容为一个4字节大小的char数组，包含确认ack的地址
                //所以将该地址复制到current_id的地址中，相当于使得current_id的值修改为该地址指向的整数
                if(current_id==ack){//若相等则证明本次发包成功，服务器端成功接收到了这一数据包，且客户端收到了确认信息
                //使得可以进行下一数据包的发送，停等协议的作用正体现于此
                    if(cnt!=0){
                        printf("重传第%d号包成功！\n\n",ack);//不为0则代表该包之前有丢包或没收到确认ack，进行过重新发包
                    }
                    else{
                        printf("第%d号包发送成功！\n\n",ack);//为0则代表第一次发包就成功了
                    }
                    break;//可以进行下一数据包的发送了
                }
                else{
                    continue;//若current_id不等于ack，则继续循环等待，直至匹配再跳出循环
                }
            }
        }
        ack++;//数据包序号+1
        cnt=0;//重传次数清零
        file_size-=send_packet_size;//文件大小减去一个数据包的大小
    }
    //为了使得在发送完文件之后服务器端程序可以自动终止，我们在发完所有数据之后额外添加一个数据包
    memcpy(id,&ack,4);//将ack当前地址以4字节形式复制到id数组中
    sendto(sock_end,id,sizeof(id),0,(SOCKADDR *)&client_send,sizeof(client_send));
    //发送一个只有4字节的数据包，服务器收到后会自动终止程序
    GET_TIME(end);//获取文件传输结束时间
    printf("传输时间为：%f秒\n",end-start);//打印传输时间
    printf("%d %d\n",ack,total);
    double sum=(1-ack*1.0/total)*100.0;
    printf("丢包率为%f%%",sum);//计算丢包率
    fclose(fp);//关闭文件
    closesocket(sock);
    closesocket(sock_end);
    //关闭套接字
    WSACleanup();//清理网络环境,释放socket所占的资源
}
