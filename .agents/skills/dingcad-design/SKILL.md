---
name: dingcad-design
description: Progettare e modificare modelli CAD parametrici in Dingcad, con assembly leggibili, riferimenti esterni separati, verifiche e disposizioni per la stampa. Usare per il lavoro sui modelli e sulle loro scene, non per lo sviluppo del viewer.
---

# Dingcad Design

Prima di creare o modificare un modello, leggere [API.md](../../../API.md) nella
radice del repository: e la fonte del contratto tecnico per geometria,
`displayParts`, quote ed esportazione. Consultare le note del progetto locale
per misure, materiali, macchina e decisioni precedenti. Le istruzioni esplicite
dell'utente prevalgono sulle convenzioni di questa skill.

## Organizzare il modello

- Tenere i progetti personali in `local-scenes/<progetto>/<versione>/`, con
  geometria, entry point, istruzioni ed esportazioni riconoscibili. Non includerli
  nei commit del viewer; non forzare l'aggiunta dei file ignorati.
- Parametrizzare le misure che governano forma, accoppiamenti e ingombri.
  Distinguere valori misurati, derivati e provvisori; rendere visibili le ipotesi
  nelle viste di verifica e nelle istruzioni. Chiedere solo le informazioni che
  impediscono una scelta sensata, proseguendo le verifiche indipendenti.
- Quando cambia la compatibilita fra pezzi, conservare la versione e le
  esportazioni precedenti e indicare quali parti vanno ristampate. Non sostituire
  gli STL rilasciati durante un semplice studio visivo senza una richiesta di
  nuova esportazione.

## Rendere leggibile l'assembly

Organizzare l'albero prima per **Riferimenti esterni**, poi per **Oggetto
progettato**. I riferimenti rappresentano ambiente, oggetti esistenti e superfici
di montaggio: visibili, ma inizialmente non esportabili. L'oggetto comprende i
pezzi da realizzare. Usare sottogruppi semantici quando aiutano a selezionare e
isolare interfacce o sottoassiemi; evitare gruppi generici senza significato.

Assegnare nomi comprensibili e ID stabili ai componenti, conservando gli ID quando
si riordina il modello o si modifica la geometria dello stesso pezzo. Impostare
l'esportabilita esplicitamente per distinguere oggetto e contesto. L'elenco dei
componenti deve coprire tutti i solidi interessati alla visualizzazione e
all'esportazione secondo il contratto in API.md.

Scegliere una palette coerente con materiali e funzione. Piccole variazioni di
colore possono distinguere pezzi adiacenti anche quando saranno realizzati nello
stesso materiale. Non ereditare colori, materiale, nozzle, misure o tolleranze
da un progetto diverso.

## Separare le viste e progettare l'assemblaggio

- **Assembly:** parti nelle coordinate di montaggio, con il contesto utile.
- **Verifica:** sezioni, esplosi, interferenze, quote e ipotesi da controllare.
- **Stampa:** soli pezzi previsti su ciascun piatto, in orientamento di stampa,
  con spazio per brim e supporti. Rendere espliciti i nomi dei file da stampare.

Condividere i parametri e la geometria fra queste viste, evitando copie che
possono divergere. Valutare orientamento degli strati, superfici appoggiate al
piatto, supporti rimovibili e accesso ai giunti. Per parti da incollare prevedere
un posizionamento ripetibile e superfici di contatto adeguate; non attribuire ai
piccoli elementi di allineamento una resistenza strutturale non verificata.
Controllare che la sequenza di montaggio sia fisicamente possibile.

## Verificare e consegnare

Eseguire verifiche proporzionate alle modifiche: validita e connessione dei
solidi, tenuta delle mesh esportate, spessori, giochi e interferenze, accesso per
montaggio/rimozione, orientamenti e ingombri sul piatto. Quando la stampabilita e
rilevante, verificare anche la slice e i supporti effettivi, se lo slicer e
disponibile. Non confondere un render convincente con una verifica geometrica.

Controllare il contenuto degli STL: componenti attesi, assenza dei riferimenti
esterni, coordinate e disposizione corrette. Per esportare una disposizione di
stampa usare il relativo entry point; un assembly non diventa automaticamente un
piatto di stampa. Tenere API.md come unica fonte dei dettagli dei controlli di
export e dei metadati.

Indicare cosa e stato verificato digitalmente e cosa richiede ancora prove
fisiche. Preparare campioni mirati quando incastri, tolleranze o giunti sono
incerti. Non dichiarare confermati carichi, adesione o durata senza le relative
prove. Consegnare il percorso dell'assembly, i file di stampa pertinenti e le
limitazioni ancora aperte nelle note locali del progetto.
