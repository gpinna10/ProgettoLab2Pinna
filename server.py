#!/usr/bin/env python3
import socket, struct, socket, sys, os, signal, argparse, logging, subprocess
from concurrent.futures import ThreadPoolExecutor

# valori default host e porta
HOST = "127.0.0.1"  # Standard loopback interface address (localhost)
PORT = 58053  # Port to listen on (non-privileged ports are > 1023)

#Inizializzaione globale della variabile del sottoprocesso archivio e della socket del server
archivio_subprocess = None
server = None

#funzione che fa partire il sottoprocesso archivio passandogli il numero di lettori e scrittori senza valgrind
def archivio(lettori, scrittori):
    global server, archivio_subprocess
    archivio_subprocess = subprocess.Popen(['./archivio.out', str(lettori), str(scrittori)])

#funzione che fa partire il sottoprocesso archivio passandogli il numero di lettori e scrittori con valgrind
def valgrind_archivio(lettori, scrittori):
    global server, archivio_subprocess
    archivio_subprocess = subprocess.Popen(["valgrind","--leak-check=full", "--show-leak-kinds=all",  "--log-file=valgrind-%p.log", "./archivio.out", str(lettori), str(scrittori)])

# funzione per la gestione della chiusura del server quando ricevo un SIGINT (CTRL+C)
def shutdown_handler(signum, frame):
  global server

   # chiudo il server
  server.shutdown(socket.SHUT_RDWR)
  server.close()
  
  # Chiudo le pipe caposc e capolet
  if os.path.exists("caposc"):
    os.unlink("caposc")
  if os.path.exists("capolet"):
    os.unlink("capolet")

  # invio ad archivio il SIGTERM
  archivio_subprocess.send_signal(signal.SIGTERM)
 
  exit(0)

def recv_all(conn,n):
  chunks = b''
  bytes_recd = 0
  while bytes_recd < n:
    # riceve blocchi di al più 1024 byte
    chunk = conn.recv(min(n - bytes_recd, 1024))
    if len(chunk) == 0:
      raise RuntimeError("socket connection broken")
    chunks += chunk
    bytes_recd = bytes_recd + len(chunk)
    assert bytes_recd == len(chunks)
  return chunks

def gestisci_connessione(conn,addr,capolet,caposc):
  tipo = conn.recv(6).decode('utf-8')
  if tipo == "Tipo A": # gestisco la connessione di tipo A
    byte_inviati = 0
    while True:
      len_byte = recv_all(conn,4)
      #controllo che non sia la fine della connessione
      if len_byte == b'\x00\x00\x00\x00':
        break
      len_s= struct.unpack("!i",len_byte[:4])[0]
      seq = recv_all(conn,len_s)
      # codifico la sequenza in byte del messaggio in 4 byte
      len_byte = struct.pack("i",len_s)
      # invio tramite pipe capolet la lunghezza
      os.write(capolet,len_byte)
      # invio ora la sequenza
      os.write(capolet,seq)
      # aggiorno il numero di byte inviati durante la connessione
      byte_inviati += len(seq)
      byte_inviati += len(len_byte)
    #ho terminato la connessione, scrivo nel file log in numero di byte inviati
    logging.info("byte inviati a capolet: %d", byte_inviati)
    conn.close() # chiudo la connessione
  elif tipo == "Tipo B":
    byte_inviati = 0
    n_seq = 0
    while True:
      #ricevo una sequenza di byte
      len_byte = recv_all(conn,4)
      #controllo che non sia la fine della connessione
      if len_byte == b'\x00\x00\x00\x00':
        #invio il numero di sequenze ricevute al client
        conn.sendall(struct.pack("!i",n_seq))
        break
      #ricevo la lunghezza del messaggio
      len_s= struct.unpack("!i",len_byte[:4])[0]
      #ricevo il messaggio
      seq = recv_all(conn,len_s)
      #codifico la sequenza in byte del messaggio in 4 byte
      len_byte = struct.pack("i",len_s)
      #invio la lunghezza in byte al caposcrittore di archivio
      os.write(caposc,len_byte)
      #invio il messaggio al caposcrittore di archivio
      os.write(caposc,seq)
      # aggiorno il numero di byte inviati durante la connessione
      byte_inviati += len(seq)
      byte_inviati += len(len_byte)
      n_seq = n_seq + 1
    # ho terminato la connessione, scrivo nel file log in numero di byte inviati
    logging.info("byte inviati a caposc: %d", byte_inviati)
    conn.close() # chiudo la connessione

def main(Pool_size,lettori,scrittori,valgrind,host=HOST,port=PORT):
  #dichiaro globalmente la variabile server per essere utilizzata nella funzione shutdown_handler
  global server

  # creo le pipe per caposcrittore e capolettore, se non esistono
  if not os.path.exists("caposc"):
    os.mkfifo("caposc",0o0666)

  if not os.path.exists("capolet"):
    os.mkfifo("capolet",0o0666)

  #se leggo da linea di comando -v faccio partire archivio con valgrind
  if valgrind:
    valgrind_archivio(lettori, scrittori)
  else:
    archivio(lettori, scrittori)

  # apro le pipe
  capolet = os.open("capolet",os.O_WRONLY)
  caposc = os.open("caposc",os.O_WRONLY)

  # inizializzo un file di log dove scrivere il numero di byte inviati
  logging.basicConfig(filename='server.log', level=logging.INFO, format='%(message)s')

  # inizializzo una threadpool
  pool = ThreadPoolExecutor(Pool_size)

  # creazione del server
  server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
  server.bind((host, port))
  server.listen()

  # chiudo il server con la funzione shutdown_handler quando ricevo un SIGINT (CTRL+C)
  signal.signal(signal.SIGINT, shutdown_handler)

  while True:
    # accetto una nuova richiesta di connessione
    conn, addr = server.accept()
    # gestisce la connessione
    pool.submit(gestisci_connessione(conn,addr,capolet,caposc), conn)  

if __name__ == '__main__':
    #Gestione parametri dati la linea di comando
    parser = argparse.ArgumentParser(description='./server.py n_thread n_lettori n_scrittori valgrind(Opzionale)')
    parser.add_argument("n_thread", type=int, help="Numero dei thread nel pool")
    parser.add_argument("-r", "--t_lettori", type=int, default=3, help="Thread lettori della funzione archivio (Default: 3)")
    parser.add_argument("-w", "--t_scrittori", type=int, default=3, help="Thread scrittori della funzione archivio (Default: 3)")
    parser.add_argument("-v", "--valgrind", action="store_true", help="Utilizzo di valgrind (Opzionale)") 
    
    #Parsing dei parametri
    args = parser.parse_args() 

main(args.n_thread, args.t_lettori, args.t_scrittori, args.valgrind)
