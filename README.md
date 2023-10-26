Relazione Progetto 

Giovanni Giuseppe Pinna [618053]

Client1:

Client1 è un programma client scritto in C che prende in input un singolo file, che grazie alla funzione getline() viene suddiviso in righe che vengono inviate al server, l'apertura avviene grazie a fopen() con l'opzione 'r' per abilitare solo la lettura. Per la connessione creo una socket, ossia un'astrazione di comunicazione tra processi, utilizzando i protocolli IPv4 (AF_INET) e connessione TCP-IP, con la struct serv_addr inserisco anche la porta e l'indirizzo di connessione. Con il comando connect() connetto la socket all'indirizzo scritto su serv.addr e inizio la comunicazione tra Client e Server, inanzitutto informo il server che la connessione è di "Tipo A", con la funzione getline() ricevo la lunghezza e la sequenza del file preso input, la lunghezza, prima di essere inviata, viene codificata con htonl(), funzione che permette che la lunghezza sia convertita in un formato standardizzato, per poi essere inviato con writen() funzione che esegue un ciclo fino a quando tutti i dati non sono stati inviati per evitare l'invio di dati incompleti o parziali. Per l'invio della sequenza ho utilizzato write(), una funzione utilizzata per scrivere dati da un buffer (line) ad un file (fd_socket). Infine in client invia al server una sequenza vuota per avvertirlo che il file è terminato, viene chiusa la connessione, il file preso in input e liberata la memoria allocata dal buffer line.

Client2: 

Molto simile al client1 a differenza che si possono ricevere un numero indefinito di file in input. In questo programma ogni file viene gestito da un thread che viene creato con xpthread_create(), funzione che permette la creazione di un thread, il nuovo thread inizia invocando task, una funzione con comportamente uguale a client1, con l'aggiunta di un readn() funzione che legge nella socket i dati ricevuti dal server e li inserisce in un buffer (nsq), in questo caso sono il numero che il server ha ricevuto durante la connessione.

Server:

A differenza dei client, il server è un programma implementato in python. All'inizio il server, nel main, apre 'caposc' e 'capolet', due unnamed pipe, ossia dei file speciali che permettono la comunicazione di thread all'interno di un processo, è da specificare che le pipe sono unidirezionali, ossia è possibile o soltanto scrivere o soltato leggere, nella funzione per la creazione della pipe os.mkfifo() oltre al nome abbiamo anche la costante "0o0666" che definisce i permessi della fifo