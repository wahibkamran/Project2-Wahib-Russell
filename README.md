# Project2-Wahib-Russell

## Description:

This is a simple TCP-like reliable data transfer protocol built on top of UDP. It features congestion control and implements TCP slow start and congestion avoidance. 

Steps to run programs:

1) Run `make` in the src folder to compile and link the files.
2) Navigate to the newly created obj folder
3) In one terminal window run `./rdt_sender <IP_ADDR> <PORT> <PATH_TO_FILE>`
4) In another window run `./rdt_receiver <PORT> <PATH_TO_NEW_FILE>`
5) In the src folder run the plot_window.py script. A window will appear showing the graph of the CWND over time of the previous file transfer.

***
IF RUNNING IN MAHIMAHI, RUN THE LINK SHELL IN THE SAME TERMINAL WINDOW AS THE SENDER PROGRAM. SIMPLY REPLACE THE IP ADDRESS ARGUMENT WITH $MAHIMAHI_BASE AND IT WILL WORK. TRACE FILES ARE PROVIDED IN THE channel_traces DIRECTORY

EXAMPLE: [delay 20 ms] [link] $./rdt_sender $MAHIMAHI_BASE 9999 my_file.txt
***

## CONGESTION CONTROL

On startup, the sender is set to Slow Start (mode=0). In slow start, the window size increases for each ACK received. Once the size gets to ss_thresh (initially set to 64), we enter congestion avoidance (mode=1) and slowly increase the window size to avoid reaching the cliff.

In the event of a loss the ss_thresh is set to half of the current window size, the window size is set back to one, we re-enter Slow Start and the packet in the window is resent.

## LOSSES

Losses in our implementation are determined in two ways:

1) A timeout - once packets are sent a timer is started. If valid ACKs don't arrive before the timer expires, the CWND gets reset, the packets are resent and the timer restarts. If ACKs come in before the timer expires, the timer is stopped, the window size grows, the newly added packets to the window are sent and the timer is restarted. Currently the timeout is set at 240ms.

2) Duplicate ACKs (Fast Retransmit) - if the same ack number is received as a duplicate three times in a row, we forego waiting for a timeout and immediately resend the packets.

## BUFFERING DATA

On the sender side, we are keeping a buffer of 1024 packets. Every time the window slides forward, the spaces freed up by moving the window forward are immediately filled with new packets read from the sending file. This way, there will always be fresh packets waiting in the buffer for when the window eventually wraps around back to the start. We wrap around back to the start of the buffer by modding the start_wnd and end_wnd pointers by 1024. So we continually cycle through the buffer as if it were a circular linked list.

On the receiver side, any out of order packets are buffered and a duplicate ACK for the desired sequence number is sent. Once the desired packet arrives, the buffer is checked to see if the next desired packet is already present, if it is, the data is written to file and the desired sequence number is updated. This occurs until the desired packet is no longer in the buffer, and then the receiver will send an ACK for said desired packet and the process continues over and over again until the enter file is received.
