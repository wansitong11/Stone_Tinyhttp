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
#define SERVER_STRING "Stone's http\r\n"

void* accept_request(void*);
int get_line(int, char*, int); //读取一行请求的内容，内部使用了recv函数
void implemented(int);
void not_found(int);
void serve_file(int, const char*);
void execute_cgi(int, const char*, const char*, const char*);
void unimplemented(int);
void execute_cgi(int, const char*, const char*, const char*);

void* accept_request(void* client_sock)
{
    int client = *(int*)client_sock; //用户套接字
    char buf[1024];
    int numchars;
    char method[255]; //请求方法
    char url[255]; //请求的url
    char path[512]; //url中请求的文件路径
    int cgi = 0; //判断是否是cgi
    char* query_string; //字符指针, 指向 ？ 后面的请求内容
    struct stat st;

    numchars = get_line(client, buf, sizeof(buf)); //读取一行请求，放入buf里面
    
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
        not_found(client);
    }
    else{
        //请求参数为目录，则自动打开test.html
        if((st.st_mode & S_IFMT) == S_IFDIR){ //请求参数为目录
            strcat(path, "/test.html");
        }
        //S_IXUSR:文件所有者具可执行权限
		//S_IXGRP:用户组具可执行权限
		//S_IXOTH:其他用户具可读取权限  
        if((st.st_mode & S_IXUSR) ||
           (st.st_mode & S_IXGRP) ||
           (st.st_mode & S_IXOTH))
        {
            cgi = 1;
        }

        if(!cgi) serve_file(client, path);
        else execute_cgi(client, path, method, query_string);
    }
    close(client);
    return NULL;
}

void execute_cgi(int client, const char* filepath, 
                 const char* method, const char* query_string)
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    
    pid_t pid;
    int status;

    int i;
    char c;

    int numchars = 1;

    int content_length = -1; //body长度

    buf[0] = 'A';
    buf[1] = '\0';
    //如果是GET，则读完并忽略剩下的内容
    if(strcasecmp(method, "GET") == 0){
        while(numchars > 0 && strcmp('\n', buf)){
            numchars = get_line(client, buf, sizeof(buf));
        }
    }
    //POST
    //查找请求的Content_Length
    else{
        numchars = get_line(client, buf, sizeof(buf));
        //循环查找header中 Content-Length 并保存
        while(numchars > 0 && strcmp('\n', buf)){
            buf[15] = '\0';
            if(strcasecmp(buf, "Content-Length") == 0){
                content_length = atoi(&(buf[16]));
            }
            //找到之后也继续遍历，知道读完head的内容
            numchars = get_line(client, buf, sizeof(buf));
        }
        if(content_length == -1){
            bad_request(client);
            return;
        }
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, sizeof(buf));
    //创建两个管道进行进程通信
    if(pipe(cgi_output) < 0){
        cannot_execute(client);
        return;
    }
    if(pipe(cgi_input) < 0){
        cannot_execute(client);
        return;
    }
    //创建子进程
    if((pid = fork()) < 0){
        cannot_execute(client);
        return;
    }
    //子进程
    if(pid == 0){
        char meth_env[255];
        char query_env[255];
        char length_env[255];
        //将子进程的输出由标准输出重定向到 cgi_ouput 的管道写端上
        dup2(cgi_output[1], 1);
        //将子进程的输出由标准输入重定向到 cgi_input 的管道读端上
        dup2(cgi_input[0], 0);

        close(cgi_output[0]); //关闭子进程cgi_output读通道
        close(cgi_input[1]);  //关闭子进程cgi_intput写通道

        //构造一个环境变量
        //并将这个环境变量加进子进程的运行环境中
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);

        //根据http 请求的不同方法，构造并存储不同的环境变量
        if(strcasecmp(method, "GET") == 0){ /*GET*/
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else{ /*POST*/
            sprintf(length_env, "CONTENT_LENGTH=%s", content_length);
            putenv(length_env);
        }
        execl(filepath, filepath, NULL);
        exit(0);
    }
    else{ /*父进程*/
        close(cgi_output[1]);
        close(cgi_input[0]);

        //如果接收到了 POST 请求，就将 body 的内容读出，并写进cgi_input管道让子进程去读
        if(strcasecmp(method, "POST") == 0){
            for(i = 0; i < content_length; i++){
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        }

        //然后从 cgi_output 管道中读子进程的输出，即cgi脚本返回的数据，并发送到客户端去
        while(read(cgi_output[0], &c, 1) > 0){
            send(client, &c, 1, 0);
        }

        close(cgi_output[0]);
        close(cgi_input[1]);

        //等待子进程退出
        waitpid(pid, &status, 0);
    }
}

void headers(int client, const char* filepath){
    char buf[1024];

    (void)filepath;  /* could use filename to determine file type */
    //发送HTTP头
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

void cat(int client, FILE* resource){
    char buf[1024];
    fgets(buf, sizeof(buf), resource);
    while(!feof(resource)){
        send(client, buf, sizeof(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

void serve_file(int client, const char* filepath){
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];
    
    //剩下的请求读完
    buf[0] = 'A';
    buf[1] = '\0';
    while(numchars > 0 && strcmp('\n', buf)){
        numchars = get_line(client, buf, sizeof(buf));
    }
    
    resource = fopen(filepath, "r");
    if(resource == NULL){
        not_found(client);
    }
    else{
        //打开成功后，将这个文件的基本信息封装成 response 的头部(header)并返回
        headers(client, filepath);
        //接着把这个文件的内容读出来作为 response 的 body 发送到客户端
        cat(client, resource);
    }
    fclose(resource);
}

int get_line(int sock, char* buf, int size){
    int i = 0;
    char c = '\0';
    int n;

    while(i < size - 1 && c != '\n'){
        n = recv(sock, &c, 1, 0);
        if(n > 0){
            if(c == '\r'){
                //只把数据读出来，但是不从缓冲区释放
                //如果是linux的话，每行结尾就是 \r,因此如果直接读的话就会读到下一行
                //这样就会造成数据的不正确
                n = recv(sock, &c, 1, MSG_PEEK);
                if(n > 0 && c == '\n'){
                    recv(sock, &c, 1, 0);
                }
                else{
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        }
        //读到最后一行
        else{
            c = '\n';
        }
    }
    buf[i] = '\0';
    return i;
}

void not_found(int client){
    char buf[1024];
    //返回404
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

void unimplemented(int client)
{
	char buf[1024];
	//发送501说明相应方法没有实现
	sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, SERVER_STRING);
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "</TITLE></HEAD>\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "</BODY></HTML>\r\n");
	send(client, buf, strlen(buf), 0);
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