#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>

#include"packet.h"
#include"common.h"

#define STDIN_FD    0
#define RETRY  120 //milli second 

int next_seqno=0;
int send_base=0;
int window_size = 1;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt[10];
tcp_packet *recvpkt;
sigset_t sigmask;       


void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        //Resend all packets range between 
        //sendBase and nextSeqNum
        // VLOG(INFO, "Timeout happened");
        for (int i = 0; i < 10; i++){
            if(sndpkt[i] != NULL){
                if(sendto(sockfd, sndpkt[i], TCP_HDR_SIZE + get_data_size(sndpkt[i]), 0, 
                        ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                {
                    error("sendto");
                }
            } else {
                break;
            }
        }
    }
}

void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}


void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}


/*
 * init_timer: Initialize timeer
 * delay: delay in milli seconds
 * sig_handler: signal handler function for resending unacknoledge packets
 */
void init_timer(int delay, void (*sig_handler)(int)) 
{
    signal(SIGALRM, resend_packets);
    timer.it_interval.tv_sec = delay / 1000;    // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;  
    timer.it_value.tv_sec = delay / 1000;       // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}


int main (int argc, char **argv)
{
    int portno, len;
    int next_seqno;
    int num_dups = 0;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;

    /* check command line arguments */
    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL) {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");


    /* initialize server server details */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    //Stop and wait protocol

    init_timer(RETRY, resend_packets);
    next_seqno = 0;

    for(int i = 0; i < 10; i++){
        sndpkt[i] = NULL;
    }

    int curr_seq = send_base;
    for(int i = 0; i < 10; i++){
        len = fread(buffer, 1, DATA_SIZE, fp);
        if ( len > 0){
            curr_seq = next_seqno;
            next_seqno = curr_seq+len;
            sndpkt[i] = make_packet(len);
            memcpy(sndpkt[i]->data, buffer, len);
            sndpkt[i]->hdr.seqno = curr_seq;
        }  
    }
    
    send_base = sndpkt[0]->hdr.seqno;

    for (int i = 0; i < 10; i++){
        if(sndpkt[i] != NULL){
            if(sendto(sockfd, sndpkt[i], TCP_HDR_SIZE + get_data_size(sndpkt[i]), 0, 
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }
        } else {
            break;
        }
    }

    start_timer();

    while(1){
        if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                    (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
        {
            error("recvfrom");
        }

        recvpkt = (tcp_packet *)buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);

        int count = 0;
        for(int i = 0; i < 10; i++){
            if(sndpkt[i] != NULL){
                if(recvpkt->hdr.ackno > sndpkt[i]->hdr.seqno){
                    count++;
                } else {
                    break;
                }
            } else {
                break;
            }
        }

        if(count > 0){
            num_dups = 0;
            stop_timer();
             
            for(int i = 0; i < 10-count; i++){
                sndpkt[i] = sndpkt[i+count];
            }
            send_base = recvpkt->hdr.ackno;

            curr_seq = send_base;
            for(int i = 10-count; i < 10; i++){
                sndpkt[i] = NULL;
                len = fread(buffer, 1, DATA_SIZE, fp);
                if ( len > 0){
                    curr_seq = next_seqno;
                    sndpkt[i] = make_packet(len);
                    memcpy(sndpkt[i]->data, buffer, len);
                    sndpkt[i]->hdr.seqno = curr_seq;
                    next_seqno = curr_seq+len;
                }
            }

            for (int i = 10-count; i < 10; i++){
                if(sndpkt[i] != NULL){
                    if(sendto(sockfd, sndpkt[i], TCP_HDR_SIZE + get_data_size(sndpkt[i]), 0, 
                            ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                    {
                        error("sendto");
                    }
                } else {
                    break;
                }
            }
            
            start_timer(); 
        } else {
            num_dups++;
            if(num_dups >= 3){
                stop_timer();
                resend_packets(SIGALRM);
                start_timer();
            }
        }

        if(sndpkt[0] == NULL){
            printf("File sent, closing connection\n");        
            sndpkt[0] = make_packet(0);
            sendto(sockfd, sndpkt[0], TCP_HDR_SIZE,  0,
                (const struct sockaddr *)&serveraddr, serverlen);
            stop_timer();
            break;
        }
    }
    
    for (int i = 0; i < 10; i++){
        if(sndpkt[i] != NULL){
            free(sndpkt[i]);
        }
    }
    
    return 0;

    //send all pkts in array xx
    //start timer xx
    //wait
    //if received and seq. no. is in array:
    //  remove all pkts up to the ACK we just got
    //  read from file and add the num we removed to the end of the array and send those
    //  restart timer
    //if timeout resend everything in array
}



