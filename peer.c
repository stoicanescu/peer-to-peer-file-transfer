#include <stdio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <stdlib.h>
#include <pthread.h>
#include <strings.h>
#include <dirent.h>
#include <sys/types.h>

#define MAXRECVSTRING 30
#define BROADCAST_PORT 25557

char addressBuffer[INET_ADDRSTRLEN];
int server_port;

struct broadcast_arguments {
    char* server_ip;
    int server_port;
};

struct socket_argument {
    int sockfd_arg;
};

typedef struct peer {
    char peer_ip[16];
    int peer_port;
    struct peer *next;
} peer;

peer *peers;
pthread_mutex_t lock;

char* get_IP() {
    struct ifaddrs * ifAddrStruct=NULL;
    struct ifaddrs * ifa=NULL;
    void * tmpAddrPtr=NULL;
    getifaddrs(&ifAddrStruct);

    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET && strcmp(ifa->ifa_name, "lo") > 0) { // check it is IP4
            // is a valid IP4 Address
            tmpAddrPtr=&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
            // printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer); 
        }
    }
    if (ifAddrStruct!=NULL) freeifaddrs(ifAddrStruct);
    return addressBuffer;
}

void *broadcast(void *arguments) {
    struct broadcast_arguments *args = arguments;
    char* server_ip = args->server_ip;
    int server_port = args->server_port;


    while(1) {
        int sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sd <= 0) {
            printf("Error: Could not open socket");
            return 0;
        }
        
        // Set socket options
        // Enable broadcast
        int broadcastEnable = 1;
        int ret = setsockopt(sd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
        if (ret) {
            printf("Error: Could not open set socket to broadcast mode");
            close(sd);
            return 0;
        }
        
        // Since we don't call bind() here, the system decides on the port for us, which is what we want.    
        
        // Configure the port and ip we want to send to
        struct sockaddr_in broadcastAddr; // Make an endpoint

        memset(&broadcastAddr, 0, sizeof broadcastAddr);
        broadcastAddr.sin_family = AF_INET;
        inet_pton(AF_INET, "255.255.255.255", &broadcastAddr.sin_addr); // Set the broadcast IP address
        broadcastAddr.sin_port = htons(BROADCAST_PORT); // Set port 25555

        // Prepare message
        char message[25];
        char port[6];
        sprintf(port, "%d", server_port);
        strcpy(message, server_ip);
        strcat(message, "\n");
        strcat(message, port);

        ret = sendto(sd, message, strlen(message), 0, (struct sockaddr*)&broadcastAddr, sizeof broadcastAddr);
        if (ret<0) {
            printf("Error: Could not open send broadcast");
            close(sd);
            return 0;        
        }
        // Get responses here using recvfrom if you want...
        close(sd);
        sleep(2);
    }
}

int init_socket() {
    int sockfd;                        /* Socket */
    struct sockaddr_in server_address; /* IP Address */

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) 
        printf("ERROR opening socket");
    bzero((char *) &server_address, sizeof(server_address));

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = 0;

    bind(sockfd, (struct sockaddr *) &server_address, sizeof(server_address)); 
    return sockfd;
}

int exists_ip(char ip[16]) {
    //pthread_mutex_lock(&lock);
    peer *current_peer = peers;
    while(current_peer != NULL) {
        if(strcmp(current_peer->peer_ip, ip) == 0) { 
            return 1;
        }
        current_peer = current_peer->next;
    }
    //pthread_mutex_unlock(&lock);
    return 0;
}

void push(peer *new_peer, peer **peer_list) {
    pthread_mutex_lock(&lock);
    peer *current_peer = *peer_list;
    if(*peer_list == NULL) {
        *peer_list = new_peer;   
    }
    else {
        while(current_peer->next != NULL) {
            current_peer = current_peer->next;
        }
        current_peer->next = new_peer;

    }
    pthread_mutex_unlock(&lock);
}

int peer_has_file(char *file_name) {
    DIR *dir;
    struct dirent *de;
    int file_found = 0;

    dir = opendir("./files");
    if(dir == NULL){
        printf("DID NOT OPEN DIR!!!!!");
    }
    while ((de = readdir(dir)) != NULL) 
        if(strcmp(file_name, de->d_name) == 0)
            file_found = 1;
    
    return file_found;
}

void *listen_for_peers() {
    int sock;                         /* Socket */
    struct sockaddr_in broadcastAddr; /* Broadcast Address */
    unsigned short broadcastPort;     /* Port */
    char recvString[MAXRECVSTRING+1]; /* Buffer for received string */
    int recvStringLen;                /* Length of received string */

    broadcastPort = BROADCAST_PORT;   /* First arg: broadcast port */

    /* Create a best-effort datagram socket using UDP */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        printf("socket() failed");

    /* Construct bind structure */
    memset(&broadcastAddr, 0, sizeof(broadcastAddr));   /* Zero out structure */
    broadcastAddr.sin_family = AF_INET;                 /* Internet address family */
    inet_pton(AF_INET, "255.255.255.255", &broadcastAddr.sin_addr);
    broadcastAddr.sin_port = htons(broadcastPort);      /* Broadcast port */
    
    /* Bind to the broadcast port */
    if (bind(sock, (struct sockaddr *) &broadcastAddr, sizeof(broadcastAddr)) < 0)
        printf("bind() failed ");

    while(1) {
        /* Receive a single datagram from the server */
        if ((recvStringLen = recvfrom(sock, recvString, MAXRECVSTRING, 0, NULL, 0)) < 0)
            printf("recvfrom() failed");
        recvString[recvStringLen] = '\0';

        peer *found_peer = NULL;
        found_peer = (struct peer*)malloc(sizeof(struct peer));
        found_peer->next = NULL;
        sscanf(recvString, "%s\n%d", found_peer->peer_ip, &found_peer->peer_port);

        if(!exists_ip(found_peer->peer_ip) && strcmp(found_peer->peer_ip, get_IP()) != 0) { // Do not add 'this' peer to list
            push(found_peer, &peers);
        }
        
        sleep(1);
    }
    close(sock);
}

void *show_list() {
    while(1) {
        //pthread_mutex_lock(&lock);
        printf("show:\n");
        peer *current_peer = peers;
        while(current_peer != NULL) {
            printf("\t%s\n", current_peer->peer_ip);
            printf("\t%d\n\n", current_peer->peer_port);
            current_peer = current_peer->next;
        }
        //pthread_mutex_unlock(&lock);
        sleep(1);
    }
}

void print_list(peer *peer_head) {
    peer *current_peer = peer_head;
    while(current_peer != NULL) {
        printf("\t%s\n", current_peer->peer_ip);
        printf("\t%d\n\n", current_peer->peer_port);
        current_peer = current_peer->next;
    }
}

void interrogate_peers(char *file_name, peer **matched_peers) {
    peer *current_peer = peers;
    while(current_peer != NULL) {
        int socket_desc;
        struct sockaddr_in server;
        char *message;
        char response; //If 'y' -> current_peer has the file
                       //If 'n' -> current_peer does not have the file

        //Create socket
        socket_desc = socket(AF_INET , SOCK_STREAM , 0);
        if (socket_desc == -1)
        {
            printf("Could not create socket");
        }
            
        server.sin_addr.s_addr = inet_addr(current_peer->peer_ip);
        server.sin_family = AF_INET;
        server.sin_port = htons(current_peer->peer_port);

        //Connect to remote server
        if (connect(socket_desc , (struct sockaddr *)&server , sizeof(server)) < 0)
        {
            puts("connect error");
            return;
        }
        
        int n;
        n = write(socket_desc,file_name,strlen(file_name));
        if (n < 0) 
            perror("ERROR writing to socket");
        n = read(socket_desc, &response, 1);
        if (n < 0) 
            perror("ERROR reading from socket");
        if(response == 'y') { // y (yes) -> a peer has the file I am looking for
            peer *matched_peer = NULL; 
            matched_peer = (struct peer*)malloc(sizeof(struct peer));
            matched_peer->next = NULL;
            strcpy(matched_peer->peer_ip,  current_peer->peer_ip);
            matched_peer->peer_port = current_peer->peer_port;
            push(matched_peer, matched_peers);
        }
        close(socket_desc);
        current_peer = current_peer->next;
    }
}

void *listen_for_peer_question(void *sockfd_arg) {
    struct socket_argument *sock_arg = sockfd_arg;
    int sockfd = sock_arg->sockfd_arg;
    while(1) {
        int newsockfd, clilen;
        struct sockaddr_in cli_addr;
        int n;
        char file_name[50];

        listen(sockfd,5);
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) 
            perror("ERROR on accept");
        bzero(file_name,50);
        n = read(newsockfd,file_name,50);
        if (n < 0) 
            perror("ERROR reading from socket");
        printf("File name he is looking for: %s\n", file_name);
        if(peer_has_file(file_name))
            n = write(newsockfd,"y",1);
        else
            n = write(newsockfd,"n",1);
        if (n < 0) 
            perror("ERROR writing to socket");
    }
}

void *menu() {
    while(1) {
        char file_name[30];
        char *p;
        peer *matched_peers;
        memset(file_name, 0, sizeof(file_name));
        printf("Hello! How can we help you? What file are you looking for?\n");
        p = fgets(file_name, sizeof(file_name), stdin);
        // printf("%s", file_name);
        interrogate_peers(file_name, &matched_peers);
        print_list(matched_peers);
    }
}

int main() {
    int sockfd;
    
    sockfd = init_socket();
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(sockfd, (struct sockaddr *)&sin, &len) != -1)
        server_port = ntohs(sin.sin_port); // Get port of current peer

    pthread_t thread_broadcast, thread_peers_listener, thread_show_list, thread_menu, thread_file_listener;
    struct broadcast_arguments args_server;
    args_server.server_ip = get_IP();
    args_server.server_port = server_port;

    struct socket_argument sockfd_arg;
    sockfd_arg.sockfd_arg = sockfd;

    pthread_create(&thread_broadcast, NULL, &broadcast, (void *)&args_server);
    pthread_create(&thread_peers_listener, NULL, &listen_for_peers, NULL);
    // pthread_create(&thread_show_list, NULL, &show_list, NULL);
    pthread_create(&thread_menu, NULL, &menu, NULL);
    pthread_create(&thread_file_listener, NULL, &listen_for_peer_question, (void *)&sockfd);
    

    pthread_join(thread_broadcast, NULL);
    pthread_join(thread_peers_listener, NULL);
    // pthread_join(thread_show_list, NULL);
    pthread_join(thread_menu, NULL);
    pthread_join(thread_file_listener, NULL);
    
}