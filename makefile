# definizione del compilatore e dei flag di compilazione
# che vengono usate dalle regole implicite
CC=gcc
CFLAGS=-std=c99 -Wall -g -O
LDLIBS=-lm -lrt -pthread

# elenco degli eseguibili da creare
EXECS=archivio.out client1 client2

# gli eseguibili sono precondizioni quindi verranno tutti creati
all: $(EXECS) 

# regola per la creazioni degli eseguibili utilizzando xerrori.o
%.out: %.o xerrori.o rwunfair.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

client1: client1.o xerrori.o rwunfair.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

client2: client2.o xerrori.o rwunfair.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# regola per la creazione di file oggetto che dipendono da xerrori.h
%.o: %.c xerrori.h rwunfair.h
	$(CC) $(CFLAGS) -c $<
 
# esempio di target che non corrisponde a una compilazione
# ma esegue la cancellazione dei file oggetto e degli eseguibili
clean: 
	rm -f *.o $(EXECS)
