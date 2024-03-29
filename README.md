# TEMA 2 - Aplicatie client-server TCP si UDP pentru gestionarea mesajelor
### Stefan Diana Maria -322CC

## SERVER:

 - Se creeaza socket-ul pentru `clientii UDP` (functia `create_bind_udp_client`) si
pentru cei `TCP` (functia `create_bind_listen_tcp_client`).
 - In vectorul `pfds` pentru ***poll*** se adauga pe prima pozitie ***file descriptor-ul de
stdin***, pe a doua cel al ***socket-ului udp***, iar pe a treia cel al ***socket-ului tcp***.

### `loop infinit`: 

 - In cazul in care este primita o ***comanda de la tastatura***, se verifica daca e 
`exit`, caz in care se trimite mesajul ***exit*** tuturor `clientilor tcp`, se inchid
***socketii***, iar programul se termina.

 - In cazul in care se primeste un mesaj de la un `client udp`, mesajul se adauga in 
`structura topicului` de care apartine.

 - In cazul in care se primeste un mesaj de pe `al treilea socket`, inseamna ca unul 
dintre `clientii tcp` s-a conectat, deci este adaugat in vectorul `pfds`.

 - In cazul in care se primeste ceva de la oricare dintre clientii tcp (***elementele 
de la poitiile > 3*** in `pfds`), se verifica daca este vorba de o ***deconectare a 
clientului*** si se elemina din vectori. Pentru mesajele de `subscribe` / `unsubscribe` 
se procedeaza corespunzator. 

 - La final, trimit mesajele care nu au fost inca trimise. => Se trimite prima data 
***dimensiunea mesajului***, iar apoi mesajul in sine (`structura msg`).

 - `Structura msg` contine: ***dimensiunea bufferului*** ce a fost trimis de udp, ***structura 
clientului udp***, ***topicul***, iar apoi ***bufferul***.

 - `Structura topic` contine: ***topicul***, ***o lista de mesaje*** ce apartin topicului, ***numarul 
mesajelor*** din lista, ***un vector de subscriberi*** si ***numarul lor***.

 - `Structura unui subscriber` contine: ***fd-ul socketului clientului tcp***, ***indexul din 
lista de mesaje*** pana la care au fost trimise, daca ***clientul este conectat sau nu***, 
***sf-ul*** si ***id-ul clientului*** respectiv.

 - Daca clientul nu doreste optiunea de `store-and-forward`, in mometul in care a fost 
deconectat si se reconecteaza, campul `sent` al structurii se actualizeaza cu 
***numarul de mesaje*** din lista, deoarece mesajele de trimit de la `sent` incolo.

 - Mesajele se trimit doar clientilor care au campul `active` setat pe `1`.

**********************************************************************************

## TCP_CLIENT:

 - Clientul se conecteaza la server, iar apoi in vectorul `pfds` pentru `poll` se 
adauga pe prima pozitie ***file descriptor-ul de stdin***, pe a doua cel al ***socket-ului 
serverului***.

### `loop infinit`: 

 - In cazul in care a fost scris un mesaj de la tastatura, daca comanda este `exit`
se inchide socket-ul, iar altfel se trimite la server, care urmeaza sa verific 
daca comanda este `subscribe` / `unsubscribe`.

 - In cazul in care mesajul este primit de la ***server***, se salveaza ***dimensiunea*** ce 
urmeaza sa fie primita, dupa care se primeste `structura msg`.
Aceasta este data ca parametru functiei `complete_message`, care parseaza 
continutul si afiseaza output-ul corect.
