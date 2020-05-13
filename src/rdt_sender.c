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
#include<math.h>

#include"packet.h"
#include"common.h"

#define STDIN_FD    0
#define RETRY  240 //milli second 



int next_seqno=0;
int send_base=0;
float window_size = 1;
int ss_thresh = 64;
int mode = 0; //0 = slow start, 1 = congestion avoidance
int start_wnd = 0;
int end_wnd = 0;
int waiting = 1;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt[1024];
tcp_packet *recvpkt;
sigset_t sigmask;      

int max(int a, int b){
    if(a > b){
        return a;
    } else {
        return b;
    }
}

void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        //Resend all packets range between 
        //sendBase and nextSeqNum
        // VLOG(INFO, "Timeout happened");
        if(waiting == 0){
            ss_thresh = max(floor(window_size/2), 2);
            // printf("setting ssthresh to %i", ss_thresh);
        }
        mode = 0;
        window_size = 1;
        end_wnd = start_wnd;

        for(int i = 0; i < window_size; i++){
            int ind = (i+start_wnd)%1024;
            // printf("Sending packet at %i\n", ind);
            if(sndpkt[ind] != NULL){
                if(sendto(sockfd, sndpkt[ind], TCP_HDR_SIZE + get_data_size(sndpkt[ind]), 0, 
                        ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                {
                    error("sendto");
                }
                // printf("Sent packet at %i with seq no %i\n", ind, sndpkt[ind]->hdr.seqno);
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
    FILE *fp, *csv;

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

    csv = fopen("window_sizes.csv", "w");

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

    for(int i = 0; i < 1024; i++){
        sndpkt[i] = NULL;
    }

    int curr_seq = send_base;
    int np = 0;
    for(int i = 0; i < 1024; i++){
        len = fread(buffer, 1, DATA_SIZE, fp);
        if ( len > 0){
            curr_seq = next_seqno;
            next_seqno = curr_seq+len;
            sndpkt[i] = make_packet(len);
            memcpy(sndpkt[i]->data, buffer, len);
            sndpkt[i]->hdr.seqno = curr_seq;
            np++;
        }  else {
            break;
        }
    }

    printf("%i\n", next_seqno);
    
    send_base = sndpkt[0]->hdr.seqno;

    for (int i = 0; i < window_size; i++){

        if(sndpkt[i] != NULL){
            if(sendto(sockfd, sndpkt[i], TCP_HDR_SIZE + get_data_size(sndpkt[i]), 0, 
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }else{
                end_wnd=i;
            }
            if(i==0){
                start_timer();
            }

        } else {
            break;
        }
    }

    fprintf(csv, "pkt_seq_no,window_size,ss_thresh\n%i,%.2f,%i\n", 0, window_size, ss_thresh);

    while(1){
        if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                    (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
        {
            error("recvfrom");
        }

        
        recvpkt = (tcp_packet *)buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);

        int count = 0;
        for(int i = 0; i < window_size; i++){
            if(sndpkt[(i+start_wnd)%1024] != NULL){
                // printf("ack recv: %i curr pkt seq no: %i\n", recvpkt->hdr.ackno, sndpkt[(i+start_wnd)%1024]->hdr.seqno);
                if(recvpkt->hdr.ackno > sndpkt[(i+start_wnd)%1024]->hdr.seqno){
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
            waiting = 0;

            //if slow start
            if(mode == 0){
                window_size += count;
                if(window_size >= ss_thresh){
                    mode = 1; //set to congestion avoidance
                }
            } else if(mode == 1) { //else if congestion avoidance
                window_size += ((float)count/window_size);
            }
         
            int temp=end_wnd;
            int temp2=start_wnd; 

            start_wnd+=count;
            end_wnd = (start_wnd + floor(window_size) - 1);

            fprintf(csv, "%i,%.2f,%i\n", recvpkt->hdr.ackno, window_size,ss_thresh);

            start_wnd%=1024;
            end_wnd%=1024;

            int diff = (end_wnd%1024)-temp;
            if(diff < 0){
                diff += 1024;
            }

            for(int i = 0; i < count; i++){
                len = fread(buffer, 1, DATA_SIZE, fp);
                int index=(i+temp2)%1024;
                if ( len > 0){
                    curr_seq = next_seqno;
                    next_seqno = curr_seq+len;
                    sndpkt[index] = make_packet(len);
                    memcpy(sndpkt[index]->data, buffer, len);
                    sndpkt[index]->hdr.seqno = curr_seq;
                    // printf("packet at %i has seq no %i\n", i, sndpkt[i]->hdr.seqno);
                } else{
                    sndpkt[index]=NULL;
                }
            }

            // printf("curr window size: %.2f\n", window_size);

            send_base = recvpkt->hdr.ackno;
            int index;
            curr_seq = send_base;
            // printf("*************************\nSending %i new packets\n", diff);
            for(int i = 1; i <= diff; i++){
                index=(i+temp)%1024;
                // printf("Sending packet at %i\n", index);
                if(sndpkt[index] != NULL){
                    if(sendto(sockfd, sndpkt[index], TCP_HDR_SIZE + get_data_size(sndpkt[index]), 0, 
                            ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                    {
                        error("sendto");
                    }
                    if(index==(temp+1)%1024){
                        start_timer(); 
                    }
                } else {
                    break;
                }
                // printf("Sent packet at %i with seq no %i\n", index, sndpkt[index]->hdr.seqno);
            }
            // printf("Sent %i new packets\n*****************\n\n", diff);

        } else {
            num_dups++;
            if(num_dups >= 3){
                stop_timer();
                resend_packets(SIGALRM);
                start_timer();
            }
        }

        if(sndpkt[start_wnd] == NULL ){

            printf("File sent, closing connection\n");        
            sndpkt[0] = make_packet(0);
            sendto(sockfd, sndpkt[0], TCP_HDR_SIZE,  0,
                (const struct sockaddr *)&serveraddr, serverlen);
            stop_timer();
            break;
        }
    }
    
    for (int i = 0; i < 1024; i++){
        if(sndpkt[i] != NULL){
            free(sndpkt[i]);
        }
    }

    fclose(csv);
    fclose(fp);
    
    return 0;
}