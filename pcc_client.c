#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <endian.h>

// #include <sys/socket.h>
// #include <sys/types.h>
// #include <netdb.h>


int DEBUG = 0;
int BUFFER_LEN = 1024; 


void print_error_and_exit()
{
    perror("Error");
    exit(1);
}


// read content from a socket into a buffer
void sock_recv(int sockfd, void *buffer_ptr, int msg_len)
{
    int curr_read, total_read;
    
    total_read = 0;
    while (total_read < msg_len)
    {
        curr_read = read(sockfd, buffer_ptr + total_read, msg_len - total_read);

        if (curr_read == -1)
            print_error_and_exit();

        total_read += curr_read;
        
        if (curr_read != 0)
            if (DEBUG) printf("\treceived %lu/%lu\n", (unsigned long)curr_read, (unsigned long)total_read);
    }
}


// write content to a socket from a buffer
void sock_send(int sockfd, void *buffer_ptr, int msg_len)
{
    int curr_sent, total_sent;

    total_sent = 0;
    while (total_sent < msg_len)
    {
        curr_sent = write(sockfd, buffer_ptr + total_sent, msg_len - total_sent); 
        
        if (curr_sent == -1)
            print_error_and_exit();

        total_sent += curr_sent;

        if (DEBUG) printf("\tsent %lu/%lu\n", (unsigned long)curr_sent, (unsigned long)total_sent);
    }
}


// send file to server, obtain pcc from server, print pcc
int get_pcc(int sockfd, FILE *datafile_ptr)
{
    uint64_t data_len,  net_data_len;
    uint64_t pcc,  net_pcc;
    int bytes_read;
    char buffer[BUFFER_LEN];

    // obtain datafile size
    fseek(datafile_ptr, 0L, SEEK_END);
    data_len     = ftell(datafile_ptr);
    net_data_len = htobe64(data_len);
    fseek(datafile_ptr, 0L, SEEK_SET);

    // send datafile size
    sock_send(sockfd, &net_data_len, sizeof(uint64_t));

    // send datafile content
    while((bytes_read = fread(buffer, sizeof(char), BUFFER_LEN, datafile_ptr)) != 0)
        sock_send(sockfd, buffer, bytes_read);

    if (ferror(datafile_ptr))
        print_error_and_exit();

    // recieve pcc & print
    sock_recv(sockfd, &net_pcc, sizeof(uint64_t));
    pcc = be64toh(net_pcc);
    printf("# of printable characters: %lu\n", pcc);


    return 0;
}


int main(int argc, char *argv[])
{
    FILE *datafile_ptr;
    int sockfd;
    struct sockaddr_in serv_addr; 


    if (argc != 4) { fprintf(stderr, "Error: invalid argument #\n"); exit(1); }

    
    // open file for reading
    if ((datafile_ptr = fopen(argv[3], "rb")) == NULL)
        print_error_and_exit();
    

    // create socket & connect to the serer
    if (DEBUG) printf("initializing socket: creation & connection...\n");
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        print_error_and_exit();

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2])); 
    inet_pton(AF_INET, argv[1], &serv_addr.sin_addr.s_addr);

    if(connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0)
        print_error_and_exit();
    if (DEBUG) printf("connected!\n");


    // communicate with server to obtain & print pcc
    get_pcc(sockfd, datafile_ptr);

    
    // close resourcs
    if (DEBUG) printf("closing connection\n");
    close(sockfd);
    fclose(datafile_ptr);

    return 0;
}
