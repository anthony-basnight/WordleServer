/* tcp-server-fork.c */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <signal.h>

#define MAXBUFFER 16

extern int total_guesses;
extern int total_wins;
extern int total_losses;
extern char ** words;
int* listeners;
int len_listeners;

struct arguments {
    struct sockaddr_in client;
    int sd;
    int seed;
};

pthread_mutex_t mutex_guesses = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_wins = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_losses = PTHREAD_MUTEX_INITIALIZER;


void signal_handler(int sig) {
    if (sig == SIGUSR1) {
        /* shut down the wordle server */
        printf("MAIN: SIGUSR1 rcvd; Wordle server shutting down...\n");
        free(words);
        for (int i = 0; i < len_listeners; i++) {
            close(*(listeners + i));
        }
        free(listeners);
    }
    exit(EXIT_SUCCESS);
}



void* wordle(void* arg) {
    /* this function is called by each thread and contains the logic to the wordle game */
    int n;
    char* buffer = calloc(9, 1);
    int sd = ((struct arguments *)arg)->sd;
    free(arg);
    
    char* hidden_word = calloc(6, sizeof(char));
    int rand_index = rand() % (sizeof(words) / 8);
    strcpy(hidden_word, *(words + rand_index));

    int guesses_left = 6;
    int correct = 0;
    do {
        printf( "THREAD %lu: waiting for guess\n", pthread_self() );  /* or we can use read() */
        n = recv( sd, buffer, MAXBUFFER, 0 );
        
        if ( n == -1 ) {
            perror( "recv() failed" );
            exit(EXIT_FAILURE);
        } else if ( n == 0 ) {
            /* send appropriate signal to signal handler that user quit */
        } else /* n > 0 */ {
            *(buffer + n) = '\0';   /* assume this is text/char data */
            printf( "THREAD %lu: rcvd guess: %s\n", pthread_self(), buffer );
            
            /* make guess all lowercase letters */
            char* guess = calloc(6, sizeof(char));
            for (int i = 0; i < 5; i++) {
                *(buffer + i) = tolower(*(buffer + i));
                *(guess + i) = *(buffer + i);
            }

            int found = 0;
            /* check if valid word was given and create the correct buffer to send back */
            for ( char ** ptr = words ; *ptr ; ptr++ ){
                if(strcmp(guess, *ptr) == 0){
                    found = 1;
                }
            }
            if (found) {
                /* valid guess*/
                /* print out correct characters as capitals, correct but in wrong place as lowercase, otherwise - */
                *(buffer) = 'Y';
                
                
                char* modified = calloc(6, sizeof(char));
                strcpy(modified, hidden_word);
                pthread_mutex_lock( &mutex_guesses );
                {
                     total_guesses += 1;   /* CRITICAL SECTION */
                }
                pthread_mutex_unlock( &mutex_guesses );
                guesses_left -= 1;
                
                int* locked = calloc(5, sizeof(int));
                for(int i = 0; i < 5; i++){
                    *(buffer + 3 + i) = '-';
                    if(*(guess + i) == *(hidden_word + i)){
                        *(locked + i) = 1;
                        *(buffer + 3 + i) = toupper(*(guess + i));
                        *(modified + i) = '-';
                    }
                }
                /*         
                
                target: radar     -ada-
                guess: muddy      error

                locked: 00100     01001
                buffer: --D--     -r--R

                
                */
                correct = 1;
                for (int i = 0; i < 5; i++) {
                    if (*(locked + i) == 0) {
                        correct = 0;
                    }
                }

                if (correct == 1) {
                    
                    /* they guessed it */
                    pthread_mutex_lock( &mutex_wins);   
                    {
                        total_wins += 1;   /* CRITICAL SECTION */
                    } 
                    pthread_mutex_unlock( &mutex_wins );
                    printf("THREAD %lu: sending reply: %s (%d guesses left)\n", pthread_self(), (buffer + 3), guesses_left);
                    printf("THREAD %lu: game over; word was %s!\n", pthread_self(), (buffer + 3));
                    break;
                    /* send signal */
                    // raise(SIGUSR1);
                }

                for (int i = 0; i < 5; i++) {
                    if (*(locked + i) == 0) {
                        for (int j = 0; j < 5; j++) {
                            if (*(guess + i) == *(modified + j)) {
                                *(buffer + 3 + i) = *(guess + i);
                                *(modified + i) = '-';
                            }
                        }
                    }
                }

                short* guesses = (short*)(buffer + 1);
                *guesses = htons((short)guesses_left);


                printf("THREAD %lu: sending reply: %s (%d guesses left)\n", pthread_self(), (buffer + 3), guesses_left);
                free(modified);
                free(locked);

            } else {
                /* invalid guess*/
                strcpy(buffer, "N??????");
                short* guesses = (short*)(buffer + 1);
                *guesses = htons((short)guesses_left);
                printf("THREAD %lu: invalid guess; sending reply: ????? (%d guesses left)\n", pthread_self(), guesses_left);

            }
            n = send( sd, buffer, 9, 0 );   /* or we can use write() */
            if ( n == -1 ) { perror( "send() failed" ); exit(EXIT_FAILURE); }
        }
    } while ( n > 0 && guesses_left > 0);

    if(correct == 0){
        pthread_mutex_lock( &mutex_losses );   
        {
             total_losses += 1;   /* CRITICAL SECTION */
        } 
        pthread_mutex_unlock( &mutex_losses );
    }

    /* send appropriate signal to determine if user got it right or not */
    free(buffer);
    close(sd);
    pthread_detach(pthread_self());
    return NULL;
}



int wordle_server(int argc, char** argv) {

    /* ignore these signals */
    signal(SIGTERM, SIG_IGN);
    // signal(SIGINT, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    /* send these signals to the signal handler */
    signal(SIGUSR1, signal_handler);

    listeners = calloc(1, sizeof(int));
    len_listeners = 0;

    if(argc != 5) {
        fprintf(stderr, "vibe check failed\n");
        return 69;
    } 

    int listener_number = atoi(*(argv + 1)); /* port number that the server will listen on */
    int seed = atoi(*(argv + 2)); /* determines which words will be randomly selected */
    char* path = *(argv + 3); /* name of the dictionary file */
    int num_words = atoi(*(argv + 4)); /* number of words in the input file */

    words = realloc(words, (num_words + 1)*8); /* allocate the dictionary to hold num_words amount of 5 letter words */
    for (int i = 0; i < num_words; i++) {
        *(words + i) = malloc(6);
    }
    
    char* buffer = calloc(7, 1);
    int fd = open(path, O_RDONLY);
    if(fd == -1) {
        fprintf(stderr, "vibe check failed\n");
        return 69;
    }
    
    for(int i = 0; i < num_words; i++) {
        int rc = read(fd, buffer, 6);
        if(rc != 6){
            fprintf(stderr, "vibe check failed\n");
            return 69;
        }
        *(buffer + rc - 1) = '\0';
        strcpy(*(words + i), buffer);
    }

    free(buffer);
    // for(int i = 0; i < num_words; i++) {
    //     printf("%s\n", *(words + i));
    // }
    *(words + num_words) = NULL;

    printf("MAIN: opened %s (%d words)\n", path, num_words);

    srand(seed);
    printf("MAIN: seeded pseudo-random number generator with %d\n", seed);

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

    if ( bind( listener, (struct sockaddr *)&tcp_server, sizeof( tcp_server ) ) == -1 ) {
        perror( "bind() failed" );
        return EXIT_FAILURE;
    }

    /* identify our port number as a TCP listener port */
    if ( listen( listener, 128 ) == -1 ) {
        perror( "listen() failed" );
        return EXIT_FAILURE;
    }

    printf( "MAIN: Wordle server listening on port {%d}\n", port );

    /* ========================= network setup code above ==================== */
    while ( 1 ) {
        struct sockaddr_in remote_client;
        int addrlen = sizeof( remote_client );

        int newsd = accept( listener, (struct sockaddr *)&remote_client, (socklen_t *)&addrlen );
        if ( newsd == -1 ) { perror( "accept() failed" ); continue; }
        
        listeners = realloc(listeners, (len_listeners + 1) * sizeof(int));
        *(listeners + len_listeners) = newsd;
        len_listeners += 1;

        printf( "MAIN: rcvd incoming connection request\n" );

        /* ========================= application-layer protocol below ============ */

        /* call thread instead of fork */

        
        // char* args = calloc(1, sizeof(arguments));
        // *args = newsd;
        struct arguments * args = malloc(sizeof(remote_client) + 4);
        args->client = remote_client;
        args->sd = newsd;
        args->seed = seed;

        pthread_t tid;
        int rc = pthread_create(&tid, NULL, wordle, args);

        if ( rc == -1 ) { 
            perror( "pthread_create() failed" ); 
            return EXIT_FAILURE; 
        }
    }

    raise(SIGUSR1);
    return EXIT_SUCCESS;
}