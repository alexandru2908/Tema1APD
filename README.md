Predescu Ioan-Alenxandru 333CB

Pentru a face tema am creat o structura in care mi-am tinut mai multe informatii despre fiecare thread, precum
id-ul, numarul total de thread-uri, grid-ul, conturul, imaginea scalata cand avem nevoie de ea, imaginea originala, o bariera si contur-ul map-ului. 
Pentru sincronizare am folosit o bariera, de fiecare data dupa o functie pe care am paraleleizat-o, am asteptat ca toate thread-urile sa termine de executat functia respectiva.

In main, am creat si initializat bariera, mi-am alocat un vector de structuri, una pentru fiecare thread. Dupa care am citit imaginea cu functia read_ppm si am initializat conturul map-ului cu functia init_contour_map.
Pentru a verifica daca imaginea initiala trebuie scala sau nu, am verificat daca dimensiunea pe x si y este mai mare decat dimensiunea maxima pe x si y. Daca da, am scalat imaginea cu functia scale_image si daca nu am pastrat-o pea cea initiala, iar apoi am creat grid-ul(mi- am creat alta functie pentru ca fiecare thread sa nu faca malloc cand intra in functie, sa fie malloc-ul facut doar o data, la fel si in cazul imaginii, daca este nevoie sa o rescalam).

In continuare, am creat inceput sa accesez fiecare structura din vectorul de structuri si am initializat campurile cu valorile corespunzatoare. Dupa care am creat thread-urile cu functia pthread_create.

In functia corespunzatoare thread-ului, am facut cast de la void* la structura mea, dupa care am verificat daca adresa imaginii initiale e diferita de scaled image, iar daca sunt am apelat functia rescale_image, al carei output l-am pastrat in atributul scaled_image, iar apoi am 
pus o bariera sa astept si celelalte thread-uri sa isi termine executia. Am continuat cu apelul functiilor sample_grid si march, punand dupa apel o alta bariera. Dupa ce toate thread-urile au terminat de executat functiile le-am dat exit.

La final in main am apelat functia de write pentru a scrie imaginea in fisier si am distrus bariera.
