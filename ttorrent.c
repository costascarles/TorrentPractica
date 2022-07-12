/**
 * @desc Implementación del Cliente y Servidor de la practica 1 Xarxes - Trivial Torrent 
 * @author Elisenda & Carles Costas 	
 */
#include "file_io.h"

#include "logger.h"

#include <string.h>

#include <stdlib.h>

#include <errno.h>

#include <assert.h>

#include <sys/socket.h>

#include <unistd.h>


static
const uint32_t MAGIC_NUMBER = 0xde1c3232;
static
const uint8_t MSG_REQUEST = 0;
static
const uint8_t MSG_RESPONSE_OK = 1;
static
const uint8_t MSG_RESPONSE_NA = 2;
static
const char * EXTENSION = ".ttorrent";

enum {
  RAW_MESSAGE_SIZE = 13
};

/**
 * Main function.
 */
int main(int argc, char ** argv) {

  set_log_level(LOG_DEBUG);

  log_printf(LOG_INFO, "Trivial Torrent (build %s %s) by %s", __DATE__, __TIME__, "Elisenda & Carles");

  struct torrent_t torrent;
  //Server
  if (argc == 4 && argv[1][0] == '-' && argv[1][1] == 'l') {
    //Validamos los parametros de entrada    
    if (strrchr(argv[3], '.') == NULL || strcmp(strrchr(argv[3], '.'), EXTENSION)) {
      printf("Error file extension is not valid %s \n", strrchr(argv[3], '.'));
      return -1;
    }
    if (atoi(argv[2]) == 0 || atoi(argv[2]) > 65535) {
      printf("Error invalid server port\n");
      return -1;
    }
    //Buscamos la longitud del nombre del ficehro sin la extension y extraemos el nombre
    size_t longitud = strlen(argv[3]) - strlen(EXTENSION);
    char * file = malloc(sizeof(char) * longitud);
    if (file == NULL) {
      log_printf(LOG_INFO, "Error in data allocation code %s", strerror(errno));
      return -1;
    }
    strncpy(file, argv[3], longitud);
    int is_completed = 1;
    struct block_t block;
    struct sockaddr_in my_addr;
    //Cargamos la meta información en torrent
    if (create_torrent_from_metainfo_file(argv[3], & torrent, file) < 0) {
      log_printf(LOG_INFO, "Error creating meta info file %s", strerror(errno));
      return -1;
    }
    //Revisamos si el server contiene los bloques correctamente en caso de no tener el ficehro ser terminara el server
    for (unsigned int i = 0; i < torrent.block_count; i++) {
      if (torrent.block_map[i] == 0) {
        is_completed = 0;
      }
    }
    if (is_completed == 0) {
      log_printf(LOG_INFO, "El servidor no tiene el fichero descargado correctamente.");
      return -1;
    }
    //Creamos el socket y llenamos la estrucutura sockaddr_in
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd == -1) {
      log_printf(LOG_INFO, "Error creating socket with code %s", strerror(errno));
      return -1;
    }
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons((uint16_t) atoi(argv[2]));
    my_addr.sin_addr.s_addr = INADDR_ANY;
    //nombramos el socket y dejamos el servidor pendinte de una conexión
    if (bind(fd, (struct sockaddr * ) & my_addr, sizeof(struct sockaddr_in)) < 0) {
      log_printf(LOG_INFO, "Error nombrando el socket %s", strerror(errno));
      return -1;
    }
    if (listen(fd, 0) < 0) {
      log_printf(LOG_INFO, "Error escuchando conexiones codigo %s", strerror(errno));
      return -1;
    }
    while (1) {
      //Se accepta conexiones entrantes y se gestionan los hilos de las nuevas conexiones
      struct sockaddr_in connection;
      socklen_t connection_len = sizeof connection;
      int connection_fd = accept(fd, (struct sockaddr * ) & connection, & connection_len);
      if (connection_fd == -1) {
        log_printf(LOG_INFO, "Error creating socket of new connection with code %s", strerror(errno));
        return -1;
      }
      int pid = fork();
      if (pid == -1) {
        log_printf(LOG_INFO, "Error creating child thread %s", strerror(errno));
        return -1;
      }
      //Gestionamos las solicitudes de la conexion acceptada
      if (pid == 0) {
        if (close(fd) == -1) {
          log_printf(LOG_INFO, "Error closing file descriptor code %s", strerror(errno));
          return -1;
        }
        while (1) {
          //Declaramos campos a recivir y seteamos buffer
          uint32_t magic_number = 0;
          uint8_t message_code = 0;
          uint64_t block_number = 0;
          uint8_t * request_buffer = malloc(sizeof(uint8_t) * RAW_MESSAGE_SIZE);
          if (request_buffer == NULL) {
            log_printf(LOG_INFO, "Error in data allocation code %s", strerror(errno));
            return -1;
          }
          //Esperamos recivir una request si se cierra la conexion se cierra el hilo y el socket
          ssize_t response_code = recv(connection_fd, request_buffer, RAW_MESSAGE_SIZE, 0);
          if (response_code == -1) {
            log_printf(LOG_INFO, "Error reciving datagram code %s", strerror(errno));
            return -1;
          }
          if (response_code == 0) {
            if (close(connection_fd) == -1) {
              log_printf(LOG_INFO, "Error closing file descriptor code %s", strerror(errno));
              return -1;
            }
            free(request_buffer);
            free(file);
            if (destroy_torrent( & torrent) < 0) {
              log_printf(LOG_INFO, "Error destroing torrent code %s", strerror(errno));
              return -1;
            }
            return 0;
          }
          //Deserializmos los datos			
          for (unsigned int i = 0; i < 4; i++) {
            magic_number += ((uint32_t) request_buffer[i]) << (24 - (i * 8));
          }
          message_code = request_buffer[4];
          for (unsigned int i = 0; i < 8; i++) {
            block_number += ((uint64_t) request_buffer[i + 5]) << (56 - (i * 8));
          }
          //Si es una request y tenemos el bloque solicitado serializamos la respuesta en este caso solo es necesario modificar el codigo
          if (message_code != MSG_REQUEST || torrent.block_map[block_number] == 0) {
            request_buffer[4] = MSG_RESPONSE_NA;
          } else {
            request_buffer[4] = MSG_RESPONSE_OK;
          }
          //Enviamos la respuesta al cliente
          if (send(connection_fd, request_buffer, RAW_MESSAGE_SIZE, MSG_NOSIGNAL) == -1) {
            log_printf(LOG_INFO, "Error sending response code %s", strerror(errno));
            return -1;
          }
          //Si hemos eanviado que tenemos el bloque cargamos el bloque correspondiente y lo enviamos
          if (request_buffer[4] == MSG_RESPONSE_OK) {
            if (load_block( & torrent, block_number, & block) < 0) {
              log_printf(LOG_INFO, "Error loading block %s", strerror(errno));
              return -1;
            }
            if (send(connection_fd, block.data, block.size, MSG_NOSIGNAL) == -1) {
              log_printf(LOG_INFO, "Error sending response code %s", strerror(errno));
              return -1;
            }
            printf("Sending Block number %ld\n", block_number);
          }
          free(request_buffer);

        }

      }
      if (close(connection_fd) == -1) {
        log_printf(LOG_INFO, "Error closing file descriptor code %s", strerror(errno));
        return -1;
      }

    }
    if (close(fd) == -1) {
      log_printf(LOG_INFO, "Error closing file descriptor code %s", strerror(errno));
      return -1;
    }
    free(file);
    if (destroy_torrent( & torrent) < 0) {
      log_printf(LOG_INFO, "Error destroing torrent code %s", strerror(errno));
      return -1;
    }
  } else if (argc == 2) {
    if (strrchr(argv[1], '.') == NULL || strcmp(strrchr(argv[1], '.'), EXTENSION)) {
      printf("Error file extension is not valid %s \n", strrchr(argv[1], '.'));
      return -1;
    }
    //Buscamos la longitud del nombre dle ficehro sin la extension
    size_t longitud = strlen(argv[1]) - strlen(EXTENSION);
    //Creamos un array de char de la longitud del nombre
    char * file = malloc(sizeof(char) * longitud);
    if (file == NULL) {
      log_printf(LOG_INFO, "Error in data allocation code %s", strerror(errno));
      return -1;
    }
    //copiamos el nombre
    strncpy(file, argv[1], longitud);
    int is_completed = 1;
    struct block_t block;
    struct sockaddr_in server_addr;
    uint8_t * response;
    if (create_torrent_from_metainfo_file(argv[1], & torrent, file) < 0) {
      log_printf(LOG_INFO, "Error creating meta info file %s", strerror(errno));
      return -1;
    }
    //Revisamos si se han descargado correctamente los bloques si falla cualquiera de los dos casos se debera volver a solicitar el bloque
    for (unsigned int i = 0; i < torrent.block_count; i++) {
      if (torrent.block_map[i] == 0) {
        is_completed = 0;
      }
    }
    if (is_completed == 1) {
      log_printf(LOG_INFO, "El fixero se ha descargado correctamente.");
      free(file);
      if (destroy_torrent( & torrent) < 0) {
        log_printf(LOG_INFO, "Error destroing torrent code %s", strerror(errno));
        return -1;
      }
      return 0;
    }

    //Creamos el socekt para cada peear
    unsigned int i = 0;
    while (i < torrent.peer_count && is_completed == 0) {
      int fd = socket(AF_INET, SOCK_STREAM, 0);
      if (fd == -1) {
        log_printf(LOG_INFO, "Error creating socket with code %s", strerror(errno));
        return -1;
      }
      //Creamos la estructura de direcciones con la info del metafile
      memset( & server_addr, '\0', sizeof(struct sockaddr_in));
      server_addr.sin_family = AF_INET;
      server_addr.sin_port = torrent.peers[i].peer_port;
      //Casteamos a uint32_t dado que en el metafile nos biene dado en uint8_t
      server_addr.sin_addr.s_addr = htonl(((uint32_t) torrent.peers[i].peer_address[0] << 24) | ((uint32_t) torrent.peers[i].peer_address[1] << 16) | ((uint32_t) torrent.peers[i].peer_address[2] << 8) | ((uint32_t) torrent.peers[i].peer_address[3] << 0));
      //Enviamos petición de conexion si falla probamos el siguiente peer
      if (connect(fd, (struct sockaddr * ) & server_addr, sizeof(struct sockaddr_in)) < 0) {
        log_printf(LOG_INFO, "Error connecting to peer %s", strerror(errno));
        i++;
        errno = 0;
        continue;
      }
      //Por cada bloque que no corresponda el hash o no se haya descagado solicitamos al server
      is_completed = 1;
      for (unsigned int j = 0; j < torrent.block_count; j++) {
        if (load_block( & torrent, j, & block) < 0) {
          log_printf(LOG_INFO, "Error loading block %s", strerror(errno));
          return -1;
        }
        if (torrent.block_map[j] == 0) {
          //Creamos el mensaje para solicitar el bloque con la estructura que se indica 
          uint32_t magic_number = MAGIC_NUMBER;
          uint8_t message_code = MSG_REQUEST;
          uint64_t block_number = j;
          uint8_t * payload = 0;
          long unsigned int payload_size = 0;
          size_t size = RAW_MESSAGE_SIZE + sizeof(uint8_t) * payload_size;
          uint8_t * message = malloc(sizeof(uint8_t) * size);
          if (message == NULL) {
            log_printf(LOG_INFO, "Error in data allocation code %s", strerror(errno));
            return -1;
          }
          //Empaquetamos los datos
          for (unsigned int x = 0; x < 4; x++) {
            message[x] = (uint8_t)(magic_number >> (24 - (x * 8)));
          }
          message[4] = message_code;
          for (unsigned int x = 0; x < 8; x++) {
            message[x + 5] = (uint8_t)(block_number >> (56 - (x * 8)));
          }
          for (unsigned int x = 0; x < payload_size; x++) {
            message[x + 13] = payload[x];
          }
          //Enviamos la solicitud
          if (send(fd, message, size, MSG_NOSIGNAL) == -1) {
            log_printf(LOG_INFO, "Error sending message code %s", strerror(errno));
            return -1;
          }
          //Si recivimos que tiene el bloque, esperamos que nos lo envie
          size_t responsesSize = RAW_MESSAGE_SIZE;
          response = malloc(sizeof(uint8_t) * responsesSize);
          if (response == NULL) {
            log_printf(LOG_INFO, "Error in data allocation code %s", strerror(errno));
            return -1;
          }
          ssize_t response_code = recv(fd, response, responsesSize, MSG_WAITALL);
          if (response_code == -1) {
            log_printf(LOG_INFO, "Error reciving datagram code %s", strerror(errno));
            return -1;
          }
          if (response[4] == MSG_RESPONSE_OK) {
            struct block_t newB;
            newB.size = get_block_size( & torrent, j);
            ssize_t res = recv(fd, newB.data, (size_t) newB.size, MSG_WAITALL);
            if (res == -1) {
              log_printf(LOG_INFO, "Error reciving datagram code %s", strerror(errno));
              return -1;
            }
            if (store_block( & torrent, j, & newB) < 0) {
              log_printf(LOG_INFO, "Error storing block in file code %s", strerror(errno));
              return -1;
            }
            free(response);
            free(message);

          }

        }
      }
      if (close(fd) == -1) {
        log_printf(LOG_INFO, "Error closing file descriptor code %s", strerror(errno));
        return -1;
      }
      i++;
    }
    free(file);
    if (destroy_torrent( & torrent) < 0) {
      log_printf(LOG_INFO, "Error destroing torrent code %s", strerror(errno));
      return -1;
    }

  } else {
    printf("Torrent Parameters are incorrect\n");
  }

  return 0;
}