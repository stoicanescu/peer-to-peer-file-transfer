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
#include <ctype.h>

#define MAXRECVSTRING 30
#define BROADCAST_PORT 55555

char addressBuffer[INET_ADDRSTRLEN]; // Here it is stored the ip of current peer
int server_port;                     // Here it is stored the port of current peer

struct broadcast_arguments { // For argument to a function called by a thread
    char* server_ip;         // Can't do other way
    int server_port;
};

struct socket_argument { // Same as above
    int sockfd_arg;
};

typedef struct peer {
    char peer_ip[16];
    int peer_port;
    struct peer *next;
} peer;

peer *peers; // List of all peers available in the network
pthread_mutex_t lock; // Used for thread safe -> same resources can be accessed by multiple threads
                      // If a thread do something with a variable, he will put a lock on it
                      // When finish -> unlock it

char* get_IP() { // Get ip of current peer
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

void *broadcast(void *arguments) { // Called by a thread every 2 seconds
                                   // Every peer broadcasts ip and port on broadcast address of network
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
        inet_pton(AF_INET, "255.255.255.255", &broadcastAddr.sin_addr); // Set the broadcast ip address
        broadcastAddr.sin_port = htons(BROADCAST_PORT); // Set port 55555

        // Prepare message
        char message[25]; // ex: 192.168.1.10\n34567\0
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
    struct sockaddr_in server_address; /* Ip Address */

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
    peer *current_peer = peers;
    while(current_peer != NULL) {
        if(strcmp(current_peer->peer_ip, ip) == 0) { 
            return 1;
        }
        current_peer = current_peer->next;
    }
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
            push(found_peer, &peers); // Store it in peers
        }
        
        sleep(1);
    }
    close(sock);
}

void print_list(peer *peer_head) {
    peer *current_peer = peer_head;
    while(current_peer != NULL) {
        printf("\t%s\n", current_peer->peer_ip);
        printf("\t%d\n\n", current_peer->peer_port);
        current_peer = current_peer->next;
    }
}

void print_list_numbered(peer *peer_head){
    peer *current_peer = peer_head;
    int number = 0;
    printf("Choose peer to download from:\n");
    while(current_peer != NULL) {
        printf("\t%d\t%s\n", ++number, current_peer->peer_ip);
        current_peer = current_peer->next;
    }
    printf("\t%d\tAbort\n", ++number);
} 

int get_list_size(peer *peer_head){
    peer *current_peer = peer_head;
    int number = 0;
    while(current_peer != NULL) {
        number++;
        current_peer = current_peer->next;
    }
    return number;
}

peer get_peer_el_from_list(peer *peer_head, int el_nr){
    peer *current_peer = peer_head;
    peer chosen_peer;
    int number = 1;
    while(number != el_nr) {
        number++;
        current_peer = current_peer->next;
    }
    strcpy(chosen_peer.peer_ip, current_peer->peer_ip);
    chosen_peer.peer_port = current_peer->peer_port;
    return chosen_peer;
}

char* itoa(int val, int base){
	
	static char buf[32] = {0};
	
	int i = 30;
	
	for(; val && i ; --i, val /= base)
	
		buf[i] = "0123456789abcdef"[val % base];
	
	return &buf[i+1];
	
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
        listen(sockfd,5);
        int newsockfd, clilen;
        struct sockaddr_in cli_addr;
        int n;
        char buffer[50];

        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) 
            perror("ERROR on accept");
        bzero(buffer,50);
        n = read(newsockfd,buffer,50);
        if (n < 0) 
            perror("ERROR reading from socket");

        char *received_message, *file_name; 
        int what_to_do; // if 0 - send 'y'/'n'
                        // if 1 - send file
        received_message = (char *)malloc(n*sizeof(char));
        strcpy(received_message, buffer);
        received_message[n] = '\0';
        what_to_do = received_message[n-1] - '0'; // '0'/'1'
        file_name = received_message;
        file_name[n-1] = '\0';
        
        if(what_to_do == 0) { // peer only asks if peer has/has not a specific file
            printf("Someone is looking for: %s\n", file_name);
            if(peer_has_file(file_name)) {
                n = write(newsockfd,"y",1);
            }
            else {
                n = write(newsockfd,"n",1);
            }
            if (n < 0) 
                perror("ERROR writing to socket");
        }
        else if(what_to_do == 1) { // peer asks for a specific file to be sent
            if(peer_has_file(file_name)) {
                printf("Sending %s to %s\n", file_name, inet_ntoa(cli_addr.sin_addr));
                
                write(newsockfd ,"Sending", 8);

                char file_path[100] = "./files/";
                strcat(file_path, file_name); 
                char buff[1024]; 
                FILE *fp = fopen(file_path, "rb");
                if(fp == NULL)
                    {
                    perror("Open file error");
                exit(1);
                }

                int file_size; 
                while((file_size = fread(buff, sizeof(char), sizeof(buff), fp)) > 0) {
                    if(write(newsockfd, buff, file_size) < 0) {
                        perror("Fail to send file");
                        break;
                    }
                    memset(buff, 0, sizeof(buff));
                }
            }
            else {
                printf("Something wrong happened!");
                close(newsockfd);
            }
            if (n < 0) 
                perror("ERROR writing to socket");
        }
        close(newsockfd);
    }
}	

void receive_file(peer server_peer, char *message, char *file_name) {
    int socket_desc;
    struct sockaddr_in server;
    socket_desc = socket(AF_INET , SOCK_STREAM , 0);
    if (socket_desc == -1) {
        printf("Could not create socket");
    }
    server.sin_addr.s_addr = inet_addr(server_peer.peer_ip);
    server.sin_family = AF_INET;
    server.sin_port = htons(server_peer.peer_port);

    // Connect to remote server
    if (connect(socket_desc , (struct sockaddr *)&server , sizeof(server)) < 0) {
        puts("connect error");
        return;
    }
    
    // Send file name and 1 - asks to download file
    int n;
    n = write(socket_desc,message,strlen(message));
    if (n < 0) 
        perror("ERROR writing to socket");

    // Receiving file
    char response[8];
    n = read(socket_desc, response, sizeof(response));
    if(strcmp(response, "Sending") == 0) { // if server-peer started to send the file
        printf("\tReceiving '%s'...", file_name);
        char file_path[50] = "./files/";
        char file_name_without_extension[30];
        char file_extension[6];
        char new_file_name[50];
        
        // String processing
        strcpy(new_file_name, file_name);
        strcpy(file_extension, strrchr(new_file_name, '.'));
        *strrchr(file_name, '.') = '\0';
        strcpy(file_name_without_extension, file_name);

        int counter = 0;
        char *scounter;
        while(peer_has_file(new_file_name)) { // if the file already exists
                                              // it creates files like "file_name'n'.bla"
            counter++;
            if(counter > 9) {
                printf("Sorry, too many files with the same name!");
                exit(1);
            }
            scounter = itoa(counter, 10);
            strcat(file_name_without_extension, scounter);
            memset(new_file_name, 0, strlen(new_file_name));
            strcat(new_file_name, file_name_without_extension);
            strcat(new_file_name, file_extension);
            file_name_without_extension[strlen(file_name_without_extension)-1] = '\0';
        }

        strcat(file_path, new_file_name);
        file_path[8 + sizeof(new_file_name)] = '\0';
        
        FILE *fp = fopen(file_path, "ab");
        char buff[1024];
        while((n = read(socket_desc, buff, sizeof(buff))) > 0) {
            if (fwrite(buff, sizeof(char), n, fp) != n)
            {
                perror("Write File Error");
                exit(1);
            }
            memset(buff, 0, sizeof(buff));
        }
        printf("Done\n\n");
        fclose(fp);
    }
    close(socket_desc);
}

void *menu() {
    while(1) {
        char file_name[37];
        char message[40];
        char *p;
        peer *matched_peers;
        memset(file_name, 0, sizeof(file_name));
        printf("Hello! How can we help you? What file are you looking for?\n");
        fscanf(stdin, "%s", file_name);

        int i = 0;
        while(*(file_name + i) != '\0')
            ++i;
        strcpy(message, file_name);
        message[i] = '0';
        message[i+1] = '\0';

        interrogate_peers(message, &matched_peers); // find all peers that has the file you are looking for
        
        int list_size = get_list_size(matched_peers);
        list_size += 1;
        int selected_number = 0;

        printf("\n");
        if(list_size != 1) {
            print_list_numbered(matched_peers);
            int n  = fscanf(stdin,"%d", &selected_number);

            while(selected_number < 1 || selected_number > list_size) {
                printf("Where do you want to take it from? Type the coresponding number to begin downloading\n");
                print_list_numbered(matched_peers);
                n = fscanf(stdin,"%d", &selected_number);
            } 
            if(selected_number == list_size)
                printf("Aborted\n\n");
            else {
                peer connected_peer = get_peer_el_from_list(matched_peers, selected_number);
                message[i] = '1';
                // file_name[strlen(file_name)-1] = '\0';
                receive_file(connected_peer, message, file_name);
            }
        }
        else {
            printf("Sorry, there are no peers available!\n");
        }
        matched_peers = NULL;
    }
}

int main() {
    int sockfd;
    
    sockfd = init_socket();
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(sockfd, (struct sockaddr *)&sin, &len) != -1)
        server_port = ntohs(sin.sin_port); // Get random port of current peer

    pthread_t thread_broadcast, thread_peers_listener, thread_show_list, thread_menu, thread_file_listener;
    struct broadcast_arguments args_server;
    args_server.server_ip = get_IP();
    args_server.server_port = server_port;

    struct socket_argument sockfd_arg;
    sockfd_arg.sockfd_arg = sockfd;

    pthread_create(&thread_broadcast, NULL, &broadcast, (void *)&args_server);
    pthread_create(&thread_peers_listener, NULL, &listen_for_peers, NULL);
    pthread_create(&thread_menu, NULL, &menu, NULL);
    pthread_create(&thread_file_listener, NULL, &listen_for_peer_question, (void *)&sockfd);
    

    pthread_join(thread_broadcast, NULL);
    pthread_join(thread_peers_listener, NULL);
    pthread_join(thread_menu, NULL);
    pthread_join(thread_file_listener, NULL);
    
}