#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define PIPE_PATH_LEN 20
#define MSG_SIZE 256
#define BUFFER_SIZE 1024

//Pipe paths
char SERVER_PIPE[PIPE_PATH_LEN] = "./server_pipe";
char CHANNEL_A_PIPE[PIPE_PATH_LEN] = "./channel_A_pipe";
char CHANNEL_B_PIPE[PIPE_PATH_LEN] = "./channel_B_pipe";
char USER_PIPE[PIPE_PATH_LEN];

int server_fd;
int read_fd;  //Pipe da cui stiamo attualmente leggendo(canale o user)
int client_id;
char current_mode; //'U': user, 'C': canale
int current_target; //ID dell'utente o del canale con cui stiamo attualmente parlando

//Inizializza il nome della pipe del client
void init_pipe_name(int id) {
    USER_PIPE[0] = '.';
    USER_PIPE[1] = '/';
    USER_PIPE[2] = 'u';
    USER_PIPE[3] = 's';
    USER_PIPE[4] = 'e';
    USER_PIPE[5] = 'r';
    USER_PIPE[6] = '_';
    USER_PIPE[7] = id + '0';
    USER_PIPE[8] = '_';
    USER_PIPE[9] = 'p';
    USER_PIPE[10] = 'i';
    USER_PIPE[11] = 'p';
    USER_PIPE[12] = 'e';
    USER_PIPE[13] = '\0';
}

//Parla con un utente
void switch_to_user(int user_id) {
    if (read_fd != -1) {
        close(read_fd);
    }
    
    char target_pipe[PIPE_PATH_LEN];
    
    //Genera la path
    if (user_id == client_id) {
        int i;
        for (i = 0; i < PIPE_PATH_LEN; i++) {
            target_pipe[i] = USER_PIPE[i];
        }
    } else {
        target_pipe[0] = '.';
        target_pipe[1] = '/';
        target_pipe[2] = 'u';
        target_pipe[3] = 's';
        target_pipe[4] = 'e';
        target_pipe[5] = 'r';
        target_pipe[6] = '_';
        target_pipe[7] = user_id + '0';
        target_pipe[8] = '_';
        target_pipe[9] = 'p';
        target_pipe[10] = 'i';
        target_pipe[11] = 'p';
        target_pipe[12] = 'e';
        target_pipe[13] = '\0';
    }
    
    //Crea la pipe del target se non esiste
    printf("Inizializzazione delle pipe...\n");
    if (mkfifo(target_pipe, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo target");
            exit(EXIT_FAILURE);
        }
    }
    
    printf("Aprendo la pipe dello user %d in lettura...\n", user_id);
    read_fd = open(target_pipe, O_RDONLY | O_NONBLOCK);
    if (read_fd == -1) {
        perror("open user pipe in lettura");
        exit(EXIT_FAILURE);
    }
    
    //La apro pure in scrittura per tenerla viva
    int write_fd = open(target_pipe, O_WRONLY | O_NONBLOCK);
    if (write_fd == -1) {
        perror("open user pipe in scrittura");
    }
    
    current_mode = 'U';
    current_target = user_id;
    printf("Stai parlando con lo user %d\n", user_id);
}

//Parla con un canale
void switch_to_channel(int channel_id) {
    if (read_fd != -1) {
        close(read_fd);
    }
    
    char* pipe_path = (channel_id == 0) ? CHANNEL_A_PIPE : CHANNEL_B_PIPE; //if-else compattato: 0=A, 1=B
    
    //Crea la pipe se non esiste
    printf("Inizializzazione delle pipe...\n");
    if (mkfifo(pipe_path, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo channel");
            exit(EXIT_FAILURE);
        }
    }
    
    printf("Aprendo la pipe del canale %c in lettura...\n", channel_id == 0 ? 'A' : 'B'); //if-else compattato: 0=A, 1=B
    read_fd = open(pipe_path, O_RDONLY | O_NONBLOCK);
    if (read_fd == -1) {
        perror("open channel pipe in lettura");
        exit(EXIT_FAILURE);
    }
    
    //La apro pure in scrittura per tenerla viva
    int write_fd = open(pipe_path, O_WRONLY | O_NONBLOCK);
    if (write_fd == -1) {
        perror("open channel pipe in scrittura");
    }
    
    current_mode = 'C';
    current_target = channel_id;
    printf("Sei nel canale %c\n", channel_id == 0 ? 'A' : 'B'); //if-else compattato: 0=A, 1=B
}

//Invia il messaggio ad un utente o canale
void send_message(char *message) {
    char buffer[MSG_SIZE];
    
    //Formato: [DESTINATION_TYPE][DESTINATION_ID][SOURCE_ID][MESSAGE]
    buffer[0] = current_mode;  //'U': user, 'C': channel
    buffer[1] = current_target + '0';  //ID di destinazione
    buffer[2] = client_id + '0';  //ID di sorgente
    
    //Copia il messaggio
    int i = 0;
    while (message[i] != '\0' && i < MSG_SIZE - 4) {
        buffer[i + 3] = message[i];
        i++;
    }
    buffer[i + 3] = '\0';
    
    //Invia il messaggio al server
    int bytes_written = write(server_fd, buffer, i + 4);
    
    if (bytes_written > 0) {
        printf("Inviati %d byte al server. Target: %c%d\n", 
            bytes_written, current_mode, current_target);
    } else {
        perror("Impossibile inviare il messaggio");
    }
}

//Leggi i messaggi in arrivo
void process_incoming_messages() {
    char buffer[MSG_SIZE];
    int bytes_read;
    
    bytes_read = read(read_fd, buffer, MSG_SIZE);
    
    if (bytes_read > 0) {
        //Formato: [SOURCE_ID][MESSAGE]
        int source_id = buffer[0] - '0';
        
        //Inserisci il carattere di terminazione alla fine del messaggio
        buffer[bytes_read] = '\0';
        
        //Assicurati di processare solo i messaggi degli altri utenti e non i propri
        if (source_id != client_id) {
            if (current_mode == 'U') {
                printf("User %d: %s\n", source_id, &buffer[1]);
            } else { //Canale
                printf("User %d nel canale %c: %s\n", source_id, current_target == 0 ? 'A' : 'B', &buffer[1]); //if-else compattato: 0=A, 1=B
            }
        }
        
        //Debug
        printf("Ricevuti %d bytes dalla pipe. Source ID: %d\n", bytes_read, source_id);
    } else if (bytes_read == -1 && errno != EAGAIN) {
        perror("Errore nella lettura dalla pipe");
    }
}

//Controlla i comandi in input
void parse_command(char *input) {
    //Comandi:
    // /user <id> - Parla direttamente con un utente
    // /channel <A|B> - Parla in un canale
    // /quit - Esci dal client
    
    if (input[0] == '/' && input[1] == 'u' && input[2] == 's' && input[3] == 'e' && input[4] == 'r' && input[5] == ' ') {
        int user_id = input[6] - '0';
        if (user_id >= 0 && user_id < 10 && user_id != client_id) {
            switch_to_user(user_id);
        } else {
            printf("User ID invalido. Deve essere fra 0 e 9, non deve essere il tuo ID.\n");
        }
    } else if (input[0] == '/' && input[1] == 'c' && input[2] == 'h' && input[3] == 'a' && input[4] == 'n' && input[5] == 'n' && input[6] == 'e' && input[7] == 'l' && input[8] == ' ') {
        if (input[9] == 'A' || input[9] == 'a') {
            switch_to_channel(0);
        } else if (input[9] == 'B' || input[9] == 'b') {
            switch_to_channel(1);
        } else {
            printf("Canale invalido. Canali disponibili: A e B.\n");
        }
    } else if (input[0] == '/' && input[1] == 'q' && input[2] == 'u' && input[3] == 'i' && input[4] == 't') {
        exit(EXIT_SUCCESS);   //TODO
    } else {
        //Non è un comando quindi tratta l'input come messaggio normale
        send_message(input);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <client_id>\n", argv[0]);
        printf("client_id: 0-9\n");
        return 1;
    }
    
    client_id = argv[1][0] - '0';
    if (client_id < 0 || client_id > 9) {
        printf("client_id deve essere fra 0 e 9\n");
        return 1;
    }
    
    //Crea la propria pipe se non esiste
    init_pipe_name(client_id);

    printf("Creando la pipe del client: %s\n", USER_PIPE);
    if (mkfifo(USER_PIPE, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo client");
            exit(EXIT_FAILURE);
        }
    }
    
    //Crea la pipe del server se non esiste
    printf("Inizializzazione delle pipe...\n");
    if (mkfifo(SERVER_PIPE, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo server");
            exit(EXIT_FAILURE);
        }
    }

    //Apri la pipe in scrittura
    printf("Aprendo la pipe del server in scrittura...\n");
    server_fd = open(SERVER_PIPE, O_WRONLY);
    if (server_fd == -1) {
        perror("open server pipe in scrittura");
        printf("Assicurati di aver avviato il server prima dei client.\n");
        exit(EXIT_FAILURE);
    }
    
    read_fd = -1;
    
    printf("Discord Client %d avviato.\n", client_id);
    printf("Comandi:\n");
    printf("  /user <id> - Parla con un utente (0-9)\n");
    printf("  /channel A|B - Accedi ad un canale\n");
    printf("  /quit - Esci dal client\n");
    printf("Ogni altro input verrà interpretato come messaggio da inviare al destinatario\n");
    
    //Inizializza il canale A come default
    switch_to_channel(0);
    
    char input_buffer[BUFFER_SIZE];

    //Client loop
    //Per farlo parallelo mi basterebbe creare un figlio e svolgere i due compiti separatamente ma crea problemi quindi ci si accontenta di quello che funziona
    while (1) {
        process_incoming_messages();

        printf("[%c%d]> ", current_mode, current_target);
        while (1) {
            //Write
            if (fgets(input_buffer, BUFFER_SIZE, stdin) != NULL) {  //inserisci l'input del terminale nel buffer
                //Assicurati di inserire il carattere terminante
                int i = 0;
                while (input_buffer[i] != '\n' && input_buffer[i] != '\0') {
                    i++;
                }
                if (input_buffer[i] == '\n') {
                    input_buffer[i] = '\0';
                }
                //Invia se c'è contenuto
                if (i > 0) {
                    parse_command(input_buffer);
                }
            }
        }
    }
    return 0;
}