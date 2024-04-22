# Local key-value database for Arduino with flash disk


Persistent storage of structured data with O(log n) access.


### Example of opening the data file and loading stored data

```C++
#include <LittleFS.h> // or #include <FFat.h> or #include <SD.h>

#define fileSystem LittleFS // or FFat or SD

#include "keyValuedatabase.hpp"
keyValueDatabase<int, String> kvdbA;  // database instance where keys are integers and values are Strings (in this example)

void setup () {
    Serial.begin (115200);
    while (!Serial)
        delay (10);
    delay (1000);

    fileSystem.begin ();
    // if (fileSystem.format ()) Serial.println ("LittleFs formatted"); else Serial.println ("LittleFs formatting failed");

    signed char e;

    // index values in the data file (load keys to balanced binary search tree)
    e = kvdbA.loadData ("/A.db");
    if (!e) //  OK
        Serial.printf ("kvdbA initially loaded %i key-value pairs\n", kvdbA.size ());
    else
        Serial.printf ("kvdbA failed to load data, check errorFalgs () for details\n");
```


### Example of inserting new key-value pair

```C++
    // Example of inserting new key-value pairs
    e = kvdbA.Insert (7, "seven");
    if (e) // != OK
        Serial.printf ("kvdbA Insert failed, check errorFalgs () for details\n");
```


### Examples of searching for a key

```C++
    uint32_t blockOffset; // not needed for searching for a key, but will coma handy later when searching for a value
    e = kvdbA.FindBlockOffset (8, blockOffset);
    switch (e) {
        case OK:        Serial.printf ("kvdbA key 8 FOUND\n"); 
                        break;
        case NOT_FOUND: Serial.printf ("kvdbA key NOT_FOUND\n"); 
                        break;
        default:        Serial.printf ("kvdbA FindBlockOffset error: %i\n", e); 
                        break;
    }
```


### Examples of searching for a value that belongs to a given key

```C++
    String value;
    e = kvdbA.FindValue (8, &value); 
    switch (e) {
        case OK:        Serial.printf ("kvdbA value for a key 8 FOUND: %s\n", (char *) value.c_str ()); 
                        break;
        case NOT_FOUND: Serial.printf ("kvdbA key NOT_FOUND\n"); 
                        break;
        default:        Serial.printf ("kvdbA FindValue error: %i\n", e); 
                        break;
    }      
```


### Examples of updating the value for a given key

```C++
    // update (1) the value (most simple version of calling update)
    e = kvdbA.Update (8, "eight"); 
    if (e) // != OK
        Serial.printf ("kvdbA Update failed, check errorFalgs () for details\n");

    // update (2) the value with calculation (update with locking to prevent other tasks changind the data while the calculation is not finished)
    kvdbA.Lock ();
        String oldValue;
        e = kvdbA.FindValue (8, &oldValue); 
        if (e == OK)
            e = kvdbA.Update (8, oldValue + "teen"); 
    kvdbA.Unlock ();
    if (e) // != OK
        Serial.printf ("kvdbA Update failed, check errorFalgs () for details\n");

    // update (3) the value with calculation using lambda callback function, the locking is already integrated so the calculation can be performed without problems
    // this mechanism is usefull for example for counters (increasing the value), etc)
    e = kvdbA.Update (9, [] (String& value) { value.toUpperCase (); } ); 
    if (e) // != OK
        Serial.printf ("kvdbA Update failed, check errorFalgs () for details\n");
```


### Example of deleting a key-value pair

```C++
    e = kvdbA.Delete (7); 
    if (e) // != OK
        Serial.printf ("kvdbA Delete failed, check errorFalgs () for details\n");
```


### Example of truncating key-value pair database (delete all the key-value pairs)

```C++
pkvpA.Truncate ();
```


### Example of iterating through all key-value pairs

```C++
    for (auto p: kvdbA) {
        // keys are always kept in memory and are obtained fast
        Serial.print (p->key); Serial.print (", "); Serial.print (p->blockOffset); Serial.print (" -> "); 
        
        // values are read from disk, obtaining a value may be much slower
        String value;
        e = kvdbA.FindValue (p->key, &value, p->blockOffset); // blockOffset is optional but since we already have it we can speed up the search a bit by providing it
        if (e) // != OK 
            Serial.printf ("FindValue error, check errorFalgs () for details\n");
        else
            Serial.println (value);
    }
```


### Example of updating the values (with calculation) during iteration

```C++
    // Please note that database is already locked throughtout the iterator, so additional locking is not needed.
    for (auto p: kvdbA) {
        e = kvdbA.Update (p->key, [] (String& value) { value = "»" + value + "«"; }, &(p->blockOffset)); // since block offset is already known it will speed up Update operation if we provide this information
        if (e) 
            Serial.printf ("Update error, check errorFalgs () for details\n");
    }
```


### Detecting errors that occured in key-value database operations

```C++
    for (auto p: kvdbA) {
        String value;
        e = kvdbA.FindValue (p->key, &value, p->blockOffset);
        if (!e) { // OK
            Serial.print (p->key); Serial.print (" - "); Serial.println (value);     
        } else {
            Serial.printf ("FindValue error while fetching a value from disk: ");
            switch (e) {
                  case BAD_ALLOC:       Serial.printf ("BAD_ALLOC\n"); break;
                  case NOT_FOUND:       Serial.printf ("NOT_FOUND\n"); break;
                  case NOT_UNIQUE:      Serial.printf ("NOT_UNIQUE\n"); break;
                  case DATA_CHANGED:    Serial.printf ("DATA_CHANGED\n"); break;
                  case FILE_IO_ERROR:   Serial.printf ("FILE_IO_ERROR\n"); break;
                  case CANT_DO_IT_NOW:  Serial.printf ("CANT_DO_IT_NOW\n"); break;
              }
        }
    }
```


### Checking if an error has occurred only once after many key-value database operations

```C++
    kvdbA.clearErrorFlags ();
    for (int i = 1000; i < 1100; i++)
        kvdbA.Insert (i, String (i));

    e = kvdbA.errorFlags ();
    if (e) { // != OK
        Serial.printf ("100 inserts error:\n");
        if (e & BAD_ALLOC)      Serial.println ("BAD_ALLOC");
        if (e & NOT_FOUND)      Serial.println ("NOT_FOUND");
        if (e & NOT_UNIQUE)     Serial.println ("NOT_UNIQUE");
        if (e & DATA_CHANGED)   Serial.println ("DATA_CHANGED");
        if (e & FILE_IO_ERROR)  Serial.println ("FILE_IO_ERROR");
        if (e & CANT_DO_IT_NOW) Serial.println ("CANT_DO_IT_NOW");
    }
```


### Finding the first and the last key-value pairs (min and max keys)

```C++
    auto firstElement = first_element (kvdbA);
    if (firstElement) // check if first element is found (if kvdbA is not empty)
        Serial.printf ("first element (min key) of kvdbA = %i\n", (*firstElement)->key);

    auto lastElement = last_element (kvdbA);
    if (lastElement) // check if last element is found (if kvdbA is not empty)
        Serial.printf ("last element (max key) of kvdbA = %i\n", (*lastElement)->key);
```
