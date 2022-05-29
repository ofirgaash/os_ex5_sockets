#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdatomic.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <endian.h>


int DEBUG = 0;
int BUFFER_LEN = 1024;

atomic_int finish = 0;

uint64_t pcc_total[127];
uint64_t pcc_tmp[127];


void print_error_and_exit()
{
    perror("Error");
    exit(1);
}


void sigint_handler(int signum)
{
    if (DEBUG) printf("\n--- signal detected ---\n");
    finish = 1;
}


// read content from a socket into a buffer
int sock_recv(int connfd, void *buffer_ptr, int msg_len)
{
    int curr_read, total_read;
    
    total_read = 0;
    while (total_read < msg_len)
    {
        curr_read = read(connfd, buffer_ptr + total_read, msg_len - total_read);

        // if read failed, determine action according to pdf instructions
        if (curr_read == -1)
        {
            if (errno == EINTR && finish)
                continue;
            else
                if (errno == ETIMEDOUT || errno == ECONNRESET || 
                    errno == EPIPE     || errno == EOF)
                {
                    perror("Error");
                    return -1;
                }
                else
                    print_error_and_exit();
        }

        total_read += curr_read;
        
        if (curr_read != 0)
            if (DEBUG) printf("\treceived %lu/%lu\n", (unsigned long)curr_read, (unsigned long)total_read);
    }

    return 0;
}


// write content to a socket from a buffer
int sock_send(int connfd, void *buffer_ptr, int msg_len)
{
    int curr_sent, total_sent;

    total_sent = 0;
    while (total_sent < msg_len)
    {
        curr_sent = write(connfd, buffer_ptr + total_sent, msg_len - total_sent); 
        
        // if write failed, determine action according to pdf instructions
        if (curr_sent == -1)
        {
            if (errno == EINTR && finish)
                continue;
            else
                if (errno == ETIMEDOUT || errno == ECONNRESET || 
                    errno == EPIPE     || errno == EOF)
                {
                    perror("Error");
                    return -1;
                }
                else
                    print_error_and_exit();
        }

        total_sent += curr_sent;

        if (DEBUG) printf("\tsent %lu/%lu\n", (unsigned long)curr_sent, (unsigned long)total_sent);
    }

    return 0;
}


// fully process communication with a new client
void get_new_data(int connfd)
{
    uint64_t data_len,  net_data_len;
    uint64_t pcc,  net_pcc;
    int bytes_read, curr_read, i;
    char buffer[BUFFER_LEN];
    

    memset(pcc_tmp, 0L, 127 * sizeof(uint64_t));

    // receive datafile length
    if (sock_recv(connfd, &net_data_len, sizeof(uint64_t)) != 0)
        return;
    data_len = be64toh(net_data_len);
    if (DEBUG) printf("\tdata_len: %lu\n", data_len);

    // receive datafile in batches & maintain pcc_total
    bytes_read = 0;
    pcc = 0;
    while (bytes_read < data_len)
    {
        // determine next batch size 
        if (data_len - bytes_read > BUFFER_LEN)
            curr_read = BUFFER_LEN;
        else
            curr_read = data_len - bytes_read;

        // receive next batch
        if (sock_recv(connfd, buffer, curr_read) != 0)
            return;
        bytes_read += curr_read;

        // maintain pcc data
        for (i = 0; i < curr_read; i++)
            if (buffer[i] >= 32  &&  buffer[i] <= 126)
            {
                pcc++;
                pcc_tmp[((int)buffer[i])]++;
            }
        
        // if (DEBUG) fwrite(buffer, sizeof(char), curr_read, stdout);
    }

    // send pcc to client
    net_pcc = htobe64(pcc);
    if (sock_send(connfd, &net_pcc, sizeof(uint64_t)) != 0)
        return;


    // connection never failed - update global pcc
    for (i = 32; i < 127; i++)
        pcc_total[i] += pcc_tmp[i];
}


int main(int argc, char *argv[])
{
    struct sockaddr_in serv_addr;
    int reuse_addr =  1;
    int listenfd   = -1;
    int connfd     = -1;
    int i;
    socklen_t addrsize = sizeof(struct sockaddr_in);


    // first things first: signal handling, arr initializing, arg validation
    struct sigaction sigint_action = {.sa_handler = sigint_handler};
    if (sigaction(SIGINT, &sigint_action, NULL) == -1)
        print_error_and_exit();

    memset(pcc_total, 0L, 127 * sizeof(uint64_t));

    if (argc != 2) { fprintf(stderr, "Error: invalid argument #\n"); exit(1); }

    
    // create listening socket, bind & listen
    if (DEBUG) printf("initiating listening socket: creation, bind, listen...\n");

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        print_error_and_exit();

    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(int)) < 0)
        print_error_and_exit();

    memset( &serv_addr, 0, addrsize );
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[1]));
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(listenfd, (struct sockaddr*) &serv_addr, addrsize) != 0)
        print_error_and_exit();

    if(listen(listenfd, 10) != 0)
        print_error_and_exit();


    // accept connections
    if (DEBUG) printf("entering loop of connection acceptance...\n");
    while(!finish)
    {
        if ((connfd = accept(listenfd, NULL, NULL)) < 0)
        {
            if (finish)
                break;
            else
                print_error_and_exit();
        }
        if (DEBUG) printf("accepted new connection!\n");

        get_new_data(connfd);

        close(connfd);
        if (DEBUG) printf("connection closed successfully.\n");
    }


    // print pcc data
    for (i = 32; i < 127; i++)
        printf("char '%c' : %u times\n", i, (uint32_t)pcc_total[i]);
    

    close(listenfd);
    return 0;
}
