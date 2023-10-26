#include "rwunfair.h"

//Soluzione unfair per gli scrittori 


void read_lock(rwHT *z) {
  pthread_mutex_lock(&z->mutexHT); //uno tra i thread attivi blocca la mutex
  while (z->writingHT ==
         true) // non posso leggere finchè qualcuno sta scrivendo
    pthread_cond_wait(&z->condHT, &z->mutexHT); 
  z->readersHT++;
  pthread_mutex_unlock(&z->mutexHT);
}

void read_unlock(rwHT *z) { 
  assert(z->readersHT > 0);
  pthread_mutex_lock(&z->mutexHT);
  z->readersHT--; 
  if (z->readersHT == 0)
    pthread_cond_signal(&z->condHT); 
  pthread_mutex_unlock(&z->mutexHT);
}

void write_lock(rwHT *z) { 
  pthread_mutex_lock(&z->mutexHT);
  while (z->writingHT || z->readersHT > 0)
    //aspetta finchè non ci sono altri lettori o scrittori attivi
    pthread_cond_wait(&z->condHT, &z->mutexHT);
  z->writingHT = true; //avverto la mia scrittura
  pthread_mutex_unlock(&z->mutexHT);
}

void write_unlock(rwHT *z) {
  assert(z->writingHT);
  pthread_mutex_lock(&z->mutexHT);
  z->writingHT = false; //fine scrittura
  pthread_cond_broadcast(&z->condHT); // sveglio tutti i thread in attesa per la lettura
  pthread_mutex_unlock(&z->mutexHT);
}