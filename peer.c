#include <stdio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <netdb.h>
#include <ifaddrs.h>


char addressBuffer[INET_ADDRSTRLEN];

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

void broadcast() {
    int sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sd <= 0) {
        printf("Error: Could not open socket");
        return;
    }
    
    // Set socket options
    // Enable broadcast
    int broadcastEnable = 1;
    int ret = setsockopt(sd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
    if (ret) {
        printf("Error: Could not open set socket to broadcast mode");
        close(sd);
        return;
    }
    
    // Since we don't call bind() here, the system decides on the port for us, which is what we want.    
    
    // Configure the port and ip we want to send to
    struct sockaddr_in broadcastAddr; // Make an endpoint

    memset(&broadcastAddr, 0, sizeof broadcastAddr);
    broadcastAddr.sin_family = AF_INET;
    inet_pton(AF_INET, "255.255.255.255", &broadcastAddr.sin_addr); // Set the broadcast IP address
    broadcastAddr.sin_port = htons(25555); // Set port 25555

    char *message = get_IP();
    ret = sendto(sd, message, strlen(message), 0, (struct sockaddr*)&broadcastAddr, sizeof broadcastAddr);
    if (ret<0) {
        // NSLog(@"Error: Could not open send broadcast");
        close(sd);
        return;        
    }
    // Get responses here using recvfrom if you want...
    close(sd);
}

int init_socket() {
    int sockfd;                        /* Socket */
    struct sockaddr_in server_address; /* IP Address */
    unsigned short port;               /* Port */

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

int main() {
    int sockfd;
    
    sockfd = init_socket();
    struct sockaddr_in sin;
    printf("%d\n", sockfd);
    socklen_t len = sizeof(sin);
    if (getsockname(sockfd, (struct sockaddr *)&sin, &len) != -1)
        printf("port number %d\n", ntohs(sin.sin_port));
}