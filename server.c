#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#define PORT 5000
#define HOST "127.0.0.1"
#define MAX_BUFFER_SIZE 256
#define OK_MSG "OK\n"
#define ERROR_MSG "ERROR\n"
#define NOTFOUND_MSG "NOTFOUND\n"

typedef enum {
	SET = 0,
	GET,
	DEL,
	INVALID,
} cmd_t;

int openServer();
void clientRead(int client);
void clientWrite(int client, const char* msg, size_t msg_len);
void processMsg(char* msg, size_t msg_len, int client);
void doSet(int client, const char* key, const char* value);
void doGet(int client, const char* key);
void doDel(int client, const char* key);
cmd_t getCmdCode(const char* cmd);

cmd_t getCmdCode(const char* cmd) {
	if(!cmd) return INVALID;

	if(strcmp(cmd, "SET") == 0) return SET;
	if(strcmp(cmd, "GET") == 0) return GET;
	if(strcmp(cmd, "DEL") == 0) return DEL;
	return INVALID;
}

int openServer() {
	int s;
	struct sockaddr_in serveraddr;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if(s < 0){
		perror("socket");
		exit(EXIT_FAILURE);
	}

	bzero((char *)&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(PORT);
	if (inet_pton(AF_INET, HOST, &(serveraddr.sin_addr)) <= 0) {
		fprintf(stderr, "ERROR invalid server IP\n");
		exit(EXIT_FAILURE);
	}

	if (bind(s, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1) {
		perror("bind");
		close(s);
		exit(EXIT_FAILURE);
	}

	if (listen(s, 5) == -1) {
		perror("listen");
		close(s);
		exit(EXIT_FAILURE);
	}

	return s;
}

void clientRead(int client) {
	char rcv[MAX_BUFFER_SIZE] = {0};

	int rcv_size = read(client, rcv, MAX_BUFFER_SIZE);
	if (rcv_size == -1) {
		perror("read");
		exit(EXIT_FAILURE);
	}

	// Si el cliente cierra la conexion sin enviar datos...
	if (rcv_size == 0) {
		printf("[SERVER]: Conexión cerrada por el cliente antes de enviar datos\n");
		return;
	}

	rcv[rcv_size] = 0x00;
	printf("[SERVER]: %d bytes recibidos. Msg=%s", rcv_size, rcv);

	processMsg(rcv, rcv_size, client);
	return;
}

void clientWrite(int client, const char* msg, size_t msg_len) {
	int snd_size = write(client, msg, msg_len);
	if (snd_size == -1) {
		perror("write");
		exit(EXIT_FAILURE);
	}
}

void processMsg(char* msg, size_t msg_len, int client) {
	if(!msg || msg_len <= 0 || client < 0) {
		perror("processMsg");
		clientWrite(client, ERROR_MSG, strlen(ERROR_MSG));
		return;
	}

	char* cmd = strtok(msg, " \n");
	char* key = strtok(NULL, " \n");
	char* value = strtok(NULL, "\n"); // Permite espacios

	if(!cmd || !key) {
		clientWrite(client, ERROR_MSG, strlen(ERROR_MSG));
		return;
	}

	cmd_t cmd_code = getCmdCode(cmd);

	switch(cmd_code) {
		case SET:
		if(!value) {
			clientWrite(client, ERROR_MSG, strlen(ERROR_MSG));
			return;
		}
		doSet(client, key, value);
		break;

		case GET:
		doGet(client, key);
		break;

		case DEL:
		doDel(client, key);
		break;

		default:
		clientWrite(client, ERROR_MSG, strlen(ERROR_MSG));
		break;
	}

	return;
}

void doSet(int client, const char* key, const char* value) {
	if(!value || !key) {
		clientWrite(client, ERROR_MSG, strlen(ERROR_MSG));
		return;
	}

	printf("[SERVER]: SET - key(%s) value(%s)\n", key, value);
	int fd = open(key, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if(fd < 0) {
		perror("open");
		clientWrite(client, ERROR_MSG, strlen(ERROR_MSG));
		return;
	}

	ssize_t wr_len = write(fd, value, strlen(value)+1);
	if(wr_len < 0) {
		perror("write");
		close(fd);
		clientWrite(client, ERROR_MSG, strlen(ERROR_MSG));
		return;
	}

	close(fd);
	clientWrite(client, OK_MSG, strlen(OK_MSG));
	return;
}

void doGet(int client, const char* key) {
	if(!key) {
		clientWrite(client, ERROR_MSG, strlen(ERROR_MSG));
		return;
	}

	printf("[SERVER]: GET - key(%s)\n", key);
	char value[MAX_BUFFER_SIZE];

	int fd = open(key, O_RDONLY);
	if(fd < 0) {
		perror("open");
		clientWrite(client, NOTFOUND_MSG, strlen(NOTFOUND_MSG));
		return;
	}

	ssize_t rd_len = read(fd, value, MAX_BUFFER_SIZE-1);
	if(rd_len < 0) {
		perror("read");
		clientWrite(client, ERROR_MSG, strlen(ERROR_MSG));
		close(fd);
		return;
	}

	value[rd_len] = '\n';
	rd_len++;
	value[rd_len] = 0x00;
	close(fd);

	clientWrite(client, OK_MSG, strlen(OK_MSG));
	clientWrite(client, value, rd_len+1);
	return;
}

void doDel(int client, const char* key) {
	if(!key) {
		clientWrite(client, ERROR_MSG, strlen(ERROR_MSG));
		return;
	}

	printf("[SERVER]: DEL - key(%s)\n", key);

	int ret = unlink(key);
	if(ret < 0) {
		if(errno == ENOENT) {
			// archivo no existe (ignorar error)
		}
		else {
			perror("unlink");
			clientWrite(client, ERROR_MSG, strlen(ERROR_MSG));
			return;
		}
	}

	clientWrite(client, OK_MSG, strlen(OK_MSG));
	return;
}

int main(void) {

	int server_fd = openServer();

	while(1) {
		socklen_t clientaddr_len = sizeof(struct sockaddr_in);
		struct sockaddr_in clientaddr;

		printf("[SERVER]: Esperando una conexion... (port: %d)\n", PORT);
		int client_fd = accept(server_fd, (struct sockaddr *)&clientaddr, &clientaddr_len);
		if(client_fd < 0) {
			perror("accept");
			continue;
		}

		char client_ip[INET_ADDRSTRLEN] = {0};
		inet_ntop(AF_INET, &(clientaddr.sin_addr), client_ip, sizeof(client_ip));
		printf("[SERVER]: %s conectado!\n", client_ip);

		clientRead(client_fd);

		close(client_fd);
	}

	close(server_fd);

	return EXIT_SUCCESS;
}
