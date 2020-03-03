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

#define MAXRECVSTRING 30
#define BROADCAST_PORT 25557

char addressBuffer[INET_ADDRSTRLEN];
int server_port;

struct broadcast_arguments {
    char* server_ip;
    int server_port;
};

typedef struct peer {
    char peer_ip[16];
    int peer_port;
    struct peer *next;
} peer;

peer *peers;
//pthread_mutex_t lock = //pthread_mutex_INITIALIZER;

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

void push(peer *found_peer) {
    //pthread_mutex_lock(&lock);
    peer *current_peer = peers;
    if(peers == NULL) {
        peers = found_peer;   
    }
    else {
        while(current_peer->next != NULL) {
            current_peer = current_peer->next;
        }
        current_peer->next = found_peer;

    }
    //pthread_mutex_unlock(&lock);
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
        // printf("Received_ip: %s\n", found_peer->peer_ip);    /* Print the received string */
        // printf("Received_port: %d\n\n\n", found_peer->peer_port);    /* Print the received string */
        if(!exists_ip(found_peer->peer_ip)) {
            push(found_peer);
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
            printf("%s\n", current_peer->peer_ip);
            printf("%d\n\n", current_peer->peer_port);
            current_peer = current_peer->next;
        }
        //pthread_mutex_unlock(&lock);
        sleep(1);
    }
}

int main() {
    int sockfd;
    
    sockfd = init_socket();
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(sockfd, (struct sockaddr *)&sin, &len) != -1)
        server_port = ntohs(sin.sin_port); // Get port of current peer

    pthread_t thread_broadcast, thread_peers_listener, thread_show_list;
    struct broadcast_arguments args_server;
    args_server.server_ip = get_IP();
    args_server.server_port = server_port;

    pthread_create(&thread_broadcast, NULL, &broadcast, (void *)&args_server);
    pthread_create(&thread_peers_listener, NULL, &listen_for_peers, NULL);
    pthread_create(&thread_show_list, NULL, &show_list, NULL);
    
    pthread_join(thread_broadcast, NULL);
    pthread_join(thread_peers_listener, NULL);
    pthread_join(thread_show_list, NULL);
    
}