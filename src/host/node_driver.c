#include "host/node_driver.h"
#include "protocol/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>

#define READ_BUF_SIZE 128

void node_driver_run(int node_id, const char *tty_device, ring_buffer_t *tx_rb)
{
    // printf("[Node Driver %d] Initializing child process (PID: %d). Conecting to: %s\n",
    //        node_id, getpid(), tty_device);

    // Open TTY device in read/write and non-blocking mode
    int tty_fd = open(tty_device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (tty_fd < 0)
    {
        perror("[Error Driver] Could not open the TTY device");
        exit(EXIT_FAILURE);
    }

    // Initialize parser
    protocol_parser_t parser;
    protocol_parser_init(&parser);

    uint8_t read_buf[READ_BUF_SIZE];
    fd_set read_fds, write_fds;

    while (1)
    {
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);

        // Always check if there is data to read from the TTY (RX)
        FD_SET(tty_fd, &read_fds);

        // If the ring buffer contains data, set TTY for writing
        if (!ring_buffer_is_empty(tx_rb))
        {
            FD_SET(tty_fd, &write_fds);
        }

        int max_fd = tty_fd;

        // Block until the file descriptors are ready for reading or writing
        int activity = select(max_fd + 1, &read_fds, &write_fds, NULL, NULL);

        if (activity < 0)
        {
            if (errno == EINTR) // Interrupted by a signal, retry
                continue;
            perror("[Error Driver] Error on select()");
            break;
        }

        // ==========================================
        // 1. RECEPTION CHANNEL (RX): TTY -> Parser
        // ==========================================
        if (FD_ISSET(tty_fd, &read_fds))
        {
            ssize_t bytes_read = read(tty_fd, read_buf, sizeof(read_buf));
            if (bytes_read < 0)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    perror("[Driver Error] Lecture error on TTY");
                    break;
                }
            }
            else if (bytes_read == 0)
            {
                printf("[Node Driver %d] Conection closed for node/dummy.\n", node_id);
                break;
            }
            else
            {
                // Feed parser byte by byte
                protocol_frame_t parsed_frame;
                for (ssize_t i = 0; i < bytes_read; i++)
                {
                    int result = protocol_parser_feed(&parser, read_buf[i], &parsed_frame);

                    if (result == PROTOCOL_FRAME_READY)
                    {
                        printf("[Node Driver %d] Valid frame decoded succesfully! (Type: 0x%02X)\n",
                               node_id, parser.frame.type);

                        // Aquí procesarías los mensajes recibidos (ej. Reportes de Tick, Sporadic, etc.)
                    }
                    else if (result == PROTOCOL_ERR_LEN || result == PROTOCOL_ERR_CRC)
                    {
                        fprintf(stderr, "[Node Driver %d] Warning: Corrupted frame detected by the parser.\n", node_id);
                    }
                }
            }
        }

        // =================================================
        // 2. TRANSMISSION CHANNEL (TX): Ring Buffer -> TTY
        // =================================================
        if (FD_ISSET(tty_fd, &write_fds))
        {
            // Send byte by byte
            uint8_t byte_to_send;
            if (ring_buffer_pop(tx_rb, &byte_to_send))
            {
                ssize_t written = write(tty_fd, &byte_to_send, 1);
                if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    perror("[Driver Error] Write error on TTY");
                    break;
                }
            }
        }
    }

    close(tty_fd);
    exit(EXIT_SUCCESS);
}
