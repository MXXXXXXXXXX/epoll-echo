/************************************************************************
	> File Name: server.c
	> Author: 
	> Mail: 
	> Created Time: 2017年11月20日 星期一 21时48分10秒
 ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/types.h>

#define IPADDRESS   "127.0.0.1"
#define PORT        9997
#define MAXSIZE     1024
#define BACKLOG     5
#define FD_SIZE     1000
#define EPOLLEVENTS 100

//函数声明
//创建套接字并进行绑定
int socket_bind(const char* ip,int port);  //forward declaration
//处理IO多路复用epoll的等待事件的全过程
void handle_epoll(int listenfd);
//事件处理函数
void handle_events(int epollfd,struct epoll_event *events,int num,int listenfd,char *buf);
//处理接收到的连接
void handle_accpet(int epollfd,int listenfd);
//读处理
void read(int epollfd,int fd,char *buf);
//写处理
void write(int epollfd,int fd,char *buf);
//添加注册事件
void add_event(int epollfd,int fd,int state);
//修改注册事件
void modify_event(int epollfd,int fd,int state);
//删除注册事件
void delete_event(int epollfd,int fd,int state);

int main(int argc,char *argv[])
{
    int  listenfd;
    listenfd = socket_bind(IPADDRESS,PORT);
    listen(listenfd,BACKLOG);
    handle_epoll(listenfd);
    return 0;
}

int socket_bind(const char* ip,int port)
{
    //监听套接字的创建
    int  listenfd;
    struct sockaddr_in servaddr;
    listenfd = socket(AF_INET,SOCK_STREAM,0);
    if (listenfd == -1)
    {
        //perror("socket error:");
        printf("an error: %s\n", strerror(errno));
        exit(1);
    }
    //绑定套接字与ip地址和端口号
    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    inet_pton(AF_INET,ip,&servaddr.sin_addr);
    servaddr.sin_port = htons(port);
    if (bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr)) == -1)
    {
        //perror("bind error: ");
        printf("an error: %s\n", strerror(errno));
        exit(1);
    }
    return listenfd;
}

void handle_epoll(int listenfd)
{
    int epollfd;
    struct epoll_event events[EPOLLEVENTS];
    int ret;
    char buf[MAXSIZE];
    memset(buf,0,MAXSIZE);
    //创建一个描述符
    epollfd = epoll_create(FDSIZE);
    //向监听描述符添加事件并放入内核事件表中
    add_event(epollfd,listenfd,EPOLLIN);
    for ( ; ; )
    {
        //获取已经准备好的描述符事件
        //注：此处的epoll_wait等价于select或poll整个进程会阻塞与此
        //因为最后一个参数为超时时间,-1代表不确定或者说永久阻塞
        ret = epoll_wait(epollfd,events,EPOLLEVENTS,-1);
        handle_events(epollfd,events,ret,listenfd,buf);
    }
    close(epollfd);
}

void handle_events(int epollfd,struct epoll_event *events,int num,int listenfd,char *buf)
{
    int i;
    int fd;
    //在需要处理的事件数目中进行遍历
    for (i = 0;i < num;i++)
    {
        //获得文件描述符
        fd = events[i].data.fd;
        //根据描述符的类型和事件类型进行处理
        if ((fd == listenfd) &&(events[i].events & EPOLLIN))
            handle_accpet(epollfd,listenfd);
        else if (events[i].events & EPOLLIN)
            read(epollfd,fd,buf);
        else if (events[i].events & EPOLLOUT)
            write(epollfd,fd,buf);
    }
}

void handle_accpet(int epollfd,int listenfd)
{
    int clifd;
    struct sockaddr_in cliaddr;
    socklen_t cliaddrlen = sizeof(cliaddr);
    clifd = accept(listenfd,(struct sockaddr*)&cliaddr,&cliaddrlen);
    if (clifd == -1)
        printf("an error: %s\n", strerror(errno));
        //perror("accpet error:");
    else
    {
        printf("one new client: %s:%d\n",inet_ntoa(cliaddr.sin_addr),cliaddr.sin_port);
        //将客户描述符和事件挂入内核事件表中
        add_event(epollfd,clifd,EPOLLIN);
    }
}

void read(int epollfd,int fd,char *buf)
{
    int nread;
    nread = read(fd,buf,MAXSIZE);
    if (nread == -1)
    {
        //如果返回值为负表明发生错误，关闭此套接字
        printf("an error: %s\n", strerror(errno));
        close(fd);
        delete_event(epollfd,fd,EPOLLIN);
    }
    else if (nread == 0)
    {
        //如果read返回值为空表明本次通话结束，客户端关闭套接字
        fprintf(stderr,"client close.\n");
        close(fd);
        delete_event(epollfd,fd,EPOLLIN);
    }
    else
    {
        printf("the echo message is : %s",buf);
        //修改描述符对应的事件，由读改为写
        modify_event(epollfd,fd,EPOLLOUT);
    }
}

void write(int epollfd,int fd,char *buf)
{
    int nwrite;
    nwrite = write(fd,buf,strlen(buf));
    if (nwrite == -1)
    {
        printf("an error: %s\n", strerror(errno));
        close(fd);
        delete_event(epollfd,fd,EPOLLOUT);
    }
    else
        modify_event(epollfd,fd,EPOLLIN);
    //清空缓冲区，避免重读
    memset(buf,0,MAXSIZE);
}

void add_event(int epollfd,int fd,int state)
{
    //根据描述符和事件类型添加到内核事件表中
    struct epoll_event ev;
    ev.events = state;
    ev.data.fd = fd;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&ev);
}

static void delete_event(int epollfd,int fd,int state)
{
    //删除注册事件
    struct epoll_event ev;
    ev.events = state;
    ev.data.fd = fd;
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,&ev);
}

static void modify_event(int epollfd,int fd,int state)
{
    struct epoll_event ev;
    ev.events = state;
    ev.data.fd = fd;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&ev);
}

