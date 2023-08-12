/* tcp-server-fork.c */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>




#define MAXBUFFER 16

extern int total_guesses;
extern int total_wins;
extern int total_losses;
extern char ** words;

void* wordle(void* arg) {

    /* this function is called by each thread and contains the logic to the wordle game */


}



int wordle_server(int argc, char** argv)
{
    if(argc != 5){
        fprintf(stderr, "vibe check failed\n");
        return 69;
    } 
    int listener_number = atoi(*(argv + 1)); /* port number that the server will listen on */
    int seed = atoi(*(argv + 2)); /* determines which words will be randomly selected */
    char* path = *(argv + 3); /* name of the dictionary file */
    int num_words = atoi(*(argv + 4)); /* number of words in the input file */

    char** dictionary = calloc(num_words, sizeof(6)); /* allocate the dictionary to hold num_words amount of 5 letter words */
    char* buffer = calloc(6, 1);
    int fd = open(path, O_RDONLY);
    for(int i = 0; i < num_words; i++){
        int rc = read(fd, buffer, 5);
        if(rc != 5){
            fprintf(stderr, "vibe check failed\n");
            return 69;
        }
        strcpy(*(dictionary+i), buffer);
    }
    free(buffer);
    for(int i = 0; i < num_words; i++){
        fprintf("%s\n", buffer + i);
    }

    
    /* Create the listener socket as TCP socket (SOCK_STREAM) */
    int listener = socket( AF_INET, SOCK_STREAM, 0 );
                                /* here, SOCK_STREAM indicates TCP */

    if ( listener == -1 ) { perror( "socket() failed" ); return EXIT_FAILURE; }

    /* populate the socket structure for bind() */
    struct sockaddr_in tcp_server;
    tcp_server.sin_family = AF_INET;   /* IPv4 */

    tcp_server.sin_addr.s_addr = htonl( INADDR_ANY );
        /* allow any remote IP address to connect to our socket */

    unsigned short port = listener_number;

    tcp_server.sin_port = htons( port );

    if ( bind( listener, (struct sockaddr *)&tcp_server, sizeof( tcp_server ) ) == -1 )
    {
        perror( "bind() failed" );
        return EXIT_FAILURE;
    }

    /* identify our port number as a TCP listener port */
    if ( listen( listener, 5 ) == -1 )
    {
        perror( "listen() failed" );
        return EXIT_FAILURE;
    }

    printf( "SERVER: TCP listener socket (fd %d) bound to port %d\n", listener, port );

    /* ========================= network setup code above ==================== */

    while ( 1 )
    {
        struct sockaddr_in remote_client;
        int addrlen = sizeof( remote_client );

        printf( "SERVER: Blocked on accept()\n" );
        int newsd = accept( listener, (struct sockaddr *)&remote_client,
                            (socklen_t *)&addrlen );
        if ( newsd == -1 ) { perror( "accept() failed" ); continue; }

        printf( "SERVER: Accepted new client connection on newsd %d\n", newsd );

        /* ========================= application-layer protocol below ============ */

        /* call thread instead of fork */
        
        pid_t p = fork();
        

        if ( p == -1 ) { perror( "fork() failed" ); return EXIT_FAILURE; }

        if ( p > 0 ) /* PARENT */
        {
        close( newsd );
        }
        else /* p == 0  CHILD */
        {
        int n;

        do
        {
            char buffer[MAXBUFFER+1];

            printf( "CHILD: Blocked on recv()\n" );  /* or we can use read() */
            n = recv( newsd, buffer, MAXBUFFER, 0 );

            if ( n == -1 )
            {
            perror( "recv() failed" );
            return EXIT_FAILURE;
            }
            else if ( n == 0 )
            {
            printf( "CHILD: Rcvd 0 from recv(); closing descriptor %d...\n", newsd );
            }
            else /* n > 0 */
            {
            buffer[n] = '\0';   /* assume this is text/char data */
            printf( "CHILD: Rcvd message (%d bytes) from %s: \"%s\"\n", n,
                    inet_ntoa( (struct in_addr)remote_client.sin_addr ), buffer );

            printf( "CHILD: Sending acknowledgement to client\n" );
            n = send( newsd, "ACK\n", 4, 0 );   /* or we can use write() */
            if ( n == -1 ) { perror( "send() failed" ); return EXIT_FAILURE; }
            }
        }
        while ( n > 0 );

        close( newsd );
        printf( "CHILD: All done\n" );
        return EXIT_SUCCESS;
        }
    }

    close( listener );
    
    free(dictionary);

    return EXIT_SUCCESS;
}