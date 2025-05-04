#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define PIPE_PATH_LEN 20
#define MSG_SIZE 256
#define NUM_CHANNELS 2
#define NUM_USERS 10

//Pipe paths
char SERVER_PIPE[PIPE_PATH_LEN] = "./server_pipe";
char CHANNEL_A_PIPE[PIPE_PATH_LEN] = "./channel_A_pipe";
char CHANNEL_B_PIPE[PIPE_PATH_LEN] = "./channel_B_pipe";
char user_pipes[NUM_USERS][PIPE_PATH_LEN];

int server_fd = -1, channel_a_fd = -1, channel_b_fd = -1;
int user_fds[NUM_USERS] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

//Inizializza il nome delle user pipes
void init_pipe_names() {
    int i;
    for (i = 0; i < NUM_USERS; i++) {
        char pipe_name[PIPE_PATH_LEN];
        pipe_name[0] = '.';
        pipe_name[1] = '/';
        pipe_name[2] = 'u';
        pipe_name[3] = 's';
        pipe_name[4] = 'e';
        pipe_name[5] = 'r';
        pipe_name[6] = '_';
        pipe_name[7] = i + '0';
        pipe_name[8] = '_';
        pipe_name[9] = 'p';
        pipe_name[10] = 'i';
        pipe_name[11] = 'p';
        pipe_name[12] = 'e';
        pipe_name[13] = '\0';
        
        int j;
        for (j = 0; j <= 13; j++) {
            user_pipes[i][j] = pipe_name[j];
        }
    }
}

/*
//Chiudi tutte le pipe
void close_pipes() {
    close(server_fd);
    close(channel_a_fd);
    close(channel_b_fd);
    
    int i;
    for (i = 0; i < NUM_USERS; i++) {
        close(user_fds[i]);
    }
}

//Rimuovi tutte le pipe completamente
void cleanup_pipes() {
    unlink(SERVER_PIPE);
    unlink(CHANNEL_A_PIPE);
    unlink(CHANNEL_B_PIPE);
    
    int i;
    for (i = 0; i < NUM_USERS; i++) {
        unlink(user_pipes[i]);
    }
}
*/

//Crea le pipe necessarie
void create_pipes() {
    // Create server pipe (se già esistono non le crea)
    if (mkfifo(SERVER_PIPE, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo server");
            exit(EXIT_FAILURE);
        }
    }
    
    // Create channel pipes
    if (mkfifo(CHANNEL_A_PIPE, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo channel A");
            exit(EXIT_FAILURE);
        }
    }
    
    if (mkfifo(CHANNEL_B_PIPE, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo channel B");
            exit(EXIT_FAILURE);
        }
    }
    
    // Create user pipes
    int i;
    for (i = 0; i < NUM_USERS; i++) {
        if (mkfifo(user_pipes[i], 0666) == -1) {
            if (errno != EEXIST) {
                perror("mkfifo user");
                exit(EXIT_FAILURE);
            }
        }
    }
}

//Indirizza i messaggi
void process_messages() {
    char buffer[MSG_SIZE];
    int bytes_read;
    
    //Controlli i messagi dei client
    bytes_read = read(server_fd, buffer, MSG_SIZE);
    
    if (bytes_read > 0) {
        //Formato: [DESTINATION_TYPE][DESTINATION_ID][SOURCE_ID][MESSAGE]
        //DESTINATION_TYPE: 'U': user, 'C': canale
        //DESTINATION_ID: ID di destinazione (0-9 per gli user, 0-1 per i canali: 0=A, 1=B)
        //SOURCE_ID: ID dello user di sorgente (0-9)
        
        //Estrai il destination type
        char dest_type = buffer[0];
        
        //buffer[x] - '0' perché il numero è in ascii e dobbiamo convertirlo in int con - '0' ('5' - '0' -> 53 - 48 = 5)
        //Estrai il destination ID
        int dest_id = buffer[1] - '0';
        
        //Estrai il source ID
        int source_id = buffer[2] - '0';
        
        //Prepara il messaggio da inoltrare: [SOURCE_ID][MESSAGE]
        char forward_msg[MSG_SIZE];
        forward_msg[0] = source_id + '0'; //+ '0' per lo stesso motivo di prima però questa volta da int ad ascii, quindi invertito
        
        int i;
        //Copia il contenuto del messaggio (inizia dal quarto carattere)
        for (i = 3; i < bytes_read; i++) {
            forward_msg[i - 2] = buffer[i];
        }
        forward_msg[bytes_read - 2] = '\0';
        
        //Indirizza il messaggio
        if (dest_type == 'U' && dest_id >= 0 && dest_id < NUM_USERS) {
            //Dirigi il messaggio all'utente desiderato
            if (user_fds[dest_id] != -1) {
                int bytes_written = write(user_fds[dest_id], forward_msg, bytes_read - 2);
                printf("Messaggio da utente %d a utente %d: %s (scritti %d byte)\n", source_id, dest_id, &forward_msg[1], bytes_written);
                
                if (bytes_written == -1) {
                    perror("Errore con la scrittura nella pipe dell'utente");
                    //Prova a riaprire la pipe
                    close(user_fds[dest_id]);
                    user_fds[dest_id] = open(user_pipes[dest_id], O_WRONLY | O_NONBLOCK);
                    if (user_fds[dest_id] != -1) {
                        printf("Riaperta la pipe dell'utente %d\n", dest_id);
                    }
                }
            } else {
                printf("Pipe dell'utente %d non ancora aperta, messaggio scartato\n", dest_id);
                //Prova a riaprire la pipe
                user_fds[dest_id] = open(user_pipes[dest_id], O_WRONLY | O_NONBLOCK);
                if (user_fds[dest_id] != -1) {
                    printf("Aperta la pipe dell'utente %d\n", dest_id);
                    //Riprova ad inviare il messaggio
                    int bytes_written = write(user_fds[dest_id], forward_msg, bytes_read - 2);
                    printf("Messaggio da utente %d a utente %d: %s (scritti %d byte riprovando)\n", 
                        source_id, dest_id, &forward_msg[1], bytes_written);
                }
            }
        } else if (dest_type == 'C') {
            //Dirigi il messaggio al canale desiderato
            int bytes_written = -1;
            if (dest_id == 0 && channel_a_fd != -1) { //Canale A
                bytes_written = write(channel_a_fd, forward_msg, bytes_read - 2);
                printf("Messaggio da utente %d a canale A: %s (scritti %d byte)\n", source_id, &forward_msg[1], bytes_written);
                
                if (bytes_written == -1) {
                    perror("Errore con la scrittura nella pipe del canale A");
                    //Prova a riparire la pipe
                    close(channel_a_fd);
                    channel_a_fd = open(CHANNEL_A_PIPE, O_WRONLY | O_NONBLOCK);
                    if (channel_a_fd != -1) {
                        printf("Riaperta la pipe del canale A\n");
                    }
                }
            } else if (dest_id == 1 && channel_b_fd != -1) { //Canale B
                bytes_written = write(channel_b_fd, forward_msg, bytes_read - 2);
                printf("Messaggio da utente %d a canale B: %s (scritti %d byte)\n", source_id, &forward_msg[1], bytes_written);
                
                if (bytes_written == -1) {
                    perror("Errore con la scrittura nella pipe del canale B");
                    //Prova a riparire la pipe
                    close(channel_b_fd);
                    channel_b_fd = open(CHANNEL_B_PIPE, O_WRONLY | O_NONBLOCK);
                    if (channel_b_fd != -1) {
                        printf("Riaperta la pipe del canale B\n");
                    }
                }
            } else {
                printf("Pipe del canale %c non ancora aperta, messaggio scartato\n", dest_id == 0 ? 'A' : 'B'); //if-else compattato: 0=A, 1=B
            }
        }
    } else if (bytes_read == -1 && errno != EAGAIN) { //Entra se non è un errore dovuto a una condizione di riprova 
        perror("Errore con la lettura dalla pipe del server");
        //Prova a riaprire la pipe
        close(server_fd);
        server_fd = open(SERVER_PIPE, O_RDONLY | O_NONBLOCK);
        if (server_fd == -1) {
            perror("Impossibile riaprire la pipe del server");
            exit(EXIT_FAILURE);
        } else {
            printf("Riaperta la pipe del server\n");
        }
    }
}

int main() {
    printf("Avviando la simulazione del server Discord...\n");
    
    init_pipe_names();
    create_pipes();
    
    // Open server pipe - just this one initially
    printf("Aprendo la pipe del server...\n");
    server_fd = open(SERVER_PIPE, O_RDONLY | O_NONBLOCK);
    if (server_fd == -1) {
        perror("open server pipe");
        exit(EXIT_FAILURE);
    }
    
    printf("Server in esecuzione. Esci con Ctrl+C.\n");
    printf("In attesa dei client...\n");
    
    //Apre le pipe dinamicamente
    int channel_pipes_opened = 0;
    int user_pipes_opened[NUM_USERS] = {0};
    
    //Server loop
    while (1) {
        //Apri le pipe dei canali se non aperte
        if (!channel_pipes_opened) {
            //Canale A
            if (channel_a_fd == -1) {
                channel_a_fd = open(CHANNEL_A_PIPE, O_WRONLY | O_NONBLOCK);
                if (channel_a_fd != -1) {
                    printf("Aperta la pipe del canale A\n");
                }
            }
            
            //Canale B
            if (channel_b_fd == -1) {
                channel_b_fd = open(CHANNEL_B_PIPE, O_WRONLY | O_NONBLOCK);
                if (channel_b_fd != -1) {
                    printf("Aperta la pipe del canale B\n");
                }
            }
            
            //Controlla se sono tutti e due aperti
            if (channel_a_fd != -1 && channel_b_fd != -1) {
                channel_pipes_opened = 1;
                printf("Tutto le pipe dei canali sono adesso aperte\n");
            }
        }
        
        //Apri le pipe degli user
        int i;
        for (i = 0; i < NUM_USERS; i++) {
            if (!user_pipes_opened[i] && user_fds[i] == -1) {
                user_fds[i] = open(user_pipes[i], O_WRONLY | O_NONBLOCK);
                if (user_fds[i] != -1) {
                    user_pipes_opened[i] = 1;
                    printf("Aperta la pipe dell'utente %d\n", i);
                }
            }
        }
        
        process_messages();
        
        usleep(100000); //Delay
    }
    
    return 0;
}