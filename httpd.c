#include<stdio.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>

#define isSpace(x) isspace((int)x) //宏

void* accept_request(void* client_sock)
{
    int client = *(int*)client_sock; //用户套接字
    char buf[1024];
    int numchars;
    char method[255]; //请求方法
    char url[255]; //请求的url
    char path[512]; //url中请求的文件路径
    int cgi = 0; //判断是否是cgi
    char* query_string; //字符指针
    struct stat st;

    numchars = get_line(client, buf, size(buf)); //读取一行请求，放入buf里面
    
    size_t i, j;
    i = 0;
    j = 0;
    //读出method
    while(!isSpace(buf[j]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[j];
        i++;
        j++;
    }
    method[i] = '\0';
    //请求方法既不是GET也不是POST
    if(strcasecmp(method, "GET") && strcasecmp(method, "POST")){
        unimplemented(client);
        return NULL;
    }
    //若是POST，cgi置为1
    if(strcasecmp(method, "POST") == 0){
        cgi = 1;
    }
    i = 0;
    //跳过空格
    while(isSpace(buf[j]) && j < sizeof(buf)){
        j++;
    }
    //获取url
    while(!isSpace(buf[j]) && i < sizeof(url) - 1 && j < sizeof(buf)){
        url[i] = buf[j];
        i++;
        j++;
    }

    //GET请求方法有可能会带有 ？，会有查询参数
    if(strcasecmp(method, "GET") == 0){
        query_string = url;
        while(*query_string != '?' && query_string != '\0'){
            query_string++;
        }
        if(*query_string == '?'){
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    //将字符串 httpdocs/url 存入path当中
    sprintf(path, "httpdocs%s", url);
    
    //如果存入的path最后一个字符是 ‘/’，则添加我们自己的文件path末尾
    if(path[strlen(path) - 1] == '/'){
        strcat(path, "test.html");
    }

    //没有找到对应文件，则清理缓存，将缓存中的数据全部读出并discard
    if(stat(path, &st) == -1){
        while(numchars > 0 && strcmp("\n", buf)){
            numchars = get_line(client, buf, sizeof(buf));
        }
        not_fount(client);
    }




}

void error_die(const char* sc)
{
    perror(sc);
    exit(1);
}

int startup(u_short* port)
{
    int httpd = 0,option;
    struct sockaddr_in name;
    //为httpd分配套接字
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if(httpd == -1){
        error_die("socket");
    }

    socklen_t optlen;
    optlen = sizeof(option);
    option = 1;
    setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, (void*)&option, optlen);

    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    //绑定用户套接字client_sock和端口
    if(bind(httpd, (struct sockaddr*)&name, sizeof(name)) < 0){
        error_die("bind");
    }
    //动态分配一个端口
    if(*port == 0){
        socklen_t namelen = sizeof(name);
        //获取系统分配给套接字httpd分配的地址
        if(getsockname(httpd, (struct sockaddr*)&name, &namelen) == -1){
            error_die("getsockname");
        }
        *port = ntohs(name.sin_port);
    }
    
    if(listen(httpd, 5) < 0){
        error_die("listen");
    }
    return httpd;
}

int main()
{
    int server_sock = -1;
    u_short port = 6379;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);
    pthread_t newthread;
    server_sock = startup(&port);

    printf("http server_sock is %d\n", server_sock);
    printf("http running on port %d\n", port);

    while(1){
        
        client_sock = accept(server_sock, 
                            (struct sockaddr*)&client_name, 
                            &client_name_len);
        printf("New connection.... ip = %s; port = %d", inet_ntoa(client_name.sin_addr), 
                                                        client_name.sin_port);
        if(client_sock == -1){
            error_die("accept");
        }

        if(pthread_create(&newthread, NULL, accept_request, (void*)&client_sock)){
            perror("pthread_create");
        }

    }
    close(server_sock);
}