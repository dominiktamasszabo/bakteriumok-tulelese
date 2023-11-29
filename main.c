#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>


//az econio.h teszi lehetővé, hogy megváltoztassam a betűk háttérszínét
#include "econio.h"

//#include "debugmalloc.h"

//const int seed=70421;

//Itt olyan konstansok vannak deklarálva, amiket később használunk különböző függvényekben
//olyan globális változók, amik a rendszer környezetét szabályozzák
const int foodToReproduce = 2;
const double fertilityChance=0.5;
const double graphEdgeChance = 0.1;
const double mutationChance = 0.5;

//Globális változó a logolásért felelős file pointer
FILE* logstream=NULL;

typedef unsigned int uint;

//Listák
typedef struct bacteria
{
    uint id;                //csak egy szam, ami minden egyes bakteriumra kulonbozo.
    uint ancestor;          //annak a bakteriumnak az id-je, akinek szaporodasa altal lett
    uint grand_ancestor;    //legrégebbi os id-je

    //allapotjelzok
    int isAlive; //eppen eletben van-e
    int isActive; //eppen aktív-e
    uint age;  //a bakterium kora
    uint inventory; //mennyi étel van nála
    uint decessors; //ez csak a nulladik napi bakteriumok szempontjabol relevans informacio sajnos:///

    char genus[25];
    char species[25];       //nemzetseg, faj
         
    uint max_lifespan;      //az az eletkor napban, amikorra biztos meghal a bakterium

    //(0-1) intervallumra lesz az osszes coef normalizalva, ez elvaras.
    double aggression_coef;	//segit kiszamolni, milyen esellyel tamad meg masikat
    double altruism_coef;	//segit kiszamolni, milyen esellyel segit egy tarsanak
    double vision_coef;     //segit kiszamolni, milyen konnyen talal etelt az adott bakterium

    double activeness_coef; //segit kiszamolni, milyen esellyel indul utnak adott napon a bakterium

    uint strength; //segit kiszamolni bakteriumok harcanak az eredmenyet

    uint q_altruism; //hasonlo szamok nagyobb esellyel segitenek egymasnak

} bacteria;

typedef struct _baclistnode{
bacteria b; //bakterium adatait tartalmazo struktura
struct _edgelistnode *meetings; //talalkozasok listara mutato pointer
struct _baclistnode *next; //kovetkezo elemre mutato pointer
} baclistnode;

typedef struct _edgelistnode{
baclistnode *d; //annak a baktériumnak a pointere, amelyikkel találkozot az a baktérium, amelyiknek a találkozáslistájában található ez a listaelem
struct _edgelistnode *next; //következő találkozás pointere
} edgelistnode;

typedef struct _fajlistnode{
    char genus[25]; //meg mindig nem tudom
    char species[25];
    uint db; //hány darab él az adott fajból
    int color; //ez az econios meno szines cucchoz kell:)
    struct _fajlistnode *next; //következő faj pointere
} fajlistnode;

int sProb(double p) //p valoszínűségű bool
{
    return ((double)rand()/(double)RAND_MAX)<=p;
}


//KÜLÖNBÖZŐ VALÓSZÍNŰSÉGEKET SZÁMOLÓ FÜGGVÉNYEK
double calcWinChance(double x) //kiszámolja a nyerési esélyt
{
    return (-1)/(x+1)+1;
}

double calcAgingDeathChance(uint age, uint lifespan) //kiszámolja az öreg baktériumok elpusztásának esélyét
{
    return (1)/(double)(lifespan-age+1);
}

double calcMeetChance(uint c) //Két baktérium találkozásának esélye
{
    uint count = c;
    if(count < 5) return graphEdgeChance;
    else
    {
        return ((2*5)/((double)(count)*(count-1))); //Ez abból a képletből származik, hogy n csúcsú teljes gráf éleinek száma n(n-1)/2
    }
}

double calcActiveChance(double actcoef) //Kiszámolja, milyen eséllyel lesz aktív egy baktérium az adott napon.
{
    return actcoef;
}

double calcAttackChance(double agrcoef) //kiszámolja, milyen eséllyel támad
{
    return agrcoef;
}

double calcFoodFindChance(double vision) //kiszámolja, milyen eséllyel talál ételt
{
    return 0.5+vision/2;
}

double calcAltruismChance(baclistnode *giver, baclistnode *taker) //kiszámolja, mekkora eséllyel lesz segítőkész egy adott baktérium egy másikkal szemben
{
    return giver->b.altruism_coef/(0.2*fabs(giver->b.q_altruism-taker->b.q_altruism)+1);
    
}

//LISTAKEZELŐ FÜGGVÉNYEK
baclistnode* ujBacList() //új elölstrázsás üres baktériumlistát hoz létre
{
    baclistnode* ujlista = (baclistnode*)malloc(sizeof(baclistnode)); //STRAZSA
    ujlista->meetings=NULL;
    ujlista->next=NULL;
    return ujlista;
}

edgelistnode* ujEdgeList() //új elölstrázsás üres találkozáslistát hoz létre
{
    edgelistnode* ujlista = (edgelistnode*)malloc(sizeof(edgelistnode)); //STRAZSA
    ujlista->next=NULL;
    return ujlista;
}

fajlistnode* ujFajList() //új elölstrázsás üres fajlistát hoz létre
{
    fajlistnode* ujlista = (fajlistnode*)malloc(sizeof(fajlistnode)); //STRAZSA
    ujlista->next=NULL;
    return ujlista;
}

void freeEdgeList(edgelistnode *listToFree) //Találkozáslisták felszabadítására használt függvény
{
    if(listToFree==NULL) //ez itt biztonsági okokból van, így kiszűröm azokat az eseteket, amikor a semminek next-jét keressük.
    {
        return;
    }
    while(listToFree->next!=NULL) //ez a strázsát meghagyja amúgy, direkt (nem lenne jó mindig újat csinálni ugyanis)
    {
        edgelistnode *tmp;
        tmp = listToFree->next;
        listToFree->next=listToFree->next->next;
        free(tmp);
    }
}

void freeBaclist(baclistnode *listToFree) //A baktérium lista felszabadítására használt függvény
{
    while(listToFree!=NULL) //amíg nincs vége
    {
        baclistnode *tmp=listToFree; //megjegyezzük az első listaelemet
        if(listToFree->meetings!=NULL) //Amennyiben létezik, fel kell szabadítani a találkozások listáját is.
        {
            freeEdgeList(listToFree->meetings);
            free(listToFree->meetings);
        }
        listToFree=listToFree->next; //Léptetjük a listát
        free(tmp); //Felszabadítjuk a régi első elemet.
    }
}

void freeFajList(fajlistnode *listToFree) //Fajlisták felszabadítására használt függvény
{
    while(listToFree!=NULL)
    {
        fajlistnode *tmp = listToFree;
        listToFree=listToFree->next;
        free(tmp);
    }
}


void pushBac(baclistnode *list, bacteria b, uint id) // Új baktériumot szúr be egy lista elejére. Az id-t is megkapja, ez a következő elérhető baktérium id.
{
    baclistnode *newNode = (baclistnode*)malloc(sizeof(baclistnode));
    newNode->b=b;
    newNode->b.id=id;
    newNode->b.decessors=0;
    newNode->b.inventory=1;
    

    //meeting lista bele:
    newNode->meetings=ujEdgeList();

    newNode->next=list->next;
    list->next=newNode;
}

void pushEdge(edgelistnode *list, baclistnode *bac) //Új találkozást hoz létre egy találkozáslista elején
{
    edgelistnode *newNode = (edgelistnode*)malloc(sizeof(edgelistnode));
    newNode->d=bac;
    newNode->next=list->next;
    list->next=newNode;
}

void pushFaj(fajlistnode *list, char *genus, char *species, int *coloroffset) //Új fajt szúr be egy fajlista elejére. Megadjuk a függvénynek a megfelelő adatokat, + coloroffset pointert
                                                                              //ami az econio.h-s színét segít meghatározni.
{
    fajlistnode *newNode = (fajlistnode*)malloc(sizeof(fajlistnode));
    strcpy(newNode->genus, genus); //bemásoljuk a stringeket
    strcpy(newNode->species, species);
    newNode->db = 0; //nullázzuk a darabot
    newNode->color = (*coloroffset)%12+2; //egy színt rendelünk a fajhoz.
    (*coloroffset)++;
    newNode->next=list->next;
    list->next=newNode;
}

void generate_meetings(baclistnode *baclist, uint c) //Ez a függvény legenerálja a találkozáslistát minden baktériumhoz egy listában
{

    for(baclistnode *it1=baclist->next;it1!=NULL;it1=it1->next) //bejárjuk a baktériumlistát
    {
        if(it1->b.isAlive && it1->b.isActive) //Akkor foglalkozunk a baktériummal, ha él és aktív
        {
            freeEdgeList(it1->meetings); //felszabadítjuk a régi találkozáslistáját, ugyanis az már nem kell
            for(baclistnode *it2=it1->next;it2!=NULL;it2=it2->next) //bejárjuk a baktériumlistát mégegyszer
            {
                if(it2->b.isAlive && it2->b.isActive && sProb(calcMeetChance(c))) //élő és aktív baktériumokkal valamekkora eséllyel találkozik
                {
                    pushEdge(it1->meetings, it2);
                }
            }
        }
    }
}

void removeDeadBacs(baclistnode *list) //a halott, nem nulladik-napi baktériumokat láncolja ki a listából és felszabadítja azokat, ez a program optimalizálását szolgálja
{
    for(baclistnode *it= list;it->next!=NULL;it=it->next)
    {
        if(it->next->b.grand_ancestor!=0 && it->next->b.isAlive==0)
        {
            baclistnode *tmp = it->next;
            it->next=it->next->next;
            if(tmp->meetings!=NULL)
            {
                freeEdgeList(tmp->meetings);
                free(tmp->meetings);
            }
            free(tmp);
        }
    }
}

//Beolvasó függvények
void scanBac(bacteria *toWrite) // beolvas egy baktériumot standard inputról
{
    //scanf("%d", &toWrite->id);
    scanf("%s%s", &toWrite->genus, &toWrite->species);
    scanf("%u%u", &toWrite->age, &toWrite->max_lifespan);
    scanf("%lf%lf%lf%lf", &toWrite->aggression_coef,&toWrite->altruism_coef, &toWrite->vision_coef, &toWrite->activeness_coef);
    scanf("%u", &toWrite->strength);
    scanf("%u", &toWrite->q_altruism);

    toWrite->id=0;
    toWrite->isAlive=1;
    toWrite->grand_ancestor=0;
    toWrite->ancestor=0;
}

bacteria fscanBac(FILE* fstream, int *X) //beolvas egy baktériumot fileból, paraméterlistán visszaadja, sikeres volt-e, ha nem akkor 1
{
    bacteria new;
    if(fscanf(fstream, "%s%s", &new.genus, &new.species)<2)  {*X=1;} //ha nem, akkor 1
    fscanf(fstream, "%u%u", &new.age, &new.max_lifespan); 
    fscanf(fstream, "%lf%lf%lf%lf", &new.aggression_coef,&new.altruism_coef, &new.vision_coef, &new.activeness_coef);
    fscanf(fstream, "%u", &new.strength);
    fscanf(fstream, "%u", &new.q_altruism);

    new.id=0;//alapértelmezett baktérium adatok
    new.isAlive=1;
    new.grand_ancestor=0;
    new.ancestor=0;

    return new;
}


//Kiíró függvények
void printBac(bacteria toPrint) //Kiír egy baktériumot releváns adataival.
{
    printf("________________________________________\n");
    printf("ID: %u\n", toPrint.id);
    printf("Genus, species: %s %s \n", toPrint.genus, toPrint.species);
    printf("Anyja : %u\nOsanyja: %u\n", toPrint.ancestor, toPrint.grand_ancestor);
    printf("Elo: ");
    if(toPrint.isAlive) printf("igen\n"); else printf("nem\n");

    printf("Kora: %u/%u\n", toPrint.age, toPrint.max_lifespan);
    printf("Tulajdonsagok:\n\taggresszio: %.5f\n\tsegitokeszseg: %.5f\n\tkeresokeszseg: %.5f\n\taktivitas: %.5f\n", toPrint.aggression_coef, toPrint.altruism_coef, toPrint.vision_coef, toPrint.activeness_coef);
    printf("\tHarci allando: %u\n", toPrint.strength);
    printf("\tMegkulonboztetesi allando: %u\n", toPrint.q_altruism);
    printf("________________________________________\n");
}

void printBacList(baclistnode *l) //Kiírja a baktériumlistát, debugolásra volt használva.
{
    printf("\n");
    for(baclistnode *it=l->next;it!=NULL;it=it->next)
    {
        printf("%u %u | %.2f %.2f %.2f %.2f | %s %s %u | %d \n", it->b.id, it->b.grand_ancestor, it->b.aggression_coef, it->b.activeness_coef, it->b.altruism_coef, it->b.vision_coef, it->b.genus, it->b.species, it->b.id, it->b.isAlive);
        //printf(".%u.\t%s\t%s\t%u\t%u\t%u/%u\t\t\t\t%f\t%f\t%f\t%f\n", it->b.grand_ancestor, it->b.genus, it->b.species, it->b.id, it->b.isAlive, it->b.age, it->b.max_lifespan, it->b.aggression_coef, it->b.activeness_coef, it->b.altruism_coef, it->b.vision_coef);
    }
}

void printMeetings(baclistnode *l) //Kiírja a találkozáslistákat, debugolásra volt használva.
{
    for(baclistnode *it=l->next;it!=NULL;it=it->next)
    {
        printf("%u :: ", it->b.id);
        
        if(it->b.isAlive==0) printf("DEAD");
        else if(it->b.isActive==0) printf("INACTIVE");
        else
        for(edgelistnode *ei=it->meetings->next;ei!=NULL;ei=ei->next)
        {
            printf("%u, ", ei->d->b.id);
        }
        printf("\n");
    }
}

void printFajlist(fajlistnode *l) //Kiírja a fajlistát, debugolásra volt használva.
{
    for(fajlistnode *it=l->next;it!=NULL;it=it->next)
    {
        printf("%s %s\t%u\t%d\n", it->genus, it->species, it->db, it->color);
    }
}

void printColorTable(fajlistnode *fajlist) //Kiírja a jelmagyarázatot
{
    printf("\n"); //üres sor
    if(fajlist == NULL) return; //üres listát hiba
    for(fajlistnode *faj = fajlist->next;faj!=NULL;faj=faj->next)
    {
        printf(" ");
        econio_textbackground(faj->color);
        printf("  ");
        econio_textbackground(16);
        printf(" : %s %s (%u)\n\n", faj->genus, faj->species, faj->db);
    }
}



//statisztika pl.

void countSpecies(baclistnode *baclist, fajlistnode *fajlist) //megszámolja egy baktériumlistában, hogy a fajokból hány darab élő egyed van
{
    if(fajlist == NULL || baclist==NULL) return; //hibaszűrés

    for(fajlistnode *faj = fajlist->next;faj!=NULL;faj=faj->next) //bejárjuk a fajlistát
    {
        //
        uint db = 0; //Megszamoljuk, hany db van a fajbol.
        for(baclistnode *it = baclist->next;it!=NULL;it=it->next)
        {
            if(it->b.isAlive)
            {
                if(strcmp(faj->genus, it->b.genus) == 0 && strcmp(faj->species, it->b.species) == 0)
                {
                    db++;
                }
            }
        }
        faj->db=db; //beleírjuk a db változóba
    }
}

void colorfulEconioStats(baclistnode *baclist, int res, uint expected_max,  fajlistnode *fajlist) //Ez a függvény írja ki a színes diagramot.
{
    if(fajlist == NULL || baclist==NULL) return;
    countSpecies(baclist, fajlist);

    uint osszes = 0; //összeszámoljuk, hány db élő egyed van összesen
    for(fajlistnode *faj = fajlist->next;faj!=NULL;faj=faj->next)
    {
        osszes+=faj->db;
    }

    if(res==0) //ha 0-t adtak meg, akkor az alapértelmezett felbontás 100 karakter
    {
        res=100;
    }

    if(expected_max==0) //ha 0-t adtak meg, akkor az egyedek számára normalizálunk
    {
        expected_max=osszes;
    }

    for(fajlistnode *faj = fajlist->next;faj!=NULL;faj=faj->next) //minden fajhoz tartozik egy szín és valamennyi karakter
    {
        if(faj->db>0)
        {
            econio_textbackground(faj->color); //Ez a sor átállítja a szöveg háttérszínét a faj-t reprezentáló színre
            int chars = round((double)res*(double)(faj->db)/(double)(expected_max)); //Fajt reprezentáló karakterek számának kiszámolása
        
            for(int i=0;i<chars;i++)
            {
                printf(" "); 
            }
        }
        
    }
    econio_textbackground(16); //Visszaallitjuk a console szinet
    printf("\n");//uj sor:D
}

uint currentlyAlive(baclistnode *list) //megszámolja, hogy egy baktériumlilstában hány db élő egyed van
{
    uint db=0;
    for(baclistnode *it=list->next;it!=NULL;it=it->next)
    {
        if(it->b.isAlive) db++;
    }
    return db;
}

void printMaxpopSpecies(fajlistnode *fajlist) //Kiírja, hogy mennyi volt a legtöbb élő egyeddel rendelkező faj.
{
    if(fajlist==NULL) return;
    uint maxdb=fajlist->next->db; //Maximum meghatározása
    for(fajlistnode *it=fajlist->next->next;it!=NULL;it=it->next)
    {
        if(maxdb<it->db)
        {
            maxdb=it->db;
        }
    }

    printf("A legtobb egyeddel rendelkezo fajnak %u egyede volt.\n", maxdb); //Kiírjuk azokat, amelyiknek annyi egyede volt, mint a maximum
    if(maxdb>0) //A 0 egyed nem valami erdekes.
    {
        char index = 'a';
        for(fajlistnode *it=fajlist->next;it!=NULL;it=it->next)
        {
            if(maxdb==it->db)
            {
                printf("\t%c. %s %s\n", index++, it->genus, it->species); //betűzzük is őket, 'a'-tól
            }
        }
    }
}

void printMostSuccesfulBac(baclistnode *list) //kiírjuk a legsikeresebb nulladik napi baktériumot/baktériumokat
{
    printf("\n");
    
    for(baclistnode *it_zero=list->next;it_zero!=NULL;it_zero=it_zero->next) //bejárjuk a baktériumlistát,
    {
        if(it_zero->b.grand_ancestor==0) //Nulladik napi baktérium esetén
        {
            it_zero->b.decessors=0;
            for(baclistnode *it=list->next;it!=NULL;it=it->next) //bejárjuk a listát még egyszer 
            {
                if(it->b.grand_ancestor==it_zero->b.id && it->b.isAlive) //és növeljük a leszármazottak számát, ha az adott nulladik-napi baktérium leszármazottja
                {
                    it_zero->b.decessors++;
                }
            }
        }
    }

    uint maxdb=0; //Megkeressük ezeknek leszármazottszámoknak a maximumát
    for(baclistnode *it_zero=list->next;it_zero!=NULL;it_zero=it_zero->next)
    {
        if(it_zero->b.grand_ancestor==0 && maxdb<it_zero->b.decessors)
        {
            maxdb=it_zero->b.decessors;
        }
    }

    printf("A legtobb elo leszarmazottal rendelkezo 0. napi bakteriumnak %u elo leszarmazottja van.\n", maxdb);
    if(maxdb>0) //A 0 egyed nem valami erdekes.
    {
        char index = 'a';
        for(baclistnode *it_zero=list->next;it_zero!=NULL;it_zero=it_zero->next) //kiírjuk azokat, amelyeknek annyi leszármazottja van, mint a maximum.
        {
            if(it_zero->b.decessors==maxdb)
            {
                printf("\t%c. %s %s (id: %u)\n", index++, it_zero->b.genus, it_zero->b.species, it_zero->b.id);
            }
        }
    }
}


void printMinMaxBacProperties(baclistnode *list) //A változók intervallumjait számolja ki
{
    //Aggression, altruism, vision, activeness, strength
    double minAgr=2, minAlt=2, minVis=2, minAct=2;
    double maxAgr=-2, maxAlt=-2, maxVis=-2, maxAct=-2;

    uint minStrength = UINT_MAX, maxStrength=0;


    for(baclistnode *it=list->next;it!=NULL;it=it->next) //Megkeressük a változók mimimumját és maximumjait, a szokásos módon, külön külön.
    {
        if(it->b.isAlive==1)
        {
            if(minAgr>it->b.aggression_coef)
            {
                minAgr=it->b.aggression_coef;
            }
            if(minAlt>it->b.altruism_coef)
            {
                minAlt=it->b.altruism_coef;
            }
            if(minVis>it->b.vision_coef)
            {
                minVis=it->b.vision_coef;
            }
            if(minAct>it->b.activeness_coef)
            {
                minAct=it->b.activeness_coef;
            }


            if(maxAgr<it->b.aggression_coef)
            {
                maxAgr=it->b.aggression_coef;
            }
            if(maxAlt<it->b.altruism_coef)
            {
                maxAlt=it->b.altruism_coef;
            }
            if(maxVis<it->b.vision_coef)
            {
                maxVis=it->b.vision_coef;
            }
            if(maxAct<it->b.activeness_coef)
            {
                maxAct=it->b.activeness_coef;
            }

            if(minStrength>it->b.strength)
            {
                minStrength=it->b.strength;
            }
            if(maxStrength<it->b.strength)
            {
                maxStrength=it->b.strength;
            }
        }
        
    }

    printf("\n"); //majd kiírjuk azokat
    printf("Agresszio:     %f - %f\n", minAgr, maxAgr);
    printf("Segitokeszseg: %f - %f\n", minAlt, maxAlt);
    printf("Kereses:       %f - %f\n", minVis, maxVis);
    printf("Aktivsag:      %f - %f\n", minAct, maxAct);
    printf("Ero:           %8u - %8u\n", minStrength, maxStrength);


}


//A SZIMULÁCIÓ KÖZPONTI FÜGGVÉNYEI
void mutate_coef(double *coef) //egy adott 'coef' szerű, 0-1 közötti változó mutációjának szimulálása
{
    int dir = -1+2*sProb(0.5); // most eldontjuk, hogy nagyobb, vagy kisebb lesz az uj ertek
    while(0<=(*coef)+dir*(0.004) && 1>=(*coef)+dir*(0.01) && sProb(mutationChance)) //Addig noveljuk/csokkentjuk a szam erteket, amig a valoszinuseg engedi, es 0<=szam<=1
    {
        *coef += dir*0.004;
    }
}

void harc(baclistnode *tamado, baclistnode *vedo) //Harc szimulálása két baktérium között
{
    
    if(tamado!=NULL && vedo!=NULL) //biztonsagi ellenorzes, hogy nem ker-e a fuggvenyhivo olyat, amit nem kellene
    {
        fprintf(logstream, "HARC :: %s %s (%u)::%s %s (%u) :: EREDMENY: ", tamado->b.genus, tamado->b.species, tamado->b.id, vedo->b.genus, vedo->b.species, vedo->b.id);
        uint t = tamado->b.strength, v = vedo->b.strength; //t mint tamado, v mint vedo.

        double x; //kiszamoljuk, mekkora legyen az erosebb fel altali nyereseg.
        if(t>v) {x=(double)t/(double)v;}
        else    {x=(double)v/(double)t;}
        //Nyer-e a?

        int sors=sProb(calcWinChance(x)); //uj valtozoba tesszuk a harc eredmenyet gyakorlatilag
        if((sors && t>v)  || (!sors && !(t>v))) //A támadó nyer, ha sors = 1 és a támadó erősebb volt, vagy a védő erősebb és sors=0
        {
            vedo->b.isAlive=0; //védo meghal.

            tamado->b.inventory+=vedo->b.inventory+1; //A nyertes megkapja a vesztes ételét (+1, mert úgy döntöttem, hogy maga az elejtett egyed is 1 ételt ér.)
            vedo->b.inventory=0; //kinullázom a vesztes ételét


            fprintf(logstream, "%s %s (%u) nyert!\n", tamado->b.genus, tamado->b.species, tamado->b.id); //Log
            fprintf(logstream, "HALAL :: %s %s (%u) meghalt. INDOK: HARC\n", vedo->b.genus, vedo->b.species, vedo->b.id);
        }
        else 
        {

            tamado->b.isAlive=0; //tamado meghal

            vedo->b.inventory+=tamado->b.inventory+1;//A nyertes megkapja a vesztes ételét (+1, mert úgy döntöttem, hogy maga az elejtett egyed is 1 ételt ér.)
            tamado->b.inventory=0;//kinullázom a vesztes ételét

            fprintf(logstream, "%s %s (%u) nyert!\n", vedo->b.genus, vedo->b.species, vedo->b.id); //Log
            fprintf(logstream, "HALAL :: %s %s (%u) meghalt. INDOK: HARC\n", tamado->b.genus, tamado->b.species, tamado->b.id);
        }
    }
}

void altruism(baclistnode *giver, baclistnode *taker) //Végrehajtuk a segítséget - valaki ételt ad a másiknak
{
    fprintf(logstream, "SEGITSEG :: %s %s (%u) etelt adott neki: %s %s (%u)\n" , giver->b.genus, giver->b.species, giver->b.id, taker->b.genus, taker->b.species, taker->b.id);
    giver->b.inventory--; //annak aki adott, kevesebb lesz
    taker->b.inventory++; //annak aki kapott, több
}

void agingDeaths(baclistnode *list) //Öregedést feldolgozó függvény
{
    for(baclistnode *it=list->next;it!=NULL;it=it->next)
    {
        if(it->b.isAlive==1) //élő baktériumokat vizsgálunk
        {
            it->b.age++; //Növeljük a korukat
            if(sProb(calcAgingDeathChance(it->b.age, it->b.max_lifespan))) //Függvényben meghatározott eséllyel pedig feldolgozzuk a halálukat
            { 
            it->b.isAlive=0;
            fprintf(logstream, "HALAL :: %s %s (%u) meghalt. INDOK: TERMESZETES\n", it->b.genus, it->b.species, it->b.id); //Logoljuk
            }
        }
    }
}

void foodDistribution(baclistnode *list, uint * foodreserve, uint dailyfood) //Ételosztásért felelős függvény
{
    for(baclistnode *it=list->next;it!=NULL && *foodreserve>0;it=it->next) //bejárjuk a listát
    {
        if(it->b.isAlive && it->b.isActive) //élő, aktív baktériumok találhatnak ételt
        {
            if(*foodreserve>0 && sProb(calcFoodFindChance(it->b.vision_coef))) //Ha van még étel, és megtalálja (függvényben foglalt eséllyel)
            {
                it->b.inventory+=2; //akkor nő a nála lévő étel száma
                (*foodreserve)--; //a maradék pedig csökken
            }
        }
    }
}

void process_activity(baclistnode *list) //Ez a függvény dolgozza fel, hogy aktív lesz-e egy baktérium az adott napon
                                        //Ha nem aktív, nem kerül ételbe élnie (kicsit olyan, mintha téli álmot aludna), de nem is szaporodhat, ez egy alternatív stratégia lehet
{
    for(baclistnode *it=list->next;it!=NULL;it=it->next) //bejárjuk a listát
    {
        if(it->b.isAlive) //ha él a baktérium
        {
            it->b.isActive=sProb(calcActiveChance(it->b.activeness_coef)); //Függvényben foglalt valószínűséggel lesz aktív
            fprintf(logstream, "AKTIVITAS :: %s %s (%u) mostantol %d\n", it->b.genus, it->b.species, it->b.id, it->b.isActive); //logoljuk
        }
    }
}

void process_meetings(baclistnode *list) //Ez a függvény dolgozza fel a baktériumok közti interakciókat
{
    for(baclistnode *it=list->next;it!=NULL;it=it->next) //bejárjuk a listát
    {
        if(it->b.isAlive && it->b.isActive) //ha él a vizsgált baktérium
        {
            for(edgelistnode *ei=it->meetings->next;ei!=NULL;ei=ei->next) //bejárjuk a találkozásait is
            {
                if(it->b.inventory==0 || sProb(calcAttackChance(it->b.aggression_coef))) //Függvényben foglalt eséllyel támadás indul, vagy ha éhezik
                {
                    harc(it, ei->d); //Ekkor harcolni kezdenek.
                }
                if(it->b.inventory>0 && sProb(calcAltruismChance(it, ei->d))) //Amennyiben van étele, adhat egy másiknak, függvényben foglallt eséllyel
                {
                    altruism(it, ei->d);
                }
            }
            freeEdgeList(it->meetings); //már nem kell, tehát felszabadítjuk.
        }
    }
}

void process_reproduction(baclistnode *list, uint *availableId) //Szaporodást feldolgozó függvény
{
    for(baclistnode *it=list->next;it!=NULL;it=it->next) //bejárjuk a baktériumlistát
    {
        if(it->b.isAlive && it->b.isActive && it->b.inventory>=1+foodToReproduce) //amennyiben élő, aktív és elég étellel rendelkezik, szaporodni kezd.
        {
            it->b.inventory=it->b.inventory-foodToReproduce; //Ez ételbe kerül

            //int t=3;
            //while(t--)
            while(sProb(fertilityChance)) //Valamennyi utódot hoz létre.
            {
                bacteria child = it->b; //új baktérium adatai ebben a struktúrában

                //Mutaciok meg ilyenek, nem vagyok biologus
                mutate_coef(&child.aggression_coef);
                mutate_coef(&child.altruism_coef);
                mutate_coef(&child.activeness_coef);
                mutate_coef(&child.vision_coef);

                child.ancestor = it->b.id; //Amennyiben a szülő nulladik napi, az utód grand-ancestora maga a szülő legyen.
                if(it->b.ancestor==0)
                {
                    child.grand_ancestor=it->b.id; //szülő id-je
                }
                else
                {
                    child.grand_ancestor=it->b.grand_ancestor; //egyébként pedig a szülő grand-ancestora.
                }

                //Hasonló a mutate_coef függvényhez, csak itt szorzás alapú változókat változtatunk, tehát szorozni kell (szorzás alapú - lásd 'harc' függvény)
                double mod = 1.05;
                if(sProb(0.5)) mod=1/mod;
                while(sProb(mutationChance)) //Addig noveljuk/csokkentjuk a szam erteket, amig a valoszinuseg engedi, es 0<=szam<=1
                {
                    child.strength = (double)child.strength*mod;
                }
                while(sProb(mutationChance)) //Addig noveljuk/csokkentjuk a szam erteket, amig a valoszinuseg engedi, es 0<=szam<=1
                {
                    child.q_altruism = (double)child.q_altruism*mod;
                }

                
                child.age = 0; //alapadatok
                child.id=*availableId;

                pushBac(list, child, *availableId);

                fprintf(logstream, "SZAPORODAS :: %s %s (%u) szaporodott. GYEREK: %s %s (%u)\n", it->b.genus, it->b.species, it->b.id, child.genus, child.species, child.id);
                (*availableId)++;
            }
            
        }
    }
}

void process_foodconsumption(baclistnode *list) //evést feldolgozó függvény
{
    for(baclistnode *it=list->next;it!=NULL;it=it->next) //bejárjuk a listát
    { 
        if(it->b.isAlive && it->b.isActive) //élő, aktív baktériumok ételt veszítenek.
        {
            
            if(it->b.inventory==0)
            {
                it->b.isAlive=0;
                fprintf(logstream, "HALAL: %s %s (%u) meghalt. INDOK: EHINSEG\n", it->b.genus, it->b.species, it->b.id);
            }

            it->b.inventory--;
        }   
        
    }
}

//MAIN
int main(void)
{
    //header
    printf("____________________________________\n");
    printf("Bakteriumok tulelese 2021 JM08B3\n");
    printf("____________________________________\n");
    printf("Log: ");

    //log file elérési útja
    char logPath[256]="./logfolder/";
    fgets(logPath+12,256-12, stdin);
    logPath[strlen(logPath)-1]='\0';
    if(strcmp(logPath,"")==0 || strcmp(logPath,"main.c")==0) //Hibakezelés, biztonság
    {
        printf("Legyszi azt ne\n"); //stdoutra irjuk a logot, ha rossz
        logstream=stdout;
    }
    else
    {
         logstream = fopen(logPath, "wt"); //
         if(logstream==NULL)
         {
             printf("Nem sikerult megnyitni a %s fajlt, igy standard outputra fogok logolni.\n", logPath+12);
             logstream=stdout;
         }
    }
   
    int res; //Felbontás
    printf("Felbontas: "); //a színes diagramhoz van kepernyomeret karakterekben
    if(!scanf("%d", &res))
    {
        res=0; //Ezt az esetet a rajzoló függvény colorfulEconioStats() kezelni fogja.
    }

    uint expected_max;
    printf("Varhato maximum bakteriumszam: "); //Erre a számra normalizáljuk a grafikonunkat.
    if(!scanf("%u", &expected_max))
    {
        expected_max=0; //Ezt az esetet a rajzoló függvény colorfulEconioStats() kezelni fogja.
    }

    int seed;
    printf("Seed: "); //a statisztikahoz van, kepernyomeret, ha 0 akkor csak arányt fogunk mutatni, vagy ha rosszat írunk be.
    if(!scanf("%d", &seed))
    {
        seed=69420;
    }
    srand(seed); //seed inicializalasa, meg ilyenek.

    printf("____________________________________\n\n");

    FILE *tulajdonsagokstream=NULL; //tulajdonsagok.txt filestreamje
    tulajdonsagokstream=fopen("tulajdonsagok.txt", "rt");
    if(tulajdonsagokstream==NULL)
    {
        printf("Nem sikerult megnyitni a 'tulajdonsagok.txt' fajlt.");
        return 1;
    }

    FILE *fajokstream=NULL; //fajok.txt filestreamje
    fajokstream=fopen("fajok.txt", "rt");
    if(fajokstream==NULL)
    {
        printf("Nem sikerult megnyitni a 'fajok.txt' fajlt.");
        return 1;
    }


    uint simlength;
    uint foodreserve;
    uint dailyfood;

    FILE *simstream =NULL; //sim.txt filestreamje
    simstream=fopen("sim.txt", "rt");
    if(simstream==NULL)
    {
        printf("Nem sikerult megnyitni a 'sim.txt' fajlt.");
        return 1;
    }
    
    //olvassuk be a megfelelo informaciokat sim.txt-bol. Ha nem sikerul, default ertekeket fogunk beallitani.
    if(fscanf(simstream, "%u", &simlength)!=1) {printf("simlength nem volt megadva: Uj simlength=20"); simlength=20;}
    if(fscanf(simstream, "%u", &foodreserve)!=1) {printf("foodreserve nem volt megadva: Uj foodreserve=500"); foodreserve=500;}
    if(fscanf(simstream, "%u", &dailyfood)!=1) {printf("dailyfood nem volt megadva: Uj dailyfood=300"); dailyfood=300;}

    fclose(simstream);

    //Keszitsuk el a nulladik napi bakteriumallomanyt


    //BAKTERIUM LISTA DEKLARACIOJA
    baclistnode* list = ujBacList();
    uint availableId=1; // Azert nem nulla, mert azt meg akartam tartani arra a celra, hogy a nulladik napi bakteriumoknak is legyen anyukaja :) (id=0 bakterium)

    //FAJLISTA
    fajlistnode *fajlist = ujFajList();
    int fajColorOffset = 0; //Az econio.h-s színekhez van használva a változó.

    uint sampledb;
    char samplegenus[25], samplespecies[25];

    while(fscanf(fajokstream, "%s%s%u", samplegenus, samplespecies, &sampledb)==3)
    {
        //printf("%s %s :: %u\n", samplegenus, samplespecies, sampledb);
        
        //Most bele kell rakni a fajok listajaba.
        pushFaj(fajlist, samplegenus, samplespecies, &fajColorOffset);


        bacteria newbac;
        int X=0;
        while(X==0) //amíg be tudunk olvasni. Lásd: fscanBac()
        {
            newbac=fscanBac(tulajdonsagokstream, &X);
            if(strcmp(samplegenus, newbac.genus)==0 && strcmp(samplespecies, newbac.species)==0)
            {
                break;
            }
        }

        rewind(tulajdonsagokstream); //Vissza a fajl elejere
        if(X==1){printf("Nem talaltam meg '%s %s' tulajdonsagait. \n\n", samplegenus, samplespecies);} //Kezeljük, hogy mi van, ha nem tudjuk megtalálni:
        else
        for(int i=0;i<sampledb;i++) //Baktérium lista megtöltése
        {
            pushBac(list, newbac, availableId);
            availableId++;
        } 
    }

    //Beallitom grand ancestort mindenkinel 0-ra
    for(baclistnode *it=list->next;it!=NULL;it=it->next)
    {
        it->b.grand_ancestor=0;
        it->b.ancestor=0;
    }

    //Bezárom a beolvasandó fileokat
    fclose(tulajdonsagokstream);
    fclose(fajokstream);

    FILE *gf = fopen("graph.txt", "wt"); // Ez a filestream arra van, hogy egy olyan fájlt hozzunk létre, ami a napi élő baktériumszámot tartalmazza és könnyű excelben grafikonná alakítani.
    if(gf==NULL)
    {
        freeFajList(fajlist);
        freeBaclist(list);
        exit(EXIT_FAILURE);
    }

    //SIMULATION STARTS NOW
    uint elozo=0;
    for(uint day=0;day<simlength;day++) //Annyi iteráció történik, amennyit a sim.txt-ben megadtunk.
    {
        uint c=currentlyAlive(list); //Megszámoljuk az élő baktériumokat
        printf("Day %5d: %5u --> %5u  (%5d) [%7u] ::: ", day, elozo, c, (int)c-elozo, foodreserve); //Kiírjuk: hanyadik nap, bakteriumszám változás, mennyi étel marad
        elozo=c;
        
        colorfulEconioStats(list, res, expected_max,  fajlist); //Egy sornyi színes diagram

        fprintf(logstream, "____________________________________\n"); //Log
        fprintf(logstream, "Day %u. | Foodreserve: %u\n", day+1, foodreserve);

        fprintf(gf,"%u\t", c); //exceles file folytatása

        agingDeaths(list); //öregedés függvény

        foodDistribution(list, &foodreserve, dailyfood); //ételosztás függvény
        foodreserve+=dailyfood; //A maradék étel számának változtatása (ennyi étel termett)


        process_activity(list); //aktív, nem aktív baktériumok feldolgozása

        generate_meetings(list, c); //találkozások generálása
        process_meetings(list); //találkozások feldolgozása (harc, segítség)

        process_reproduction(list, &availableId); //szaporodás
        
        process_foodconsumption(list); //ételfogyasztás feldolgozása


        removeDeadBacs(list); //halott baktériumok eltávolítása a listából, optimalizálás
    }

    //ügyeskedés :  Az colorfulEconioStats() függvény noha kiszámolta a fajon belüli élő egyedek számát, az egy nappal le van maradva. ezért újra megtesszük ezt.
    removeDeadBacs(list);
    countSpecies(list, fajlist);

    printColorTable(fajlist); //Jelmagyarázat kiírása

    printMaxpopSpecies(fajlist); //Legtöbb egyedszámú faj(ok) megkeresése, kiírása
    printMostSuccesfulBac(list); //Legtöbb élő leszármazottal rendelkező (valamilyen szempontból legsikeresebb) nulladik napi baktérium megkeresése

    if(currentlyAlive(list) > 0)
    {
        printMinMaxBacProperties(list); //Tulajdonság intervallumok kiírása
    }


    //printFajlist(fajlist);
    //printBacList(list);
    
    //Felszabadítjuk a listákat.
    freeFajList(fajlist);
    freeBaclist(list);

    //Bezárjuk a filet.
    fclose(gf);
    
    fclose(logstream);

    scanf("%*c");
    scanf("%*c");
    scanf("%*c");
    
    return 0;
}