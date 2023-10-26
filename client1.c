#define _GNU_SOURCE // permette di usare estensioni GNU
#include "xerrori.h"
#include <arpa/inet.h>
#include <assert.h>  // permette di usare la funzione ass
#include <errno.h>   // richiesto per usare errno
#include <stdbool.h> // gestisce tipo bool
#include <stdio.h>   // permette di usare scanf printf etc ...
#include <stdlib.h>  // conversioni stringa exit() etc ...
#include <string.h>  // funzioni per stringhe
#include <sys/socket.h>
#include <unistd.h>
#define QUI __LINE__, __FILE__

// host e port a cui connettersi
#define HOST "127.0.0.1"
#define PORT 58053
#define Max_sequence_length 2048 

/* Write "n" bytes to a descriptor
   analoga alla funzione python sendall() */
ssize_t writen(int fd, void *ptr, size_t n) {
  size_t nleft;
  ssize_t nwritten;

  nleft = n;
  while (nleft > 0) {
    if ((nwritten = write(fd, ptr, nleft)) < 0) {
      if (nleft == n)
        return -1; /* error, return -1 */
      else
        break; /* error, return amount written so far */
    } else if (nwritten == 0)
      break;
    nleft -= nwritten;
    ptr += nwritten;
  }
  return (n - nleft); // return >= 0
}

int main(int argc, char *argv[]) {
  //ho bisogno solamento di un solo argomento, ossia il file
  if (argc != 2)
    xtermina("uso: client1 <File>", QUI);

  // apertura del file
  FILE *file = fopen(argv[1], "r");
  if (!file)
    xtermina("apertura file fallita", QUI);

  // variabili per la lettura con getline()
  char *line = NULL;
  size_t len = 0;
  ssize_t nread;
  int e; // variabile per controllare se errori nell'invio delle sequenze
  
  // creazione socket
  int fd_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (fd_socket < 0)
    xtermina("Errore creazione socket", QUI);
  
  // assegna indirizzo
  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);
  serv_addr.sin_addr.s_addr = inet_addr(HOST);

  // apre connessione
  if (connect(fd_socket, &serv_addr, sizeof(serv_addr)) < 0) {
    fclose(file);
    free(line);
    xtermina("Errore connessione", QUI);
  }

  // informo il server quale tipo di connesione sta ricevendo
  char *type = "Tipo A";
  // invio la stringa che indica al server quale connessione sta ricevendo
  e = write(fd_socket, type, strlen(type));
  if (e < 0)
    xtermina("Errore invio stringa tipo a", QUI);

  // leggo le linee del file tramite getline()
  while ((nread = getline(&line, &len, file)) != -1) {
    //non invio le linee vuote
    if(nread == 1 && line[0] == '\n') {
      continue;
    }
    if(len>Max_sequence_length)
      xtermina("Sequenza troppo lunga", QUI);
    // invio la lunghezza della sequenza al server
    int lnr = htonl(strlen(line));
    e = writen(fd_socket, &lnr, sizeof(lnr));
    if (e < 0)
      xtermina("Errore invio lunghezza sequenza", QUI);
    // invio la sequenza al server
    e = write(fd_socket, line, nread);
    if (e < 0)
      xtermina("Errore invio sequenza", QUI);
  }

  // invio sequenza vuota per indicare la fine del file
  int lnr = htonl(0);
  e = writen(fd_socket, &lnr, sizeof(lnr));
  if (e < 0)
    xtermina("Errore invio lunghezza sequenza", QUI);

  // fine invio con chiusura della connessione
  close(fd_socket);
  // deallocazione memoria
  fclose(file);
  free(line);

  return 0;
}
