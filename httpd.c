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

void* accept_request();

int startup(u_short* port)
{
    
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