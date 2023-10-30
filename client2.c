#define _GNU_SOURCE   // permette di usare estensioni GNU
#include <stdio.h>    // permette di usare scanf printf etc ...
#include <stdlib.h>   // conversioni stringa exit() etc ...
#include <stdbool.h>  // gestisce tipo bool
#include <assert.h>   // permette di usare la funzione ass
#include <string.h>   // funzioni per stringhe
#include <errno.h>    // richiesto per usare errno
#include <unistd.h>
#include <arpa/inet.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include "xerrori.h"
#define QUI __LINE__,__FILE__

// host e port a cui connettersi
#define HOST "127.0.0.1"
#define PORT 58053
#define Max_sequence_length 2048 //dimensione massima della sequenza 

// variabile globale che conta le sequenze inviate al server
atomic_int nsq;

/* Read "n" bytes from a descriptor 
   analoga alla funzione python recv_all() */
ssize_t readn(int fd, void *ptr, size_t n) {  
   size_t   nleft;
   ssize_t  nread;
 
   nleft = n;
   while (nleft > 0) {
     if((nread = read(fd, ptr, nleft)) < 0) {
        if (nleft == n) return -1; /* error, return -1 */
        else break; /* error, return amount read so far */
     } else if (nread == 0) break; /* EOF */
     nleft -= nread;
     ptr   += nread;
   }
   return(n - nleft); /* return >= 0 */
}

/* Write "n" bytes to a descriptor 
   analoga alla funzione python sendall() */
ssize_t writen(int fd, void *ptr, size_t n) {  
   size_t   nleft;
   ssize_t  nwritten;
 
   nleft = n;
   while (nleft > 0) {
     if((nwritten = write(fd, ptr, nleft)) < 0) {
        if (nleft == n) return -1; /* error, return -1 */
        else break; /* error, return amount written so far */
     } else if (nwritten == 0) break; 
     nleft -= nwritten;
     ptr   += nwritten;
   }
   return(n - nleft); /* return >= 0 */
}

//task eseguito dal thread
void *task(void *v){

  // creazione socket
  int filed_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (filed_socket < 0)
    xtermina("Errore creazione socket", QUI);

  // assegna indirizzo
  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);
  serv_addr.sin_addr.s_addr = inet_addr(HOST);

  // apre connessione
  if (connect(filed_socket, &serv_addr, sizeof(serv_addr)) < 0) {
    xtermina("Errore connessione", QUI);
  }

  // variabili per la lettura con getline()
  char *line = NULL;
  size_t len = 0;
  ssize_t nread;

  // apertura del file da leggere
  FILE *file = xfopen(v, "r", QUI);
  if (!file)
    xtermina("Apertura file da leggere fallita", QUI);

  // informo il server quale tipo di connesione sta ricevendo
  char *type = "Tipo B";
  // invio la stringa che indica al server quale connessione sta ricevendo
  int e = write(filed_socket, type, strlen(type));
  if (e < 0)
    xtermina("Errore invio indentificazione connessione", QUI);
    
  // leggo le linee del file tramite getline()
  while ((nread = getline(&line, &len, file)) != -1) {
    
    //non invio le linee vuote
    if(nread == 1 && line[0] == '\n') {
      continue;
    }
    if(len>Max_sequence_length)
      xtermina("Errore lunghezza sequenza maggiore di 2048\n", QUI);

    // invio la lunghezza della sequenza al server
    int lnr = htonl(strlen(line));
    e = writen(filed_socket, &lnr, sizeof(lnr));
    if (e < 0)
      xtermina("Errore invio lunghezza sequenza", QUI);

    // invio la sequenza al server
    e = write(filed_socket, line, nread);
    if (e < 0)
      xtermina("Errore invio sequenza", QUI);
  }

  // invio stringa con 0 per indicare fine file
  int lnr = htonl(0);
  //invio stringa fine file
  e = writen(filed_socket, &lnr, sizeof(lnr));
  if (e<0)
    xtermina("Errore invio stringa fine file", QUI);

  // ricevo la lunghezza della sequenza dal server
  int x;
  e = readn(filed_socket, &x, sizeof(x));
  if (e < 0)
    xtermina("Errore ricezione sequenze ricevute", QUI);

  // aggiorno il contatore delle sequenze ricevute
  nsq = nsq + ntohl(x);

  fclose(file);
  free(line);
  pthread_exit(NULL);
}

int main(int argc, char *argv[]){
  if (argc < 2) 
    xtermina("Uso: client2 File1 [File2...]\n", QUI);

  // inizializzo i thread
  pthread_t t[argc-1];

  for(int i=0; i<argc-1; i++){
    // assegno il nome del file al campo nome_file della struct
    if(xpthread_create(&t[i], NULL, &task, argv[i+1], QUI) != 0)
      xtermina("Errore creazione thread\n", QUI);
  }

  // aspetto che tutti i thread terminino
  for(int i=0; i<argc-1; i++){
    if(xpthread_join(t[i], NULL, QUI) != 0)
      xtermina("Errore join thread\n", QUI);
  }

  fprintf(stdout, "sequenze totali ricevute: %d\n", nsq);
    
  return 0;
}
