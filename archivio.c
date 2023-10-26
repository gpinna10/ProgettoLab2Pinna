#define _GNU_SOURCE /* See feature_test_macros(7) */
#include "xerrori.h"
#include "rwunfair.h"
#include <assert.h> // permette di usare la funzione assert
#include <errno.h>
#include <arpa/inet.h>
#include <stdatomic.h>
#include <semaphore.h>
#include <fcntl.h>
#include <pthread.h>
#include <search.h>
#include <sys/syscall.h>
#include <signal.h>
#include <stdbool.h> // gestisce tipo bool (variabili booleane)
#include <stdio.h>   // permette di usare scanf printf etc ...
#include <stdlib.h>  // conversioni stringa/numero exit() etc ...
#include <string.h>  // confronto/copia/etc di stringhe
#include <unistd.h>  // per sleep

#define QUI __LINE__, __FILE__
#define Num_elem 1000000
#define PC_buffer_len 10
#define Max_sequence_length 2048 

//------------------ STRUCT ------------------

// struct con tutti gli argomenti da passare ai thread caposcrittore e scrittori
typedef struct {
  int pipe_caposc;               //pipe caposcrittore
  sem_t *sem_full_w;              //semaforo per buffer scrittori pieno
  sem_t *sem_empty_w;             //semaforo per buffer scrittori vuoto
  pthread_mutex_t *mutex_writer; //mutex per scrivere nel buffer scrittori
  char **buffer_writer;          //buffer per i lettori
  int ind_caposc;                //indice da cui il caposcrittore inserisce
  int ind_write;                 //indice buffer scrittori
} writer_args;

// struct con tutti gli argomenti da passare ai thread capolettore e lettori
typedef struct {
  int pipe_capolet;              //pipe capolettore
  sem_t *sem_full_r;             //semaforo per buffer lettori pieno
  sem_t *sem_empty_r;            //semaforo per buffer lettori vuoto
  pthread_mutex_t *mutex_reader; //mutex per scrivere nel buffer lettori
  char **buffer_reader;          //buffer per i scrittori
  int ind_capolet;               //indice da cui il capolettore inserisce
  int ind_read;                  //indice da cui il lettore deve leggere
  FILE *log_r;                   //file di log per i lettori
} reader_args;

//struct coppia della hash table
typedef struct {
  int valore;  
  ENTRY *next;  
} coppia;

typedef struct{
  pthread_t *capolett;         //Thread capolettore che deve terminare con appena riceva il SIGTERM
  pthread_t *caposcritt;       //Thread caposcrittore che deve terminare con appena riceva il SIGTERM
} signal_args;

//------------------ VARIABILI GLOBALI ------------------
rwHT struct_rwHT;                     //struct gestione tabella hash dichiarata in rwunfair.h
atomic_int elem_HT = 0;               //indica il numero di elementi nella tabella hash 
ENTRY *testa_lista_entry = NULL;      // Testa della lista di entry

// ---------- FUNZIONI HASH TABLE -------------
ENTRY *crea_entry(char *s, int n) {
  ENTRY *e = malloc(sizeof(ENTRY));
  if (e == NULL)
    xtermina("errore malloc entry 1", QUI);
  e->key = strdup(s); // salva copia di s
  if (e->key == NULL)
    xtermina("errore strdup entry 2", QUI);
  e->data = (int *)malloc(sizeof(coppia));
  if (e->data == NULL)
    xtermina("errore malloc entry 3", QUI);
  *((int *)e->data) = n;
  // Ora eseguo un casting per poter accedere ai campi della struct coppia
  coppia *c = (coppia *)e->data;
  c->valore = n;
  c->next = NULL;
  return e;
}

void distruggi_entry(ENTRY *e) {
  free(e->key);
  free(e->data);
  free(e);
}

void aggiungi(char *s){
  ENTRY *e = crea_entry(s, 1); //creo la entry da inserire nella tabella, con valore 1
  ENTRY *ep = hsearch(*e, FIND); // cerco la entry nella tabella hash
  if (ep == NULL && elem_HT<Num_elem){ // se non è presente allora la inserisco
    ep = hsearch(*e, ENTER);
    if(ep == NULL)
      xtermina("aggiungi(): errore inserimento entry", QUI);
    coppia *c = (coppia *)e->data;
    // inserisco il nuovo valore in testa e aggiorno il puntatore
    c->next = testa_lista_entry;
    testa_lista_entry = e;
    // incremento il numero di occorrenze di questa stringa nella tabella
    elem_HT++;
  }
  // caso in cui la stringa è presente nella tabella e quindi va solo aggiornato il valore
  else {
    // entry già presente nella hash perciò incremento il valore ad esso associato
    if(strcmp(e->key, ep->key) != 0)
      xtermina("aggiungi(): errore incremento valore entry", QUI);
    coppia *c = (coppia *)ep->data;
    c->valore += 1;
    distruggi_entry(e); //La entry, non trattandosi di un nuovo inserimento, non va allocata bensì deallocata
  }
}

int conta(char *s){
  int num = 0;
  ENTRY *e = crea_entry(s, 1);
  ENTRY *ep = hsearch(*e, FIND);
  if (ep != NULL) // controllo se la entry è presente nella tabella hash
    num = *((int *)ep->data); //prendo il valore associato alla entry
  distruggi_entry(e);//La entry non deve essere allocata, perciò va subito deallocata
  return num;
}

void hash_delete(ENTRY *lis){
  if (lis != NULL) {
    coppia *c = lis->data;
    hash_delete(c->next); //distruggo ricorsivamente
    distruggi_entry(lis);
  }
}

// ---------- FUNZIONI THREAD LETTORI -------------

void *t_capolet(void *v) {
  // inizializzo gli argomenti utili da str_arg
  reader_args *a = (reader_args *)v;
  // creo ed alloco il buffer per la pipe capolet
  char *pipe_buffer = malloc(Max_sequence_length * sizeof(char**));
  int pipe_len; // indica la lunghezza della pipe capolet
  char *delimitatori = ".,:; \n\r\t";

  while(true){
    // ricevo la lunghezza della pipe capolet
    ssize_t n_byte = read(a->pipe_capolet, &pipe_len, sizeof(int));
    if (n_byte == -1)
      xtermina("errore lettura lunghezza pipe capolet", QUI);
    if (n_byte == 0){
      // svuoto il buffer dei scrittori
      for(int i=0; i<PC_buffer_len; i++){
        a->buffer_reader[i] = NULL;
        a->ind_read = i;
        sem_post(a->sem_empty_r);
      }
      free(pipe_buffer);
      pthread_exit(NULL);
    }
  
    // ricevo la sequenza dalla pipe capolet
    ssize_t seq_b = read(a->pipe_capolet, pipe_buffer, pipe_len);
    if (seq_b == -1)
      xtermina("errore lettura sequenza pipe capolet", QUI);
    // inserisco 0 alla fine della sequenza
    pipe_buffer[seq_b] = 0;
    //creo la toker con strtok
    char *token = strtok(pipe_buffer, delimitatori);
    //inserisco il token nel buffer dei lettori
    while(token != NULL){
      // entro nella sezione critica
      xsem_wait(a->sem_full_r, QUI);
      //inserisce nel buffer il token, e incrementa gli indici
      a->buffer_reader[a->ind_capolet] = strdup(token);
      a->ind_capolet = ((a->ind_capolet)+1) % PC_buffer_len;
      token = strtok(NULL, delimitatori);
      xsem_post(a->sem_empty_r, QUI); 
    }
  }
}

void *t_lettore(void *v){
  // inizializzo gli argomenti utili da str_arg
  reader_args *a = (reader_args *)v;
  rwHT *rw_HT = &struct_rwHT;
  char *token = NULL;
  // devo leggere (estrarre) dal buffer dei lettori e inserire nella tabella hash
  // entro in mutua esclusione sul buffer dei lettori
  while(true){
    xsem_wait(a->sem_empty_r, QUI);
    xpthread_mutex_lock(a->mutex_reader, QUI);
    // attendo finchè non ricevo un segnale dal capolettore
    token = a->buffer_reader[a->ind_read];    
    if(token  == NULL){
      pthread_mutex_unlock(a->mutex_reader);
      xsem_post(a->sem_empty_r, QUI);
      pthread_exit(NULL);
    }
    // eseguo conta con il token trovato
    read_lock(rw_HT);
    int occ = conta(token);
    //l'output di conta() viene scritto nel file log_r
    fprintf(a->log_r, "%s %d\n", token, occ);
    read_unlock(rw_HT);
    free(a->buffer_reader[a->ind_read]);
    a->buffer_reader[a->ind_read] = NULL;
    a->ind_read = ((a->ind_read)+1) % PC_buffer_len;
    pthread_mutex_unlock(a->mutex_reader);
    xsem_post(a->sem_full_r, QUI);
  }
}

// ---------- FUNZIONI THREAD SCRITTORI -------------

void *t_caposc(void *v) {
  // inizializzo gli argomenti utili da str_arg
  writer_args *a = (writer_args *)v;
  // creo ed alloco il buffer per la pipe capolet
  char *pipe_buffer = malloc(Max_sequence_length * sizeof(char**));
  int pipe_len; // indica la lunghezza della pipe capolet
  char *delimitatori = ".,:; \n\r\t";

  while(true){
    // ricevo la lunghezza della pipe capolet
    ssize_t n_byte = read(a->pipe_caposc, &pipe_len, sizeof(int));
    if (n_byte == -1)
      xtermina("errore lettura lunghezza pipe caposc", QUI);
    if (n_byte == 0){
      // svuoto il buffer dei scrittori
      for(int i=0; i<PC_buffer_len; i++){
        a->buffer_writer[i] = NULL;
        a->ind_write = i;
        sem_post(a->sem_empty_w);
      }
      free(pipe_buffer);
      pthread_exit(NULL);
    }  
    // ricevo la sequenza dalla pipe caposc
    ssize_t seq_b = read(a->pipe_caposc, pipe_buffer, pipe_len);
    if (seq_b == -1)
      xtermina("errore lettura sequenza pipe caposc", QUI);

    // inserisco 0 alla fine della sequenza
    pipe_buffer[seq_b] = 0;

    //creo la toker con strtok
    char *token = strtok(pipe_buffer, delimitatori);
    //inserisco il token nel buffer dei lettori
    while(token != NULL){
      // entro nella sezione critica
      xsem_wait(a->sem_full_w, QUI);
      xpthread_mutex_lock(a->mutex_writer, QUI);
      //inserisce nel buffer il token, e incrementa gli indici
      a->buffer_writer[a->ind_caposc] = strdup(token);
      a->ind_caposc = ((a->ind_caposc)+1) % PC_buffer_len;
      token = strtok(NULL, delimitatori); 
      xsem_post(a->sem_empty_w, QUI); 
      xpthread_mutex_unlock(a->mutex_writer, QUI);
    }
  }
}

void *t_scrittore(void *v){
  // inizializzo gli argomenti utili da str_arg
  writer_args *a = (writer_args *)v;
  rwHT *rw_HT = &struct_rwHT;
  char *token;
  // devo leggere (estrarre) dal buffer dei lettori e inserire nella tabella hash
  // entro in mutua esclusione sul buffer dei lettori
  while(true){
    xsem_wait(a->sem_empty_w, QUI);
    xpthread_mutex_lock(a->mutex_writer, QUI);
    // attendo finchè non ricevo un segnale dal capolettore
    token = a->buffer_writer[a->ind_write];
    if(token == NULL){
      pthread_mutex_unlock(a->mutex_writer);
      xsem_post(a->sem_empty_w, QUI);
      pthread_exit(NULL);
    }
    // eseguo conta con il token trovato
    read_lock(rw_HT);
    aggiungi(token);  
    read_unlock(rw_HT);
    free(a->buffer_writer[a->ind_write]);
    a->buffer_writer[a->ind_write] = NULL;
    a->ind_write = ((a->ind_write)+1) % PC_buffer_len;
    pthread_mutex_unlock(a->mutex_writer);
    xsem_post(a->sem_full_w, QUI);
  }
}

// ---------- SIGNAL HANDLER -------------
void *signal_handler(void *v){
  // inizializzo gli argomenti utili da str_arg
  signal_args *a = (signal_args *)v;
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGUSR1);
  int signo; //numero del segnale restituito dal sigwait
  while(true){
    //attendo il ricevimento di un segnale
    int e = sigwait(&mask, &signo);
    if(e != 0)
      xtermina("errore sigwait", QUI);

    if(signo == SIGINT)   //ricevo il SIGINT
      fprintf(stderr, "SIGINT: elementi presenti nella tabella: %d\n", elem_HT);

    if(signo == SIGTERM){ //ricevo il SIGTERM
      //termino i thread capolettore e caposcrittore
      if(xpthread_join(*(a->capolett),NULL,QUI) != 0)
        xtermina("errore pthread_cond_wait capolettore", QUI);
      if(xpthread_join(*(a->caposcritt),NULL,QUI) != 0)
        xtermina("errore pthread_cond_wait caposcrittore", QUI);
      fprintf(stderr, "elementi presenti nella tabella: %d\n", elem_HT);

      //dealloco la tabella hash
      hash_delete(testa_lista_entry);
      hdestroy();
      pthread_exit(NULL);
    }

    if(signo == SIGUSR1){ //ricevo il SIGUSR1
      //dealloco la tabella hash
      hash_delete(testa_lista_entry);
      hdestroy();
      //creo una nuova lista
      testa_lista_entry = NULL;
      elem_HT = 0;
      hcreate(Num_elem);
    }
  }
}


// --------------- MAIN -----------------

int main(int argc, char *argv[]) {
  // controllo di avere sia il numero di lettori che di scrittori
  if (argc != 3)
    xtermina("uso: archivio <numero scrittori> <numero lettori>", QUI);
  
  //controllo che il numero di lettori e scrittori sia >=0
  if ((atoi(argv[1]) < 0 && atoi(argv[1]) > PC_buffer_len) || (atoi(argv[2]) < 0 && atoi(argv[2]) > PC_buffer_len))
    xtermina("il numero di lettori e/o scrittori deve essere >= 0", QUI);

  // numero lettori e scrittori
  int n_lettori = atoi(argv[1]);
  int n_scrittori = atoi(argv[2]);

  //inizializzo la tabella hash
  int ht = hcreate(Num_elem);
  if (ht == 0)
    xtermina("Errore creazione Tabella Hash", QUI);

  //creazione thread lettori e scrittori e i loro capi
  pthread_t sig_handler;
  pthread_t capo_lettore;
  pthread_t capo_scrittore;
  pthread_t lettori[n_lettori];
  pthread_t scrittori[n_scrittori];

  // inizializzo la struct rwHT
  pthread_mutex_t mutex_HT = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t cond_HT = PTHREAD_COND_INITIALIZER;
  struct_rwHT.mutexHT = mutex_HT;
  struct_rwHT.condHT = cond_HT;
  struct_rwHT.writingHT = false;
  struct_rwHT.readersHT = 0;
  
  // inizializzo struct argomenti
  reader_args ra;
  writer_args wa;

  // pipe capolettore e scrittore
  ra.pipe_capolet = open("capolet", O_RDONLY);
  if (ra.pipe_capolet == -1)
    xtermina("errore apertura pipe capolet", QUI);

  wa.pipe_caposc = open("caposc", O_RDONLY);
  if (wa.pipe_caposc == -1)
    xtermina("errore apertura pipe caposc!", QUI);

  // mutex, cv e sem lettori e scrittori
  sem_t sem_full_w;
  sem_t sem_empty_w;
  sem_t sem_full_r;
  sem_t sem_empty_r;
  xsem_init(&sem_full_w, 0, PC_buffer_len, QUI);
  xsem_init(&sem_empty_w, 0, 0, QUI);
  xsem_init(&sem_full_r, 0, PC_buffer_len, QUI);
  xsem_init(&sem_empty_r, 0, 0, QUI);
  wa.sem_full_w = &sem_full_w;  
  wa.sem_empty_w = &sem_empty_w;
  ra.sem_full_r = &sem_full_r;
  ra.sem_empty_r = &sem_empty_r;

  pthread_mutex_t mutex_writer = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_t mutex_reader = PTHREAD_MUTEX_INITIALIZER;
  wa.mutex_writer = &mutex_writer;
  ra.mutex_reader = &mutex_reader;

  // buffer e indici
  wa.buffer_writer = malloc(PC_buffer_len * sizeof(char *));
  if(wa.buffer_writer == NULL)
    xtermina("errore malloc buffer scrittori", QUI);
  wa.ind_caposc = 0;
  wa.ind_write = 0;

  ra.buffer_reader = malloc(PC_buffer_len * sizeof(char *));
  if(ra.buffer_reader == NULL)
    xtermina("errore malloc buffer lettori!", QUI);
  ra.ind_capolet = 0;
  ra.ind_read = 0;

  //file log
  ra.log_r = xfopen("lettori.log", "w", QUI);
  if(ra.log_r == NULL)
    xtermina("errore apertura file log_r", QUI);

  //Inizializzo il gestore dei segnali
  sigset_t mask;
  signal_args sa;
  sa.capolett = &capo_lettore;
  sa.caposcritt = &capo_scrittore;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &mask, NULL);

  xpthread_create(&sig_handler, NULL, &signal_handler, &sa, QUI);

  // creazione thread
  if(xpthread_create(&capo_lettore, NULL, &t_capolet, &ra, QUI) != 0)
    xtermina("errore creazione capo lettore", QUI);

  if(xpthread_create(&capo_scrittore, NULL, &t_caposc, &wa, QUI) != 0)
    xtermina("errore creazione capo scrittore", QUI);

  for(int i=0; i<n_lettori; i++){
    if(xpthread_create(&lettori[i], NULL, &t_lettore, &ra, QUI) != 0)
      xtermina("errore creazione lettore", QUI);
  }

  for(int i=0; i<n_scrittori; i++){
    if(xpthread_create(&scrittori[i], NULL, &t_scrittore, &wa, QUI) != 0)
      xtermina("errore creazione scrittore", QUI);
  }

  //join thread

  for(int i=0; i<n_lettori; i++){
    if(pthread_join(lettori[i], NULL) != 0)
      xtermina("errore join lettore", QUI);
  }

  for(int i=0; i<n_scrittori; i++){
    if(pthread_join(scrittori[i], NULL) != 0)
      xtermina("errore join scrittore", QUI);
  }

  if (xpthread_join(sig_handler, NULL, QUI) != 0)
    xtermina("errore join signal handler", QUI);

  // Dealloco tutto
  for(int i=0; i<PC_buffer_len; i++){
    free(wa.buffer_writer[i]);
    wa.buffer_writer[i] = NULL;
    free(ra.buffer_reader[i]);
    ra.buffer_reader[i] = NULL;
  }
  free(wa.buffer_writer);
  free(ra.buffer_reader);
  xpthread_mutex_destroy(&mutex_writer, QUI);
  xpthread_mutex_destroy(&mutex_reader, QUI);
  xpthread_mutex_destroy(&mutex_HT, QUI);
  xsem_destroy(&sem_full_w, QUI);
  xsem_destroy(&sem_empty_w, QUI);
  xsem_destroy(&sem_full_r, QUI);
  xsem_destroy(&sem_empty_r, QUI);
  fclose(ra.log_r);
  xclose(ra.pipe_capolet, QUI);
  xclose(wa.pipe_caposc, QUI);

  return 0;
}