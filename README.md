# UDP Client/Server with Selective repeat
An UDP client/server using Selective repeat for affidable transmission

Istruzioni per l’utilizzo

Lista comandi creazione Server/Client:

gcc -o client client.c  -lpthread: permette di compilare il client.
gcc -o server server.c  -lpthread: permette di compilare il server.


Lista comandi Client:

./client  <ip_address>: lancia il client connettendolo ad un server già aperto in precedenza
In seguito viene chiesto all’utente di inserire i seguenti paramenti:

N (int): dimensione della window utilizzata dal selective repeat che è condivisa con il server.
p (int): probabilità di perdita dei pacchetti e degli ack.

[0] o [1] per scegliere il tipo di timer da utilizzare (0 per statico ed 1 per adattivo)
    In caso venga scelto [0] viene chiesto all’utente di scegliere un (int) per determinare il timeout statico

Successivamente appare un menù che permette all’utente di eseguire varie funzioni:
[1]: visualizzare la lista dei file presenti nel server.
[2]: effettuare il download di file presenti nel server.
successivamente verrà chiesto all’utente di selezionare il file che desidera scaricare.
[3]: effettuare l'upload di file presenti nel client verso il server.
successivamente verrà chiesto all’utente di selezionare il file che desidera caricare.
[4]: chiude il client.


Lista comandi Server:

./server: lancia il server che risponderà con la porta in cui si è stabilita la connessione con il client.
       se risponderà “Binded” allora la connessione è stata stabilita.
       se risponderà “Not Binded” allora si è verificato un errore nella connessione.

